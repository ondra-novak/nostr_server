#pragma once

#include "MetricPoint.h"
#include "Exporter.h"

#include "../typemap.h"
#include "../provider.h"
#include <sstream>

#include <functional>

namespace telemetry {

namespace open_metrics {

///Generic collector
/**
 * Not abstract class, can be used standalone. Collects metrics and builds
 * OpenMetrics compatible report
 */
class Collector: public AbstractProvider {
public:

    ///No special constructor
    Collector();

    ///can't be copied
    Collector(const Collector &) = delete;
    ///can't be assigned
    Collector &operator=(const Collector &) = delete;




    ///Defines lockable typemap, locked by mutex
    template<typename T, Access access>
    using TypeMap = TypeMapLockable<T, access, std::mutex>;

    ///Reader function, this function is defined by the user to read a particular sensor
    /**
     *  @param Emiter callable object, acts as overloaded function.
     *
     *  @see Emitter
     */
    using MetricsReader = std::function<void(Emitter)>;


    ///Map of shared sensors
    /**
     * For each registered type (sensor's type), function must return MetricReader
     * instance. The reader have guarantee access to the Sensor data
     * through the reference to it.
     */
    TypeMap<MetricsReader, Access::MUTABLE> shared_sensors;
    TypeMap<MetricsReader, Access::MUTABLE> unique_sensors;

    ///Collect the sensors and generate report in valid OpenMetrics format
    /**
     * @param output output stream.
     *
     * @note Function doesn't not put mandatory line '# END' at the end
     * of the output. If you need it, you must put it manually
     *
     */
    void collect(std::ostream &output);

    ///Collect the data and export them to the exporter
    /**
     * @param exporter reference to exporter's instance
     */
    void collect_and_export(Exporter &exporter);


    ///Timestamp shortcut
    using Timestamp = std::chrono::system_clock::time_point;

protected:

    virtual SensorType can_register_sensor_auto(const std::type_info &type) override;
    virtual bool can_register_unique_sensor(const std::type_info &type) override;
    virtual bool can_register_shared_sensor(const std::type_info &type) override;
    virtual PSensor get_shared_sensor(const std::type_info &type) override;
    virtual PSensor register_shared_sensor(PSensor sensor) override;
    virtual PSensor register_unique_sensor(PSensor sensor) override;
    virtual void push() override {/*not implemented*/}

    virtual TracerPtr begin_span(any_cref ident, void *alloc_space, std::size_t alloc_space_size) override;

    //sensor definition
    struct SensorDef {
        //associated sensor
        PSensor _sensor;
        //metric reader function
        MetricsReader _reader;
        //when sensor created
        Timestamp _created;
    };

    using SensorList = std::vector<SensorDef>;

    using MetricPointTimestamp = std::pair<MetricPoint, Timestamp> ;


    std::mutex _mx;
    TypeInfoMap<std::weak_ptr<AbstractSensor> > _shared_sensors;
    SensorList _active_shared;
    SensorList _active_unique;
    Timestamp _collect_start;

    //metric + created timestamp
    std::vector<MetricPointTimestamp > _tmp_metrics;
    //current created timestamp - when metrics are pushed through the pusher
    Timestamp _tmp_cur_created;
    //pusher - used by Emitter
    std::function<void(MetricPoint &&)> _store_pusher;

    std::ostringstream _tmp_stream;

    ///Converts timestamp to nanoseconds (exact, 9 decimals)
    static Decimal<9> to_nanosec(Timestamp ts);

    //function called from _store_pusher,
    void store_pusher(MetricPoint &&pt);

    ///render metrics
    /**
     * @param now timestamp of collection
     * @param created timestamp when metric point was created
     * @param pt metric point itself
     * @param render_description set true to include description (for first of the group)
     * @param output output stream
     */
    static void render_metrics(Timestamp now, Timestamp created, const MetricPoint &pt,  bool render_description, std::ostream &output);
    ///Render definition
    static void render_definition(const MetricDefinition &def, std::ostream &output);
    ///Convert MetricType to string
    static std::string_view metric_type_to_string(MetricType type);
    ///Render metric identifier (by specification)
    /**
     * @param name identifier
     * @param output output
     */
    static void render_metric_identifier(std::string_view name, std::ostream &output);
    ///Render complete metric identifier (including unit)
    /**
     * @param def metric definition
     * @param output output
     */
    static void render_metric_name(const MetricDefinition &def, std::ostream &output);
    ///Render OpenMetrics's file comment (by specification)
    static void render_metric_comment(std::string_view name, std::ostream &output);

    ///Render value (by specification)
    /**
     * @param ts timestamp
     * @param value metric point value
     * @param output output
     */
    static void render_value(Timestamp ts,
            const MetricPoint &value,
            std::ostream &output);

    ///Render metric point
    /**
     * @param def pointer to definition
     * @param suffix suffix (mandatory for some metric types)
     * @param attrs pointer to attributes
     * @param extra_attributes extra attributes (enforced by metric type)
     * @param value_render function responsible to render value.
     * @param ts timestamp
     * @param output output
     */
    template<typename Fn>
    static void render_metric_point(const MetricDefinition *def,
            const std::string_view &suffix,
            const AttributeList *attrs,
            std::initializer_list<AttributeKeyValue> extra_attributes,
            Fn &&value_render, Timestamp ts,
            std::ostream &output);



    static void render_attributes(const AttributeList *list, std::initializer_list<AttributeKeyValue> extra_attributes, std::ostream &output);
    static void render_attribute_key(std::string_view name, std::ostream &output);
    static void render_attribute_value(const AttributeValue &val, std::ostream &output);
    void collect_lk(std::ostream &output);
    void collect_and_export_lk(Exporter &exporter);

};


}
}
