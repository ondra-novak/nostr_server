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

bool SignatureTools::verify(const HashSha256 &id, const PublicKey &pubkey, const Signature &sig) const {
    secp256k1_xonly_pubkey pubkey_parsed;
    if (!secp256k1_xonly_pubkey_parse(ctx.get(), &pubkey_parsed, pubkey.data())) return false;
    return secp256k1_schnorrsig_verify(
                ctx.get(),
                sig.data(),
                id.data(),
#ifdef SECP256K1_SCHNORRSIG_EXTRAPARAMS_INIT // old versions of libsecp256k1 didn't take a msg size param, this define added just after
                id.size(),
#endif
                &pubkey_parsed
    );
}


bool SignatureTools::sign(const PrivateKey &key, const HashSha256 &id, Signature &sig, PublicKey &pub) const {
    secp256k1_keypair kp;
    if (!secp256k1_keypair_create(ctx.get(), &kp, key.data())) return false;
    secp256k1_xonly_pubkey xpub;
    if (!secp256k1_keypair_xonly_pub(ctx.get(), &xpub, nullptr, &kp)) return false;
    if (!secp256k1_xonly_pubkey_serialize(ctx.get(), pub.data(), &xpub)) return false;
    return secp256k1_schnorrsig_sign(ctx.get(), sig.data(), id.data(), &kp, nullptr);
}



static std::string to_hex(unsigned char *content, std::size_t len) {
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

template<int from, int to, typename Iterator,  typename Fn>
static void convert_bits(Iterator at, Iterator end, Fn fn) {
    constexpr unsigned int input_mask = ~((~0U) << from);
    constexpr unsigned int output_mask = ~((~0U) << to);
    unsigned int  accum = 0;
    int sz = 0;
    while (at != end) {
        unsigned int val = (*at) & input_mask;
        sz+=from;
        accum = (accum << from) | val;
        while (sz >= to) {
            unsigned int b = (accum >> (sz - to)) & output_mask;
            fn(b);
            sz -= to;
        }
        ++at;
    }
    if constexpr(to < from) {
        if (sz) {
            accum <<= (to - sz);
            unsigned int b = accum & output_mask;
            fn(b);
        }
    }
}



bool SignatureTools::from_nsec(const std::string &nsec, PrivateKey &pk) {
    auto r = bech32::decode(nsec);
    if (r.encoding == bech32::Encoding::Invalid) return false;
    if (r.hrp != "nsec") return false;
    convert_bits<5,8>(r.dp.begin(), r.dp.end(),[&, pos = 0U](unsigned char c) mutable {
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
    convert_bits<5,8>(r.dp.begin(), r.dp.end(),[&, pos = 0U](unsigned char c) mutable {
        if (pos < pk.size()) {
            pk[pos++] = c;
        }
    });
    return to_hex(pk.data(), pk.size());
}

std::string SignatureTools::from_bech32(const std::string &bech, std::string_view cat) {
    auto r = bech32::decode(bech);
    if (r.encoding == bech32::Encoding::Invalid) return {};
    if (r.hrp != cat) return {};
    PublicKey pk;
    convert_bits<5,8>(r.dp.begin(), r.dp.end(),[&, pos = 0U](unsigned char c) mutable {
        if (pos < pk.size()) {
            pk[pos++] = c;
        }
    });
    return to_hex(pk.data(), pk.size());

}



template<typename Fn>
static void hexToBytes(
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


std::string SignatureTools::to_npub(std::string_view public_key) {
    PublicKey key;
    hexToBytes(public_key, [&, p = 0U](unsigned char c) mutable {
        if (p<key.size()) key[p++] = c;
    });
    std::vector<unsigned char> datapart;
    convert_bits<8,5>(key.begin(), key.end(), [&](unsigned char c){datapart.push_back(c);});
    return bech32::encode("npub", datapart);
}

std::string SignatureTools::to_bech32(std::string_view hex, const std::string &type) {
    std::vector<unsigned char> data;
    hexToBytes(hex, [&](unsigned char c) mutable {
        data.push_back(c);
    });
    std::vector<unsigned char> datapart;
    convert_bits<8,5>(data.begin(), data.end(), [&](unsigned char c){datapart.push_back(c);});
    return bech32::encode(type, datapart);
}

std::string SignatureTools::to_nsec(const PrivateKey &key) {
    std::vector<unsigned char> datapart;
    convert_bits<8,5>(key.begin(), key.end(), [&](unsigned char c){datapart.push_back(c);});
    return bech32::encode("nsec", datapart);
}

static int copy_shared_pt_x(unsigned char *output,const unsigned char *x32,const unsigned char *,void *) {
    std::copy(x32, x32+32, output);
    return 1;
}

bool SignatureTools::shared_secret(const PrivateKey &pk, const PublicKey &pub, SharedSecret &secret) const {
    unsigned char pubkey_bin[33];

    pubkey_bin[0] = 2;
    std::copy(pub.begin(), pub.end(), pubkey_bin+1);

    secp256k1_pubkey pubk;
    if (!secp256k1_ec_pubkey_parse(ctx.get(), &pubk, pubkey_bin, sizeof(pubkey_bin))) {
        return false;
    }

    if (!secp256k1_ecdh(ctx.get(), secret.data(), &pubk, pk.data(),copy_shared_pt_x, nullptr)) {
        return false;
    }
    return true;

}


bool SignatureTools::encrypt(const PrivateKey &sender, const PublicKey &receiver, std::string_view message, std::string &encrypted_message) const {
    SharedSecret secret;
    unsigned char iv[16];
    if (!shared_secret(sender, receiver, secret)) return false;
    if (RAND_bytes(iv, sizeof(iv)) != 1) return false;
    PEVP_CIPHER_CTX evp_ctx(EVP_CIPHER_CTX_new());
    if (!evp_ctx) return false;
    if (EVP_EncryptInit_ex(evp_ctx.get(), EVP_aes_256_cbc(), nullptr, secret.data(), iv) != 1) return false;
    std::vector<uint8_t> ciphertext(message.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
    int len = ciphertext.size();
    if (EVP_EncryptUpdate(evp_ctx.get(), ciphertext.data(), &len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) != 1) return false;
    int len2 = ciphertext.size()- len;;
    if (EVP_EncryptFinal_ex(evp_ctx.get(), ciphertext.data() + len, &len2) != 1) return false;
    len+=len2;
    coroserver::base64::encode(std::string_view(reinterpret_cast<char *>(ciphertext.data()), len),
            [&](char c){encrypted_message.push_back(c);});
    encrypted_message.append("?iv=");
    coroserver::base64::encode(std::string_view(reinterpret_cast<char *>(iv), sizeof(iv)),
            [&](char c){encrypted_message.push_back(c);});
    return true;
}
bool SignatureTools::decrypt(const PrivateKey &receiver, const PublicKey &sender, std::string_view encrypted_message, std::string &message) const {
    auto pos = encrypted_message.find("?iv=");
    if (pos == encrypted_message.npos) return false;
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

    SharedSecret secret;
    if (!shared_secret(receiver, sender, secret)) return false;

    if (EVP_DecryptInit_ex(evp_ctx.get(), EVP_aes_256_cbc(), nullptr, secret.data(), iv) != 1) return false;

    int len;

    EVP_CIPHER_CTX_set_padding(evp_ctx.get(), 1);
    std::vector<uint8_t> plaintext(bin_message.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
    int plaintextLen = bin_message.size();

    if (EVP_DecryptUpdate(evp_ctx.get(), plaintext.data(), &len, reinterpret_cast<const unsigned char*>(bin_message.data()), bin_message.size()) != 1) return false;
    plaintextLen = len;
    if (EVP_DecryptFinal_ex(evp_ctx.get(), plaintext.data() + len, &len) != 1) return false;
    plaintextLen += len;

    message.append(reinterpret_cast<const char *>(plaintext.data()), plaintextLen);
    return true;
}

bool SignatureTools::random_private_key(PrivateKey &key) const {
    while (true) {
        if (RAND_bytes(key.data(), key.size()) != 1) {
            return {};
        }
        secp256k1_keypair kp;
        if (secp256k1_keypair_create(ctx.get(), &kp, key.data())) {
            return true;
        }
    }

}

}

