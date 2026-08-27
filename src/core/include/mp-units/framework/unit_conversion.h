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

// IWYU pragma: private, include <mp-units/framework/unit.h>
#include <mp-units/bits/hacks.h>
#include <mp-units/bits/module_macros.h>
#include <mp-units/bits/type_list.h>
#include <mp-units/bits/unit_conversion_impl.h>  // IWYU pragma: keep
#include <mp-units/framework/symbol_text.h>
#include <mp-units/framework/unit_concepts.h>
#include <mp-units/framework/unit_definitions.h>
#include <mp-units/framework/unit_magnitude.h>

namespace mp_units {

MP_UNITS_EXPORT_BEGIN

[[nodiscard]] MP_UNITS_CONSTEVAL Unit auto inverse(Unit auto u) { return one / u; }

// common dimensionless units
// clang-format off
/// @brief The "%" (percent) unit, equal to 1/100
inline constexpr struct percent final : named_unit<"%", mag_ratio<1, 100> * one> {} percent;  ///< The unique instance of the percent unit.
/// @brief The "‰" (per mille) unit, equal to 1/1000
inline constexpr struct per_mille final : named_unit<symbol_text{u8"‰" /* U+2030 PER MILLE SIGN */, "%o"}, mag_ratio<1, 1000> * one> {} per_mille;  ///< The unique instance of the per mille unit.
/// @brief The "ppm" (parts per million) unit, equal to 1/1,000,000
inline constexpr struct parts_per_million final : named_unit<"ppm", mag_ratio<1, 1'000'000> * one> {} parts_per_million;  ///< The unique instance of the parts-per-million unit.
/// @brief Alias for `parts_per_million`
inline constexpr auto ppm = parts_per_million;
// TODO make it a unit constant rather than a named unit when such support will be provided
/// @brief The mathematical constant π, exposed as a dimensionless named constant
inline constexpr struct pi final : named_constant<symbol_text{u8"π" /* U+03C0 GREEK SMALL LETTER PI */, "pi"}, mag<pi_c> * one> {} pi;  ///< The unique instance of the π constant.
/// @brief Alias for `pi`
inline constexpr auto π /* U+03C0 GREEK SMALL LETTER PI */ = pi;
// clang-format on

/**
 * @brief Returns the common unit of a single unit
 *
 * @param u the unit
 * @return @p u unchanged
 */
[[nodiscard]] consteval Unit auto get_common_unit(Unit auto u) { return u; }

/**
 * @brief Computes the common unit of two convertible units
 *
 * @param u1 first unit
 * @param u2 second unit
 * @return the common unit of @p u1 and @p u2
 */
template<Unit U1, detail::UnitConvertibleTo<U1{}> U2>
[[nodiscard]] consteval Unit auto get_common_unit(U1 u1, U2 u2)
{
  return detail::get_common_unit_impl(u1, u2);
}

/**
 * @brief Computes the common unit of a `common_unit` compound and a single new unit
 *
 * @param cu the `common_unit` compound
 * @param new_unit the new unit to merge into the compound's unit list
 * @return the common unit obtained by merging the new unit into the compound's unit list
 */
template<Unit... Us, detail::UnitConvertibleTo<common_unit<Us...>{}> NewUnit>
[[nodiscard]] consteval Unit auto get_common_unit(common_unit<Us...> cu, NewUnit new_unit)
{
  using type = detail::collapse_common_unit<NewUnit, Us...>;
  if constexpr (detail::type_list_size<type> == 1)
    return detail::type_list_front<type>{};
  else
    return detail::type_list_map<type, common_unit>{};
}

/**
 * @brief Computes the common unit of a single new unit and a `common_unit` compound
 *
 * @param nu the single unit
 * @param cu the `common_unit` compound
 * @return the common unit obtained by merging @p nu into @p cu's unit list
 */
template<Unit... Us, detail::UnitConvertibleTo<common_unit<Us...>{}> NewUnit>
[[nodiscard]] consteval Unit auto get_common_unit(NewUnit nu, common_unit<Us...> cu)
{
  return mp_units::get_common_unit(cu, nu);
}

/**
 * @brief Computes the common unit of two `common_unit` compounds by merging their unit lists
 *
 * @param cu1 first `common_unit` compound
 * @param cu2 second `common_unit` compound
 * @return the common unit obtained by merging the unit lists of @p cu1 and @p cu2
 */
template<Unit Front, Unit... Rest, Unit... Us>
  requires(detail::UnitConvertibleTo<common_unit<Front, Rest...>, common_unit<Us...>{}>)
[[nodiscard]] consteval Unit auto get_common_unit(common_unit<Front, Rest...> cu1, common_unit<Us...> cu2)
{
  if constexpr (sizeof...(Rest) == 1)
    return mp_units::get_common_unit(Front{}, mp_units::get_common_unit(Rest{}..., common_unit<Us...>{}));
  else
    return mp_units::get_common_unit(Front{}, mp_units::get_common_unit(common_unit<Rest...>{}, common_unit<Us...>{}));
}

/**
 * @brief Computes the common unit of three or more units
 *
 * @param u1 first unit
 * @param u2 second unit
 * @param u3 third unit
 * @param rest remaining units, if any
 * @return the common unit obtained by folding `get_common_unit` over @p u1, @p u2, @p u3, and @p rest
 */
[[nodiscard]] consteval Unit auto get_common_unit(Unit auto u1, Unit auto u2, Unit auto u3, Unit auto... rest)
  requires requires { mp_units::get_common_unit(mp_units::get_common_unit(u1, u2), u3, rest...); }
{
  return mp_units::get_common_unit(mp_units::get_common_unit(u1, u2), u3, rest...);
}

MP_UNITS_EXPORT_END

}  // namespace mp_units
