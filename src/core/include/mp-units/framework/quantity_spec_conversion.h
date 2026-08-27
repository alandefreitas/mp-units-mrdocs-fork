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

// IWYU pragma: private, include <mp-units/framework/quantity_spec.h>
#include <mp-units/bits/module_macros.h>
#include <mp-units/bits/quantity_spec_conversion_impl.h>  // IWYU pragma: keep
#include <mp-units/framework/quantity_spec_concepts.h>
#include <mp-units/framework/quantity_spec_definitions.h>

namespace mp_units {

MP_UNITS_EXPORT_BEGIN

template<QuantitySpec From, QuantitySpec To>
[[nodiscard]] consteval bool implicitly_convertible(From from, To to)
{
  return detail::convertible(from, to) == detail::specs_convertible_result::yes;
}

template<QuantitySpec From, QuantitySpec To>
[[nodiscard]] consteval bool explicitly_convertible(From from, To to)
{
  return detail::convertible(from, to) >= detail::specs_convertible_result::explicit_conversion_beyond_kind;
}

/**
 * @brief Checks whether a quantity of quantity spec `From` can be cast to quantity spec `To`
 *
 * `castable` is a weaker requirement than @ref explicitly_convertible and is used to validate quantities
 * belonging to the same kind but with otherwise unrelated equations.
 *
 * @tparam From source quantity specification type
 * @tparam To target quantity specification type
 * @param from source quantity specification value
 * @param to target quantity specification value
 * @return `true` if `from` can be cast to `to`
 */
template<QuantitySpec From, QuantitySpec To>
[[nodiscard]] consteval bool castable(From from, To to)
{
  return detail::convertible(from, to) >= detail::specs_convertible_result::cast;
}

/**
 * @brief Checks whether two quantity specifications are convertible to each other in both directions
 *
 * @tparam QS1 first quantity specification type
 * @tparam QS2 second quantity specification type
 * @param qs1 first quantity specification value
 * @param qs2 second quantity specification value
 * @return `true` if `qs1` is implicitly convertible to `qs2` and `qs2` is implicitly convertible to `qs1`
 */
template<QuantitySpec QS1, QuantitySpec QS2>
[[nodiscard]] consteval bool interconvertible(QS1 qs1, QS2 qs2)
{
  return mp_units::implicitly_convertible(qs1, qs2) && mp_units::implicitly_convertible(qs2, qs1);
}

/**
 * @brief Returns the kind of a quantity specification
 *
 * The kind is obtained by walking up to the root of the quantity's hierarchy tree.
 *
 * @tparam Q quantity specification type
 * @param q quantity specification value
 * @return the quantity kind of the argument
 */
template<QuantitySpec Q>
[[nodiscard]] consteval detail::QuantityKindSpec auto get_kind(Q q)
{
  return kind_of<detail::get_kind_tree_root(Q{})>;
}

/**
 * @brief Checks whether a quantity specification is tagged as non-negative
 *
 * @tparam Q quantity specification type
 * @param q quantity specification value
 * @return `true` if quantities of `Q` can never hold a negative value
 */
template<QuantitySpec Q>
[[nodiscard]] consteval bool is_non_negative(Q q)
{
  return Q::_is_non_negative_;
}

/**
 * @brief Returns the common quantity specification of a single quantity specification
 *
 * @param q a quantity specification value
 * @return `q` unchanged
 */
[[nodiscard]] consteval QuantitySpec auto get_common_quantity_spec(QuantitySpec auto q) { return q; }

/**
 * @brief Returns the common quantity specification of two quantity specifications
 *
 * @tparam Q1 first quantity specification type
 * @tparam Q2 second quantity specification type
 * @param q1 first quantity specification value
 * @param q2 second quantity specification value
 * @return the strongest quantity specification that both arguments are convertible to
 */
template<QuantitySpec Q1, QuantitySpec Q2>
  requires(detail::have_common_quantity_spec(Q1{}, Q2{}))
[[nodiscard]] consteval QuantitySpec auto get_common_quantity_spec(Q1 q1, Q2 q2)
{
  return detail::get_common_quantity_spec_result<Q1, Q2>;
}

/**
 * @brief Returns the common quantity specification of three or more quantity specifications
 *
 * @param q1 first quantity specification value
 * @param q2 second quantity specification value
 * @param q3 third quantity specification value
 * @param rest any remaining quantity specification values
 * @return the common quantity specification obtained by folding all arguments pairwise
 */
[[nodiscard]] consteval QuantitySpec auto get_common_quantity_spec(QuantitySpec auto q1, QuantitySpec auto q2,
                                                                   QuantitySpec auto q3, QuantitySpec auto... rest)
  requires requires { mp_units::get_common_quantity_spec(mp_units::get_common_quantity_spec(q1, q2), q3, rest...); }
{
  return mp_units::get_common_quantity_spec(mp_units::get_common_quantity_spec(q1, q2), q3, rest...);
}

MP_UNITS_EXPORT_END

}  // namespace mp_units
