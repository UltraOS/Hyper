#pragma once

#include "Types.h"

struct false_value {
    static constexpr bool value = false;
};

struct true_value {
    static constexpr bool value = true;
};

template <typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

// ---- remove_const_volatile ----
template <typename T>
struct remove_const_volatile {
    using type = T;
};

template <typename T>
struct remove_const_volatile<const T> {
    using type = T;
};

template <typename T>
struct remove_const_volatile<volatile T> {
    using type = T;
};

template <typename T>
struct remove_const_volatile<const volatile T> {
    using type = T;
};

template <typename T>
using remove_const_volatile_t = typename remove_const_volatile<T>::type;
// --------------------

// ---- remove_pointer ----
template <typename T>
struct remove_pointer {
    using type = T;
};

template <typename T>
struct remove_pointer<T*> {
    using type = T;
};

template <typename T>
using remove_pointer_t = typename remove_pointer<T>::type;
// --------------------

// ---- is_integral ----
// TODO: implement is_any_of and remove_const_volatile here
template <typename T>
struct is_integral : false_value {
};

template <>
struct is_integral<char> : true_value {
};

template <>
struct is_integral<signed char> : true_value {
};

template <>
struct is_integral<unsigned char> : true_value {
};

template <>
struct is_integral<short> : true_value {
};

template <>
struct is_integral<unsigned short> : true_value {
};

template <>
struct is_integral<int> : true_value {
};

template <>
struct is_integral<unsigned int> : true_value {
};

template <>
struct is_integral<long> : true_value {
};

template <>
struct is_integral<unsigned long> : true_value {
};

template <>
struct is_integral<long long> : true_value {
};

template <>
struct is_integral<unsigned long long> : true_value {
};

template <typename T>
inline constexpr bool is_integral_v = is_integral<T>::value;
// --------------------

// ---- is_floating_point ----
template <typename T>
struct is_floating_point : false_value {
};

template <>
struct is_floating_point<float> : true_value {
};

template <>
struct is_floating_point<double> : true_value {
};

template <>
struct is_floating_point<long double> : true_value {
};

template <typename T>
inline constexpr bool is_floating_point_v = is_floating_point<T>::value;
// --------------------

// ---- is_arithmetic ----
template <typename T>
struct is_arithmetic : integral_constant<bool, is_floating_point_v<T> || is_integral_v<T>> {
};

template <typename T>
inline constexpr bool is_arithmetic_v = is_arithmetic<T>::value;
// --------------------

// ---- is_pointer ----
template <typename T>
struct is_actually_pointer : false_value {
};

template <typename T>
struct is_actually_pointer<T*> : true_value {
};

template <typename T>
struct is_pointer : is_actually_pointer<typename remove_const_volatile<T>::type> {
};

template <typename T>
inline constexpr bool is_pointer_v = is_pointer<T>::value;
// --------------------

// ---- is_trivially_constructible ----
template <typename T>
struct is_trivially_constructible : integral_constant<bool, __is_trivially_constructible(T)> {
};

template <typename T>
inline constexpr bool is_trivially_constructible_v = is_trivially_constructible<T>::value;
// --------------------

// ---- is_trivially_destructible ----
template <typename T>
struct is_trivially_destructible : integral_constant<bool, __has_trivial_destructor(T)> {
};

template <typename T>
inline constexpr bool is_trivially_destructible_v = is_trivially_destructible<T>::value;
// --------------------

// ---- is_trivially_copyable ----
template <typename T>
struct is_trivially_copyable : integral_constant<bool, __is_trivially_copyable(T)> {
};

template <typename T>
inline constexpr bool is_trivially_copyable_v = is_trivially_copyable<T>::value;
// --------------------

// ---- is_base_of ----
template <typename Base, typename Derived>
struct is_base_of : integral_constant<bool, __is_base_of(Base, Derived)> {
};

template <typename Base, typename Derived>
inline constexpr bool is_base_of_v = is_base_of<Base, Derived>::value;
// --------------------

// ---- is_same ----
template <typename T, typename U>
struct is_same : false_value {
};

template <typename T>
struct is_same<T, T> : true_value {
};

template <typename T, typename U>
inline constexpr bool is_same_v = is_same<T, U>::value;
// --------------------

// ---- remove_reference ----
template <typename T>
struct remove_reference {
    using type = T;
};

template <typename T>
struct remove_reference<T&> {
    using type = T;
};

template <typename T>
struct remove_reference<T&&> {
    using type = T;
};

template <typename T>
using remove_reference_t = typename remove_reference<T>::type;
// --------------------

// ---- enable_if ----
template <bool value, typename T = void>
struct enable_if {
};

template <typename T>
struct enable_if<true, T> {
    using type = T;
};

template <bool value, typename T = void>
using enable_if_t = typename enable_if<value, T>::type;
// --------------------

// ---- conditional ----
template <bool test, typename IfTrue, typename IfFalse>
struct conditional {
    using type = IfTrue;
};

template <typename IfTrue, typename IfFalse>
struct conditional<false, IfTrue, IfFalse> {
    using type = IfFalse;
};

template <bool test, typename IfTrue, typename IfFalse>
using conditional_t = typename conditional<test, IfTrue, IfFalse>::type;
// --------------------

// ---- in_place ----
struct in_place_t {
    explicit in_place_t() = default;
};

inline constexpr in_place_t in_place {};
// --------------------

// ---- void_t ----
template <class...>
using void_t = void;
// --------------------

// ---- make_unsigned ----
template <typename T>
struct make_unsigned { };

template <>
struct make_unsigned<i8> {
    using type = u8;
};

template <>
struct make_unsigned<i16> {
    using type = u16;
};

template <>
struct make_unsigned<i32> {
    using type = u32;
};

template <>
struct make_unsigned<i64> {
    using type = u64;
};

template <>
struct make_unsigned<u8> {
    using type = u8;
};

template <>
struct make_unsigned<u16> {
    using type = u16;
};

template <>
struct make_unsigned<u32> {
    using type = u32;
};

template <>
struct make_unsigned<u64> {
    using type = u64;
};

template <typename T>
using make_unsigned_t = typename make_unsigned<T>::type;
// -----------------------

// ---- is_unsigned ----
// TODO: implement is_any_of and remove_const_volatile here
template <typename T>
struct is_unsigned : false_value {
};

template <>
struct is_unsigned<char> : conditional_t<static_cast<char>(-1) < 0, false_value, true_value> {
};

template <>
struct is_unsigned<signed char> : false_value {
};

template <>
struct is_unsigned<unsigned char> : true_value {
};

template <>
struct is_unsigned<short> : false_value {
};

template <>
struct is_unsigned<unsigned short> : true_value {
};

template <>
struct is_unsigned<int> : false_value {
};

template <>
struct is_unsigned<unsigned int> : true_value {
};

template <>
struct is_unsigned<long> : false_value {
};

template <>
struct is_unsigned<unsigned long> : true_value {
};

template <>
struct is_unsigned<long long> : false_value {
};

template <>
struct is_unsigned<unsigned long long> : true_value {
};

template <typename T>
inline constexpr bool is_unsigned_v = is_unsigned<T>::value;
// --------------------

// ---- is_signed ----
// TODO: implement is_any_of and remove_const_volatile here
template <typename T>
struct is_signed : false_value {
};

template <>
struct is_signed<char> : conditional_t<static_cast<char>(-1) < 0, true_value, false_value> {
};

template <>
struct is_signed<signed char> : true_value {
};

template <>
struct is_signed<unsigned char> : false_value {
};

template <>
struct is_signed<short> : true_value {
};

template <>
struct is_signed<unsigned short> : false_value {
};

template <>
struct is_signed<int> : true_value {
};

template <>
struct is_signed<unsigned int> : false_value {
};

template <>
struct is_signed<long> : true_value {
};

template <>
struct is_signed<unsigned long> : false_value {
};

template <>
struct is_signed<long long> : true_value {
};

template <>
struct is_signed<unsigned long long> : false_value {
};

template <typename T>
inline constexpr bool is_signed_v = is_signed<T>::value;
// --------------------

// ---- numeric_limits ----
// TODO: implement is_any_of and remove_const_volatile here
template <typename T>
struct numeric_limits {
};

template <>
struct numeric_limits<char> {
    static constexpr char min()
    {
        if constexpr (is_unsigned_v<char>)
           return 0;

        return -__SCHAR_MAX__ - 1;
    }

    static constexpr char max()
    {
        if constexpr (is_unsigned_v<char>)
            return __SCHAR_MAX__ * 2 + 1;

        return __SCHAR_MAX__;
    }
};

template <>
struct numeric_limits<signed char> {
    static constexpr signed char min() { return -__SCHAR_MAX__ - 1; }
    static constexpr signed char max() { return __SCHAR_MAX__; }
};

template <>
struct numeric_limits<unsigned char> {
    static constexpr unsigned char min() { return 0; }
    static constexpr unsigned char max() { return __SCHAR_MAX__ * 2 + 1; }
};

template <>
struct numeric_limits<short> {
    static constexpr short min() { return -__SHRT_MAX__ - 1; }
    static constexpr short max() { return __SCHAR_MAX__; }
};

template <>
struct numeric_limits<unsigned short> {
    static constexpr unsigned short min() { return 0; }
    static constexpr unsigned short max() { return __SHRT_MAX__ * 2 + 1; }
};

template <>
struct numeric_limits<int> {
    static constexpr int min() { return -__INT_MAX__ - 1; }
    static constexpr int max() { return __INT_MAX__; }
};

template <>
struct numeric_limits<unsigned int> {
    static constexpr unsigned int min() { return 0; }
    static constexpr unsigned int max() { return __INT_MAX__ * 2u + 1; }
};

template <>
struct numeric_limits<long> {
    static constexpr long min() { return -__LONG_MAX__ - 1; }
    static constexpr long max() { return __LONG_MAX__; }
};

template <>
struct numeric_limits<unsigned long> {
    static constexpr unsigned long min() { return 0; }
    static constexpr unsigned long max() { return __LONG_MAX__ * 2ul + 1; }
};

template <>
struct numeric_limits<long long> {
    static constexpr long long min() { return -__LONG_LONG_MAX__ - 1; }
    static constexpr long long max() { return __LONG_LONG_MAX__; }
};

template <>
struct numeric_limits<unsigned long long> {
    static constexpr unsigned long long min() { return 0; }
    static constexpr unsigned long long max() { return __LONG_LONG_MAX__ * 2ull + 1; }
};
// --------------------
