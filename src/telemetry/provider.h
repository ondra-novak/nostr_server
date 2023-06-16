#pragma once
#include "any_ref.h"

#include <memory>

namespace telemetry {


///Base class for sensor
/**
 *
 */
class AbstractSensor {
public:
    virtual ~AbstractSensor() = default;
    ///Get content of sensor (as any_ref data)
    virtual any_ref get()  = 0;
    ///lock for access (mandatory before sensor is accessed)
    virtual void lock() const = 0;
    ///unlock after access
    virtual void unlock() const = 0;
    ///retrieve type of the sensor (as runtime type informations)
    virtual const std::type_info &type() const = 0;
    ///returns true, whether there is any Sensor instance
    /** Used by collector to detect no longer registered sensors
     * They can be removed from collectors (or not).
     *
     * @retval true there is a sensor
     * @retval false no sensors
     */
    virtual bool any_sensor() const = 0;


};

using PSensor = std::shared_ptr<AbstractSensor>;

///Base class for tracer
/**
 * Tracer is created by provider and immediately records its creation
 * Traces should also record its destruction
 */
class AbstractTracer {
public:
    ///record arbitrary event
    /**
     * Records event on the tracer. The current time, tracer's identification
     * and tracer's context should be recorded as well.
     *
     * @param event description of event - type must be registered as an event
     */
    virtual void event(any_cref event) = 0;
    ///Record arbitrary event with attributes
    /**
     * Records event on the tracer. The current time, tracer's identification
     * and tracer's context should be recorded as well.
     *
     * @param event description of event - type must be registered as an event
     * @param attrs attributes. Type must be registered as attributes holder
     */
    virtual void event(any_cref event, any_cref attrs) = 0;
    ///Record an attribute(s)
    /**
     * @param attrs object which contains attributes.
     */
    virtual void attr(any_cref attrs) = 0;
    ///Record any key-value
    /**
     * @param key key, type must be registered on a provider. In the most of cases,
     * std::string, std::string_view, const char * can be used without registration
     *
     * @param value value, type must be registered on a provider. In the most of cases,
     * integral types such a int, bool , or float can be used without registration
     */
    virtual void attr(any_cref key, any_cref value) = 0;
    virtual ~AbstractTracer() = default;

    struct Deleter {
        bool elided;
        void operator()(AbstractTracer *x) {
            if (!elided) delete x;
            else x->~AbstractTracer();
        }
    };


};

using TracerPtr = std::unique_ptr<AbstractTracer, AbstractTracer::Deleter>;

///Allocate tracer, check whether trace can be allocated in given space
/**
 *
 * @tparam T tracer's type
 * @param alloc_space pointer to preallocated space
 * @param alloc_space_size size of preallocated space
 * @param args arguments
 * @return TracerPtr
 */
template<typename T, typename ... Args >
TracerPtr make_tracer(void *alloc_space, std::size_t alloc_space_size, Args && ... args) {
    if (sizeof(T) <= alloc_space_size)  {
        return TracerPtr(new(alloc_space) T(std::forward<Args>(args)...),{true});
    } else {
        return TracerPtr(new T(std::forward<Args>(args)...),{false});
    }
}

///Abstract provider - provides all supported measurements (traces and metrics)
class AbstractProvider: public std::enable_shared_from_this<AbstractProvider> {
public:

    enum class SensorType {
        ///this sensor can't be created
        none,
        ///this sensor is shared
        shared,
        ///this sensor is uniqued
        unique,
    };

    virtual ~AbstractProvider() = default;

    ///Create new tracer - called when span() is constructred
    /**
     * @param ident span identifier
     * @param alloc_space pointer to a small space preallocated on the span. Can be nullptr
     * @param alloc_space_size size of a small space preallocated on the span. Can be zero, must be zero if the alloc_space is nullptr
     * @return if the tracing is supported, returns pointer to tracer, otherwise returns nullptr
     */
    virtual TracerPtr begin_span(any_cref ident, void *alloc_space, std::size_t alloc_space_size) = 0;


    ///Select as which type this sensor can be registered
    /**
     *
     * @param type type of sensor data
     * @retval SensorType::none This type can't be registered as sensor, or
     * it is impossible to register this sensor in automatic mode (This
     * happen, when type is either not defined or is defined for both types)
     * @retval SensorType::shared Register as shared
     * @retval SensorType::unique Register as unique
     */
    virtual SensorType can_register_sensor_auto(const std::type_info &type) = 0;

    ///Determines, whether specified sensor type can be registered as shared sensor
    /**
     * @param type type of sensor
     * @retval true yes, sensor can be registered
     * @retval false no, sensor is not supported
     */
    virtual bool can_register_shared_sensor(const std::type_info &type) = 0;

    ///Determines, whether specified sensor type can be registered as unique sensor
    /**
     * @param type type of sensor
     * @retval true yes, sensor can be registered as unique sensor
     * @retval false no, sensor is not supported
     */
    virtual bool can_register_unique_sensor(const std::type_info &type) = 0;

    ///Retrieves current shared sensor for given sensor type
    /**
     * @param type type of sensor
     * @return shared pointer to sensor, if exists, otherwise returns null-pointer.
     * In this case, sensor must be registered.
     */
    virtual PSensor get_shared_sensor(const std::type_info &type) = 0;

    ///Registers sensor as shared sensor
    /**
     * @param sensor sensor to be registered
     * @return shared pointer to registered sensor. Note that different
     * instance can be returned. If there is a race, when two threads requests to
     * register a shared sensor, only one sensor is registered and seconds sensor
     * should be destroyed. In this case, instance of first sensor is returned,
     * which should replace pointer of second sensor. Function can also return nullptr
     * when such sensor is not supported
     */
    virtual PSensor register_shared_sensor(PSensor sensor) = 0;

    ///Registers an unique sensor
    /**
     * @param sensor sensor to register
     * @return function can return nullptr if sensor is not supported, it
     * should return the argument when registration is successful. It can
     * also return pointer to a different instance. This can happen, when there
     * is extra identification of sensors which must be unique and the registering
     * sensor doesn't use contain an unique identification (i.e. it is duplicated).
     * In this case, the provider can choose to return already registered sensor
     * instead
     *
     */
    virtual PSensor register_unique_sensor(PSensor sensor) = 0;


    ///Requests for push
    /**
     * Function is not mandatory. Collector can ignore it. But if it
     * is implemented, it should push the metrics to the external collector
     * or gateway.
     */
    virtual void push() = 0;


    ///Static function to push metrics in active provider
    friend void push() {
        auto lk = instance.lock();
        if (lk) lk->push();
    }


    ///Global instance
    static std::weak_ptr<AbstractProvider> instance;

    ///make active
    /** provider can override this function, but finally should call parent implementation */
    virtual void make_active() {
        instance = weak_from_this();
    }


protected:

};

inline std::weak_ptr<AbstractProvider> AbstractProvider::instance;



}

