#include "Collector.h"

#include <cstring>
#include <sstream>
#include <iomanip>

namespace telemetry {

namespace open_metrics {

template<typename T>
static void render_double(std::ostream &output, T v) {
    if (std::isnan(v)) output<<"NaN";
    else if (std::isfinite(v)) output<<v;
    else if (v<0) output << "-Inf";
    else output << "+Inf";
}

static void render_escaped(std::ostream &output, std::string_view v) {
    for (char c: v) {
        switch (c) {
            case '\n': output << "\\n";break;
            case '"': output << "\\\"";break;
            case '\\': output << "\\\\";break;
            default: output.put(c);
        }
    }
}

template<typename T>
static void render(std::ostream &output, const T &val) {
    if constexpr(std::is_base_of_v<DecimalString, T>) {
        output << val;
    }else if constexpr(std::is_integral_v<T>) {
        output << val;
    } else if constexpr(std::is_arithmetic_v<T>) {
        render_double(output, val);
    } else {
        render_escaped(output, val);
    }
}

Collector::Collector()
        :shared_sensors(_mx)
        ,unique_sensors(_mx)
        ,_collect_start(Timestamp::clock::now())
        ,_store_pusher([this](MetricPoint &&pt){store_pusher(std::move(pt));})
{}

bool Collector::can_register_unique_sensor(const std::type_info &type) {
    //already locked
    return unique_sensors.defined(type);
}

bool Collector::can_register_shared_sensor(const std::type_info &type) {
    //already locked
    return shared_sensors.defined(type);
}

PSensor Collector::get_shared_sensor(const std::type_info &type) {
    std::lock_guard _(_mx);
    auto iter = _shared_sensors.find(type);
    if (iter != _shared_sensors.end()) return iter->second.lock();
    return {};
}

PSensor Collector::register_shared_sensor(PSensor sensor) {
    std::lock_guard _(_mx);
    auto iter= _shared_sensors.find(sensor->type());
    if (iter != _shared_sensors.end()) {
        PSensor s = iter->second.lock();
        if (s) return s;
        iter->second = sensor;
    } else {
        _shared_sensors.emplace(sensor->type(), sensor);
    }

    //no locking need now, as the sensor is not yet created
    std::optional<MetricsReader> mr = shared_sensors.TypeMap::convert(sensor->get());
    if (!mr.has_value()) return {};
    _active_shared.push_back({sensor, *mr, Timestamp::clock::now()});
    return sensor;
}

PSensor Collector::register_unique_sensor(PSensor sensor) {
    std::lock_guard _(_mx);
    //no locking need now, as the sensor is not yet created
    std::optional<MetricsReader> mr = unique_sensors.TypeMap::convert(sensor->get());
    if (!mr.has_value()) return {};
    _active_shared.push_back({sensor, *mr, Timestamp::clock::now()});
    return sensor;
}

TracerPtr Collector::begin_span(any_cref , void *, std::size_t) {
    return {};
}

void Collector::store_pusher(MetricPoint &&pt) {
    _tmp_metrics.push_back({
        std::move(pt),
        _tmp_cur_created
    });
}

static auto uptime_metric = defMetric({
    MetricType::counter,
    "uptime",
    "Measure the up-time of the component. When the uptime stops to raise over the time, the component is probably down",
    "seconds"
});




void Collector::collect(std::ostream &output) {
    std::lock_guard _(_mx);
    collect_lk(output);
}

void Collector::collect_and_export(Exporter &exporter) {
    std::lock_guard _(_mx);
    collect_and_export_lk(exporter);
}


void Collector::collect_and_export_lk(Exporter &exporter) {
    _tmp_stream.str(std::string());
    collect_lk(_tmp_stream);
    auto s = _tmp_stream.str(); //TODO use .view() in C++20
    exporter(s);
}

static int compareDefintion(const MetricDefinition &a, const MetricDefinition &b) {
    return a._identifier.compare(b._identifier);
}

static const MetricDefinition emptyDesc = {};


void Collector::collect_lk(std::ostream &output) {

    Emitter emit(_store_pusher);

    _tmp_metrics.clear();
    auto now = std::chrono::system_clock::now();

    for (auto &sdef: _active_unique) {
        _tmp_cur_created = sdef._created;
        std::lock_guard _(*sdef._sensor);
        sdef._reader(emit);
    }
    for (auto &sdef: _active_shared) {
        _tmp_cur_created = sdef._created;
        std::lock_guard _(*sdef._sensor);
        sdef._reader(emit);
    }



    //sort to have grouped by definition and then by attributes
    std::stable_sort(_tmp_metrics.begin(), _tmp_metrics.end(), [&](const MetricPointTimestamp &a, const MetricPointTimestamp &b){
       int c = compareDefintion(*a.first._definition,*b.first._definition);
       if (!c) {
          return a.first._ts < b.first._ts;
       } else {
           return c<0;
       }
    });

    _tmp_cur_created = _collect_start;
    emit(uptime_metric, to_nanosec(now) - to_nanosec(_collect_start));


    auto prev_def = &emptyDesc;
    for (const auto &[pt, cts]: _tmp_metrics) {
        auto cur_def = pt._definition;
        Timestamp rts = pt._ts == Timestamp::min()?now:pt._ts;
        render_metrics(rts, cts, pt, compareDefintion(*prev_def, *cur_def), output);
        prev_def = cur_def.get();
    }

    _active_shared.erase(
       std::remove_if(_active_shared.begin(), _active_shared.end(), [&](SensorDef &def){
            return !def._sensor->any_sensor();
       }),_active_shared.end());
    _active_unique.erase(
       std::remove_if(_active_unique.begin(), _active_unique.end(), [&](SensorDef &def){
            return !def._sensor->any_sensor();
       }),_active_unique.end());


}

Decimal<9> Collector::to_nanosec(Timestamp ts) {
    return Decimal<9>::from_raw(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                    ts.time_since_epoch()).count());
}

void Collector::render_metrics(Timestamp now, Timestamp created, const MetricPoint &pt,  bool render_description, std::ostream &output) {
    if (render_description) {
            render_definition(*pt._definition, output);
    }
    render_value(now, pt, output);
    switch (pt._definition->_type) {
        case MetricType::summary:
        case MetricType::counter:
        case MetricType::histogram: {
            render_metric_point(pt._definition.get(), "_created", pt._attributes.get(), {}, [&](auto &output){
                auto n = to_nanosec(created);
                n.to_string([&](char c){output.put(c);});
            }, now, output);
        }break;
        default:
            break;
    }
}

std::string_view Collector::metric_type_to_string(MetricType type) {
    switch (type) {
        case MetricType::info: return "info";
        case MetricType::counter: return "counter";
        case MetricType::gauge: return "gauge";
        case MetricType::histogram: return "histogram";
        case MetricType::gauge_histogram: return "gaugehistogram";
        case MetricType::summary: return "summary";
        case MetricType::state_set: return "stateset";
        case MetricType::state_enum: return "stateset";
        default: return "unknown";
    }
}

void Collector::render_definition(const MetricDefinition &def, std::ostream &output) {
    output << "# TYPE ";
    render_metric_name(def, output);
    output << " " << metric_type_to_string(def._type) << "\n";
    if (!def._description.empty()) {
        output << "# HELP ";
        render_metric_name(def, output);
        output << " ";
        render_metric_comment(def._description, output);
        output.put('\n');
    }
    if (!def._unit.empty()) {
        output << "# UNIT ";
        render_metric_name(def, output);
        output << " ";
        render_metric_comment(def._unit, output);
        output.put('\n');
    }
}

void Collector::render_metric_identifier(std::string_view name, std::ostream &output) {
    if (name.empty()) {
        output << "error:__empty__";
        return;
    }
    char f = name[0];
    if (!std::isalpha(f) & (f != ':')) output.put('x');  //first character can't be '_';
    for (char c: name) {
        char d = branchless_if(std::isalnum(c) | (c == ':'), c, '_');
        output.put(d);
    }
}

void Collector::render_metric_name(const MetricDefinition &def, std::ostream &output) {
    render_metric_identifier(def._identifier, output);
    if (!def._unit.empty()) {
        output << "_";
        render_metric_identifier(def._unit, output);
    }


}

void Collector::render_metric_comment(std::string_view name, std::ostream &output) {
    for (char c: name) {
        switch (c) {
            case '\n': output << "\\n";break;
            case '"': output << "\\\"";break;
            case '\\': output << "\\\\";break;
            default: output.put(c);
        }
    }
}

template<typename Fn>
void Collector::render_metric_point(const MetricDefinition *def,
        const std::string_view &suffix,
        const AttributeList *attrs,
        std::initializer_list<AttributeKeyValue> extra_attributes,
        Fn &&value_render, Timestamp ts,
        std::ostream &output) {
    render_metric_name(*def, output);
    output << suffix;
    render_attributes(attrs, extra_attributes, output);
    output.put(' ');
    value_render(output);
    output.put(' ');
    auto tmv = Decimal<9>::from_raw(std::chrono::duration_cast<std::chrono::nanoseconds>(ts.time_since_epoch()).count());
    tmv.to_string([&](char c){output.put(c);});
    output.put('\n');
}


void Collector::render_value(Timestamp ts, const MetricPoint &pt, std::ostream &output) {

    std::visit([&](const auto &v){
        using T = std::remove_const_t<std::remove_reference_t<decltype(v)> >;
        auto definition = pt._definition.get();
        auto attrs = pt._attributes.get();

        if constexpr(std::is_null_pointer_v<T>) {
            render_metric_point(definition, "_info",attrs, {},
                    [&](auto &output){render(output,1);},
                    ts, output);

        } else if constexpr(std::is_same_v<T, StateData>) {
            const StateDef &stdef = *v._def;
            std::uint64_t idx = 0;
            auto renderer = definition->_type == MetricType::state_enum
                    ?[](std::ostream &output, std::uint64_t &idx, std::uint64_t val){
                        render(output, idx == val?1:0);
                    }
                    :[](std::ostream &output, std::uint64_t &idx, std::uint64_t val){
                        render(output, ((1<<idx) & val)?1:0);
                    };
            for (const auto &n: stdef.values) {
                render_metric_point(definition, "", attrs,
                        {{stdef.key, n}}, [&](auto &output){renderer(output, idx, v._value);}, ts, output);
                ++idx;
            }

        } else if constexpr(std::is_base_of_v<SummaryData<std::intmax_t>, T>
                        || std::is_base_of_v<SummaryData<std::uintmax_t>, T>
                        || std::is_base_of_v<SummaryData<double>, T>
                        || std::is_base_of_v<SummaryData<DecimalString>, T>) {
            switch (pt._definition->_type) {
                default: return;    //don't print anything - not match
                case MetricType::summary:
                case MetricType::histogram:
                    render_metric_point(definition, "_sum",attrs, {},
                            [&](auto &output){render(output,v._sum);},
                            ts, output);
                    render_metric_point(definition, "_count",attrs, {},
                            [&](auto &output){render(output, v._count);},
                            ts, output);
                break;
                case MetricType::gauge_histogram:
                    render_metric_point(definition, "_gsum",attrs, {},
                            [&](auto &output){render(output, v._sum);},
                            ts, output);
                    render_metric_point(definition, "_gcount",attrs, {},
                            [&](auto &output){render(output, v._count);},
                            ts, output);
                 break;
            }
            if constexpr(std::is_base_of_v<HistogramData<std::intmax_t>, T>
                                || std::is_base_of_v<HistogramData<std::uintmax_t>, T>
                                || std::is_base_of_v<HistogramData<double>, T>
                                || std::is_base_of_v<HistogramData<DecimalString>, T>) {
                if (pt._definition->_type == MetricType::histogram) {
                    std::size_t cnt = std::visit([](const auto &x){
                        if constexpr(std::is_pointer_v<std::remove_reference_t<decltype(x)> >) {
                            return x->size();
                        } else {
                            return x.size();
                        }
                    }, v._boundaries);
                    std::uintmax_t sum = 0;
                    for (std::size_t i = 0; i < cnt; i++) {
                        std::string le = std::visit([&](const auto &x) -> std::string {
                            if constexpr(std::is_convertible_v<decltype(x), const std::vector<DecimalString> *>) {
                                return x->at(i);
                            } else if constexpr(std::is_pointer_v<std::remove_reference_t<decltype(x)> >) {
                                return std::to_string(x->at(i));
                            } else {
                                return x.at(i);
                            }
                        },v._boundaries);

                        render_metric_point(definition, "_bucket", attrs,
                                {{"le",le}},
                                [&](auto &output){render(output,sum+=v._counts.at(i));}, ts, output);
                    }
                    render_metric_point(definition, "_bucket", attrs,
                            {{"le","+Inf"}},
                            [&](auto &output){render(output , sum+=v._counts[cnt]);}, ts, output);
                }

            }

        } else {
            auto direct_print = [&](auto &output){
                render(output, v);
            };
            switch (pt._definition->_type) {
                case MetricType::counter:
                    render_metric_point(definition, "_total",attrs, {},direct_print, ts, output);
                    break;
                default:
                case MetricType::gauge:
                    render_metric_point(definition, "",attrs, {},direct_print, ts, output);
                    break;
            }
        }
    }, pt._value);

}

void Collector::render_attributes(const AttributeList *list,
        std::initializer_list<AttributeKeyValue> extra_attributes,
        std::ostream &output) {
    if ((!list || list->empty()) && extra_attributes.size() == 0) return;
    char sep = '{';
    if (list) {
        for (const auto &[key, value] : *list) {
            output.put(sep);
            render_attribute_key(key, output);
            output.put('=');
            render_attribute_value(value, output);
            sep = ',';
        }
    }
    for (const auto &[key, value] : extra_attributes) {
        output.put(sep);
        render_attribute_key(key, output);
        output.put('=');
        render_attribute_value(value, output);
        sep = ',';
    }
    output.put('}');
}

void Collector::render_attribute_key(std::string_view name, std::ostream &output) {
    if (name.empty()) {
        output << "__empty";
        return;
    }
    char c = name[0];
    if (std::isdigit(c)) output.put('n');
    for (char c: name) {
        output.put(branchless_if(std::isalnum(c),c,'_'));
    }
}

void Collector::render_attribute_value(const AttributeValue &val, std::ostream &output) {
    output.put('"');
    std::visit([&](const auto &v){
        render(output, v);
    }, val);
    output.put('"');

}

AbstractProvider::SensorType Collector::can_register_sensor_auto(const std::type_info &type) {
    std::lock_guard _(_mx);
    int r =  (unique_sensors.TypeMap::defined(type)?1:0)
            +(shared_sensors.TypeMap::defined(type)?2:0);
    SensorType results[4] = {
            SensorType::none,
            SensorType::unique,
            SensorType::shared,
            SensorType::none};
    return results[r];
}

}

}
