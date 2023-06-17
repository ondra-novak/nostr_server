#include "signature.h"
#include <docdb/json.h>

#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>
#include <secp256k1_ecdh.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <coroserver/ssl_common.h>
#include <coroserver/strutils.h>

#include "bech32.h"

namespace nostr_server {

struct EVP_CIPHER_CTX_deleter {
    void operator()(EVP_CIPHER_CTX *ctx) {EVP_CIPHER_CTX_free(ctx);}
};

using PEVP_CIPHER_CTX = std::unique_ptr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_deleter>;


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
    SHA256(reinterpret_cast<const uint8_t*>(eventData.data()), eventData.size(), hash.data());
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
            PublicKey pubk;
            if (secp256k1_xonly_pubkey_serialize(ctx.get(), pubk.data(), &pub)) {
                return to_hex(pubk.data(), pubk.size());
            }
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

template<typename Fn>
static void convert_bits_5_to_8(std::vector<unsigned char> data, Fn fn) {
    std::uint16_t accum = 0;
    int sz = 0;
    for (unsigned char x: data) {
        sz+=5;
        accum  = (accum << 5) | x;
        if (sz >= 8) {
            unsigned char b = accum >> (sz - 8);
            fn(b);
            sz -= 8;
        }
    }
}


bool SignatureTools::from_nsec(const std::string &nsec, PrivateKey &pk) {
    auto r = bech32::decode(nsec);
    if (r.encoding == bech32::Encoding::Invalid) return false;
    if (r.hrp != "nsec") return false;
    convert_bits_5_to_8(r.dp,[&, pos = 0U](unsigned char c) mutable {
        if (pos < pk.size()) {
            pk[pos++] = c;
        }
    });
    return true;
}

std::string SignatureTools::from_npub(const std::string &npub) {
    auto r = bech32::decode(npub);
    if (r.encoding == bech32::Encoding::Invalid) return {};
    if (r.hrp != "npub") return {};
    PublicKey pk;
    convert_bits_5_to_8(r.dp,[&, pos = 0U](unsigned char c) mutable {
        if (pos < pk.size()) {
            pk[pos++] = c;
        }
    });
    return to_hex(pk.data(), pk.size());
}

static int copy_shared_pt_x(unsigned char *output,const unsigned char *x32,const unsigned char *,void *) {
    std::copy(x32, x32+32, output);
    return 1;
}

bool SignatureTools::create_shared_secret(const PrivateKey &pk, const std::string_view &to_publickey, SharedSecret &secret) {
    unsigned char pubkey_bin[33];

    pubkey_bin[0] = 2;
    hexToBytes(to_publickey, [&,pos = 1U](unsigned char x) mutable {
       if (pos < sizeof(pubkey_bin)) pubkey_bin[pos++] = x;
    });

    secp256k1_pubkey pubk;
    if (!secp256k1_ec_pubkey_parse(ctx.get(), &pubk, pubkey_bin, sizeof(pubkey_bin))) {
        return false;
    }

    if (!secp256k1_ecdh(ctx.get(), secret.data(), &pubk, pk.data(),copy_shared_pt_x, nullptr)) {
        return false;
    }
    return true;

}

std::optional<Event> SignatureTools::create_private_message(const PrivateKey &pk,
                const std::string_view &to_publickey, const std::string_view &text, std::time_t created_at) {

    SharedSecret shared_secret;
    if (!create_shared_secret(pk, to_publickey, shared_secret)) return {};

    unsigned char iv[16];

    if (RAND_bytes(iv, sizeof(iv)) != 1) {
        return {};
    }


    PEVP_CIPHER_CTX evp_ctx(EVP_CIPHER_CTX_new());
    if (!evp_ctx) return {};

    if (EVP_EncryptInit_ex(evp_ctx.get(), EVP_aes_256_cbc(), nullptr, shared_secret.data(), iv) != 1) {
        return {};
    }

    std::vector<uint8_t> ciphertext(text.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
    int len = ciphertext.size();
    if (EVP_EncryptUpdate(evp_ctx.get(), ciphertext.data(), &len, reinterpret_cast<const unsigned char*>(text.data()), text.size()) != 1) {
        return {};
    }
    int len2 = ciphertext.size()- len;;
    if (EVP_EncryptFinal_ex(evp_ctx.get(), ciphertext.data() + len, &len2) != 1) {
        return {};
    }
    len+=len2;
    std::string outmsg;
    coroserver::base64::encode(std::string_view(reinterpret_cast<char *>(ciphertext.data()), len),
            [&](char c){outmsg.push_back(c);});
    outmsg.append("?iv=");
    coroserver::base64::encode(std::string_view(reinterpret_cast<char *>(iv), sizeof(iv)),
            [&](char c){outmsg.push_back(c);});

    Event out_ev = {
            {"content",outmsg},
            {"kind",4},
            {"created_at", created_at},
            {"tags",Event::Array {
                    {"p",to_publickey}
            }}
    };
    sign(pk, out_ev);
    return out_ev;
}

std::optional<std::string> SignatureTools::decrypt_private_message(const PrivateKey &pk, const Event &ev) {

    std::string_view to_publickey = ev["pubkey"].as<std::string_view>();
    std::string_view message = ev["content"].as<std::string_view>();
    auto pos = message.find("?iv=");
    if (pos == message.npos) return {};
    auto iv_text = message.substr(pos+4);
    auto cipher_text = message.substr(0,pos);
    unsigned char iv[16];
    coroserver::base64::decode(iv_text, [&, pos = 0U](unsigned char c) mutable {
        if (pos < sizeof(iv)) {
            iv[pos++]  = c;
        }
    });
    std::vector<unsigned char> bin_message;
    coroserver::base64::decode(cipher_text, [&](unsigned char c) mutable {
        bin_message.push_back(c);
    });

    PEVP_CIPHER_CTX evp_ctx(EVP_CIPHER_CTX_new());

    SharedSecret shared_secret;
    if (!create_shared_secret(pk, to_publickey, shared_secret)) return {};

    if (EVP_DecryptInit_ex(evp_ctx.get(), EVP_aes_256_cbc(), nullptr, shared_secret.data(), iv) != 1) {
        return {};
    }

    int len;

    EVP_CIPHER_CTX_set_padding(evp_ctx.get(), 1);
    std::vector<uint8_t> plaintext(bin_message.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
    int plaintextLen = bin_message.size();

    if (EVP_DecryptUpdate(evp_ctx.get(), plaintext.data(), &len, reinterpret_cast<const unsigned char*>(bin_message.data()), bin_message.size()) != 1) {
        return {};
    }
    plaintextLen = len;
    if (EVP_DecryptFinal_ex(evp_ctx.get(), plaintext.data() + len, &len) != 1) {
        return {};
    }
    plaintextLen += len;

    return std::string(reinterpret_cast<const char *>(plaintext.data()), plaintextLen);
}

}

