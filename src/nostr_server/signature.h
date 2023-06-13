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


namespace nostr_server {

class Secp256Context {
public:

    Secp256Context();

    bool verify(const Event &event) const;



protected:
    struct Deleter {
        void operator()(secp256k1_context *ptr) const;
    };

    std::unique_ptr<secp256k1_context, Deleter> ctx;
};


bool verifySignature(const Event &event);

}

#endif /* SRC_NOSTR_SERVER_SIGNATURE_H_ */
