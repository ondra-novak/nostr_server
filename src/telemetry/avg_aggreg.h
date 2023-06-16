#pragma once
#include "no_lock.h"
#include "branchless.h"

#include <cstddef>

namespace telemetry {

///Aggregation "container" which calculates average
/**
 * @tparam T type to aggregate (default double)
 * @tparam Lock Specify lock, if you need to MT Safe to object. Default is NoLock
 */
template<typename T = double, typename Lock = NoLock>
class AvgAggregation {
public:

    using value_type = T;

    AvgAggregation() = default;
    AvgAggregation(const T &sum, const std::size_t &count)
        :_sum(sum), _count(count) {}

    void clear() {
        _sum = T{}; //assume that it is zero initialized
        _count = 0;

    }

    ///Push back the value
    void push_back(const T &v) {
        std::lock_guard _(_lock);
        _sum = branchless_if(_count > 0, _sum+v, v);
        ++_count;
    }

    ///Append value
    AvgAggregation &operator += (const T &v) {
        push_back(v);
    }


    ///Merge two containers into new one
    /**
     * @param other other container
     * @return result of merged containers. It is not average of two averages,
     * it is average of both containes together
     */
    template<typename L>
    AvgAggregation operator + (const AvgAggregation<T,L> &other) const {
        std::scoped_lock<Lock, L> _(_lock, other._lock);
        AvgAggregation<T,L>(
                branchless_aggregate(_count>0, other._count>0,
                                    _sum, other._sum, [](const T &a, const T &b){
                    return a+b;}), _count + other._count);
    }

    ///Merge two containers into new one
    /**
     * @param other other container
     * @return result of merged containers. It is not average of two averages,
     * it is average of both containes together
     */
    template<typename L>
    AvgAggregation &operator += (const AvgAggregation<T,L> &other) {
        std::scoped_lock<Lock, L> _(_lock, other._lock);
        _sum = branchless_aggregate(_count>0, other._count>0,
                            _sum, other._sum, [](const T &a, const T &b){
                    return a+b;});
        _count += other._count;
        return *this;
    }

    ///Returns average
    auto value() const {
        std::lock_guard _(_lock);
        return _sum/(double)_count;
    }

    ///Atomicaly retrieves sample and resets the container
    AvgAggregation sample()  {
        std::lock_guard _(_lock);
        return std::exchange(*this, AvgAggregation());
    }
    ///Retrieve count of items
    std::size_t count() const {
        std::lock_guard _(_lock);
        return _count;
    }

    ///Retrieve sum of items
    T sum() const {
        std::lock_guard _(_lock);
        return _sum;
    }

protected:
    [[no_unique_address]] mutable Lock _lock;
    T _sum = {};
    std::size_t _count = 0;


};



}
