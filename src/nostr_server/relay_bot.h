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

#include <docdb/map.h>
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
    IApp::RecordSetCalculator _rccalc;
    bool _active = false;

    enum class ObjectType {
        group
    };

    docdb::Map<docdb::FixedRowDocument<ObjectType, SignatureTools::PrivateKey> > _object_map;

    std::string _public_key;
    std::string _admin_public_key;

    static std::string_view get_kind4_sender(const Event &ev);
    std::string_view find_object_pubkey(const Event &ev);
    void process_message(std::string_view target, const Event &ev);
    void process_relaybot_command(std::string_view command, std::string_view sender);
    void process_group_command(const SignatureTools::PrivateKey &pk, std::string_view command, std::string_view sender);
    void send_response(const SignatureTools::PrivateKey &pk, std::string_view target, std::string_view response, Event &&skelet = Event::KeyPairs());
    void process_mention(std::string_view target, const Event &ev);


    std::vector<std::string> get_user_list(std::string_view pubkey, unsigned int kind, std::string_view ref) const;
    void change_profile(const SignatureTools::PrivateKey &pk, std::string pubkey, std::string key, std::string value);
    void mod_userlist(const SignatureTools::PrivateKey &pk, std::string pubkey, unsigned int kind, std::string cat, std::string npub, bool add);
    std::string delete_group_post(const SignatureTools::PrivateKey &pk, const std::string &group_pubkey, const std::string &sender, const std::string &noteid, bool is_moderator);
};

} /* namespace nostr_server */

#endif /* SRC_NOSTR_SERVER_RELAY_BOT_H_ */
