#include "signature.h"
#include <docdb/json.h>

#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>
#include <openssl/sha.h>

#include "bech32.h"

namespace nostr_server {

SignatureTools::SignatureTools():ctx(secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY)) {}
void SignatureTools::Deleter::operator ()(secp256k1_context *ptr) const {
    secp256k1_context_destroy(ptr);
}

void SignatureTools::get_hash(const Event &event, HashSha256 &hash) const {
    Event eventToSign = {0,
            &event["pubkey"],
            &event["created_at"],
            &event["kind"],
            &event["tags"],
            &event["content"]
    };

    std::string eventData = eventToSign.to_json(docdb::Structured::flagUTF8);

    // Calculate sha256 hash of serialized event data
    SHA256(reinterpret_cast<const uint8_t*>(eventData.c_str()), hash.size(), hash.data());
}


bool SignatureTools::verify(const Event &event) const {
    HashSha256 hash;
    get_hash(event, hash);

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
                hash.data(),
#ifdef SECP256K1_SCHNORRSIG_EXTRAPARAMS_INIT // old versions of libsecp256k1 didn't take a msg size param, this define added just after
                hash.size(),
#endif
                &pubkey_parsed
    );


}

#if 0
void SignatureTools::sign(Event &event) const {
    HashSha256 hash;
    get_hash(event, hash);

    secp256k1_keypair_create(ctx, keypair, seckey)
    secp256k1_schnorrsig_sign(ctx, sig64, msg32, keypair, aux_rand32)ctx, sig64, msg32, keypair, aux_rand32)

}
#endif

std::string SignatureTools::calculate_keypair(const PrivateKey &key, secp256k1_keypair &kp) const {
    if (secp256k1_keypair_create(ctx.get(), &kp, key.data())) {
        secp256k1_xonly_pubkey pub;
        if (secp256k1_keypair_xonly_pub(ctx.get(), &pub, nullptr, &kp)) {
            return to_hex(pub.data, sizeof(pub.data));
        }
    }
    return {};
}


bool SignatureTools::sign(const PrivateKey &key, Event &event) const {
    secp256k1_keypair kp;
    std::string pubkey = calculate_keypair(key, kp);
    if (pubkey.empty()) return false;
    event.set("pubkey", pubkey);
    unsigned char sig64[64];
    HashSha256 hash;
    get_hash(event, hash);
    if (secp256k1_schnorrsig_sign(ctx.get(), sig64, hash.data(), &kp, nullptr)) {
        event.set("sig", to_hex(sig64, sizeof(sig64)));
        return true;
    }
    return false;
}

std::string SignatureTools::calculate_pubkey(const PrivateKey &key) const {
    secp256k1_keypair kp;
    return calculate_keypair(key, kp);
}


std::string SignatureTools::to_hex(unsigned char *content, std::size_t len) {
    std::string out;
    constexpr char hexletters[] = "0123456789abcdef";
    for (std::size_t i = 0; i < len; i++ ) {
        unsigned char c = content[i];
        unsigned char c1 = c >> 4;
        unsigned char c2 = c & 0xF;
        out.push_back(hexletters[c1]);
        out.push_back(hexletters[c2]);
    }
    return out;
}

bool SignatureTools::from_nsec(const std::string &nsec, PrivateKey &pk) {
    auto r = bech32::Decode(nsec);
    if (r.encoding == bech32::Encoding::INVALID) return false;
    if (r.hrp != "nsec") return false;
    if (r.data.size() != pk.size()) return false;
    std::copy(r.data.begin(), r.data.end(), pk.begin());
    return true;
}

std::string SignatureTools::from_npub(const std::string &npub) {
    auto r = bech32::Decode(npub);
    if (r.encoding == bech32::Encoding::INVALID) return {};
    if (r.hrp != "npub") return {};
    return to_hex(r.data.data(), r.data.size());
}

}

