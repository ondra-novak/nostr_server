/*
 * signature.h
 *
 *  Created on: 13. 6. 2023
 *      Author: ondra
 */

#ifndef SRC_NOSTR_SERVER_SIGNATURE_H_
#define SRC_NOSTR_SERVER_SIGNATURE_H_
#include "publisher.h"


#include <secp256k1.h>
#include <secp256k1_extrakeys.h>



namespace nostr_server {

class SignatureTools {
public:

    using HashSha256 = std::array<unsigned char, 32>;
    using SharedSecret = std::array<unsigned char, 32>;
    using PrivateKey = std::array<unsigned char, 32>;
    using PublicKey = std::array<unsigned char, 32>;

    SignatureTools();

    bool verify(const Event &event) const;
    ///sign event
    /** Function also fills correct pubkey in the event
     *
     * @param key private key
     * @param event event (without sig and pubkey)
     * @retval true event has been signed
     * @retval false failure to sign - try different key
     */

    bool sign(const PrivateKey &key, Event &event) const;
    void get_hash(const Event &event, HashSha256 &hash) const;

    ///Calculate pubkey from private key
    /**
     * @param key private key
     * @return pubkey as hex string, if empty returned, failed to generate keypair,
     * try different private key
     */
    std::string calculate_pubkey(const PrivateKey &key) const;

    std::string calculate_keypair(const PrivateKey &key, secp256k1_keypair &kp) const;


    static std::string to_hex(unsigned char *content, std::size_t len);


    template<typename Fn>
    static void hexToBytes(const std::string_view& hexString, Fn &&fn);


    static bool from_nsec(const std::string &nsec, PrivateKey &pk);
    static std::string from_npub(const std::string &npub);

    ///Create encrypted message
    /**
     * @param pk private key
     * @param to_publickey public key of receiver
     * @param text text
     * @return event
     */
    std::optional<Event> create_private_message(const PrivateKey &pk, const std::string_view &to_publickey, const std::string_view &text, std::time_t created_at);
    ///Decrypt private message
    /**
     * @param pk private key
     * @param ev event containing message
     * @return text of the message, or empty, if message cannot be decrypted
     */
    std::optional<std::string> decrypt_private_message(const PrivateKey &pk, const Event &ev);

    bool create_shared_secret(const PrivateKey &pk, const std::string_view &to_publickey, SharedSecret &secret);

protected:
    struct Deleter {
        void operator()(secp256k1_context *ptr) const;
    };

    std::unique_ptr<secp256k1_context, Deleter> ctx;
};





template<typename Fn>
inline void SignatureTools::hexToBytes(
        const std::string_view &hexString, Fn &&fn) {
    char buff[3];
    buff[2] = 0;
    for (size_t i = 0; i < hexString.length(); i += 2) {
        buff[0] = hexString[i];
        buff[1] = hexString[i+1];
        auto byte = static_cast<unsigned char>(std::stoi(buff, nullptr, 16));
        fn(byte);
    }

}

}
#endif /* SRC_NOSTR_SERVER_SIGNATURE_H_ */

