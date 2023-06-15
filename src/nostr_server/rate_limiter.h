/*
 * rate_limiter.h
 *
 *  Created on: 15. 6. 2023
 *      Author: ondra
 */

#ifndef SRC_NOSTR_SERVER_RATE_LIMITER_H_
#define SRC_NOSTR_SERVER_RATE_LIMITER_H_
#include <chrono>
#include <queue>

namespace nostr_server {

class RateLimiter {
public:
    RateLimiter(unsigned int window_size, unsigned int window_limit);


    bool test_and_add(std::chrono::system_clock::time_point pt) ;


protected:
    unsigned int _window_size;
    unsigned int _window_limit;
    std::queue<std::chrono::system_clock::time_point> _q;


};

} /* namespace nostr_server */

#endif /* SRC_NOSTR_SERVER_RATE_LIMITER_H_ */
