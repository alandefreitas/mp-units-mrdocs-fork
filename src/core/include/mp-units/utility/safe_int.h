// The MIT License (MIT)
//
// Copyright (c) 2018 Mateusz Pusz
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <mp-units/bits/fixed_point.h>
#include <mp-units/bits/hacks.h>
#include <mp-units/bits/module_macros.h>
#include <mp-units/utility/constrained.h>
#if MP_UNITS_HOSTED
#include <mp-units/ext/format.h>
#endif
#include <mp-units/framework/customization_points.h>
#include <mp-units/framework/representation_concepts.h>
#include <mp-units/framework/scaling.h>

#ifndef MP_UNITS_IN_MODULE_INTERFACE
#ifdef MP_UNITS_IMPORT_STD
import std;
#else
#include <compare>
#include <concepts>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#if MP_UNITS_HOSTED
#include <stdexcept>
#endif
#endif
#endif

namespace mp_units::utility {

// ============================================================================
// OverflowPolicy concept
// ============================================================================

/**
 * @brief Concept satisfied by overflow error policies usable with `safe_int`.
 * @tparam EP the candidate error policy type
 */
MP_UNITS_EXPORT template<typename EP>
concept OverflowPolicy = requires(std::string_view msg) { EP::on_overflow(msg); };

// ============================================================================
// Error policies (inherit base policies from constrained.h and add on_overflow)
// ============================================================================

/**
 * @brief Error policy that terminates the program on overflow (always available, freestanding-safe).
 */
MP_UNITS_EXPORT struct safe_int_terminate_policy : terminate_policy {
  /**
   * @brief Terminates the program by aborting; called by `safe_int` on overflow.
   * @param msg description of the overflow condition (unused; the program aborts unconditionally)
   */
  [[noreturn]] static void on_overflow(std::string_view msg) noexcept { std::abort(); }
};

#if MP_UNITS_HOSTED

/**
 * @brief Error policy that throws std::overflow_error on overflow (hosted only).
 */
MP_UNITS_EXPORT struct safe_int_throw_policy : throw_policy {
  /**
   * @brief Throws `std::overflow_error`; called by `safe_int` on overflow.
   * @param msg description of the overflow condition
   */
  [[noreturn]] static void on_overflow(std::string_view msg) { throw std::overflow_error(std::string(msg)); }
};

#endif  // MP_UNITS_HOSTED


// ============================================================================
// Overflow detection helpers (for detail::integral T)
//
// NOTE: These helpers work on a SINGLE type T, not heterogeneous types.
// The calling code (in safe_int_binary_ops) handles integral promotion
// by first determining the promoted result type R = decltype(T() + U()),
// then calling add_overflows<R>(static_cast<R>(lhs), static_cast<R>(rhs)).
// This means both operands are converted to R before checking, which is
// consistent with how C++ arithmetic actually behaves.
// ============================================================================

namespace detail {

using namespace ::mp_units::detail;

/**
 * @brief Checks whether `lhs + rhs` overflows for the signed or unsigned integral type `T`.
 *
 * Uses `~T{0}` instead of `std::numeric_limits<T>::max()` for the unsigned case so that
 * the check is correct even for types (e.g. `uint128_t` on GCC in strict mode) for which
 * `std::numeric_limits` is not specialized.
 *
 * @tparam T the integral type of both operands
 * @param lhs the left-hand operand
 * @param rhs the right-hand operand
 * @return `true` if the addition overflows `T`, `false` otherwise
 */
template<integral T>
[[nodiscard]] constexpr bool add_overflows(T lhs, T rhs) noexcept
{
  if constexpr (is_signed_v<T>) {
    // positive overflow: both positive and sum negative
    if (rhs > 0 && lhs > std::numeric_limits<T>::max() - rhs) return true;
    // negative overflow: both negative and sum positive
    if (rhs < 0 && lhs < std::numeric_limits<T>::min() - rhs) return true;
    return false;
  } else {
    return lhs > ~T{0} - rhs;  // ~T{0} = unsigned maximum without numeric_limits
  }
}

/**
 * @brief Checks whether `lhs - rhs` overflows for the signed or unsigned integral type `T`.
 * @tparam T the integral type of both operands
 * @param lhs the left-hand operand
 * @param rhs the right-hand operand
 * @return `true` if the subtraction overflows `T`, `false` otherwise
 */
template<integral T>
[[nodiscard]] constexpr bool sub_overflows(T lhs, T rhs) noexcept
{
  if constexpr (is_signed_v<T>) {
    if (rhs < 0 && lhs > std::numeric_limits<T>::max() + rhs) return true;
    if (rhs > 0 && lhs < std::numeric_limits<T>::min() + rhs) return true;
    return false;
  } else {
    return lhs < rhs;
  }
}

/**
 * @brief Checks whether `lhs * rhs` overflows for the signed or unsigned integral type `T`.
 *
 * For types narrower than the widest native integer, uses double-width arithmetic.
 * For the widest native integer (e.g. `uint128_t` on platforms with `__SIZEOF_INT128__`)
 * where no wider native type exists, uses a division-based check.
 *
 * The division-based path avoids `std::numeric_limits<T>::max/min` because on GCC in
 * strict mode (`-std=c++20`) `std::numeric_limits` is not specialized for `__int128` types.
 * Instead it derives max/min directly from the binary representation:
 *   - unsigned max = `~T{0}`
 *   - signed max  = `static_cast<signed>(unsigned_max >> 1)`
 *   - signed min  = `-signed_max - 1`
 *
 * @tparam T the integral type of both operands
 * @param lhs the left-hand operand
 * @param rhs the right-hand operand
 * @return `true` if the multiplication overflows `T`, `false` otherwise
 */
template<integral T>
[[nodiscard]] constexpr bool mul_overflows(T lhs, T rhs) noexcept
{
  // Use a wider type for the overflow check whenever one exists.  On platforms without native
  // `__int128`, `int128_t` is the synthetic `double_width_int<int64_t>` — still a usable
  // 128-bit type — so `max_native_width` (which only counts builtin widths) is the wrong
  // ceiling; check directly against `integer_rep_width_v<int128_t>` instead.  The else branch
  // below assumes T is the widest available signed integer (i.e. (u)int128_t) and computes
  // `max` by truncating uint128_t — that truncation is what produced the bug on MSVC for
  // T == long long.
  if constexpr (integer_rep_width_v<T> < integer_rep_width_v<int128_t>) {
    using wide = double_width_int_for_t<T>;
    const wide product = static_cast<wide>(lhs) * static_cast<wide>(rhs);
    return product > static_cast<wide>(std::numeric_limits<T>::max()) ||
           product < static_cast<wide>(std::numeric_limits<T>::min());
  } else {
    // No wider native type available; use a division-based overflow check.
    if (lhs == T{0} || rhs == T{0}) return false;
    if constexpr (!is_signed_v<T>) {
      const T max = ~T{0};  // all bits set = unsigned maximum; no numeric_limits needed
      return lhs > max / rhs;
    } else {
      // Compute signed max/min without std::numeric_limits (not specialized for __int128
      // in GCC strict mode) and without std::make_unsigned_t (same issue).
      // T is the widest signed type; uint128_t is its unsigned counterpart.
      const uint128_t umax = ~uint128_t{0};
      const T max = static_cast<T>(umax >> 1);  // INT128_MAX (or equiv.) without numeric_limits
      const T min = -max - T{1};                // INT128_MIN (or equiv.) without numeric_limits
      if (lhs > T{0} && rhs > T{0}) return lhs > max / rhs;
      if (lhs < T{0} && rhs < T{0}) return lhs < max / rhs;
      if (lhs > T{0} && rhs < T{0}) return rhs < min / lhs;
      /* lhs < T{0} && rhs > T{0} */ return lhs < min / rhs;
    }
  }
}

/**
 * @brief Checks whether `lhs / rhs` overflows for the signed or unsigned integral type `T`.
 *
 * Overflow occurs only for signed division of `T`'s minimum value by `-1`, or when
 * `rhs` is zero.
 *
 * @tparam T the integral type of both operands
 * @param lhs the dividend
 * @param rhs the divisor
 * @return `true` if the division overflows `T` or `rhs` is zero, `false` otherwise
 */
template<integral T>
[[nodiscard]] constexpr bool div_overflows(T lhs, T rhs) noexcept
{
  if (rhs == 0) return true;
  if constexpr (is_signed_v<T>)
    return lhs == std::numeric_limits<T>::min() && rhs == T{-1};
  else
    return false;
}

/**
 * @brief Checks whether negating `v` overflows for the signed or unsigned integral type `T`.
 *
 * For signed `T`, only the minimum representable value overflows. For unsigned `T`,
 * negating any non-zero value overflows.
 *
 * @tparam T the integral type of `v`
 * @param v the value to negate
 * @return `true` if `-v` overflows `T`, `false` otherwise
 */
template<integral T>
[[nodiscard]] constexpr bool neg_overflows(T v) noexcept
{
  if constexpr (is_signed_v<T>)
    return v == std::numeric_limits<T>::min();
  else
    return v != T{0};  // negation of any non-zero unsigned overflows
}

/**
 * @brief Extracts the underlying integral type from an arithmetic wrapper.
 *
 * Primary template, used for a plain integral (or otherwise non-wrapper) type `T`, which
 * maps to itself. Non-arithmetic types (e.g. `std::string`) also fall back to this
 * template, mapping to themselves, and are later rejected by an `std::integral` check.
 *
 * @tparam T the type to extract the underlying integral type from
 */
template<typename T>
struct underlying_int_type_helper {
  /// @brief The underlying integral type: `T` itself.
  using type = T;
};

/**
 * @brief Specialization for integral wrappers exposing a `value_type` member
 * (e.g. `safe_int<T>`, `constrained<T, ...>`), which map to `T::value_type`.
 *
 * Guarded by `std::numeric_limits<T>::is_specialized` to correctly exclude containers
 * like `std::string`, whose `value_type` is `char` even though `std::string` is not an
 * arithmetic type. Uses a type-membership check (`typename T::value_type`) rather than
 * a function-call requires-expression (`{ v.value() }`) to avoid over-eager template
 * instantiation in Clang 16, which would cause recursive satisfaction of
 * `convertible_to` constraints.
 */
template<typename T>
  requires requires { typename T::value_type; } && integral<typename T::value_type> &&
           std::numeric_limits<T>::is_specialized
struct underlying_int_type_helper<T> {
  /// @brief The underlying integral type: `T::value_type`.
  using type = typename T::value_type;
};

/// @brief The underlying integral type of `T`, as computed by `underlying_int_type_helper`.
template<typename T>
using underlying_int_type_t = typename underlying_int_type_helper<T>::type;

/**
 * @brief Checks whether `v` is representable as type `To`.
 *
 * Avoids `std::in_range<To>` and `std::cmp_*` because those require `std::integral<To>`,
 * which is `false` for `__int128` / `unsigned __int128` on GCC. Uses widened comparisons
 * against `std::numeric_limits<To>::min/max` instead.
 *
 * @tparam To the target integral type
 * @tparam From the source integral type
 * @param v the value to check
 * @return `true` if `v` fits in `To`, `false` otherwise
 */
template<integral To, integral From>
[[nodiscard]] constexpr bool int_in_range(From v) noexcept
{
  if constexpr (sizeof(From) < sizeof(To)) {
    // Strictly narrower source: fits unless signed-to-unsigned (negatives don't fit).
    if constexpr (is_signed_v<From> && !is_signed_v<To>) return v >= From{0};
    return true;
  } else if constexpr (sizeof(From) == sizeof(To)) {
    if constexpr (is_signed_v<From> == is_signed_v<To>) return true;  // same sign, same width
    if constexpr (!is_signed_v<From>)                                 // unsigned From -> signed To, same width
      return v <= static_cast<From>(std::numeric_limits<To>::max());
    else  // signed From -> unsigned To, same width
      return v >= From{0};
  } else {
    // From is wider: do runtime range check using From as the comparison type.
    if constexpr (!is_signed_v<From>) {
      // unsigned From, narrower To: upper-bound check only (From is always >= 0)
      return v <= static_cast<From>(std::numeric_limits<To>::max());
    } else {
      if (v < From{0}) {
        if constexpr (!is_signed_v<To>)
          return false;  // negative -> unsigned: never
        else
          return v >= static_cast<From>(std::numeric_limits<To>::min());
      }
      return v <= static_cast<From>(std::numeric_limits<To>::max());
    }
  }
}

/**
 * @brief `true` iff every value of `From` is exactly representable in `To`.
 *
 * Works for plain integrals and integral-valued wrappers with a `value_type` member
 * (e.g. `safe_int<T>`). Uses `int_in_range` on the extremes of `From` rather than
 * `std::in_range`, so it works for `__int128` / `unsigned __int128` on GCC where
 * `std::integral<__int128>` is `false`.
 *
 * @tparam From the source type
 * @tparam To the target type
 */
template<typename From, typename To>
inline constexpr bool is_value_preserving_int_v = [] {
  using from_raw_t = underlying_int_type_t<From>;
  using to_raw_t = underlying_int_type_t<To>;
  if constexpr (integral<from_raw_t> && integral<to_raw_t>)
    return int_in_range<to_raw_t>(std::numeric_limits<from_raw_t>::min()) &&
           int_in_range<to_raw_t>(std::numeric_limits<from_raw_t>::max());
  else
    return false;
}();

/**
 * @brief `true` iff converting `From` to `To` is guaranteed to preserve the value.
 *
 * Uses an integer range check when both types have integral underlying types, otherwise
 * evaluates to `false` (non-integral types cannot be statically guaranteed
 * value-preserving).
 *
 * @tparam From the source type
 * @tparam To the target type
 */
template<typename From, typename To>
inline constexpr bool is_value_preserving_v = [] {
  if constexpr (integral<underlying_int_type_t<From>> && integral<underlying_int_type_t<To>>)
    return is_value_preserving_int_v<From, To>;
  else
    return false;
}();

/**
 * @brief Converts `v` to `To`, raising via `EP::on_overflow` if the value would not fit.
 *
 * The range check is skipped when `From` is value-preserving into `To` (`To` is at least
 * as wide as `From`), because every possible `From` value is then guaranteed to fit.
 *
 * @tparam To the target integral type
 * @tparam EP the overflow policy invoked when `v` does not fit in `To`
 * @tparam From the source type
 * @param v the value to convert
 * @return `v` converted to `To`
 */
template<integral To, typename EP, typename From>
  requires std::is_constructible_v<To, const From&>
[[nodiscard]] constexpr To checked_int_cast(const From& v)
{
  if constexpr (integral<std::remove_cvref_t<From>> && !is_value_preserving_int_v<std::remove_cvref_t<From>, To>)
    if (!int_in_range<To>(v)) EP::on_overflow("safe_int: narrowing conversion overflow");
  return silent_cast<To>(v);
}

}  // namespace detail

MP_UNITS_EXPORT template<detail::integral T, OverflowPolicy EP>
class safe_int;

namespace detail {

/// @brief `true` iff `T` is a specialization of `safe_int`.
template<typename T>
inline constexpr bool is_safe_int_v = is_specialization_of<T, safe_int>;

/**
 * @brief The result type of a binary arithmetic operation between `A` and `B`, after
 * integral promotion.
 * @tparam A the first operand's integral type
 * @tparam B the second operand's integral type
 */
template<integral A, integral B>
using integral_op_result_t = decltype(A{} + B{});

/**
 * @brief `true` iff `A` and `B` have the same signedness.
 *
 * Uses `detail::is_signed_v` (from fixed_point.h) rather than `std::is_signed_v` so that
 * `__int128` and `unsigned __int128` are treated correctly on GCC in strict mode
 * (`-std=c++20`): `std::is_signed<__int128>` is `false` there (not specialized), but
 * `__int128` is a signed type. `detail::is_signed_v` has explicit specializations.
 *
 * @tparam A the first integral type
 * @tparam B the second integral type
 */
template<integral A, integral B>
inline constexpr bool same_sign_v = is_signed_v<A> == is_signed_v<B>;

// ============================================================================
// safe_int_binary_ops: Hidden Friend Injection base
//
// This non-template base struct injects heterogeneous safe_int comparison
// operators as hidden friends with ALL four template parameters free.
// Because safe_int<T,EP> inherits from this struct, it becomes an associated
// class of every safe_int<T,EP> specialization; ADL finds these friends
// whenever either argument is any safe_int<*,*>, giving full symmetry.
//
// These friends handle every heterogeneous combination (different T, different
// EP, or both). The homogeneous case (same T, same EP) is handled by the
// non-template hidden friends inside safe_int itself — in overload resolution
// a non-template beats a template, so there is no ambiguity.
//
// std::cmp_* is used throughout; it is correct for both same-sign and
// cross-sign integer comparisons.
// ============================================================================

/**
 * @brief Base class injecting heterogeneous `safe_int` comparison and arithmetic
 * operators as hidden friends.
 *
 * This non-template base struct injects heterogeneous `safe_int` operators as hidden
 * friends with all relevant template parameters free. Because `safe_int<T, EP>` inherits
 * from this struct, it becomes an associated class of every `safe_int<T, EP>`
 * specialization; ADL finds these friends whenever either argument is any
 * `safe_int<*, *>`, giving full symmetry.
 *
 * These friends handle every heterogeneous combination (different `T`, different `EP`,
 * or both). The homogeneous case (same `T`, same `EP`) is handled by the non-template
 * hidden friends inside `safe_int` itself — in overload resolution a non-template beats
 * a template, so there is no ambiguity.
 *
 * `std::cmp_*` is used throughout for comparisons; it is correct for both same-sign and
 * cross-sign integer comparisons.
 */
struct safe_int_binary_ops {
  template<typename T, typename EP1, typename U, typename EP2>
  [[nodiscard]] friend constexpr bool operator==(safe_int<T, EP1> lhs, safe_int<U, EP2> rhs) noexcept
  {
    return std::cmp_equal(lhs.value_, rhs.value_);
  }

  template<typename T, typename EP1, typename U, typename EP2>
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(safe_int<T, EP1> lhs, safe_int<U, EP2> rhs) noexcept
  {
    if (std::cmp_less(lhs.value_, rhs.value_)) return std::strong_ordering::less;
    if (std::cmp_greater(lhs.value_, rhs.value_)) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
  }

  // Heterogeneous arithmetic: safe_int<T,EP> op safe_int<U,EP>, T != U, matching sign.
  // Same idiom as the comparison operators above — ADL finds these via the base class,
  // the homogeneous (T == U) inline-friend operators inside `safe_int<T>` win for same-T
  // calls because non-template beats template in overload resolution, and the
  // heterogeneous calls have no competition.  Resolves the MSVC C2666 ambiguity on
  // `safe_int<int> + safe_int<long>` when `int` and `long` have equal width (Windows).
  template<integral T, integral U, typename EP>
    requires(!std::same_as<T, U> && same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator+(safe_int<T, EP> lhs, safe_int<U, EP> rhs)
    -> safe_int<integral_op_result_t<T, U>, EP>
  {
    using R = integral_op_result_t<T, U>;
    if (add_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs.value_)))
      EP::on_overflow("safe_int: addition overflow");
    return lhs.value_ + rhs.value_;
  }

  template<integral T, integral U, typename EP>
    requires(!std::same_as<T, U> && same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator-(safe_int<T, EP> lhs, safe_int<U, EP> rhs)
    -> safe_int<integral_op_result_t<T, U>, EP>
  {
    using R = integral_op_result_t<T, U>;
    if (sub_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs.value_)))
      EP::on_overflow("safe_int: subtraction overflow");
    return lhs.value_ - rhs.value_;
  }

  template<integral T, integral U, typename EP>
    requires(!std::same_as<T, U> && same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator*(safe_int<T, EP> lhs, safe_int<U, EP> rhs)
    -> safe_int<integral_op_result_t<T, U>, EP>
  {
    using R = integral_op_result_t<T, U>;
    if (mul_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs.value_)))
      EP::on_overflow("safe_int: multiplication overflow");
    return lhs.value_ * rhs.value_;
  }

  template<integral T, integral U, typename EP>
    requires(!std::same_as<T, U> && same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator/(safe_int<T, EP> lhs, safe_int<U, EP> rhs)
    -> safe_int<integral_op_result_t<T, U>, EP>
  {
    using R = integral_op_result_t<T, U>;
    if (div_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs.value_)))
      EP::on_overflow("safe_int: division overflow");
    return lhs.value_ / rhs.value_;
  }

  template<integral T, integral U, typename EP>
    requires(!std::same_as<T, U> && same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator%(safe_int<T, EP> lhs, safe_int<U, EP> rhs)
    -> safe_int<integral_op_result_t<T, U>, EP>
  {
    if (rhs.value_ == U{0}) EP::on_overflow("safe_int: modulo by zero");
    return lhs.value_ % rhs.value_;
  }
};

}  // namespace detail

// ============================================================================
// safe_int<T, ErrorPolicy>
// ============================================================================

/**
 * @brief Wraps an integral type with overflow detection.
 *
 * This class wraps any integral type and models all requirements for mp-units
 * representation types (RealScalar, UnitMagnitudeScalable, etc.). Every arithmetic
 * operation checks for overflow and delegates to the ErrorPolicy::on_overflow()
 * handler on detection.
 *
 * @tparam T            the underlying integral type (e.g. int, long, uint32_t)
 * @tparam ErrorPolicy  how to react to overflow — default: safe_int_throw_policy on hosted,
 *                      safe_int_terminate_policy on freestanding
 */
MP_UNITS_EXPORT template<detail::integral T,
#if MP_UNITS_HOSTED
                         OverflowPolicy ErrorPolicy = safe_int_throw_policy>
#else
                         OverflowPolicy ErrorPolicy = safe_int_terminate_policy>
#endif
class safe_int : detail::safe_int_binary_ops {
  // Invoke ErrorPolicy and return a safe default (for use from noexcept compound-assign paths).
  static constexpr void handle_overflow(std::string_view msg) { ErrorPolicy::on_overflow(msg); }
public:
  // public members required to satisfy structural type requirements :-(
  /// The wrapped value.
  T value_{};
  /// The underlying integral type `T`.
  using value_type = T;
  /// The `ErrorPolicy` used to react to overflow.
  using error_policy = ErrorPolicy;

  /// @brief Default-constructs a `safe_int` holding a value-initialized `T`.
  [[nodiscard]] safe_int() = default;

  /**
   * @brief Constructs a `safe_int` from a non-floating-point value, checking for overflow.
   * @param v the value to convert from
   */
  template<typename U>
    requires(!treat_as_floating_point<std::remove_cvref_t<U>>) && std::is_constructible_v<T, U>
  [[nodiscard]] constexpr explicit(!detail::is_value_preserving_v<std::remove_cvref_t<U>, T> ||
                                   !std::convertible_to<U, T>) safe_int(const U& v) :
      value_(detail::checked_int_cast<T, ErrorPolicy>(v))
  {
  }

  /**
   * @brief Constructs a `safe_int<T, ErrorPolicy>` from a `safe_int` wrapping a different integral type,
   * checking for overflow.
   * @param other the `safe_int` to convert from
   */
  template<detail::integral U>
  [[nodiscard]] constexpr explicit(!detail::is_value_preserving_int_v<U, T>) safe_int(safe_int<U, ErrorPolicy> other) :
      value_(detail::checked_int_cast<T, ErrorPolicy>(other.value()))
  {
  }

  /**
   * @brief Converts to the wrapped value.
   * @return the wrapped value, converted to `T`
   */
  [[nodiscard]] constexpr explicit operator T() const noexcept { return value_; }
  /**
   * @brief Returns the wrapped value.
   * @return the wrapped value
   */
  [[nodiscard]] constexpr T value() const noexcept { return value_; }

  // ==========================================================================
  // Unary operators (+, -, ++, --)
  // Models integral promotion: sub-int types (char, short) promote to int.
  // ==========================================================================

  // -- Unary arithmetic --
  /**
   * @brief Returns a copy of the wrapped value, promoted as `+value_` would promote `T`.
   * @return the (possibly promoted) wrapped value
   */
  [[nodiscard]] constexpr auto operator+() const -> safe_int<decltype(+value_), ErrorPolicy> { return +value_; }
  MP_UNITS_DIAGNOSTIC_PUSH
  // Generic operator- is intentionally instantiated with unsigned T as well (where unary minus
  // returns the same unsigned type by C++ rules; MSVC C4146 flags this even though it is the
  // documented behavior).
  MP_UNITS_DIAGNOSTIC_IGNORE_UNARY_MINUS_UNSIGNED
  /**
   * @brief Returns the negation of the wrapped value, reacting to `ErrorPolicy` on overflow.
   * @return the negated (and possibly promoted) wrapped value
   */
  [[nodiscard]] constexpr auto operator-() const -> safe_int<decltype(-value_), ErrorPolicy>
  {
    if (detail::neg_overflows(+value_)) handle_overflow("safe_int: negation overflow");
    return -value_;
  }
  MP_UNITS_DIAGNOSTIC_POP

  // -- Increment / decrement (use add/sub overflow check when T is integral) --
  /**
   * @brief Pre-increments the wrapped value, reacting to `ErrorPolicy` on overflow.
   * @return a reference to `*this`, after the increment
   */
  constexpr safe_int& operator++()
  {
    if (detail::add_overflows(value_, T{1})) handle_overflow("safe_int: increment overflow");
    ++value_;
    return *this;
  }

  /**
   * @brief Post-increments the wrapped value, reacting to `ErrorPolicy` on overflow.
   * @param tag unused tag parameter that distinguishes this postfix form from the prefix
   * `operator++()`
   * @return a copy of `*this`, before the increment
   */
  constexpr safe_int operator++(int tag)
  {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }

  /**
   * @brief Pre-decrements the wrapped value, reacting to `ErrorPolicy` on overflow.
   * @return a reference to `*this`, after the decrement
   */
  constexpr safe_int& operator--()
  {
    if (detail::sub_overflows(value_, T{1})) handle_overflow("safe_int: decrement overflow");
    --value_;
    return *this;
  }

  /**
   * @brief Post-decrements the wrapped value, reacting to `ErrorPolicy` on overflow.
   * @param tag unused tag parameter that distinguishes this postfix form from the prefix
   * `operator--()`
   * @return a copy of `*this`, before the decrement
   */
  constexpr safe_int operator--(int tag)
  {
    auto tmp = *this;
    --(*this);
    return tmp;
  }

  // ==========================================================================
  // Compound assignment operators (+=, -=, *=, /=, %=)
  // Only accept safe_int<T,EP> rhs — no implicit scalar conversions to preserve
  // explicit overflow semantics. Use binary operators for mixed-type operations.
  // ==========================================================================

  // -- Compound assignment --
  /**
   * @brief Adds @p rhs to the wrapped value, reacting to `ErrorPolicy` on overflow.
   * @param rhs the value to add
   * @return a reference to `*this`, after the addition
   */
  constexpr safe_int& operator+=(safe_int rhs)
  {
    if (detail::add_overflows(value_, rhs.value_)) handle_overflow("safe_int: addition overflow");
    value_ += rhs.value_;
    return *this;
  }

  /**
   * @brief Subtracts @p rhs from the wrapped value, reacting to `ErrorPolicy` on overflow.
   * @param rhs the value to subtract
   * @return a reference to `*this`, after the subtraction
   */
  constexpr safe_int& operator-=(safe_int rhs)
  {
    if (detail::sub_overflows(value_, rhs.value_)) handle_overflow("safe_int: subtraction overflow");
    value_ -= rhs.value_;
    return *this;
  }

  /**
   * @brief Multiplies the wrapped value by @p rhs, reacting to `ErrorPolicy` on overflow.
   * @param rhs the value to multiply by
   * @return a reference to `*this`, after the multiplication
   */
  constexpr safe_int& operator*=(safe_int rhs)
  {
    if (detail::mul_overflows(value_, rhs.value_)) handle_overflow("safe_int: multiplication overflow");
    value_ *= rhs.value_;
    return *this;
  }

  /**
   * @brief Divides the wrapped value by @p rhs, reacting to `ErrorPolicy` on overflow or division by zero.
   * @param rhs the value to divide by
   * @return a reference to `*this`, after the division
   */
  constexpr safe_int& operator/=(safe_int rhs)
  {
    if (detail::div_overflows(value_, rhs.value_)) handle_overflow("safe_int: division overflow");
    value_ /= rhs.value_;
    return *this;
  }

  /**
   * @brief Replaces the wrapped value with its remainder after division by @p rhs, reacting to
   * `ErrorPolicy` on division by zero.
   * @param rhs the value to divide by
   * @return a reference to `*this`, after the modulo operation
   */
  constexpr safe_int& operator%=(safe_int rhs)
  {
    if (rhs.value_ == T{0}) handle_overflow("safe_int: modulo by zero");
    value_ %= rhs.value_;
    return *this;
  }

  // ========================================================================
  // Binary operators
  // ========================================================================

  // ========================================================================
  // HOMOGENEOUS (safe_int × safe_int, same T and EP)
  //
  // Models integral promotion: sub-int types (char, short) promote to int.
  // ========================================================================

  /**
   * @brief Computes the sum of @p lhs and @p rhs.
   * @param lhs the first value to add
   * @param rhs the second value to add
   * @return the sum of `lhs` and `rhs`; triggers `ErrorPolicy::on_overflow` on overflow
   */
  [[nodiscard]] friend constexpr auto operator+(safe_int lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<T, T>, ErrorPolicy>
  {
    using R = detail::integral_op_result_t<T, T>;
    if (detail::add_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs.value_)))
      ErrorPolicy::on_overflow("safe_int: addition overflow");
    return lhs.value_ + rhs.value_;
  }

  /**
   * @brief Computes the difference of @p lhs and @p rhs.
   * @param lhs the value to subtract from
   * @param rhs the value to subtract
   * @return the result of `lhs - rhs`; triggers `ErrorPolicy::on_overflow` on overflow
   */
  [[nodiscard]] friend constexpr auto operator-(safe_int lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<T, T>, ErrorPolicy>
  {
    using R = detail::integral_op_result_t<T, T>;
    if (detail::sub_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs.value_)))
      ErrorPolicy::on_overflow("safe_int: subtraction overflow");
    return lhs.value_ - rhs.value_;
  }

  /**
   * @brief Computes the product of @p lhs and @p rhs.
   * @param lhs the first value to multiply
   * @param rhs the second value to multiply
   * @return the product of `lhs` and `rhs`; triggers `ErrorPolicy::on_overflow` on overflow
   */
  [[nodiscard]] friend constexpr auto operator*(safe_int lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<T, T>, ErrorPolicy>
  {
    using R = detail::integral_op_result_t<T, T>;
    if (detail::mul_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs.value_)))
      ErrorPolicy::on_overflow("safe_int: multiplication overflow");
    return lhs.value_ * rhs.value_;
  }

  /**
   * @brief Computes the quotient of dividing @p lhs by @p rhs.
   * @param lhs the dividend
   * @param rhs the divisor
   * @return the result of `lhs / rhs`; triggers `ErrorPolicy::on_overflow` on overflow or division by zero
   */
  [[nodiscard]] friend constexpr auto operator/(safe_int lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<T, T>, ErrorPolicy>
  {
    using R = detail::integral_op_result_t<T, T>;
    if (detail::div_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs.value_)))
      ErrorPolicy::on_overflow("safe_int: division overflow");
    return lhs.value_ / rhs.value_;
  }

  /**
   * @brief Computes the remainder of dividing @p lhs by @p rhs.
   * @param lhs the dividend
   * @param rhs the divisor
   * @return the remainder of `lhs / rhs`; triggers `ErrorPolicy::on_overflow` on modulo by zero
   */
  [[nodiscard]] friend constexpr auto operator%(safe_int lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<T, T>, ErrorPolicy>
  {
    if (rhs.value_ == T{0}) ErrorPolicy::on_overflow("safe_int: modulo by zero");
    return lhs.value_ % rhs.value_;
  }

  /**
   * @brief Checks whether two `safe_int` values wrap equal underlying values.
   * @param lhs the first value to compare
   * @param rhs the second value to compare
   * @return `true` if the wrapped values are equal
   */
  [[nodiscard]] friend constexpr bool operator==(safe_int lhs, safe_int rhs) noexcept
  {
    return lhs.value_ == rhs.value_;
  }

  /**
   * @brief Compares two `safe_int` values by their wrapped underlying values.
   * @param lhs the first value to compare
   * @param rhs the second value to compare
   * @return the ordering of the wrapped values
   */
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(safe_int lhs, safe_int rhs) noexcept
  {
    return lhs.value_ <=> rhs.value_;
  }

  // ========================================================================
  // INTEGRAL SCALAR: safe_int × integral scalar (wrapper preserved, overflow-checked).
  //
  // Handles heterogeneous integral cases that cannot go through the implicit
  // converting constructor — e.g. safe_int<unsigned long> * __int128 where
  // __int128 is wider than unsigned long (explicit-only narrowing ctor).
  // Same-type cases (e.g. safe_int<int> + int) also resolve here (exact match
  // beats the homogeneous operator's implicit-conversion path).
  // ========================================================================

  /**
   * @brief Adds an integral scalar to a `safe_int`, checking for overflow.
   * @tparam U the integral scalar type
   * @param lhs the `safe_int` operand
   * @param rhs the integral scalar to add
   * @return the sum, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on overflow
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator+(safe_int lhs, U rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    using R = decltype(lhs.value_ + rhs);
    if (detail::add_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs)))
      ErrorPolicy::on_overflow("safe_int: addition overflow");
    return lhs.value_ + rhs;
  }

  /**
   * @brief Adds a `safe_int` to an integral scalar, checking for overflow.
   * @tparam U the integral scalar type
   * @param lhs the integral scalar operand
   * @param rhs the `safe_int` to add
   * @return the sum, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on overflow
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<U, T>)
  [[nodiscard]] friend constexpr auto operator+(U lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    using R = decltype(lhs + rhs.value_);
    if (detail::add_overflows<R>(static_cast<R>(lhs), static_cast<R>(rhs.value_)))
      ErrorPolicy::on_overflow("safe_int: addition overflow");
    return lhs + rhs.value_;
  }

  /**
   * @brief Subtracts an integral scalar from a `safe_int`, checking for overflow.
   * @tparam U the integral scalar type
   * @param lhs the `safe_int` operand
   * @param rhs the integral scalar to subtract
   * @return the difference, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on overflow
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator-(safe_int lhs, U rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    using R = decltype(lhs.value_ - rhs);
    if (detail::sub_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs)))
      ErrorPolicy::on_overflow("safe_int: subtraction overflow");
    return lhs.value_ - rhs;
  }

  /**
   * @brief Subtracts a `safe_int` from an integral scalar, checking for overflow.
   * @tparam U the integral scalar type
   * @param lhs the integral scalar operand
   * @param rhs the `safe_int` to subtract
   * @return the difference, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on overflow
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<U, T>)
  [[nodiscard]] friend constexpr auto operator-(U lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    using R = decltype(lhs - rhs.value_);
    if (detail::sub_overflows<R>(static_cast<R>(lhs), static_cast<R>(rhs.value_)))
      ErrorPolicy::on_overflow("safe_int: subtraction overflow");
    return lhs - rhs.value_;
  }

  /**
   * @brief Multiplies a `safe_int` by an integral scalar, checking for overflow.
   * @tparam U the integral scalar type
   * @param lhs the `safe_int` operand
   * @param rhs the integral scalar to multiply by
   * @return the product, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on overflow
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator*(safe_int lhs, U rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    using R = decltype(lhs.value_ * rhs);
    if (detail::mul_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs)))
      ErrorPolicy::on_overflow("safe_int: multiplication overflow");
    return lhs.value_ * rhs;
  }

  /**
   * @brief Multiplies an integral scalar by a `safe_int`, checking for overflow.
   * @tparam U the integral scalar type
   * @param lhs the integral scalar operand
   * @param rhs the `safe_int` to multiply by
   * @return the product, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on overflow
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<U, T>)
  [[nodiscard]] friend constexpr auto operator*(U lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    using R = decltype(lhs * rhs.value_);
    if (detail::mul_overflows<R>(static_cast<R>(lhs), static_cast<R>(rhs.value_)))
      ErrorPolicy::on_overflow("safe_int: multiplication overflow");
    return lhs * rhs.value_;
  }

  /**
   * @brief Divides a `safe_int` by an integral scalar, checking for overflow.
   * @tparam U the integral scalar type
   * @param lhs the `safe_int` operand
   * @param rhs the integral scalar to divide by
   * @return the quotient, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on overflow
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator/(safe_int lhs, U rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    using R = decltype(lhs.value_ / rhs);
    if (detail::div_overflows<R>(static_cast<R>(lhs.value_), static_cast<R>(rhs)))
      ErrorPolicy::on_overflow("safe_int: division overflow");
    return lhs.value_ / rhs;
  }

  /**
   * @brief Divides an integral scalar by a `safe_int`, checking for overflow.
   * @tparam U the integral scalar type
   * @param lhs the integral scalar operand
   * @param rhs the `safe_int` to divide by
   * @return the quotient, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on overflow
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<U, T>)
  [[nodiscard]] friend constexpr auto operator/(U lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    using R = decltype(lhs / rhs.value_);
    if (detail::div_overflows<R>(static_cast<R>(lhs), static_cast<R>(rhs.value_)))
      ErrorPolicy::on_overflow("safe_int: division overflow");
    return static_cast<R>(lhs) / static_cast<R>(rhs.value_);
  }

  /**
   * @brief Computes the remainder of dividing a `safe_int` by an integral scalar.
   * @tparam U the integral scalar type
   * @param lhs the `safe_int` operand
   * @param rhs the integral scalar divisor
   * @return the remainder, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on modulo by zero
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<T, U>)
  [[nodiscard]] friend constexpr auto operator%(safe_int lhs, U rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    if (rhs == U{0}) ErrorPolicy::on_overflow("safe_int: modulo by zero");
    return lhs.value_ % rhs;
  }

  /**
   * @brief Computes the remainder of dividing an integral scalar by a `safe_int`.
   * @tparam U the integral scalar type
   * @param lhs the integral scalar operand
   * @param rhs the `safe_int` divisor
   * @return the remainder, as a `safe_int` of the promoted result type; triggers `ErrorPolicy::on_overflow` on modulo by zero
   */
  template<typename U>
    requires(detail::integral<U> && detail::same_sign_v<U, T>)
  [[nodiscard]] friend constexpr auto operator%(U lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    if (rhs.value_ == T{0}) ErrorPolicy::on_overflow("safe_int: modulo by zero");
    return lhs % rhs.value_;
  }

  /**
   * @brief Checks whether a `safe_int` equals an integral scalar, comparing safely across signedness.
   * @tparam U the integral scalar type
   * @param lhs the `safe_int` operand
   * @param rhs the integral scalar to compare against
   * @return `true` if the wrapped value equals @p rhs
   */
  template<typename U>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr bool operator==(safe_int lhs, U rhs)
  {
    return std::cmp_equal(lhs.value_, rhs);
  }

  /**
   * @brief Compares a `safe_int` with an integral scalar, comparing safely across signedness.
   * @tparam U the integral scalar type
   * @param lhs the `safe_int` operand
   * @param rhs the integral scalar to compare against
   * @return the ordering between the wrapped value and @p rhs
   */
  template<typename U>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(safe_int lhs, U rhs)
  {
    if (std::cmp_less(lhs.value_, rhs)) return std::strong_ordering::less;
    if (std::cmp_greater(lhs.value_, rhs)) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
  }

  // ========================================================================
  // FLOATING-POINT SCALAR: safe_int × treat_as_floating_point<U> (float, double,
  //   long double, or any user-defined type with treat_as_floating_point = true)
  //
  // Return type is auto (deduced from body) so that the return type is NOT
  // evaluated during overload-candidate enumeration — this avoids infinite
  // template recursion that would occur with decltype(T op U) in the trailing
  // return when U = safe_int<T> is speculatively substituted.
  // treat_as_floating_point<safe_int<T>> = false ensures such candidates are
  // dropped before the body is instantiated.
  // The trailing requires-expression gates on the actual operations for SFINAE.
  // ========================================================================

  /**
   * @brief Adds a floating-point value to a `safe_int`.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the `safe_int` operand
   * @param rhs the floating-point value to add
   * @return the sum of the wrapped value and @p rhs
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr auto operator+(safe_int lhs, U rhs)
    requires requires { static_cast<U>(lhs.value_) + rhs; }
  {
    return static_cast<U>(lhs.value_) + rhs;
  }

  /**
   * @brief Adds a `safe_int` to a floating-point value.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the floating-point value
   * @param rhs the `safe_int` to add
   * @return the sum of @p lhs and the wrapped value
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr auto operator+(U lhs, safe_int rhs)
    requires requires { lhs + static_cast<U>(rhs.value_); }
  {
    return lhs + static_cast<U>(rhs.value_);
  }

  /**
   * @brief Subtracts a floating-point value from a `safe_int`.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the `safe_int` operand
   * @param rhs the floating-point value to subtract
   * @return the difference between the wrapped value and @p rhs
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr auto operator-(safe_int lhs, U rhs)
    requires requires { static_cast<U>(lhs.value_) - rhs; }
  {
    return static_cast<U>(lhs.value_) - rhs;
  }

  /**
   * @brief Subtracts a `safe_int` from a floating-point value.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the floating-point value
   * @param rhs the `safe_int` to subtract
   * @return the difference between @p lhs and the wrapped value
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr auto operator-(U lhs, safe_int rhs)
    requires requires { lhs - static_cast<U>(rhs.value_); }
  {
    return lhs - static_cast<U>(rhs.value_);
  }

  /**
   * @brief Multiplies a `safe_int` by a floating-point value.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the `safe_int` operand
   * @param rhs the floating-point value to multiply by
   * @return the product of the wrapped value and @p rhs
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr auto operator*(safe_int lhs, U rhs)
    requires requires { static_cast<U>(lhs.value_) * rhs; }
  {
    return static_cast<U>(lhs.value_) * rhs;
  }

  /**
   * @brief Multiplies a floating-point value by a `safe_int`.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the floating-point value
   * @param rhs the `safe_int` to multiply by
   * @return the product of @p lhs and the wrapped value
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr auto operator*(U lhs, safe_int rhs)
    requires requires { lhs * static_cast<U>(rhs.value_); }
  {
    return lhs * static_cast<U>(rhs.value_);
  }

  /**
   * @brief Divides a `safe_int` by a floating-point value.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the `safe_int` operand
   * @param rhs the floating-point value to divide by
   * @return the quotient of the wrapped value and @p rhs
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr auto operator/(safe_int lhs, U rhs)
    requires requires { static_cast<U>(lhs.value_) / rhs; }
  {
    return static_cast<U>(lhs.value_) / rhs;
  }

  /**
   * @brief Divides a floating-point value by a `safe_int`.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the floating-point value
   * @param rhs the `safe_int` to divide by
   * @return the quotient of @p lhs and the wrapped value
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr auto operator/(U lhs, safe_int rhs)
    requires requires { lhs / static_cast<U>(rhs.value_); }
  {
    return lhs / static_cast<U>(rhs.value_);
  }

  /**
   * @brief Checks whether a `safe_int` equals a floating-point value.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the `safe_int` operand
   * @param rhs the floating-point value to compare against
   * @return `true` if the wrapped value equals @p rhs
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr bool operator==(safe_int lhs, U rhs)
  {
    return lhs.value_ == rhs;
  }

  /**
   * @brief Compares a `safe_int` with a floating-point value.
   * @tparam U a floating-point-like type (`treat_as_floating_point<U>` is `true`)
   * @param lhs the `safe_int` operand
   * @param rhs the floating-point value to compare against
   * @return the ordering between the wrapped value and @p rhs
   */
  template<typename U>
    requires treat_as_floating_point<U>
  [[nodiscard]] friend constexpr auto operator<=>(safe_int lhs, U rhs)
  {
    return lhs.value_ <=> rhs;
  }

  // ========================================================================
  // CONSTRAINED
  //
  // constrained<U,CP> has no implicit conversion to/from safe_int, so these
  // must be explicit operators.
  // ========================================================================

  // ========================================================================
  // integral → safe_int wins
  // ========================================================================

  /**
   * @brief Adds a constrained value to a safe_int
   * @param lhs the constrained value to add
   * @param rhs the safe_int to add
   * @return the wrapped sum of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator+(constrained<U, CP> lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    return lhs.value() + rhs;
  }

  /**
   * @brief Adds a safe_int to a constrained value
   * @param lhs the safe_int to add
   * @param rhs the constrained value to add
   * @return the wrapped sum of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator+(safe_int lhs, constrained<U, CP> rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    return lhs + rhs.value();
  }

  /**
   * @brief Subtracts a safe_int from a constrained value
   * @param lhs the constrained value to subtract from
   * @param rhs the safe_int to subtract
   * @return the wrapped difference of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator-(constrained<U, CP> lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    return lhs.value() - rhs;
  }

  /**
   * @brief Subtracts a constrained value from a safe_int
   * @param lhs the safe_int to subtract from
   * @param rhs the constrained value to subtract
   * @return the wrapped difference of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator-(safe_int lhs, constrained<U, CP> rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    return lhs - rhs.value();
  }

  /**
   * @brief Multiplies a constrained value by a safe_int
   * @param lhs the constrained value to multiply
   * @param rhs the safe_int to multiply by
   * @return the wrapped product of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator*(constrained<U, CP> lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    return lhs.value() * rhs;
  }

  /**
   * @brief Multiplies a safe_int by a constrained value
   * @param lhs the safe_int to multiply
   * @param rhs the constrained value to multiply by
   * @return the wrapped product of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator*(safe_int lhs, constrained<U, CP> rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    return lhs * rhs.value();
  }

  /**
   * @brief Divides a constrained value by a safe_int
   * @param lhs the constrained value to divide
   * @param rhs the safe_int to divide by
   * @return the wrapped quotient of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator/(constrained<U, CP> lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    return lhs.value() / rhs;
  }

  /**
   * @brief Divides a safe_int by a constrained value
   * @param lhs the safe_int to divide
   * @param rhs the constrained value to divide by
   * @return the wrapped quotient of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator/(safe_int lhs, constrained<U, CP> rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    return lhs / rhs.value();
  }

  /**
   * @brief Computes the remainder of dividing a constrained value by a safe_int
   * @param lhs the constrained value to divide
   * @param rhs the safe_int to divide by
   * @return the wrapped remainder of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator%(constrained<U, CP> lhs, safe_int rhs)
    -> safe_int<detail::integral_op_result_t<U, T>, ErrorPolicy>
  {
    return lhs.value() % rhs;
  }

  /**
   * @brief Computes the remainder of dividing a safe_int by a constrained value
   * @param lhs the safe_int to divide
   * @param rhs the constrained value to divide by
   * @return the wrapped remainder of the underlying values
   */
  template<typename U, typename CP>
    requires detail::integral<U>
  [[nodiscard]] friend constexpr auto operator%(safe_int lhs, constrained<U, CP> rhs)
    -> safe_int<detail::integral_op_result_t<T, U>, ErrorPolicy>
  {
    return lhs % rhs.value();
  }

  // ========================================================================
  // non-integral → constrained wins
  // ========================================================================

  /**
   * @brief Adds a safe_int to a non-integral constrained value
   * @param lhs the constrained value to add
   * @param rhs the safe_int to add
   * @return the wrapped sum of the underlying values
   */
  template<typename U, typename CP>
    requires(!std::integral<U>)
  [[nodiscard]] friend constexpr auto operator+(const constrained<U, CP>& lhs, safe_int rhs)
    -> constrained<decltype(std::declval<U>() + std::declval<T>()), CP>
  {
    return lhs + rhs.value_;
  }

  /**
   * @brief Adds a non-integral constrained value to a safe_int
   * @param lhs the safe_int to add
   * @param rhs the constrained value to add
   * @return the wrapped sum of the underlying values
   */
  template<typename U, typename CP>
    requires(!std::integral<U>)
  [[nodiscard]] friend constexpr auto operator+(safe_int lhs, const constrained<U, CP>& rhs)
    -> constrained<decltype(std::declval<T>() + std::declval<U>()), CP>
  {
    return lhs.value_ + rhs;
  }

  /**
   * @brief Subtracts a safe_int from a non-integral constrained value
   * @param lhs the constrained value to subtract from
   * @param rhs the safe_int to subtract
   * @return the wrapped difference of the underlying values
   */
  template<typename U, typename CP>
    requires(!std::integral<U>)
  [[nodiscard]] friend constexpr auto operator-(const constrained<U, CP>& lhs, safe_int rhs)
    -> constrained<decltype(std::declval<U>() - std::declval<T>()), CP>
  {
    return lhs - rhs.value_;
  }

  /**
   * @brief Subtracts a non-integral constrained value from a safe_int
   * @param lhs the safe_int to subtract from
   * @param rhs the constrained value to subtract
   * @return the wrapped difference of the underlying values
   */
  template<typename U, typename CP>
    requires(!std::integral<U>)
  [[nodiscard]] friend constexpr auto operator-(safe_int lhs, const constrained<U, CP>& rhs)
    -> constrained<decltype(std::declval<T>() - std::declval<U>()), CP>
  {
    return lhs.value_ - rhs;
  }

  /**
   * @brief Multiplies a non-integral constrained value by a safe_int
   * @param lhs the constrained value to multiply
   * @param rhs the safe_int to multiply by
   * @return the wrapped product of the underlying values
   */
  template<typename U, typename CP>
    requires(!std::integral<U>)
  [[nodiscard]] friend constexpr auto operator*(const constrained<U, CP>& lhs, safe_int rhs)
    -> constrained<decltype(std::declval<U>() * std::declval<T>()), CP>
  {
    return lhs * rhs.value_;
  }

  /**
   * @brief Multiplies a safe_int by a non-integral constrained value
   * @param lhs the safe_int to multiply
   * @param rhs the constrained value to multiply by
   * @return the wrapped product of the underlying values
   */
  template<typename U, typename CP>
    requires(!std::integral<U>)
  [[nodiscard]] friend constexpr auto operator*(safe_int lhs, const constrained<U, CP>& rhs)
    -> constrained<decltype(std::declval<T>() * std::declval<U>()), CP>
  {
    return lhs.value_ * rhs;
  }

  /**
   * @brief Divides a non-integral constrained value by a safe_int
   * @param lhs the constrained value to divide
   * @param rhs the safe_int to divide by
   * @return the wrapped quotient of the underlying values
   */
  template<typename U, typename CP>
    requires(!std::integral<U>)
  [[nodiscard]] friend constexpr auto operator/(const constrained<U, CP>& lhs, safe_int rhs)
    -> constrained<decltype(std::declval<U>() / std::declval<T>()), CP>
  {
    return lhs / rhs.value_;
  }

  /**
   * @brief Divides a safe_int by a non-integral constrained value
   * @param lhs the safe_int to divide
   * @param rhs the constrained value to divide by
   * @return the wrapped quotient of the underlying values
   */
  template<typename U, typename CP>
    requires(!std::integral<U>)
  [[nodiscard]] friend constexpr auto operator/(safe_int lhs, const constrained<U, CP>& rhs)
    -> constrained<decltype(std::declval<T>() / std::declval<U>()), CP>
  {
    return lhs.value_ / rhs;
  }

  /**
   * @brief Checks whether a safe_int and a constrained value are equal
   * @param lhs the safe_int to compare
   * @param rhs the constrained value to compare against
   * @return `true` if the underlying values are equal
   */
  template<typename U, typename CP>
  [[nodiscard]] friend constexpr bool operator==(safe_int lhs, const constrained<U, CP>& rhs)
  {
    return lhs.value_ == rhs.value();
  }

  /**
   * @brief Compares a safe_int with a constrained value
   * @param lhs the safe_int to compare
   * @param rhs the constrained value to compare against
   * @return the ordering of the underlying values
   */
  template<typename U, typename CP>
  [[nodiscard]] friend constexpr auto operator<=>(safe_int lhs, const constrained<U, CP>& rhs)
  {
    return lhs.value_ <=> rhs.value();
  }

#if MP_UNITS_HOSTED
  /**
   * @brief Streams a safe_int's wrapped value to an output stream
   * @param os the output stream to write to
   * @param v the safe_int to stream
   * @return a reference to @p os
   */
  template<typename CharT, typename Traits>
  friend std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, safe_int v)
  {
    if constexpr (sizeof(T) == 1)
      return os << static_cast<int>(v.value_);  // promote char-width types to int for streaming
    else
      return os << v.value_;
  }
#endif
};

/// @brief Deduces `safe_int<T>` (with the default `ErrorPolicy`) from an integral value.
template<detail::integral T>
safe_int(T) -> safe_int<T>;

// ============================================================================
// Convenience type aliases
// ============================================================================

/// @brief `safe_int` wrapping `std::int8_t`.
MP_UNITS_EXPORT using safe_i8 = safe_int<std::int8_t>;
/// @brief `safe_int` wrapping `std::int16_t`.
MP_UNITS_EXPORT using safe_i16 = safe_int<std::int16_t>;
/// @brief `safe_int` wrapping `std::int32_t`.
MP_UNITS_EXPORT using safe_i32 = safe_int<std::int32_t>;
/// @brief `safe_int` wrapping `std::int64_t`.
MP_UNITS_EXPORT using safe_i64 = safe_int<std::int64_t>;

/// @brief `safe_int` wrapping `std::uint8_t`.
MP_UNITS_EXPORT using safe_u8 = safe_int<std::uint8_t>;
/// @brief `safe_int` wrapping `std::uint16_t`.
MP_UNITS_EXPORT using safe_u16 = safe_int<std::uint16_t>;
/// @brief `safe_int` wrapping `std::uint32_t`.
MP_UNITS_EXPORT using safe_u32 = safe_int<std::uint32_t>;
/// @brief `safe_int` wrapping `std::uint64_t`.
MP_UNITS_EXPORT using safe_u64 = safe_int<std::uint64_t>;

}  // namespace mp_units::utility

namespace mp_units {

template<typename T, typename ErrorPolicy>
struct constraint_violation_handler<utility::safe_int<T, ErrorPolicy>> {
  static constexpr void on_violation(std::string_view msg) { ErrorPolicy::on_constraint_violation(msg); }
};

}  // namespace mp_units

// std::numeric_limits specialization — required for representation_values<safe_int<T>>
namespace std {

template<typename T, typename ErrorPolicy>
class numeric_limits<mp_units::utility::safe_int<T, ErrorPolicy>> : public numeric_limits<T> {
  using S = mp_units::utility::safe_int<T, ErrorPolicy>;
public:
  [[nodiscard]] static constexpr S lowest() noexcept { return numeric_limits<T>::lowest(); }
  [[nodiscard]] static constexpr S min() noexcept { return numeric_limits<T>::min(); }
  [[nodiscard]] static constexpr S max() noexcept { return numeric_limits<T>::max(); }
  [[nodiscard]] static constexpr S epsilon() noexcept { return numeric_limits<T>::epsilon(); }
  [[nodiscard]] static constexpr S round_error() noexcept { return numeric_limits<T>::round_error(); }
  [[nodiscard]] static constexpr S infinity() noexcept { return numeric_limits<T>::infinity(); }
  [[nodiscard]] static constexpr S quiet_NaN() noexcept { return numeric_limits<T>::quiet_NaN(); }
  [[nodiscard]] static constexpr S signaling_NaN() noexcept { return numeric_limits<T>::signaling_NaN(); }
  [[nodiscard]] static constexpr S denorm_min() noexcept { return numeric_limits<T>::denorm_min(); }
};

}  // namespace std

#if MP_UNITS_HOSTED
template<typename T, typename ErrorPolicy, typename Char>
struct MP_UNITS_STD_FMT::formatter<mp_units::utility::safe_int<T, ErrorPolicy>, Char> : formatter<T, Char> {
  template<typename FormatContext>
  auto format(const mp_units::utility::safe_int<T, ErrorPolicy>& v, FormatContext& ctx) const
  {
    return formatter<T, Char>::format(v.value(), ctx);
  }
};
#endif
