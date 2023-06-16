#pragma once

#include <mutex>
namespace telemetry {

///Disables lock - useful when atomic variables are used (so locking has no effect at all)
struct NoLock {
    constexpr void lock() noexcept {}
    constexpr void unlock() noexcept  {}
    constexpr bool try_lock() noexcept {return true;}
};

}
