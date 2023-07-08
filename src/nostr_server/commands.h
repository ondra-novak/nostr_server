#pragma once
#ifndef SRC_NOSTR_SERVER_COMMANDS_H_
#define SRC_NOSTR_SERVER_COMMANDS_H_

#include <coroserver/static_lookup.h>
#include <coroserver/websocket_stream.h>
#include <docdb/json.h>


namespace nostr_server {


NAMED_ENUM(Command,
        unknown,
        REQ,
        EVENT,
        CLOSE,
        COUNT,
        EOSE,
        NOTICE,
        AUTH,
        OK,
        HELLO,
        WELCOME
);
constexpr NamedEnum_Command commands={};


template<typename Fn>
auto send(coroserver::ws::Stream &stream, Command command, std::initializer_list<docdb::Structured> args, Fn &&log) {
    docdb::Buffer<char,256> line;
    line.push_back('[');
    line.push_back('"');
    line.append(commands[command]);
    line.push_back('"');
    for (const auto &a: args) {
        line.push_back(',');
        a.to_json(std::back_inserter(line), docdb::Structured::flagUTF8);
    }
    line.push_back(']');
    std::string_view lineview =line;
    auto r = stream.write({lineview});
    log(lineview);
    return r;
}


template<typename Fn>
bool receive(const coroserver::ws::Message &wsmsg, Command &command, std::vector<docdb::Structured> &args, Fn &&log) {
    try {
        switch (wsmsg.type) {
            case coroserver::ws::Type::text: {
                log(wsmsg.payload);
                docdb::Structured data = docdb::Structured::from_json(wsmsg.payload);
                const auto &darr =data.array();
                if (!darr.empty()) {
                    command = commands[darr[0].as<std::string_view>()];
                    args.clear();
                    for (std::size_t i = 1; i < darr.size(); ++i) {
                        args.push_back(std::move(darr[i]));
                    }
                    return true;
                }
            }break;
            case coroserver::ws::Type::connClose:
                return false;
            default:
                args.clear();
                log("unsupported frame type");
                command = Command::unknown;
                return true;
        }
    } catch (std::exception &e) {
        log(e.what());
    }
    return false;
}




}



#endif /* SRC_NOSTR_SERVER_COMMANDS_H_ */
