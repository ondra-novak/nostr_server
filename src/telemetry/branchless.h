#pragma once
#include <algorithm>

namespace telemetry {

///Provides branchless if - selects first or second result depend on condition
/**
 * Because it is branchless, expressions are always calculated
 *
 * Equivalent code
 * @code
 * return cond?expr1:expr2;  //compiler always generates conditional jump here
 * @endcode
 *
 * @param cond condition
 * @param expr1 first expression
 * @param expr2 second expression
 * @return final expression depend on condition
 *
 * @note it is branchless, there should be no conditional jump.
 *
 */
template<typename T>
T branchless_if(bool cond, T expr1, T expr2) {
    const T *tmp[] {&expr2, &expr1};
    return *tmp[bool(cond)];
}

///Provides branchless aggregate of two values depend on whether value is also valid or not
/**
 * Equivalent code
 * @code
 * if (!ok2) return v1;
 * if (!ok1) return v2;
 * return binop(v1,v2);
 * @endcode
 *
 * @param ok1 set true, if v1 is valid
 * @param ok2 set true, if v2 is valid
 * @param v1 value 1
 * @param v2 value 2
 * @param binop binary operation it result is returned
 * @return result of operation
 */
template<typename T, typename Fn>
T branchless_aggregate(bool ok1, bool ok2, T v1, T v2, Fn &&binop ) {
    int b1 =int(ok1);   //convert bool to 1=true, 0=false
    int b2 =int(ok2);
    int ofs = b1+b2*2;  //just convert to two bit index (b2,b1) -> 00, 01, 10, 11
    T aggr = binop(v1,v2);  //calculate binary operation
    const T *tmp[] {&v1, &v1, &v2, &aggr}; //00 -> v1, 01 -> v2, 10 -> v1, 11 -> binop
    return *tmp[ofs]; //retrieve result
}

///Branchless min calculation
/**
 * Equivalent code
 * @code
 * return std::min(a,b)
 * @endcode
 *
 * @param a first
 * @param b second
 * @return
 */
template<typename T>
T branchless_max(T a, T b) {
    return branchless_if(a>b,a,b);
}
///Branchless max calculation
/**
 * Equivalent code
 * @code
 * return std::max(a,b)
 * @endcode
 *
 * @param a first
 * @param b second
 * @return
 */
template<typename T>
T branchless_min(T a, T b) {
    return branchless_if(a<b,a,b);
}


}
