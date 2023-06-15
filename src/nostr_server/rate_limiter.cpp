
#include "rate_limiter.h"

namespace nostr_server {


RateLimiter::RateLimiter(unsigned int window_size, unsigned int window_limit)
:_window_size(window_size)
,_window_limit(window_limit)
{
}


bool RateLimiter::test_and_add(std::chrono::system_clock::time_point pt)  {
    if (_window_size == 0) return true;
    auto begin_window = pt-std::chrono::seconds(_window_size);
    while (!_q.empty() && _q.front() < begin_window) {
        _q.pop();
    }
    if (_q.size() >= _window_limit) return false;
    _q.push(pt);
    return true;
}


} /* namespace nostr_server */
