#pragma once

#include "../decimal.h"
#include "../avg_aggreg.h"

#include <chrono>
#include <variant>
#include <vector>
#include <functional>
#include <memory>
#include <type_traits>

namespace telemetry {


namespace open_metrics {

///Summary object. To construct this object from AvgAggregation, call summary() from tools
template<typename T>
struct SummaryData {
    T _sum = 0;
    std::size_t _count = 0;

    SummaryData() = default;
    SummaryData(T sum, std::size_t count):_sum(sum),_count(count) {}

    template<typename X, typename L, typename = std::enable_if_t<std::is_convertible_v<X, T> > >
    SummaryData(const AvgAggregation<T, L> &avg)
        :_sum(avg.sum())
        ,_count(avg.count()) {}

};

template<typename T, typename Lock>
class Histogram;

///Histogram object.
template<typename T>
struct HistogramData: SummaryData<T> {
    std::variant<
        //reference to boundaries in double
        const std::vector<double> *,
        //reference to boundaries in int
        const std::vector<int> *,
        //reference to boundaries in DecimalString
        const std::vector<DecimalString> *,
        //when boundaries cannot be stored as reference, because they were converted from Decimal<>
        std::vector<DecimalString>
        > _boundaries;
    std::vector<std::size_t> _counts;

    HistogramData()= default;
    HistogramData(T sum, std::size_t count, const std::vector<double> &boundaries, std::vector<std::size_t> counts)
        :SummaryData<T>(sum, count)
        , _boundaries(&boundaries)
        , _counts(std::move(counts)) {}
    HistogramData(T sum, std::size_t count, const std::vector<int> &boundaries, std::vector<std::size_t> counts)
        :SummaryData<T>(sum, count)
        , _boundaries(&boundaries)
        , _counts(std::move(counts)) {}
    HistogramData(T sum, std::size_t count, const std::vector<DecimalString> &boundaries, std::vector<std::size_t> counts)
        :SummaryData<T>(sum, count)
        , _boundaries(&boundaries)
        , _counts(std::move(counts)) {}
    template<typename X, typename = std::enable_if_t<std::is_convertible_v<X, DecimalString> > >
    HistogramData(T sum, std::size_t count, const std::vector<X> &boundaries, std::vector<std::size_t> counts)
        :SummaryData<T>(sum, count)
        , _counts(std::move(counts)) {
        std::vector<DecimalString> x;
        std::transform(boundaries.begin(),
                boundaries.end(),
                std::back_inserter(x),
                [](const auto &n) {return DecimalString(n);});
        _boundaries = x;
    }

    ///prevent to construct from temporary storage
    HistogramData(T sum, std::size_t count, const std::vector<double> &&boundaries, std::vector<std::size_t> ) = delete;
    ///prevent to construct from temporary storage
    HistogramData(T sum, std::size_t count, const std::vector<int> &&boundaries, std::vector<std::size_t>) = delete;
    ///prevent to construct from temporary storage
    HistogramData(T sum, std::size_t count, const std::vector<DecimalString> &&boundaries, std::vector<std::size_t>) = delete;

    template<typename X,typename L>
    HistogramData(const Histogram<X, L> &h)
        :HistogramData(h.sum(), h.count(), h.boundares(), h.buckets()) {}
};


///Contains definition of state
struct StateDef {
    ///attribute key
    std::string key;
    ///defines values
    std::vector<std::string> values;
};

///Stores state_set or state_enum
struct StateData {

    StateData() = default;
    StateData(std::shared_ptr<const StateDef> def):_def(def) {}
    StateData(std::shared_ptr<const StateDef> def, std::uint64_t value):_def(def),_value(value) {}

    template<typename Enum, typename = std::enable_if<std::is_enum_v<Enum> > >
    StateData(std::shared_ptr<const StateDef> def, Enum enm)
        :_def(def), _value(static_cast<std::uint64_t>(enm)) {}
    ///state selection
    std::shared_ptr<const StateDef> _def;
    ///stored value
    std::uint64_t _value = 0;


};

///Measured value is variant, it can hold value in many form
/**
 * Output format is defined by MetricType, however some types generates one
 * format regardless on what MetricType is set
 *
 *  - @b std::monostate - reserved for info type, which doesn't have a value
 *  - @b int - counter or gauge
 *  - @b unsigned int - counter or gauge
 *  - @b std::intmax_t - counter or gauge
 *  - @b std::uintmax_t - counter or gauge
 *  - @b double - counter or gauge
 *  - @b bool - emits "stateset". For given attributes can have 1 or 0
 *  - @b DecomalString - counter or gauge - use to store Decimal
 *  - @b Summary - emits summary metric regardless on MetricType
 *  - @b Histogram - emits histogram metric regardless on MetricType
 */
using MeasuredValue = std::variant<
        std::nullptr_t,               //info (empty)
        int,                        //just integer for convenience
        unsigned int,                        //just integer for convenience
        std::intmax_t,  //maximal integer number  - gauge or counter
        std::uintmax_t, //maximal unsigned number  - gauge or counter
        double,             //just double          - gauge or counter
        bool,               //                     - always generates stateset
        DecimalString,       //Decimal<> converted to string,  - gauge or counter
        StateData,          //for state_set or state_enum
        SummaryData<std::intmax_t>,            //summary              - always generates summary
        SummaryData<std::uintmax_t>,            //summary              - always generates summary
        SummaryData<DecimalString>,            //summary              - always generates summary
        SummaryData<double>,            //summary              - always generates summary
        HistogramData<std::intmax_t>,          //histogram             - always generates histogram
        HistogramData<std::uintmax_t>,          //histogram             - always generates histogram
        HistogramData<DecimalString>,          //histogram             - always generates histogram
        HistogramData<double>          //histogram             - always generates histogram
>;


///Attribute value can contain any of specified type
/**
 * It is recommended to use Decimal->DecimalString to store decimal number as attribute value
 */
using AttributeValue = std::variant<bool, int, unsigned int, long, unsigned long,
        long long, unsigned long long, float, double, long double,
        std::string, DecimalString>;

///Metric type - is set in MetricDescription
/**
 * The type must match to emited metric type
 */
enum class MetricType {
    info,       ///<just info
    counter,  ///<any number
    gauge,      ///<any number
    histogram,      ///<histogram
    gauge_histogram,    ///<histogram
    summary,         ///<summary
    state_set,        ///<bool, StateDate
    state_enum,       ///StateData
    unknown          ///<number

};




///Metric description
struct MetricDefinition {       //recommended to declare MetricDefintion as static!
    MetricType _type;           //should match with actual metric point returned,
    std::string _identifier;     //don't add suffixes - they added automatically
    std::string _description;
    std::string _unit;
};


using AttributeKeyValue = std::pair<std::string, AttributeValue>;

///Metric attributes
using AttributeList = std::vector<AttributeKeyValue> ;

///Const pointer to MetricDefintion
/**
 * This type is expected by the emitter. You need to use defMetric()
 * to create variable of this type
 */
using CPMetricDefinition = std::shared_ptr<const MetricDefinition>;
///Const pointer to attribute list
/**
 * This type is expected by the emitter. You need to use defAttributes()
 * to create variable of this type
 */
using CPAttributeList = std::shared_ptr<const AttributeList>;

///Metric point defines complete information about measurement
/** Library user never directly access this structure. Metric is emitted by the Emitter */
struct MetricPoint {

    std::shared_ptr<const MetricDefinition> _definition;
    std::shared_ptr<const AttributeList> _attributes;
    MeasuredValue _value;           //value (skip for info, or use std::monostate)
    std::chrono::system_clock::time_point _ts;  //measure timestamp (optional, if skipped, collection timestamp is used)

};
///Collection of the metrics
using Metrics = std::vector<MetricPoint>;


///Utility object helps to emit metrics to the collector from the reader
/**
 * You get Emitter instance as argument of value reader. Use this object to emit
 * telemetric data to the collector. Emitter instance acts as a function.
 *
 */
class Emitter {
public:


    ///Emitter is helper object, which is initialized by reference to emitter function
    /** It can be copied */
    Emitter(std::function<void(MetricPoint &&)> &emitter_fn):_emitter_fn(emitter_fn) {}

    ///Emit metric point
    /**
     * @param def metric definition. The reference must not refer to temporary object
     * @param value measured value
     */
    void operator()(CPMetricDefinition def, MeasuredValue value) const {
        _emitter_fn(MetricPoint{std::move(def), nullptr, std::move(value), std::chrono::system_clock::time_point::min()});
    }
    ///Emit metric point
    /**
     * @param def metric definition. The reference must not refer to temporary object
     * @param value measured value
     * @param measure_time time when this value has been measured. Must increase
     */
    void operator()(CPMetricDefinition def, MeasuredValue value,
                            std::chrono::system_clock::time_point measure_time)  const {
        _emitter_fn(MetricPoint{std::move(def), nullptr, std::move(value), measure_time});
    }
    ///Emit metric point
    /**
     * @param def metric definition. The reference must not refer to temporary object
     * @param attrs attributes. The reference must not refer to temporary object
     * @param value measured value
     */
    void operator()(CPMetricDefinition def, CPAttributeList attrs, MeasuredValue value) const {
        _emitter_fn(MetricPoint{std::move(def), std::move(attrs), std::move(value), std::chrono::system_clock::time_point::min()});
    }
    ///Emit metric point
    /**
     * @param def metric definition. The reference must not refer to temporary object
     * @param attrs attributes. The reference must not refer to temporary object
     * @param value measured value
     * @param measure_time time when this value has been measured. Must increase
     */
    void operator()(CPMetricDefinition def, CPAttributeList attrs, MeasuredValue value,
                            std::chrono::system_clock::time_point measure_time) const {
        _emitter_fn(MetricPoint{std::move(def), std::move(attrs), std::move(value), measure_time});
    }


protected:
    static AttributeList empty_attribute_list;
    std::function<void(MetricPoint &&)> &_emitter_fn;
};

inline std::shared_ptr<const MetricDefinition> defMetric(MetricDefinition def) {
    return std::make_shared<MetricDefinition>(std::move(def));
}

inline std::shared_ptr<const MetricDefinition> defMetric(MetricType type,
        std::string identifier,
        std::string description = std::string(),
        std::string unit = std::string()) {
    return std::make_shared<MetricDefinition>(MetricDefinition{
        type, std::move(identifier), std::move(description), std::move(unit)
    });
}

inline std::shared_ptr<const AttributeList> defAttributes(AttributeList lst) {
   return std::make_shared<AttributeList>(std::move(lst));
}

inline std::shared_ptr<const StateDef> defStates(std::string key, std::vector<std::string> values) {
   return std::make_shared<StateDef>(StateDef{
       std::move(key), std::move(values)
   });
}


inline AttributeList Emitter::empty_attribute_list;





}


}
