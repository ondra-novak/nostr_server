#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <utility>


namespace telemetry {

namespace _details {

template<typename Fn, typename BaseType>
void to_string_helper(Fn &fn, BaseType n, int decimals) {
    if (decimals>=0 || n) {
        to_string_helper(fn, n/10, decimals-1);
        if (decimals == 1) fn('.');
        fn('0' + (n % 10));
    }
}

}

class DecimalString;

///Fixed decimal number. Stores number also with exact count of decimal numbers.
/**
 * Number is stored as integer with fixed count of decimal numbers.
 *
 * @tparam decimals count of decimal numbers. Zero is allowed (stores as int), and
 *    negative number can also help to store large number while zeroing small
 *    numbers. For example value -2 means that you can store value in step 100
 *    (100,200,300, etc)
 * @tparam BaseType Base type defaults to int64_t
 */
template<int decimals, typename BaseType = std::int64_t, bool interlocked = false>
class Decimal {
public:

    using StoreType = typename std::conditional_t<interlocked, std::atomic<BaseType>, BaseType>;

    static constexpr BaseType pow10(int pos_count) {
        BaseType n = 1;
        for (int i = 0; i < pos_count; i++) n = n * 10;
        return n;
    }
    static constexpr BaseType multiply_by = pow10(decimals);
    static constexpr BaseType divide_by = pow10(-decimals);
    static constexpr BaseType divide_round(BaseType x, BaseType y) {
        if (x > 0) return (x + y/2) / y;  //(145+5)/10 = 15
        else return (x - y/2) / y;        //(-145-5)/10 = -15
    }

    struct Internal {
        BaseType v;
    };

    constexpr Decimal(): Decimal(Internal{}) {}
    constexpr Decimal(BaseType v): _n(divide_round(v*multiply_by,divide_by)) {}
    constexpr Decimal(double v): _n(static_cast<BaseType>(std::round(v*multiply_by/divide_by))) {}
    constexpr Decimal(float v): _n(static_cast<BaseType>(std::round(v*multiply_by/divide_by))) {}
    constexpr Decimal(int x): Decimal(static_cast<BaseType>(x)) {}
    constexpr Decimal(Internal v): _n(v.v) {}

    static Decimal from_raw(BaseType v) {
        return Decimal(Internal{v});
    }

    template<int d, bool b>
    constexpr Decimal(const Decimal<d, BaseType, b> &other) {
        constexpr int dec_diff = decimals - d;
        if (dec_diff > 0) {
            _n = other.get_raw() * pow10(dec_diff);
        } else if (dec_diff < 0) {
            _n = other.get_raw();
            for (int i = 0; i > dec_diff; --i) {
                _n = (_n + 5)/10; //rounding
            }
        } else {
            _n = other.get_raw();
        }
    }

    constexpr explicit Decimal(std::string_view strn) {
        _n = 0;
        bool neg = false;
        bool dot = false;
        int decs = 0;
        auto iter = strn.begin();
        auto end = strn.end();
        if (iter != end) {
            if (*iter == '-') {neg = true; ++iter;}
            if (*iter == '+') {++iter;}
            if (iter != end) {
                while (iter != end) {
                    char c = *iter;
                    if (isdigit(c)) {
                        _n = _n * 10 + (c - '0');
                        if (dot) {
                            ++decs;
                            if (decs > decimals) {
                                break;  //enough decimals
                            }
                        }
                    } else if (c == '.') {
                        if (dot) throw std::invalid_argument("Invalid numeric format (multiple dots)");
                        dot = true;
                        decs = 0;
                    } else {
                        throw std::invalid_argument("Invalid numeric format (unexpected character)");
                    }
                    ++iter;
                }
                while (decs < decimals) {
                    _n *= 10;
                    decs++;
                }
                while (decs > decimals) {
                    _n = divide_round(_n, 10);
                    decs--;
                }
                if (neg) _n = -_n;
                return;
            }

        }
        throw std::invalid_argument("Invalid numeric format (empty string)");
    }
    constexpr BaseType get_raw() const {
        return _n;
    }
    constexpr BaseType &get_raw()  {
        return _n;
    }

    std::string to_string() const {
        std::string ret;
        to_string([&](char c){ret.push_back(c);});
        return ret;
    }

    constexpr double to_double() const {
        return static_cast<double>(_n) * divide_by / multiply_by;
    }
    constexpr float to_float() const {
        return static_cast<float>(_n) * divide_by / multiply_by;
    }
    constexpr BaseType to_int() const {
        return (divide_round(_n * divide_by, multiply_by));
    }
    constexpr explicit operator double() const {
        return to_double();
    }
    constexpr explicit operator float() const {
        return to_float();
    }
    constexpr explicit operator BaseType() const {
        return to_int();
    }
    ///convert to decimal string
    operator DecimalString() const;

    template<typename Fn> void to_string(Fn &&fn) const {
        if (_n == 0) {
            fn('0');
            if constexpr (decimals>0) {
                fn('.');
                for (int i = 0; i< decimals; i++) {
                    fn('0');
                }
            }
            return;
        }
        if (_n < 0) {
            fn('-');
            _details::to_string_helper(fn, -get_raw(), decimals);
        } else {
            _details::to_string_helper(fn, get_raw(), decimals);
        }
        for (int i = 0; i > decimals; --i) {
            fn('0');
        }
    }

    constexpr Decimal operator+(const Decimal<decimals, BaseType> &b) const {
        return Decimal(Internal{_n+b.get_raw()});
    }
    constexpr Decimal operator-(const Decimal<decimals, BaseType> &b) const{
        return Decimal(Internal{_n-b.get_raw()});
    }
    constexpr Decimal operator*(const Decimal<decimals, BaseType> &b) const {
        //multiplication is calculates as X*Y/pow10(decimals)
        return Decimal(Internal{
            divide_round(_n*b.get_raw() * divide_by,multiply_by)
        });
    }
    template<typename X>
    constexpr Decimal operator/(const X &b) const {
        //if b is integer number
        if constexpr(std::is_convertible_v<X, BaseType>) {
            //just divide raw number by integer
            //because X*pow10(decimals)/b/pow10(decimals) = X/b
            return Decimal(Internal{divide_round(_n, b)});
        } else {
            //otherwise, it is better to use 'double'
            return Decimal(to_double() / static_cast<double>(b));
        }

    }
    constexpr  Decimal &operator+=(const Decimal<decimals, BaseType> &b) {
        _n += b.get_raw();
        return *this;
    }
    constexpr Decimal &operator-=(const Decimal<decimals, BaseType>  &b){
        _n -= b.get_raw();
        return *this;
    }
    constexpr Decimal &operator*=(const Decimal<decimals, BaseType>  &b){
        _n *= b.get_raw();
        _n = divide_round(_n * divide_by, multiply_by);
        return *this;
    }
    template<typename X>
    constexpr Decimal &operator/=(const X &b) {
        if constexpr(std::is_convertible_v<X, BaseType>) {
            _n = divide_round(_n, b);
        } else {
            //otherwise, it is better to use 'double'
            auto x = Decimal(to_double() / static_cast<double>(b));
            _n = x.get_raw();
        }
        return *this;
    }


    Decimal &operator++() {++_n;return *this;}
    Decimal &operator--() {--_n;return *this;}
    Decimal operator++(int) {Decimal r(Internal{_n++});return r;}
    Decimal operator--(int) {Decimal r(Internal{_n--});return r;}

    Decimal sample() {
        if constexpr(interlocked) {
            return Decimal(Internal{_n.exchange(BaseType{})});
        } else {
            return Decimal(std::exchange(_n,BaseType{}));
        }
    }


protected:
    StoreType _n;
};

///Helper class, which can be used to store Decimal in form of precise string
/**
 * You cannot use std::string directly if you need to ensure, that string value
 * is always a number
 */
class DecimalString: public std::string {
public:
    template<int decimals, typename BaseType, bool interlocked>
    DecimalString(const Decimal<decimals, BaseType, interlocked> &v) {
        v.to_string([&](char c){push_back(c);});
    }
};


template<int decimals, typename BaseType, bool il1, bool il2>
bool operator==(const Decimal<decimals, BaseType, il1> &a, const Decimal<decimals, BaseType, il2> &b) {return a.get_raw() == b.get_raw();}
template<int decimals, typename BaseType, bool il1, bool il2>
bool operator!=(const Decimal<decimals, BaseType, il1> &a, const Decimal<decimals, BaseType, il2> &b) {return a.get_raw() != b.get_raw();}
template<int decimals, typename BaseType, bool il1, bool il2>
bool operator>=(const Decimal<decimals, BaseType, il1> &a, const Decimal<decimals, BaseType, il2> &b) {return a.get_raw() >= b.get_raw();}
template<int decimals, typename BaseType, bool il1, bool il2>
bool operator<=(const Decimal<decimals, BaseType, il1> &a, const Decimal<decimals, BaseType, il2> &b) {return a.get_raw() <= b.get_raw();}
template<int decimals, typename BaseType, bool il1, bool il2>
bool operator>(const Decimal<decimals, BaseType, il1> &a, const Decimal<decimals, BaseType, il2> &b) {return a.get_raw() > b.get_raw();}
template<int decimals, typename BaseType, bool il1, bool il2>
bool operator<(const Decimal<decimals, BaseType, il1> &a, const Decimal<decimals, BaseType, il2> &b) {return a.get_raw() <  b.get_raw();}



template<int decimals, typename BaseType , bool interlocked >
inline Decimal<decimals, BaseType, interlocked>::operator DecimalString() const {
    return DecimalString(*this);
}

}
