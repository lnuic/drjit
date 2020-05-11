/*
    enoki/array_traits.h -- Type traits for Enoki arrays

    Enoki is a C++ template library for efficient vectorization and
    differentiation of numerical kernels on modern processor architectures.

    Copyright (c) 2020 Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a BSD-style
    license that can be found in the LICENSE file.
*/

#pragma once

#include <enoki/fwd.h>
#include <utility>
#include <stdint.h>

NAMESPACE_BEGIN(enoki)

using ssize_t = std::make_signed_t<size_t>;

// -----------------------------------------------------------------------
//! @{ \name General type traits (not specific to Enoki arrays)
// -----------------------------------------------------------------------

/// Convenience wrapper around std::enable_if
template <bool B> using enable_if_t = std::enable_if_t<B, int>;

namespace detail {
    /// Identity function for types
    template <typename T, typename...> struct identity {
        using type = T;
    };

    /// Detector pattern that is used to drive many type traits below
    template <typename, template <typename...> typename Op, typename... Ts>
    struct detector : std::false_type { };

    template <template <typename...> typename Op, typename... Ts>
    struct detector<std::void_t<Op<Ts...>>, Op, Ts...>
        : std::true_type { };

    template <typename... > constexpr bool false_v = false;

    template <typename T>
    constexpr bool is_integral_ext_v =
        std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>;

    /// Relaxed type equivalence to work around 'long' vs 'long long' differences
    template <typename T0, typename T1>
    static constexpr bool is_same_v =
        sizeof(T0) == sizeof(T1) &&
        std::is_floating_point_v<T0> == std::is_floating_point_v<T1> &&
        std::is_signed_v<T0> == std::is_signed_v<T1> &&
        is_integral_ext_v<T0> == is_integral_ext_v<T1>;

    template <typename T>
    static constexpr bool is_bool_v = std::is_same_v<std::decay_t<T>, bool>;

    /// SFINAE checker for component-based array constructors
    template <size_t Size, typename... Ts>
    using enable_if_components_t = enable_if_t<sizeof...(Ts) == Size && Size != 1 &&
              (!std::is_same_v<Ts, detail::reinterpret_flag> && ...)>;

}

/// True for any type that can reasonably be packed into a 32 bit integer array
template <typename T>
using enable_if_int32_t = enable_if_t<sizeof(T) == 4 && detail::is_integral_ext_v<T>>;

/// True for any type that can reasonably be packed into a 64 bit integer array
template <typename T>
using enable_if_int64_t = enable_if_t<sizeof(T) == 8 && detail::is_integral_ext_v<T>>;

template <typename... Ts> using identity_t = typename detail::identity<Ts...>::type;

template <template<typename ...> class Op, class... Args>
constexpr bool is_detected_v = detail::detector<void, Op, Args...>::value;

constexpr size_t Dynamic = (size_t) -1;

//! @}
// -----------------------------------------------------------------------

// -----------------------------------------------------------------------
//! @{ \name Type traits for querying the properties of Enoki arrays
// -----------------------------------------------------------------------

namespace detail {
    template <typename T> using is_array_det           = std::enable_if_t<T::IsEnoki>;
    template <typename T> using is_masked_array_det    = std::enable_if_t<T::IsEnoki && T::Derived::IsMaskedArray>;
    template <typename T> using is_static_array_det    = std::enable_if_t<T::IsEnoki && T::Derived::Size != Dynamic>;
    template <typename T> using is_dynamic_array_det   = std::enable_if_t<T::IsEnoki && T::Derived::Size == Dynamic>;
    template <typename T> using is_packed_array_det    = std::enable_if_t<T::IsEnoki && T::Derived::IsPacked>;
    template <typename T> using is_recursive_array_det = std::enable_if_t<T::IsEnoki && T::Derived::IsRecursive>;
    template <typename T> using is_cuda_array_det      = std::enable_if_t<T::IsEnoki && T::Derived::IsCUDA>;
    template <typename T> using is_llvm_array_det      = std::enable_if_t<T::IsEnoki && T::Derived::IsLLVM>;
    template <typename T> using is_jit_array_det       = std::enable_if_t<T::IsEnoki && T::Derived::IsJIT>;
    template <typename T> using is_diff_array_det      = std::enable_if_t<T::IsEnoki && T::Derived::IsDiff>;
    template <typename T> using is_mask_det            = std::enable_if_t<T::IsEnoki && T::Derived::IsMask>;
    template <typename T> using is_dynamic_det         = std::enable_if_t<T::IsDynamic>;
}

template <typename T>
constexpr bool is_array_v = is_detected_v<detail::is_array_det, std::decay_t<T>>;
template <typename T> using enable_if_array_t = enable_if_t<is_array_v<T>>;
template <typename T> using enable_if_not_array_t = enable_if_t<!is_array_v<T>>;

template <typename T>
constexpr bool is_static_array_v = is_detected_v<detail::is_static_array_det, std::decay_t<T>>;
template <typename T> using enable_if_static_array_t = enable_if_t<is_static_array_v<T>>;

template <typename T>
constexpr bool is_dynamic_array_v = is_detected_v<detail::is_dynamic_array_det, std::decay_t<T>>;
template <typename T> using enable_if_dynamic_array_t = enable_if_t<is_dynamic_array_v<T>>;

template <typename T>
constexpr bool is_dynamic_v = is_detected_v<detail::is_dynamic_det, std::decay_t<T>>;
template <typename T> using enable_if_dynamic_t = enable_if_t<is_dynamic_v<T>>;

template <typename T>
constexpr bool is_packed_array_v = is_detected_v<detail::is_packed_array_det, std::decay_t<T>>;
template <typename T> using enable_if_packed_array_t = enable_if_t<is_packed_array_v<T>>;

template <typename T>
constexpr bool is_cuda_array_v = is_detected_v<detail::is_cuda_array_det, std::decay_t<T>>;
template <typename T> using enable_if_cuda_array_t = enable_if_t<is_cuda_array_v<T>>;

template <typename T>
constexpr bool is_llvm_array_v = is_detected_v<detail::is_llvm_array_det, std::decay_t<T>>;
template <typename T> using enable_if_llvm_array_t = enable_if_t<is_llvm_array_v<T>>;

template <typename T>
constexpr bool is_jit_array_v = is_detected_v<detail::is_jit_array_det, std::decay_t<T>>;
template <typename T> using enable_if_jit_array_t = enable_if_t<is_jit_array_v<T>>;

template <typename T>
constexpr bool is_diff_array_v = is_detected_v<detail::is_diff_array_det, std::decay_t<T>>;
template <typename T> using enable_if_diff_array_t = enable_if_t<is_diff_array_v<T>>;

template <typename T>
constexpr bool is_recursive_array_v = is_detected_v<detail::is_recursive_array_det, std::decay_t<T>>;
template <typename T> using enable_if_recursive_array_t = enable_if_t<is_recursive_array_v<T>>;

template <typename T>
constexpr bool is_mask_v = std::is_same_v<T, bool> || is_detected_v<detail::is_mask_det, std::decay_t<T>>;
template <typename T> using enable_if_mask_t = enable_if_t<is_mask_v<T>>;

template <typename... Ts> constexpr bool is_array_any_v = (is_array_v<Ts> || ...);
template <typename... Ts> using enable_if_array_any_t = enable_if_t<is_array_any_v<Ts...>>;

template <typename T> constexpr bool has_struct_support_v = struct_support<T>::Defined;

namespace detail {
    template <typename T, typename = int> struct scalar {
        using type = std::decay_t<T>;
    };

    template <typename T> struct scalar<T, enable_if_array_t<T>> {
        using type = typename std::decay_t<T>::Derived::Scalar;
    };

    template <typename T, typename = int> struct value {
        using type = std::decay_t<T>;
    };

    template <typename T> struct value<T, enable_if_array_t<T>> {
        using type = typename std::decay_t<T>::Derived::Value;
    };

    template <typename T, typename = int> struct array_depth {
        static constexpr size_t value = 0;
    };

    template <typename T> struct array_depth<T, enable_if_array_t<T>> {
        static constexpr size_t value = std::decay_t<T>::Derived::Depth;
    };

    template <typename T, typename = int> struct array_size {
        static constexpr size_t value = 1;
    };

    template <typename T> struct array_size<T, enable_if_array_t<T>> {
        static constexpr size_t value = std::decay_t<T>::Derived::Size;
    };
}

/// Type trait to access the base scalar type underlying a potentially nested array
template <typename T> using scalar_t = typename detail::scalar<T>::type;

/// Type trait to access the value type of an array
template <typename T> using value_t = typename detail::value<T>::type;

/// Determine the depth of a nested Enoki array (scalars evaluate to zero)
template <typename T> constexpr size_t array_depth_v = detail::array_depth<T>::value;

/// Determine the size of a nested Enoki array (scalars evaluate to one)
template <typename T> constexpr size_t array_size_v = detail::array_size<T>::value;

template <typename T> constexpr bool is_floating_point_v = std::is_floating_point_v<scalar_t<T>> && !is_mask_v<T>;
template <typename T> constexpr bool is_integral_v = std::is_integral_v<scalar_t<T>> && !is_mask_v<T>;
template <typename T> constexpr bool is_arithmetic_v = std::is_arithmetic_v<scalar_t<T>> && !is_mask_v<T>;
template <typename T> constexpr bool is_signed_v = std::is_signed_v<scalar_t<T>>;
template <typename T> constexpr bool is_unsigned_v = std::is_unsigned_v<scalar_t<T>>;

namespace detail {
    template <typename T, typename = int> struct mask {
        using type = bool;
    };

    template <typename T> struct mask<T, enable_if_array_t<T>> {
        using type = typename std::decay_t<T>::Derived::MaskType;
    };

    template <typename T, typename = int> struct array {
        using type = T;
    };

    template <typename T> struct array<T, enable_if_array_t<T>> {
        using type = typename std::decay_t<T>::Derived::ArrayType;
    };
}

/// Type trait to access the mask type underlying an array
template <typename T> using mask_t = typename detail::mask<T>::type;

/// Type trait to access the array type underlying a mask
template <typename T> using array_t = typename detail::array<T>::type;

namespace detail {
    template <typename T, typename = int> struct diff_array {
        using type = void;
    };

    template <typename T> struct diff_array<T, enable_if_t<is_diff_array_v<value_t<T>>>> {
        using type = diff_array<value_t<T>>;
    };

    template <typename T>
    struct diff_array<
        T, enable_if_t<is_diff_array_v<T> && !is_diff_array_v<value_t<T>>>> {
        using type = T;
    };
};

/// Get the differentiable array underlying a potentially nested array
template <typename T> using diff_array_t = typename detail::diff_array<T>::type;

//! @}
// -----------------------------------------------------------------------

// -----------------------------------------------------------------------
//! @{ \name Traits for determining the types of desired Enoki arrays
// -----------------------------------------------------------------------

namespace detail {
    /// Convenience class to choose an arithmetic type based on its size and flavor
    template <size_t Size> struct sized_types { };

    template <> struct sized_types<1> {
        using Int = int8_t;
        using UInt = uint8_t;
    };

    template <> struct sized_types<2> {
        using Int = int16_t;
        using UInt = uint16_t;
        using Float = half;
    };

    template <> struct sized_types<4> {
        using Int = int32_t;
        using UInt = uint32_t;
        using Float = float;
    };

    template <> struct sized_types<8> {
        using Int = int64_t;
        using UInt = uint64_t;
        using Float = double;
    };

    template <typename T, typename Value, typename = int>
    struct replace_scalar {
        using type = Value;
    };

    template <typename T, typename Value> struct replace_scalar<T, Value, enable_if_array_t<T>> {
        using Entry = typename replace_scalar<value_t<T>, Value>::type;
        using type = typename std::decay_t<T>::Derived::template ReplaceValue<Entry>;
    };
};

/// Replace the base scalar type of a (potentially nested) array
template <typename T, typename Value>
using replace_scalar_t = typename detail::replace_scalar<T, Value>::type;

/// Integer-based version of a given array class
template <typename T>
using int_array_t = replace_scalar_t<T, typename detail::sized_types<sizeof(scalar_t<T>)>::Int>;

/// Unsigned integer-based version of a given array class
template <typename T>
using uint_array_t = replace_scalar_t<T, typename detail::sized_types<sizeof(scalar_t<T>)>::UInt>;

/// Floating point-based version of a given array class
template <typename T>
using float_array_t = replace_scalar_t<T, typename detail::sized_types<sizeof(scalar_t<T>)>::Float>;

template <typename T> using int8_array_t   = replace_scalar_t<T, int8_t>;
template <typename T> using uint8_array_t  = replace_scalar_t<T, uint8_t>;
template <typename T> using int16_array_t   = replace_scalar_t<T, int16_t>;
template <typename T> using uint16_array_t  = replace_scalar_t<T, uint16_t>;
template <typename T> using int32_array_t   = replace_scalar_t<T, int32_t>;
template <typename T> using uint32_array_t  = replace_scalar_t<T, uint32_t>;
template <typename T> using int64_array_t   = replace_scalar_t<T, int64_t>;
template <typename T> using uint64_array_t  = replace_scalar_t<T, uint64_t>;
template <typename T> using float16_array_t = replace_scalar_t<T, half>;
template <typename T> using float32_array_t = replace_scalar_t<T, float>;
template <typename T> using float64_array_t = replace_scalar_t<T, double>;
template <typename T> using bool_array_t    = replace_scalar_t<T, bool>;
template <typename T> using size_array_t    = replace_scalar_t<T, size_t>;
template <typename T> using ssize_array_t   = replace_scalar_t<T, ssize_t>;

//! @}
// -----------------------------------------------------------------------

// -----------------------------------------------------------------------
//! @{ \name Trait for determining the type of an expression
// -----------------------------------------------------------------------

namespace detail {
    /// Extract the most deeply nested Enoki array type from a list of arguments
    template <typename... Args> struct deepest;
    template <> struct deepest<> { using type = void; };

    template <typename Arg, typename... Args> struct deepest<Arg, Args...> {
    private:
        using T0 = Arg;
        using T1 = typename deepest<Args...>::type;

        // Give precedence to dynamic arrays
        static constexpr size_t D0 = array_depth_v<T0>;
        static constexpr size_t D1 = array_depth_v<T1>;

    public:
        using type = std::conditional_t<(D1 > D0 || D0 == 0), T1, T0>;
    };

    template <typename... Ts> struct expr {
        using type = decltype((std::declval<Ts>() + ...));
    };

    template <typename T> struct expr<T> {
        using type = std::decay_t<T>;
    };

    template <typename T> struct expr<T, T> : expr<T> { };
    template <typename T> struct expr<T, T, T> : expr<T> { };
    template <typename T> struct expr<T*, std::nullptr_t> : expr<T*> { };
    template <typename T> struct expr<std::nullptr_t, T*> : expr<T*> { };

    template <typename ... Ts> using deepest_t = typename deepest<Ts...>::type;
}

/// Type trait to compute the type of an arithmetic expression involving Ts...
template <typename... Ts>
using expr_t = replace_scalar_t<typename detail::deepest_t<Ts...>,
                                typename detail::expr<scalar_t<Ts>...>::type>;

/// Intermediary for performing a cast from 'const Source &' to 'const Target &'
template <typename Source, typename Target>
using ref_cast_t =
    std::conditional_t<std::is_same_v<Source, Target>, const Target &, Target>;

/// As above, but move-construct if possible. Convert values with the wrong type.
template <typename Source, typename Target>
using move_cast_t = std::conditional_t<
    std::is_same_v<std::decay_t<Source>, Target>,
    std::conditional_t<std::is_reference_v<Source>, Source, Source &&>, Target>;


//! @}
// -----------------------------------------------------------------------

NAMESPACE_END(enoki)
