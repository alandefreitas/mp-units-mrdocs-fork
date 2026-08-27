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

// IWYU pragma: private, include <mp-units/framework.h>
#include <mp-units/bits/module_macros.h>
#include <mp-units/compat_macros.h>
#include <mp-units/framework/quantity_concepts.h>
#include <mp-units/framework/quantity_point_concepts.h>
#include <mp-units/framework/quantity_spec.h>
#include <mp-units/framework/reference_concepts.h>
#include <mp-units/framework/representation_concepts.h>

#ifndef MP_UNITS_IN_MODULE_INTERFACE
#ifdef MP_UNITS_IMPORT_STD
import std;
#else
#include <cstdint>
#endif
#endif

namespace mp_units {

namespace detail {

template<QuantitySpec auto Q, Unit auto U>
using reference_t = reference<MP_UNITS_REMOVE_CONST(decltype(Q)), MP_UNITS_REMOVE_CONST(decltype(U))>;

}  // namespace detail

MP_UNITS_EXPORT_BEGIN

/**
 * @brief Quantity reference type
 *
 * Quantity reference describes all the properties of a quantity besides its
 * representation type.
 *
 * In most cases this class template is not explicitly instantiated by the user.
 * It is implicitly instantiated by the library's framework while binding a quantity
 * specification with a compatible unit.
 *
 * @code{.cpp}
 * Reference auto kmph = isq::speed[km / h];
 * QuantityOf<isq::speed[km / h]> auto speed = 90 * kmph;
 * @endcode
 *
 * The following syntaxes are not allowed:
 * `2 / kmph`, `kmph * 3`, `kmph / 4`, `70 * isq::length[km] / isq:time[h]`.
 */
template<QuantitySpec Q, Unit U>
struct reference {
  /**
   * @brief Returns the quantity specification of a reference
   *
   * @param r the reference to query
   * @return the quantity specification `Q`
   */
  [[nodiscard]] friend consteval QuantitySpec auto get_quantity_spec(reference r) { return Q{}; }

  /**
   * @brief Returns the quantity specification of a reference whose `Q` is marked `final`
   *
   * @param r the reference to query
   * @return the quantity specification `Q`
   */
  [[deprecated("2.6.0: `Q` definition should not be marked `final`")]] [[nodiscard]] friend consteval QuantitySpec auto
  get_quantity_spec(reference r)
    requires std::is_final_v<Q>
  {
    return Q{};
  }

  /**
   * @brief Returns the unit of a reference
   *
   * @param r the reference to query
   * @return the unit `U`
   */
  [[nodiscard]] friend consteval Unit auto get_unit(reference r) { return U{}; }

  /**
   * @brief Checks whether two references are equal
   *
   * @param r1 the first reference to compare
   * @param r2 the other reference to compare against
   * @return `true` if both the quantity specifications and the units are equal
   */
  template<typename Q2, typename U2>
  [[nodiscard]] friend consteval bool operator==(reference r1, reference<Q2, U2> r2)
  {
    return Q{} == Q2{} && U{} == U2{};
  }

  /**
   * @brief Checks whether a reference is equal to a unit
   *
   * @param r the reference to compare
   * @param u2 the unit to compare against
   * @return `true` if the reference's quantity specification and unit match those implied by @p u2
   */
  template<Unit U2>
  [[nodiscard]] friend consteval bool operator==(reference r, U2 u2)
  {
    return Q{} == get_quantity_spec(u2) && U{} == u2;
  }

  /**
   * @brief Multiplies two references
   *
   * @param r1 the reference to multiply
   * @param r2 the reference to multiply by
   * @return a reference resulting from multiplying the quantity specifications and the units
   */
  template<typename Q2, typename U2>
  [[nodiscard]] friend consteval detail::reference_t<Q{} * Q2{}, U{} * U2{}> operator*(reference r1,
                                                                                        reference<Q2, U2> r2)
  {
    return {};
  }

  /**
   * @brief Multiplies a reference by a unit
   *
   * @param r the reference to multiply
   * @param u2 the unit to multiply by
   * @return a reference resulting from multiplying the quantity specification and the unit
   */
  template<Unit U2>
  [[nodiscard]] friend consteval detail::reference_t<Q{} * get_quantity_spec(U2{}), U{} * U2{}> operator*(reference r,
                                                                                                            U2 u2)
  {
    return {};
  }

  /**
   * @brief Multiplies a unit by a reference
   *
   * @param u1 the unit to multiply
   * @param r the reference to multiply by
   * @return a reference resulting from multiplying the quantity specification and the unit
   */
  template<Unit U1>
  [[nodiscard]] friend consteval detail::reference_t<get_quantity_spec(U1{}) * Q{}, U1{} * U{}> operator*(U1 u1,
                                                                                                            reference r)
  {
    return {};
  }

  /**
   * @brief Divides a reference by another reference
   *
   * @param r1 the reference to divide
   * @param r2 the reference to divide by
   * @return a reference resulting from dividing the quantity specifications and the units
   */
  template<typename Q2, typename U2>
  [[nodiscard]] friend consteval detail::reference_t<Q{} / Q2{}, U{} / U2{}> operator/(reference r1,
                                                                                        reference<Q2, U2> r2)
  {
    return {};
  }

  /**
   * @brief Divides a reference by a unit
   *
   * @param r the reference to divide
   * @param u2 the unit to divide by
   * @return a reference resulting from dividing the quantity specification and the unit
   */
  template<Unit U2>
  [[nodiscard]] friend consteval detail::reference_t<Q{} / get_quantity_spec(U2{}), U{} / U2{}> operator/(reference r,
                                                                                                            U2 u2)
  {
    return {};
  }

  /**
   * @brief Divides a unit by a reference
   *
   * @param u1 the unit to divide
   * @param r the reference to divide by
   * @return a reference resulting from dividing the quantity specification and the unit
   */
  template<Unit U1>
  [[nodiscard]] friend consteval detail::reference_t<get_quantity_spec(U1{}) / Q{}, U1{} / U{}> operator/(U1 u1,
                                                                                                            reference r)
  {
    return {};
  }

  /**
   * @brief Computes the inverse of a reference
   *
   * @param r reference being inverted
   * @return the result of the computation
   */
  [[nodiscard]] friend consteval detail::reference_t<inverse(Q{}), inverse(U{})> inverse(reference r) { return {}; }

  /**
   * @brief Computes the value of a reference raised to the `Num/Den` power
   *
   * @tparam Num Exponent numerator
   * @tparam Den Exponent denominator
   * @param r Reference being the base of the operation
   *
   * @return The result of computation
   */
  template<std::intmax_t Num, std::intmax_t Den = 1>
    requires(Den != 0)
  [[nodiscard]] friend consteval detail::reference_t<pow<Num, Den>(Q{}), pow<Num, Den>(U{})> pow(reference r)
  {
    return {};
  }

  /**
   * @brief Computes the square root of a reference
   *
   * @param r Reference being the base of the operation
   *
   * @return The result of computation
   */
  [[nodiscard]] friend consteval detail::reference_t<sqrt(Q{}), sqrt(U{})> sqrt(reference r) { return {}; }

  /**
   * @brief Computes the cubic root of a reference
   *
   * @param r Reference being the base of the operation
   *
   * @return The result of computation
   */
  [[nodiscard]] friend consteval detail::reference_t<cbrt(Q{}), cbrt(U{})> cbrt(reference r) { return {}; }
};


/**
 * @brief Constructs a quantity from a representation value and a reference
 *
 * @param lhs representation value to become the quantity's numerical value
 * @param r reference defining the quantity specification and unit
 * @return quantity of reference `r` holding the value of `lhs`
 */
template<typename FwdRep, Reference R, RepresentationOf<get_quantity_spec(R{})> Rep = std::remove_cvref_t<FwdRep>>
  requires(!detail::OffsetUnit<decltype(get_unit(R{}))>)
[[nodiscard]] constexpr quantity<R{}, Rep> operator*(FwdRep && lhs, R r)
{
  return quantity{std::forward<FwdRep>(lhs), r};
}

/**
 * @brief Constructs a quantity from a representation value and the inverse of a reference
 *
 * @param lhs representation value to become the quantity's numerical value
 * @param r reference whose inverse defines the resulting quantity's specification and unit
 * @return quantity of the inverse of reference `r` holding the value of `lhs`
 */
template<typename FwdRep, Reference R, RepresentationOf<get_quantity_spec(R{})> Rep = std::remove_cvref_t<FwdRep>>
  requires(!detail::OffsetUnit<decltype(get_unit(R{}))>)
[[nodiscard]] constexpr Quantity auto operator/(FwdRep && lhs, R r)
{
  return quantity{std::forward<FwdRep>(lhs), inverse(r)};
}

/**
 * @brief Deprecated constructor of a quantity from a representation value and an offset-unit reference
 *
 * @param lhs representation value to become the quantity's numerical value
 * @param r offset-unit reference defining the quantity specification and unit
 * @return quantity of reference `r` holding the value of `lhs`
 * @deprecated Use the `delta` or `point` helpers instead
 */
template<typename FwdRep, Reference R, RepresentationOf<get_quantity_spec(R{})> Rep = std::remove_cvref_t<FwdRep>>
  requires detail::OffsetUnit<decltype(get_unit(R{}))>
[[deprecated(
  "2.3.0: References using offset units (e.g., temperatures) should be constructed with the `delta` or `point` "
  "helpers")]] constexpr auto operator*(FwdRep && lhs, R r)
{
  return quantity{std::forward<FwdRep>(lhs), r};
}

/**
 * @brief Deprecated constructor of a quantity from a representation value and the inverse of an offset-unit reference
 *
 * @param lhs representation value to become the quantity's numerical value
 * @param r offset-unit reference whose inverse defines the resulting quantity's specification and unit
 * @return quantity of the inverse of reference `r` holding the value of `lhs`
 * @deprecated Use the `delta` or `point` helpers instead
 */
template<typename FwdRep, Reference R, RepresentationOf<get_quantity_spec(R{})> Rep = std::remove_cvref_t<FwdRep>>
  requires detail::OffsetUnit<decltype(get_unit(R{}))>
[[deprecated(
  "2.3.0: References using offset units (e.g., temperatures) should be constructed with the `delta` or `point` "
  "helpers")]] constexpr auto operator/(FwdRep && lhs, R r)
{
  return quantity{std::forward<FwdRep>(lhs), inverse(r)};
}

/**
 * @brief Deleted overload rejecting `reference * representation` in favor of `representation * reference`
 *
 * @param r reference operand (unused)
 * @param rep representation operand (unused)
 */
template<Reference R, typename Rep>
  requires RepresentationOf<std::remove_cvref_t<Rep>, get_quantity_spec(R{})>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
constexpr auto operator*(R r, Rep&& rep)
#if __cpp_deleted_function
  = delete ("To create a `quantity` use `Rep * R`");
#else
  = delete;
#endif

/**
 * @brief Deleted overload rejecting `reference / representation`, which has no defined meaning
 *
 * @param r reference operand (unused)
 * @param rep representation operand (unused)
 */
template<Reference R, typename Rep>
  requires RepresentationOf<std::remove_cvref_t<Rep>, get_quantity_spec(R{})>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
constexpr auto operator/(R r, Rep&& rep)
#if __cpp_deleted_function
  = delete ("To create a `quantity` use `Rep / R`");
#else
  = delete;
#endif

/**
 * @brief Rebinds a quantity to a reference obtained by multiplying its own reference by `R`
 *
 * @param q quantity being rebound
 * @param r reference to multiply the quantity's reference by
 * @return quantity holding the same numerical value as `q`, with reference `Q::reference * R{}`
 */
template<typename FwdQ, Reference R, Quantity Q = std::remove_cvref_t<FwdQ>>
[[nodiscard]] constexpr Quantity auto operator*(FwdQ&& q, R r)
{
  return quantity{std::forward<FwdQ>(q).numerical_value_is_an_implementation_detail_, Q::reference * R{}};
}

/**
 * @brief Rebinds a quantity to a reference obtained by dividing its own reference by `R`
 *
 * @param q quantity being rebound
 * @param r reference to divide the quantity's reference by
 * @return quantity holding the same numerical value as `q`, with reference `Q::reference / R{}`
 */
template<typename FwdQ, Reference R, Quantity Q = std::remove_cvref_t<FwdQ>>
[[nodiscard]] constexpr Quantity auto operator/(FwdQ&& q, R r)
{
  return quantity{std::forward<FwdQ>(q).numerical_value_is_an_implementation_detail_, Q::reference / R{}};
}

/**
 * @brief Deleted overload rejecting `reference * quantity` in favor of `quantity * reference`
 *
 * @param r reference operand (unused)
 * @param q quantity operand (unused)
 */
template<Reference R, typename Q>
  requires Quantity<std::remove_cvref_t<Q>>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
constexpr auto operator*(R r, Q&& q) = delete;

/**
 * @brief Deleted overload rejecting `reference / quantity`, which has no defined meaning
 *
 * @param r reference operand (unused)
 * @param q quantity operand (unused)
 */
template<Reference R, typename Q>
  requires Quantity<std::remove_cvref_t<Q>>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
constexpr auto operator/(R r, Q&& q) = delete;

[[nodiscard]] consteval Unit auto get_common_unit(Unit auto u);

template<Unit U1, detail::UnitConvertibleTo<U1{}> U2>
[[nodiscard]] consteval Unit auto get_common_unit(U1 u1, U2 u2);

/**
 * @brief Returns the common unit of two or more units usable as a reference
 *
 * @param u1 first unit
 * @param u2 second unit
 * @param rest any remaining units
 * @return the common unit of all the arguments
 */
[[nodiscard]] consteval Unit auto get_common_reference(Unit auto u1, Unit auto u2, Unit auto... rest)
  requires requires {
    mp_units::get_common_quantity_spec(get_quantity_spec(u1), get_quantity_spec(u2), get_quantity_spec(rest)...);
    mp_units::get_common_unit(u1, u2, rest...);
  }
{
  return mp_units::get_common_unit(u1, u2, rest...);
}

template<Reference R1, Reference R2, Reference... Rest>
[[nodiscard]] consteval Reference auto get_common_reference(R1 r1, R2 r2, Rest... rest)
  requires(!(Unit<R1> && Unit<R2> && (... && Unit<Rest>))) && requires {
    mp_units::get_common_quantity_spec(get_quantity_spec(r1), get_quantity_spec(r2), get_quantity_spec(rest)...);
    mp_units::get_common_unit(get_unit(r1), get_unit(r2), get_unit(rest)...);
  }
{
  return detail::reference_t<mp_units::get_common_quantity_spec(get_quantity_spec(R1{}), get_quantity_spec(R2{}),
                                                                get_quantity_spec(rest)...),
                             mp_units::get_common_unit(get_unit(R1{}), get_unit(R2{}), get_unit(rest)...)>{};
}

MP_UNITS_EXPORT_END

namespace detail {

template<Unit auto To, Unit From>
[[nodiscard]] consteval MP_UNITS_REMOVE_CONST(decltype(To)) clone_reference_with(From)
{
  return {};
}

template<Unit auto To, QuantitySpec QS, Unit U>
[[nodiscard]] consteval reference<QS, MP_UNITS_REMOVE_CONST(decltype(To))> clone_reference_with(reference<QS, U>)
{
  return {};
}

}  // namespace detail

}  // namespace mp_units
