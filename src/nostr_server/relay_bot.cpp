/*
 * relay_bot.cpp
 *
 *  Created on: 17. 6. 2023
 *      Author: ondra
 */

#include "relay_bot.h"
#include "event_tools.h"
#include <iostream>
#include <docdb/json.h>

namespace nostr_server {

cocls::async<void> RelayBot::run_bot(IApp * app, RelayBotConfig cfg) {
    RelayBot bot(std::move(app), std::move(cfg));
    if (!bot._active) co_return;
    bool b = co_await bot._subscriber.next();
    while (b) {
        const auto &item = bot._subscriber.value();
        const Event &ev = item.first;
        unsigned int kind = ev["kind"].as<unsigned int>();
        std::string_view object = bot.find_object_pubkey(ev);
        if (!object.empty()) {
            if (kind == 4) bot.process_message(object, ev);
            if (kind == 1) bot.process_mention(object, ev);
        }

        b = co_await bot._subscriber.next();
    }

}

std::string_view RelayBot::find_object_pubkey(const Event &ev) {
    const auto *tag = find_tag_if(ev, [&](const Event::Array &tag) {
        if (tag[0].as<std::string_view>() == "p") {
            std::string_view pubkey = tag[1].as<std::string_view>();
            return (pubkey == _public_key || _object_map.find(pubkey));
        }
        return false;
    });
    if (tag) return (*tag)[1].as<std::string_view>();
    else return {};

}

std::string_view RelayBot::get_kind4_sender(const Event &ev) {
    if (ev["kind"].as<unsigned int>() == 4) {
        const auto &tags = ev["tags"].array();
        std::string_view found = {};
        for (const Event &x: tags) {
            if (x[0].as<std::string_view>() == "p") {
               if (found.empty()) found = x[1].as<std::string_view>();
               else return {};
            }
        }
        return found;
    }
    return {};
}

RelayBot::RelayBot(IApp * app, RelayBotConfig cfg)
    :_app(std::move(app))
    ,_cfg(std::move(cfg))
    ,_subscriber(_app->get_publisher())
    ,_object_map(_app->get_database(), "relaybot_objects") {
    if (_cfg.nsec.empty()) return;

    if (!_sigtool.from_nsec(_cfg.nsec, _pk))
        throw std::runtime_error("Failed to decode relaybot's private key");
    _public_key = _sigtool.calculate_pubkey(_pk);
    if (_public_key.empty()) {
        throw std::runtime_error("Failed to calculate relaybot's public key");
    }
    _active = true;
    _admin_public_key = _sigtool.from_npub(_cfg.admin);

}

void RelayBot::process_message(std::string_view target, const Event &ev) {
    std::string_view sender = ev["pubkey"].as<std::string_view>();
    if (target == _public_key) {
        auto msg = _sigtool.decrypt_private_message(_pk, ev);
        if (msg.has_value()) {
            process_relaybot_command(*msg, sender);
        }
    } else {
        auto fres = _object_map.find(target);
        if (fres) {
            auto [type, pk] = fres->get();
            auto msg = _sigtool.decrypt_private_message(pk, ev);
            if (msg.has_value()) {
                switch (type) {
                    case ObjectType::group: process_group_command(pk, *msg, sender);
                                            return;
                    default: break;
                }
            }
        }
    }
}

void RelayBot::send_response(const SignatureTools::PrivateKey &pk, std::string_view target, std::string_view response, Event &&skelet) {
    auto now =  std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto ev = _sigtool.create_private_message(pk, target, response, now, std::move(skelet));
    if (ev.has_value()) {
        _app->publish(std::move(*ev), this);
    }
}

void RelayBot::process_relaybot_command(std::string_view command, std::string_view sender) {
    bool is_admin = sender == _admin_public_key;
    auto cmd = coroserver::trim(command);
    if (cmd.compare(0,6,"/ping ") == 0) {
        send_response(_pk, sender, cmd.substr(6));
    } else if (cmd == "/create_group") {
        if (_cfg.groups == "on"
                || (_cfg.groups == "local" && _app->is_home_user(sender))
                || is_admin) {

            if (_cfg.this_relay_url.empty()) {
                send_response(_pk, sender, "error: not properly configured");
                return;
            }

            SignatureTools::PrivateKey key;
            std::string s = _sigtool.create_private_key(key);
            std::string response = "Group created: #[0]. \n\nTo configure group, post commands as private messages to the account of the group";

            Event kind10002 = create_event(10002, "", {
                    {"r", _cfg.this_relay_url},
            });
            Event kind30000 = create_event(30000, "", {
                    {"d","owner"},{"p", sender}
            });

            _sigtool.sign(key, kind10002);
            _sigtool.sign(key, kind30000);
            _app->publish(std::move(kind10002), this);
            _app->publish(std::move(kind30000), this);
            Event::Array mention{"p",s};
            Event::Array tags(1,mention);
            _object_map.put(s,{ObjectType::group, key});
            send_response(_pk, sender, response, Event::KeyPairs{
                    {"tags",tags}
            });
        } else {
            send_response(_pk, sender, "restricted");
        }

    } else if (cmd == "/help") {
        send_response(_pk, sender,
                "/ping <message>            - just replies with message (for test)\n"
                "/create_group              - create new group\n"
                "/set_name <text>           - set relaybot's name (admin)\n"
                "/set_description <text>    - set relaybot's description (admin)\n"
                "/delete <pubkey>           - delete relaybot's object (admin)\n"
                "/compact                   - compact database (admin)\n"
        );

    } else {
        send_response(_pk, sender, "Hi, I am just a bot. Type '/help' for commands");
    }
}

std::vector<std::string> RelayBot::get_user_list(std::string_view pubkey, unsigned int kind, std::string_view category) const {
    std::vector<std::string> out;
    auto docid = _app->find_replacable(pubkey, kind, category);
    if (docid) {
        auto evinfo = _app->get_storage().find(docid);
        if (evinfo) {
            const Event &ev = evinfo->content;
            for (const auto &item : ev["tags"].array()) {
                if (item[0].as<std::string_view>() == "p") {
                    out.push_back(std::string(item[1].as<std::string_view>()));
                }
            }
        }
    }
    return out;

}


void RelayBot::process_group_command(const SignatureTools::PrivateKey &pk,
                            std::string_view command, std::string_view sender) {
    try {
        std::string pubkey = _sigtool.calculate_pubkey(pk);


        auto banlist = get_user_list(pubkey, 10000, {});
        auto admins = get_user_list(pubkey, 30000, "owner");
        auto moderators = get_user_list(pubkey, 30000, "moderator");
        bool is_admin = std::find(admins.begin(), admins.end(), sender) != admins.end();
        bool is_moderator = std::find(moderators.begin(), moderators.end(), sender) != moderators.end();
        bool is_banned = std::find(banlist.begin(), banlist.end(), sender) != banlist.end();

        auto cmd = coroserver::trim(command);
        if (cmd.empty()) return;
        if (cmd[0] == '/') {
            std::istringstream line(std::string(cmd.substr(1)));
            std::string c, a;
            line >> c;
            std::getline(line,a);
            a = coroserver::trim(a);
            if (c == "delete") {
                auto del = delete_group_post(pk, std::move(pubkey), std::string(sender), std::move(a), is_moderator || is_admin);
                if (del.empty()) {
                   send_response(pk, sender, "Nothing found");
                }else{
                   send_response(pk, sender, "OK, #[0] deleted", create_event(0, "", {{"e", del}}));
                }
            } else if (is_admin || is_moderator) {
                if (c == "help") {
                    send_response(pk, sender, "/set_name <name>\n"
                            "/set_description <description>\n"
                            "/add_moderator <npub user>\n"
                            "/kick_moderator <npub user>\n"
                            "/ban <npub user>\n"
                            "/unban <npub user>\n");
                } else {
                    if (c == "set_name") change_profile(pk,pubkey, "name", a);
                    else if (c == "set_description") change_profile(pk,pubkey, "description", a);
                    else if (c == "add_moderator") mod_userlist(pk,pubkey, 30000, "moderator", a, true);
                    else if (c == "kick_moderator") mod_userlist(pk,pubkey, 30000, "moderator", a, false);
                    else if (c == "ban") mod_userlist(pk,pubkey, 10000, "", a, true);
                    else if (c == "unban") mod_userlist(pk,pubkey, 10000, "", a, false);
                    else {
                        send_response(pk, sender, "Unknown command");
                        return;
                    }
                    send_response(pk, sender, "OK");
                }
            } else {
                send_response(pk, sender, "Access denied!");
            }
        } else if (is_banned) {
            send_response(pk, sender, "You are banned!");
        } else {
            Event post = {
                    {"kind", 1},
                    {"content",std::string("#[0]:").append(command)},
                    {"tags",{docdb::undefined,
                            {"p",std::string(sender)}}},
            };
            _sigtool.sign(pk, post);
            send_response(pk, sender, "Posted #[0].\n\nType /delete to delete your last post from the group", {
                    {"tags",{docdb::undefined,{"e",post["id"]}}}
            });
            _app->publish(std::move(post), this);

        }
    } catch (std::exception &e) {
        send_response(pk, sender, std::string("Error: ") + e.what());
    }

}

void RelayBot::process_mention(std::string_view target, const Event &ev) {

}

void RelayBot::change_profile(const SignatureTools::PrivateKey &pk, std::string pubkey, std::string key, std::string value) {
    auto docinfo = _app->get_storage().find(_app->find_replacable(pubkey, 0, ""));
    Event ev ((Event::KeyPairs()));
    if (docinfo) {
        ev = docinfo->content;
    }
    Event profile ((Event::KeyPairs()));
    try {
        profile = Event::from_json(ev["content"].as<std::string_view>());
    } catch (...) {
        //empty
    }
    profile.set(key, value);
    Event newev = create_event(0, profile.to_json(), {});
    _sigtool.sign(pk, newev);
    _app->publish(std::move(newev), this);

}

void RelayBot::mod_userlist(const SignatureTools::PrivateKey &pk, std::string pubkey, unsigned int kind,
        std::string cat, std::string npub, bool add) {
    std::string upub = _sigtool.from_npub(npub);
    if (upub.empty()) throw std::invalid_argument("Invalid npub");

    auto docinfo = _app->get_storage().find(_app->find_replacable(pubkey, kind, cat));
    Event ev ((Event::KeyPairs()));
    if (docinfo) {
        ev = docinfo->content;
    }
    auto tags = ev["tags"].array();
    auto iter = std::find_if(tags.begin(), tags.end(), [&](const Event &t){
        return t[0].as<std::string_view>() == "p" && t[1].as<std::string_view>() == upub;
    });
    if (iter == tags.end() && add) {
        tags.push_back({"p", upub});
    } else if (!add) {
        tags.erase(iter);
    }

    Event newev = create_event(kind, "", {{"d", cat}});
    newev.set("tags", tags);
    _sigtool.sign(pk, ev);
    _app->publish(std::move(ev), this);
}

std::string RelayBot::delete_group_post(const SignatureTools::PrivateKey &pk,
        const std::string &group_pubkey, const std::string &sender,
        const std::string &noteid, bool is_moderator) {

    std::string nt;
    try {
        nt = _sigtool.from_bech32(noteid, "note");
    } catch (...) {

    }
    if (nt.empty()) is_moderator = false;
    Filter flt;
    flt.kinds.push_back(1);
    flt.authors.push_back(group_pubkey);
    if (!is_moderator) flt.tags.emplace_back('p',sender);
    if (nt.empty()) {
        flt.since = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() - std::chrono::hours(24));
    } else {
        flt.ids.push_back(nt);
    }
    _app->find_in_index(_rccalc, {flt});
    if (!_rccalc.is_top_empty()) {
        auto set = _rccalc.pop();
        auto doc = _app->get_storage().find(set.back().id);
        if (doc) {
            std::string id = doc->content["id"].as<std::string>();
            Event ev = create_event(5, "", {{"e", id}});
            _sigtool.sign(pk,ev);
            _app->publish(std::move(ev), this);
            _app->get_storage().erase(set.back().id);
            return id;
        }
    }
    return {};

}

} /* namespace nostr_server */
