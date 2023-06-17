/*
 * relay_bot.cpp
 *
 *  Created on: 17. 6. 2023
 *      Author: ondra
 */

#include "relay_bot.h"
#include <iostream>

namespace nostr_server {

cocls::async<void> RelayBot::run_bot(IApp * app, RelayBotConfig cfg) {
    RelayBot bot(std::move(app), std::move(cfg));
    if (!bot._active) co_return;
    bool b = co_await bot._subscriber.next();
    while (b) {
        const auto &item = bot._subscriber.value();
        const Event &ev = item.first;

        if (ev["kind"].as<unsigned int>() == 4) {
            const auto &tags = ev["tags"].array();
            auto iter = std::find_if(tags.begin(), tags.end(), [&](const Event &r){
               return r[0].as<std::string_view>() == "p" && r[1].as<std::string_view>() == bot._public_key;
            });
            if (iter != tags.end()) {
                auto msg = bot._sigtool.decrypt_private_message(bot._pk, ev);
                if (msg.has_value()) {
                    std::cout << *msg << std::endl;
                }
            }
        }

        b = co_await bot._subscriber.next();
    }

}

RelayBot::RelayBot(IApp * app, RelayBotConfig cfg)
    :_app(std::move(app))
    ,_cfg(std::move(cfg))
    ,_subscriber(_app->get_publisher()) {
    if (_cfg.nsec.empty()) return;

    if (!_sigtool.from_nsec(_cfg.nsec, _pk))
        throw std::runtime_error("Failed to decode relaybot's private key");
    _public_key = _sigtool.calculate_pubkey(_pk);
    if (_public_key.empty()) {
        throw std::runtime_error("Failed to calculate relaybot's public key");
    }
    _active = true;

}

} /* namespace nostr_server */
