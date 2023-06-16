#pragma once
#include "provider.h"

#include "no_lock.h"

#include <mutex>
#include <atomic>
#include <type_traits>

namespace telemetry {

namespace _details {

    template<typename T>
    auto need_lock_selector(T *) -> typename T::DefaultLock *;
    NoLock *need_lock_selector(...);

    template<typename T>
    using LockSelector = std::remove_pointer_t<decltype(need_lock_selector(std::declval<T *>()))>;


}

template<typename T>
using DefaultLock = _details::LockSelector<T>;

///Holds the measured structure T
template<typename T, typename Lock = DefaultLock<T> >
class SensorHolder: public AbstractSensor {
public:
    template<typename ... Args>
    SensorHolder(Args && ... args):_data(std::forward<Args>(args)...) {}


    virtual any_ref get() override {
        return _data;
    }

    virtual void lock() const override {
        _lock.lock();
    }

    virtual void unlock() const override {
        _lock.unlock();
    }

    virtual const std::type_info &type() const override{
        return typeid(T);
    }
    virtual bool any_sensor() const override{
        return _scount.load(std::memory_order_relaxed) > 0;
    }



    T _data;
    mutable Lock _lock;
    std::atomic<std::size_t> _scount=0;
};


///Class provides measuring and storing values
/**
 * @tparam T type that holds measured values. Variable of this type
 * is creates inside of sensor and during measurement is being updated.
 * @tparam Lock Specifies lock. If not specified, default lock can be
 * defined at T, as T::DefaultLock = <lock type>. If such definition
 * doesn't exist, it defaults to NoLock.
 *
 * Sensor can be shared (SharedSensor) or unique (UniqueSensor). If used
 * directly as Sensor, the type is selected by the collector (which depends
 * on, how the type is registered on the collector. If decision
 * cannot be made, the sensor is not enabled. You need to choose
 * right class (SharedSensor / UniqueSensor) instead. Or you can use
 * enable_shared() or enable_unique()
 *
 */
template<typename T, typename Lock = DefaultLock<T> >
class Sensor {
public:
    ///Update measured value
    /**
     * @param fn lambda function, which receives T & as argument. You can
     * modify the whatever you need
     *
     * @note if there is no telemetry provider, the function is not called. You can
     * benefit from this behaviour and put all calculations related to measurement
     * into the function - they will be skipped when no telemetry is active. Also
     * note this also happen, if update() is called before init()
     *
     * @note MT Safety - MT Safety of T depends on argument Lock. The object
     * itself is not MT Safe
     *
     *
     */
    template<typename Fn>
    void update(Fn &&fn) const {
        if (_h) {
            std::lock_guard _(_h->_lock);
            fn(_h->_data);
        }
    }

    ///initialize this structure
    /**
     * @param args optional arguments passed to constructor of the T.
     *
     * When this function is called, measured structure is registered to be
     * collected by telemetry provider. If there is no provider, then nothing
     * happen (so no constructor is called!)
     *
     * @note MT Safety - MT Safety of T depends on argument Lock. The object
     * itself is not MT Safe. Accessing the provider is MT Safe
     *
     */
    template<typename ... Args>
    void enable_unique(Args && ... args) {
        enable_unique_for(AbstractProvider::instance, std::forward<Args>(args)...);
    }

    template<typename ... Args>
    void enable_unique_for(const std::weak_ptr<AbstractProvider> &provider, Args && ... args) {
        auto lkp = provider.lock();
        if (lkp && lkp->can_register_unique_sensor(typeid(T))) {
            _h = std::make_shared<SensorHolder<T,Lock> >(std::forward<Args>(args)...);
            _h->_scount.fetch_add(1,std::memory_order_relaxed);
            _h = std::dynamic_pointer_cast<SensorHolder<T, Lock> >(lkp->register_unique_sensor(_h));
        }
    }

    ///Enable shared
    template<typename ... Args>
    void enable_shared(Args && ... args) {
        enable_shared_for(AbstractProvider::instance, std::forward<Args>(args)...);
    }

    template<typename ... Args>
    void enable_shared_for(const std::weak_ptr<AbstractProvider> &provider, Args && ... args) {
        auto lkp = provider.lock();
        if (lkp && lkp->can_register_shared_sensor(typeid(T))) {
            _h = std::dynamic_pointer_cast<SensorHolder<T, Lock> >(lkp->get_shared_sensor(typeid(T)));
            if (!_h) {
                _h = std::make_shared<SensorHolder<T, Lock> >(std::forward<Args>(args)...);
                _h->_scount.fetch_add(1,std::memory_order_relaxed);
                _h = std::dynamic_pointer_cast<SensorHolder<T, Lock> >(lkp->register_shared_sensor(_h));
            }
        }
    }


    ///Enable in automatic mode
    /**
     * Type of sensor is selected by collector - depends on whether
     * type is registered as shared or unique sensor.
     *
     * @param args arguments to be passed to the sensor's constructor
     */
    template<typename ... Args>
    void enable(Args && ... args) {
        enable_for(AbstractProvider::instance, std::forward<Args>(args)...);
    }

    ///Enable in automatic mode
    /**
     * Type of sensor is selected by collector - depends on whether
     * type is registered as shared or unique sensor.
     *
     * @param provider provider
     * @param args arguments to be passed to the sensor's constructor
     */
    template<typename ... Args>
    void enable_for(const std::weak_ptr<AbstractProvider> &provider, Args && ... args) {
        auto lkp = provider.lock();
        if (lkp) {
            AbstractProvider::SensorType t = lkp->can_register_sensor_auto(typeid(T));
            switch (t) {
                default:
                case AbstractProvider::SensorType::none: return;
                case AbstractProvider::SensorType::shared:
                    _h = std::dynamic_pointer_cast<SensorHolder<T, Lock> >(lkp->get_shared_sensor(typeid(T)));
                    if (!_h) {
                        _h = std::make_shared<SensorHolder<T, Lock> >(std::forward<Args>(args)...);
                        _h->_scount.fetch_add(1,std::memory_order_relaxed);
                        _h = std::dynamic_pointer_cast<SensorHolder<T, Lock> >(lkp->register_shared_sensor(_h));
                    }
                    break;
                case AbstractProvider::SensorType::unique:
                    _h = std::make_shared<SensorHolder<T,Lock> >(std::forward<Args>(args)...);
                    _h->_scount.fetch_add(1,std::memory_order_relaxed);
                    _h = std::dynamic_pointer_cast<SensorHolder<T, Lock> >(lkp->register_unique_sensor(_h));
                    break;
            }
        }
    }

    ///Disable sensor
    /**
     * Sensor is disabled, unregistered and destroyed.
     *
     * Disabled sensor cannot be updated and it is no longer monitored.
     * Disabled sensor can be reenabled.
     * The destructor automatically disables the sensor
     * @note function is not MT Safe (in context of this sensor)
     *
     * @note disabled sensor is not automatically destroyed. It can still be collected by
     * the collector and then it is eventually destroyed
     *
     */
    void disable() {
        if (_h){
            _h->_scount.fetch_sub(1, std::memory_order_relaxed);
            _h.reset();
        }

    }

    ///Determines whether sensor is enabled
    /**
     * @return true enabled
     * @retval false disabled
     * @note function is not MT Safe (in context of this sensor)
     */
    bool is_enabled() const {
        return static_cast<bool>(_h);
    }

    ~Sensor() {
        disable();
    }

protected:
    std::shared_ptr<SensorHolder<T, Lock> > _h;

};



///Shared sensor
/**
 * @tparam T custom structure acts as sensor. Must be unique type and this
 * type must be registered on the provider.
 *
 * @tparam Lock specifies lock to use while update. Default value disables locking
 *
 * Shared sensor is created only once, regardless on how many instances of sensors
 * are created. It is recommended for sum-metric type, where measurement is
 * aggregated across instances.
 */
template<typename T, typename Lock = DefaultLock<T> >
class SharedSensor: public Sensor<T, Lock> {
public:



    ///Initialize and enable sensor
    /** Sensors are created disables, they must be initialized manually. Some
     * Sensor can optionally require arguments
     *
     * @param args arguments to initialize structure within sensor
     *
     * @note shared sensor is initialized once and further initializations are
     * not performed - It means that arguments for second, third etc, initialization
     * are not used. However, if initialization is performed from different threads,
     * it can happen, that two sensors are created but one of then is immediately
     * destroyed when multiple instances are detected on the provider. So
     * there is no guarantee, that constructor of shared sensor will be called only
     * once in this case.
     *
     * @note function is not MT Safe (in context of this sensor)
     *
     */
    template<typename ... Args>
    void enable(Args && ... args) {
        Sensor<T,Lock>::enable_shared(std::forward<Args>(args)...);
    }

    ///Initialize and enable sensor for explictly specified provider
    /** Sensors are created disables, they must be initialized manually. Some
     * Sensor can optionally require arguments
     *
     * @param provider weak pointer to provider. If the provider is no longer valid,
     * the sensor remains disabled
     * @param args arguments to initialize structure within sensor
     *
     * @note shared sensor is initialized once and further initializations are
     * not performed - It means that arguments for second, third etc, initialization
     * are not used. However, if initialization is performed from different threads,
     * it can happen, that two sensors are created but one of then is immediately
     * destroyed when multiple instances are detected on the provider. So
     * there is no guarantee, that constructor of shared sensor will be called only
     * once in this case.
     *
     * @note function is not MT Safe (in context of this sensor)
     *
     */
    template<typename ... Args>
    void enable_for(const std::weak_ptr<AbstractProvider> &provider, Args && ... args) {
        Sensor<T,Lock>::enable_shared_for(provider, std::forward<Args>(args)...);
    }

    template<typename ... Args> void enable_unique(Args && ... args) = delete;
    template<typename ... Args> void enable_unique_for(const std::weak_ptr<AbstractProvider> &provider, Args && ... args) = delete;
};

///Unique sensor
/**
 * @tparam T custom structure acts as sensor. Must be unique type and this
 * type must be registered on the provider.
 *
 * @tparam Lock specifies lock to use while update. Default value disables locking
 *
 * Unique sensor holds unique instance of structure for given instance. Multiple
 * sensors are registered as unique instances. Each contains unique metric data.
 */
template<typename T, typename Lock = DefaultLock<T> >
class UniqueSensor: public Sensor<T, Lock> {
public:


    ///Initialize and enable sensor
    /**
     * @param args arguments passed to the sensor
     *
     * @note function is not MT Safe (in context of this sensor)
     *
     */
    template<typename ... Args>
    void enable(Args && ... args) {
        Sensor<T,Lock>::enable_unique(std::forward<Args>(args)...);
    }

    ///Initialize and enable sensor for given provider
    /**
     *
     * @param provider provider
     * @param args arguments passed to the sensor
     *
     * @note function is not MT Safe (in context of this sensor)
     */
    template<typename ... Args>
    void enable_for(const std::weak_ptr<AbstractProvider> &provider, Args && ... args) {
        Sensor<T,Lock>::enable_unique_for(provider, std::forward<Args>(args)...);
    }

    template<typename ... Args> void enable_unique(Args && ... args) = delete;
    template<typename ... Args> void enable_unique_for(const std::weak_ptr<AbstractProvider> &provider, Args && ... args) = delete;
    template<typename ... Args> void enable_shared(Args && ... args) = delete;
    template<typename ... Args> void enable_shared_for(const std::weak_ptr<AbstractProvider> &provider, Args && ... args) = delete;
};

}


