#include "signature.h"
#include <docdb/json.h>

#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>
#include <openssl/sha.h>

namespace nostr_server {

Secp256Context::Secp256Context():ctx(secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY)) {}
void Secp256Context::Deleter::operator ()(secp256k1_context *ptr) const {
    secp256k1_context_destroy(ptr);
}




// Helper function to convert hex string to byte vector
template<typename Fn>
void hexToBytes(const std::string_view& hexString, Fn &&fn) {
    char buff[3];
    buff[2] = 0;
    for (size_t i = 0; i < hexString.length(); i += 2) {
        buff[0] = hexString[i];
        buff[1] = hexString[i+1];
        auto byte = static_cast<unsigned char>(std::stoi(buff, nullptr, 16));
        fn(byte);
    }
}
#if 0
bool verifySig(secp256k1_context* ctx, std::string_view sig, std::string_view hash, std::string_view pubkey) {
    if (sig.size() != 64 || hash.size() != 32 || pubkey.size() != 32) throw herr("verify sig: bad input size");

    secp256k1_xonly_pubkey pubkeyParsed;
    if (!secp256k1_xonly_pubkey_parse(ctx, &pubkeyParsed, (const uint8_t*)pubkey.data())) throw herr("verify sig: bad pubkey");

    return secp256k1_schnorrsig_verify(
                ctx,
                (const uint8_t*)sig.data(),
                (const uint8_t*)hash.data(),
#ifdef SECP256K1_SCHNORRSIG_EXTRAPARAMS_INIT // old versions of libsecp256k1 didn't take a msg size param, this define added just after
                hash.size(),
#endif
                &pubkeyParsed
    );
}
#endif

bool Secp256Context::verify(const Event &event) const {
    Event eventToSign = {0,
            &event["pubkey"],
            &event["created_at"],
            &event["kind"],
            &event["tags"],
            &event["content"]
    };

    std::string eventData = eventToSign.to_json();

    // Calculate sha256 hash of serialized event data
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const uint8_t*>(eventData.c_str()), eventData.length(), hash);

    // convert pubkey to binary
    unsigned char pubkey_bin[32];
    hexToBytes(event["pubkey"].as<std::string_view>(), [&,pos = 0U](unsigned char x) mutable {
       if (pos < sizeof(pubkey_bin)) pubkey_bin[pos++] = x;
    });

    secp256k1_xonly_pubkey pubkey_parsed;
    if (!secp256k1_xonly_pubkey_parse(ctx.get(), &pubkey_parsed, pubkey_bin)) return false;

    unsigned char sig[64];
    hexToBytes(event["sig"].as<std::string_view>(), [&,pos = 0U](unsigned char x) mutable {
       if (pos < sizeof(sig)) sig[pos++] = x;
    });

    return secp256k1_schnorrsig_verify(
                ctx.get(),
                sig,
                hash,
#ifdef SECP256K1_SCHNORRSIG_EXTRAPARAMS_INIT // old versions of libsecp256k1 didn't take a msg size param, this define added just after
                sizeof(hash),
#endif
                &pubkey_parsed
    );


}

}
