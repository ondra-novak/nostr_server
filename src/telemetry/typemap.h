#pragma once
#include "any_ref.h"

#include <functional>
#include <memory>
#include <optional>
#include <mutex>

namespace telemetry {

///Wraps type_info
using TypeInfoRef = std::reference_wrapper<const std::type_info>;


namespace _details {

    ///Hash function
    struct hashFn {
        std::size_t operator()(const TypeInfoRef &t) const {
            const std::type_info &r = t;
            return r.hash_code();
        }
    };
    ///Comparison function
    struct cmpFn {
        bool operator()(const TypeInfoRef &a, const TypeInfoRef &b) const {
            const std::type_info &aa = a;
            const std::type_info &bb = b;
            return aa == bb;
        }
    };

    ///If T is lambda function, retrieves type of argument
    template<typename T> struct DeduceArg;

    template<typename _Res, typename _Tp, bool _Nx, typename A>
    struct DeduceArg< _Res (_Tp::*) (A &) noexcept(_Nx) > {using type = A;};
    template<typename _Res, typename _Tp, bool _Nx, typename A>
    struct DeduceArg< _Res (_Tp::*) (A &) const noexcept(_Nx) > {using type = A;};


}

template<typename Value>
using TypeInfoMap = std::unordered_map<TypeInfoRef, Value, _details::hashFn, _details::cmpFn>;


enum class Access {
    ///const access, callback function receives const reference
    CONST,
    ///mutable acces, callback function receives plain reference
    MUTABLE
};


///TypeMap is map which allows to convert anything to desired type.
/**
 * @tparam ResultType desired type.
 * @tparam access specifies access mode to converted data. Default is Access::CONST, which
 * means, that callback function receives const reference. Access::MUTABLE causes
 * that callback function receives mutable reference
 */
template<typename ResultType, Access access = Access::CONST>
class TypeMap {
public:

    using AnyRef = typename std::conditional_t<access == Access::MUTABLE, any_ref, any_cref>;


    ///Converts the argument to a value of desired type if the conversion exists
    /**
     * @param var AnyRef variable to convert
     * @return converted value as optional. So if the conversion is not possible,
     * result is empty value
     *
     * @exception any. Can throw any exception - the exception is thrown from the
     * registered handler
     */
    std::optional<ResultType> convert(AnyRef var);




    ///Register type conversion for single type
    /**
     * @tparam T source type - to convert from
     * @param fn function which handles conversion. It accepts one argument and
     * returns ResultType. Function can also accept argument in compatible type,
     * because default conversion is also possible here
     *
     * @code
     * TypeMap<std::string> tmap;
     * tmap.reg_type<int>([](int v) {return std::to_string(v);});
     * @endcode
     *
     */
    template<typename T, typename Fn>
    void reg_type(Fn &&fn);

    ///Register to conversion for multiple types
    /**
     * @tparam Types must be a std::tuple<> of types. Function is registered
     * for multiple types.
     * @param fn function which handles conversion.
     *
     * @code
     * TypeMap<std::string> tmap;
     * tmap.reg_types<std::tuple<double, float> >([](double v) {return std::to_string(v);});
     * @endcode
     *
     */
    template<typename Types, typename Fn>
    void reg_types(Fn &&fn);

    ///Insert new type and its convertor to the map
    /**
     * Automatically deduces type from the argument of the lambda function
     *
     * @param fn lambda function, must accept argument
     * of type bing inserted
     */
    template<typename Fn, typename = decltype(&Fn::operator())>
    void insert(Fn &&fn) {
        using Type = typename _details::DeduceArg<decltype(&Fn::operator())>::type;
        reg_type<Type, Fn>(std::forward<Fn>(fn));
    }

    template<typename Fn, typename = decltype(&Fn::operator())>
    void operator+=(Fn &&fn) {
        using Type = typename _details::DeduceArg<decltype(&Fn::operator())>::type;
        reg_type<Type, Fn>(std::forward<Fn>(fn));
    }


    bool defined(const std::type_info &t) {
        return _type_map.find(std::cref(t)) != _type_map.end();
    }

protected:


    ///Wrapper for conversion function
    class AbstrConvFn {
    public:
        virtual ~AbstrConvFn() = default;
        virtual ResultType do_conv(AnyRef var) noexcept = 0;
    };

    ///Pointer to wrapper
    using PAbstrConvFn = std::unique_ptr<AbstrConvFn>;

    ///Conversion function for given T
    /**
     * @tparam Fn function which handles conversion
     * @tparam T type of the value - helps to convert AnyRef to T before the function is called
     */
    template<typename Fn, typename T>
    class ConvFn:public AbstrConvFn {
    public:
        ConvFn(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
        virtual ResultType do_conv(AnyRef var) noexcept override {
            return _fn(var.template get<T>());
        }
    protected:
        Fn _fn;
    };
    ///Unordered map of types
    TypeInfoMap<PAbstrConvFn> _type_map;

    ///Helper to register types
    template<typename Fn, typename T, typename U, typename ... Args>
    void reg_types2(Fn &&fn) {
        reg_type<T,Fn>(Fn(fn));
        reg_types2<Fn, U, Args...>(std::forward<Fn>(fn));
    }

    ///Helper to register types
    template<typename Fn, typename T>
    void reg_types2(Fn &&fn) {
        reg_type<T,Fn>(std::forward<Fn>(fn));
    }


    ///Helper class to detect arguments of the tuple
    template<typename T>
    struct TupleHelper {
        TypeMap *me;
        template<typename Fn>
        void reg_types(Fn &&fn) {
            me->reg_types2<Fn, T>(std::forward<Fn>(fn));
        }
    };

    ///Helper class to detect arguments of the tuple
    template<typename ... Args>
    struct TupleHelper<std::tuple<Args...> > {
        TypeMap *me;
        template<typename Fn>
        void reg_types(Fn &&fn) {
            me->reg_types2<Fn, Args ... >(std::forward<Fn>(fn));
        }
    };


};



template<typename ResultType, Access access, typename LockRef>
class TypeMapLockable: public TypeMap<ResultType, access> {
public:
    TypeMapLockable(LockRef &lock):_lock(lock) {}

    using AnyRef = typename TypeMap<ResultType, access>::AnyRef;

    template<typename T, typename Fn>
    void reg_type(Fn &&fn) {
        std::lock_guard _(_lock);
        TypeMap<ResultType, access>::template reg_type<T,Fn>(std::forward<Fn>(fn));
    }

    template<typename Types, typename Fn>
    void reg_types(Fn &&fn) {
        std::lock_guard _(_lock);
        TypeMap<ResultType, access>::template reg_types<Types,Fn>(std::forward<Fn>(fn));
    }

    ///Insert new type and its convertor to the map
    /**
     * Automatically deduces type from the argument of the lambda function
     *
     * @param fn lambda function, must accept argument
     * of type bing inserted
     */

    template<typename Fn, typename = decltype(&Fn::operator())>
    void insert(Fn &&fn) {
        using Type = typename _details::DeduceArg<decltype(&Fn::operator())>::type;
        reg_type<Type, Fn>(std::forward<Fn>(fn));
    }

    template<typename Fn, typename = decltype(&Fn::operator())>
    void operator+=(Fn &&fn) {
        using Type = typename _details::DeduceArg<decltype(&Fn::operator())>::type;
        reg_type<Type, Fn>(std::forward<Fn>(fn));
    }

    std::optional<ResultType> convert(AnyRef var) {
        std::lock_guard _(_lock);
        return TypeMap<ResultType, access>::convert(var);

    }

    bool defined(const std::type_info &t) {
        std::lock_guard _(_lock);
        return TypeMap<ResultType, access>::defined(t);
    }



protected:
    LockRef &_lock;

};

template<typename ResultType, Access a>
inline std::optional<ResultType> TypeMap<ResultType,a>::convert(AnyRef var) {
    if (var.get_type() == typeid(ResultType)) {
        if constexpr(a == Access::CONST) {
            return std::optional<ResultType>(var.template get<ResultType>());
        } else {
            return std::optional<ResultType>(std::move(var.template get<ResultType>()));
        }
    }
    auto iter =_type_map.find(std::cref(var.get_type()));
    if (iter == _type_map.end()) return {};
    return iter->second->do_conv(var);
}

template<typename ResultType, Access a>
template<typename T, typename Fn>
inline void TypeMap<ResultType,a>::reg_type(Fn &&fn) {
    _type_map.emplace(std::cref(typeid(T)), std::make_unique<ConvFn<Fn, T> >(std::forward<Fn>(fn)));
}

template<typename ResultType, Access a>
template<typename Types, typename Fn>
inline void TypeMap<ResultType,a>::reg_types(Fn &&fn) {
    TupleHelper<Types> hlp{this};
    hlp.reg_types(std::forward<Fn>(fn));
}

}

