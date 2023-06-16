#pragma once
#include <typeinfo>
#include <type_traits>


namespace telemetry {




///Constructs const reference to anything while it also keeps the type as runtime type information
/**
 * Useful to able pass anything to the function or return anything as reference.
 * The reference is always const.
 *
 * Similar to std::any, but it is only holds reference and type, no allocation is involved
 *
 */

class any_cref;

///Constructs non-const reference to anything while it also keeps the type as runtime type information
/**
 * Useful to able pass anything to the function or return anything as reference.
 * The reference is always non-const.
 *
 * Similar to std::any, but it is only holds reference and type, no allocation is involved
 *
 */
class any_ref {
public:
    ///construct reference
    /** This constructor is disabled for arrays */
    template<typename T,
             typename = std::enable_if_t<!std::is_array_v<T> && !std::is_const_v<T> > >
    any_ref(T &val) noexcept
        :_ptr(&val), _tinfo(typeid(T)) {
        static_assert(!std::is_same_v<T, any_cref>, "Cannot convert any_cref to any_ref. Use any_cref::constCast()");

    }

    ///construct reference to an C-array
    /**
     * Because standard constructor is disabled for arrays, this constructor
     * is called, when T is array with unspecified bounds - which emits different
     * type then const T *
     */
    template<typename T,
             typename = std::enable_if_t<!std::is_const_v<T> > >
    any_ref(T (&val)[]) noexcept
        :_ptr(&val), _tinfo(typeid(T[])) {}

    ///construct reference to an C-array with bounds
    /**
     * C-arrays with bounds are converted to unbounded C-arrays. That
     * because it is impossible to construct compatible type for all possible bounds.
     * This emits type information for T[] - which is different then const T *.
     *
     */
    template<typename T, std::size_t n,
             typename = std::enable_if_t<!std::is_const_v<T> > >
    any_ref(T (&val)[n]) noexcept
        :_ptr(&val), _tinfo(typeid(T[])) {}

    any_ref(void *ptr, const std::type_info &type):_ptr(ptr),_tinfo(type) {}

///reference can be copied
    any_ref(any_ref &other) = default;
    ///reference can't by assigned
    any_ref &operator=(const any_ref &other) = delete;


    ///get reference if the content is of given type
    /**
     * @tparam T type to get
     * @return reference to given value
     * @exception std::bad_cast - contains  value of different type
     *
     * @note doesn't support inheritance. T must be exact type (without const)
     */
    template<typename T>
    T &get() const {
        if (typeid(T) == _tinfo) {
            return *reinterpret_cast<T *>(_ptr);
        } else {
            throw std::bad_cast();
        }
    }

    ///Conversion to reference directly
    /** @exception std::bad_cast - contains  value of different type
    *
    * @note doesn't support inheritance. T must be exact type (without const)
    */

    template<typename T>
    operator T &() const {
        return get<T>();
    }

    ///Conversion and copy of value
    /** @exception std::bad_cast - contains  value of different type
    *
    * @note doesn't support inheritance. T must be exact type (without const)
    */
    template<typename T>
    operator T () const {
        return get<T>();
    }

    ///Retrieve pointer or nullptr
    /**
     *
     * @tparam T expected type
     * @return pointer to value of given type or nullptr if the type is different
     *
     * @note doesn't throw exception
     */
    template<typename T>
    T *get_ptr() const noexcept {
        if (typeid(T) == _tinfo) {
            return reinterpret_cast<T *>(_ptr);
        } else {
            return nullptr;
        }
    }

    ///Get information of carried type
    const std::type_info &get_type() const noexcept  {
        return _tinfo;
    }

    [[nodiscard]] void *get_void_ptr() const noexcept {
        return _ptr;
    }



protected:
    ///Reference is stored as type erased pointer
    void *_ptr;
    ///we also store runtime type information here
    const std::type_info &_tinfo;

};



class any_cref {
public:
    ///construct reference
    /** This constructor is disabled for arrays */
    template<typename T, typename = std::enable_if_t<!std::is_array_v<T> && !std::is_same_v<T,any_ref> > >
    any_cref(const T &val) noexcept
        :_ptr(&val), _tinfo(typeid(T)) {}

    ///construct reference to an C-array
    /**
     * Because standard constructor is disabled for arrays, this constructor
     * is called, when T is array with unspecified bounds - which emits different
     * type then const T *
     */
    template<typename T>
    any_cref(const T (&val)[]) noexcept
        :_ptr(&val), _tinfo(typeid(T[])) {}

    ///construct reference to an C-array with bounds
    /**
     * C-arrays with bounds are converted to unbounded C-arrays. That
     * because it is impossible to construct compatible type for all possible bounds.
     * This emits type information for T[] - which is different then const T *.
     *
     */
    template<typename T, std::size_t n>
    any_cref(const T (&val)[n]) noexcept
        :_ptr(&val), _tinfo(typeid(T[])) {}

    any_cref(const void *ptr, const std::type_info &type):_ptr(ptr),_tinfo(type) {}


    ///convert from any_ref
    any_cref(any_ref x):_ptr(x.get_void_ptr()),_tinfo(x.get_type()) {}

    ///reference can be copied
    any_cref(const any_cref &other) = default;
    ///reference can't by assigned
    any_cref &operator=(const any_cref &other) = delete;


    ///get reference if the content is of given type
    /**
     * @tparam T type to get
     * @return reference to given value
     * @exception std::bad_cast - contains  value of different type
     *
     * @note doesn't support inheritance. T must be exact type (without const)
     */
    template<typename T>
    const T &get() const {
        if (typeid(T) == _tinfo) {
            return *reinterpret_cast<const T *>(_ptr);
        } else {
            throw std::bad_cast();
        }
    }

    ///Conversion to reference directly
    /** @exception std::bad_cast - contains  value of different type
    *
    * @note doesn't support inheritance. T must be exact type (without const)
    */

    template<typename T>
    operator const T &() const {
        return get<T>();
    }

    ///Conversion and copy of value
    /** @exception std::bad_cast - contains  value of different type
    *
    * @note doesn't support inheritance. T must be exact type (without const)
    */
    template<typename T>
    operator T () const {
        return get<T>();
    }

    ///Retrieve pointer or nullptr
    /**
     *
     * @tparam T expected type
     * @return pointer to value of given type or nullptr if the type is different
     *
     * @note doesn't throw exception
     */
    template<typename T>
    const T *get_ptr() const noexcept {
        if (typeid(T) == _tinfo) {
            return reinterpret_cast<const T *>(_ptr);
        } else {
            return nullptr;
        }
    }

    ///Get information of carried type
    const std::type_info &get_type() const noexcept  {
        return _tinfo;
    }


    any_ref constCast() const noexcept {
        return any_ref(const_cast<void *>(_ptr), _tinfo);
    }

    [[nodiscard]] const void *get_void_ptr() const noexcept {
        return _ptr;
    }


protected:
    ///Reference is stored as type erased pointer
    const void *_ptr;
    ///we also store runtime type information here
    const std::type_info &_tinfo;

};

}
