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
    using Signature = std::array<unsigned char, 64>;

    SignatureTools();

    bool verify(const HashSha256 &id, const PublicKey &pubkey, const Signature &sig) const ;
    bool sign(const PrivateKey &key, const HashSha256 &id, Signature &sig, PublicKey &pub) const;
    bool public_key(const PrivateKey &priv, PublicKey &pub) const;

    static bool from_nsec(const std::string &nsec, PrivateKey &pk);
    static std::string from_npub(const std::string &npub);
    static std::string from_bech32(const std::string &bech, std::string_view cat);
    static std::string to_npub(std::string_view public_key);
    static std::string to_nsec(const PrivateKey &key);
    static std::string to_bech32(std::string_view hex, const std::string &type);

    bool encrypt(const PrivateKey &sender, const PublicKey &receiver, std::string_view message, std::string &encrypted_message) const;
    bool decrypt(const PrivateKey &receiver, const PublicKey &sender, std::string_view encrypted_message, std::string &message) const; 
    bool shared_secret(const PrivateKey &priv, const PublicKey &pub, SharedSecret &secret) const;

    bool random_private_key(PrivateKey &key) const;


protected:
    struct Deleter {
        void operator()(secp256k1_context *ptr) const;
    };

    std::unique_ptr<secp256k1_context, Deleter> ctx;
};




}
#endif /* SRC_NOSTR_SERVER_SIGNATURE_H_ */

