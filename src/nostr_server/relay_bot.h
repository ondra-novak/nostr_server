/*
 * relay_bot.h
 *
 *  Created on: 17. 6. 2023
 *      Author: ondra
 */

#ifndef SRC_NOSTR_SERVER_RELAY_BOT_H_
#define SRC_NOSTR_SERVER_RELAY_BOT_H_
#include "publisher.h"
#include "signature.h"
#include "iapp.h"

#include "config.h"
namespace nostr_server {

class RelayBot {
public:



    static cocls::async<void> run_bot(IApp *app, RelayBotConfig cfg);



protected:

    RelayBot(IApp *app, RelayBotConfig cfg);

    IApp *_app;
    RelayBotConfig _cfg;
    EventSubscriber _subscriber;
    SignatureTools::PrivateKey _pk;
    SignatureTools _sigtool;
    bool _active = false;

    std::string _public_key;
};

} /* namespace nostr_server */

#endif /* SRC_NOSTR_SERVER_RELAY_BOT_H_ */
