#pragma once

#include "../histogram_aggreg.h"
#include "../no_lock.h"
#include "../avg_aggreg.h"
#include "../counter_aggreg.h"

namespace telemetry {

namespace open_metrics {


///Histogram class
/**
 * In contract to generic histogram, this histogram represents OpenMetrics histogram compatible object
 * @tparam T recorder type - supported std::uintmax_t, std::intmax_t, double and Decimal<>. The
 * limitation is defined by support of MetricPoint structure
 * @tparam Lock lock
 */
template<typename T, typename Lock = NoLock>
class Histogram {
public:
    ///Construct histogram.
    /**
     * @param boundaries boundaries, mandatory
     */
    Histogram(std::vector<T> boundaries)
        :_hist(std::move(boundaries)) {}


    ///Push new value
    void push_back(T val) {
        std::lock_guard _(_mx);
        _hist.push_back(val);
        _smr.push_back(val);
    }

    ///Retrieve boundaries
    const std::vector<T> &boundares() const {
        return _hist.ranges();
    }

    ///Retrieve buckets
    std::vector<std::size_t> buckets() const {
        std::lock_guard _(_mx);
        std::vector<std::size_t> out;
        const auto &src = _hist.buckets();
        out.reserve(src.size());
        std::transform(src.begin(), src.end(), std::back_inserter(out),
                [](const auto &x)->std::size_t {return x.value();}
        );
        return out;
    }
    ///Retrieve sum
    T sum() const {
        std::lock_guard _(_mx);
        return _smr.sum();
    }
    ///Retrieve count
    std::size_t count() const {
        std::lock_guard _(_mx);
        return _smr.count();
    }

    ///clear container
    void clear() {
        std::lock_guard _(_mx);
        _hist.clear();
        _smr.clear();
    }

    ///sample - return value and reset content
    Histogram sample() {
        std::lock_guard _(_mx);
        return std::exchange(*this, Histogram(std::vector<T>(_hist.ranges())));
    }

protected:
    HistogramT<EventCounter<T,std::size_t>, true> _hist;
    AvgAggregation<T> _smr;
    mutable Lock _mx;
};



}

}
