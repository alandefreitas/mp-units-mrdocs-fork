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

// Formatting library for C++ - the core API for char/UTF-8
//
// Copyright (c) 2012 - present, Victor Zverovich
// All rights reserved.
//
// For the license information refer to format.h.

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-union-access)
#pragma once

// The pieces a `std::formatter` specialization is built from, in `mp_units::utility`:
//
// - `fmt_align`, `fill_t`, `fmt_arg_ref`, and `fill_align_width_format_specs` describe the
//   fill/align/width part of a `std-format-spec`,
// - `parse_align`, `parse_dynamic_spec`, `parse_fill_align_width`, and `parse_nonnegative_int`
//   parse it (`parse_fill_align_width` covers the common case in one call, the others let a
//   formatter interleave its own options in the order the standard grammar defines them),
// - `handle_dynamic_spec` with `width_checker` resolves a `{}` width against the format
//   arguments at format time, and `write_padded` writes the result honoring fill and alignment.
//
// The library's own formatters for `unit`, `dimension`, `quantity`, and `uncertain` are written
// against exactly this set, so it is known to be sufficient for a non-trivial formatter.
//
// TODO these should be exposed by the C++ Standard Library

#include <mp-units/bits/requires_hosted.h>
//
#include <mp-units/bits/module_macros.h>
#include <mp-units/compat_macros.h>
#include <mp-units/ext/algorithm.h>

#ifndef MP_UNITS_IN_MODULE_INTERFACE
#include <mp-units/ext/contracts.h>
#include <mp-units/ext/format.h>
#include <mp-units/ext/type_traits.h>
#ifdef MP_UNITS_IMPORT_STD
import std;
#else
#include <array>
#include <concepts>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#endif
#endif

// most of the below code is based on/copied from fmtlib

// The vocabulary of a `std-format-spec`. Public so that users can write their own formatters
// on top of the same pieces the library's formatters use.
// TODO the below should be exposed by the C++ Standard Library (used in our examples)
namespace mp_units::utility {
MP_UNITS_EXPORT_BEGIN

/// @brief The alignment option of a `std-format-spec`'s fill/align part.
enum class fmt_align : std::int8_t {
  /// No alignment was specified.
  none,
  /// Left alignment (`<`).
  left,
  /// Right alignment (`>`).
  right,
  /// Center alignment (`^`).
  center,
  /// Alignment defaulting to right for non-string types and applying to the sign/prefix (`=`).
  numeric
};

/// @brief Which alternative of @ref fmt_arg_ref::value is active.
enum class fmt_arg_id_kind : std::int8_t {
  /// The reference does not point to any argument.
  none,
#if MP_UNITS_USE_FMTLIB
  /// The reference points to a named argument (only available when `MP_UNITS_USE_FMTLIB` is set).
  name,
#endif
  /// The reference points to an argument by its index.
  index
};

/// @brief A reference to a format argument, by index or (when using fmtlib) by name.
template<typename Char>
struct fmt_arg_ref {
  /// Which alternative of @ref value is active.
  fmt_arg_id_kind kind = fmt_arg_id_kind::none;
  /// Stores either the referenced argument's index or (when using fmtlib) its name.
  union value {
    /// The referenced argument's index, active when `kind == fmt_arg_id_kind::index`.
    int index = 0;
#if MP_UNITS_USE_FMTLIB
    std::basic_string_view<Char> name;
#endif

    /// Default-constructs the union with the `index` alternative active, set to `0`.
    constexpr value() {}
    /// Constructs the union with the `index` alternative active, set to @p idx.
    /// @param idx the argument index to store
    constexpr explicit value(int idx) : index(idx) {}
#if MP_UNITS_USE_FMTLIB
    constexpr value(std::basic_string_view<Char> n) : name(n) {}
#endif
  } val{};  ///< The referenced argument's index or (when using fmtlib) name, per @ref kind.

  /// Default-constructs a reference that does not point to any argument (`kind == fmt_arg_id_kind::none`).
  fmt_arg_ref() = default;
  /// Constructs a reference to the argument at @p index.
  /// @param index the index of the referenced argument
  constexpr explicit fmt_arg_ref(int index) : kind(fmt_arg_id_kind::index), val(index) {}
#if MP_UNITS_USE_FMTLIB
  constexpr explicit fmt_arg_ref(std::basic_string_view<Char> name) : kind(fmt_arg_id_kind::name), val(name) {}
#endif

  /**
   * @brief Rebinds this reference to the argument at @p idx.
   * @param idx the index of the argument to reference
   * @return a reference to `*this`, after the assignment
   */
  [[nodiscard]] constexpr fmt_arg_ref& operator=(int idx)
  {
    kind = fmt_arg_id_kind::index;
    val.index = idx;
    return *this;
  }
};

/// @brief The fill character(s) of a `std-format-spec`, storing at most one Unicode code point.
template<typename Char>
struct fill_t {
private:
  static constexpr std::size_t max_size = 4 / sizeof(Char);
  // At most one codepoint (so one char32_t or four utf-8 char8_t)
  std::array<Char, max_size> data_ = {Char{' '}};
  unsigned char size_ = 1;

public:
  /**
   * @brief Replaces the fill with the code point stored in @p str.
   * @param str the code unit sequence encoding a single fill code point
   * @return a reference to `*this`, after the assignment
   */
  constexpr fill_t& operator=(std::basic_string_view<Char> str)
  {
    auto size = str.size();
    if (size > max_size) MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("invalid fill"));
    for (std::size_t i = 0; i < size && i < max_size; ++i) data_[i] = str[i];
    size_ = static_cast<unsigned char>(size);
    return *this;
  }

  /// The number of code units in the fill.
  /// @return the number of code units in the fill
  [[nodiscard]] constexpr std::size_t size() const { return size_; }
  /// A pointer to the fill's code units.
  /// @return a pointer to the fill's code units
  [[nodiscard]] constexpr const Char* data() const { return data_.data(); }

  /**
   * @brief Accesses the code unit at @p index.
   * @param index the position of the code unit
   * @return a reference to the code unit at @p index
   */
  [[nodiscard]] constexpr Char& operator[](std::size_t index) { return data_[index]; }
  /**
   * @brief Accesses the code unit at @p index.
   * @param index the position of the code unit
   * @return a reference to the code unit at @p index
   */
  [[nodiscard]] constexpr const Char& operator[](std::size_t index) const { return data_[index]; }
};

MP_UNITS_EXPORT_END
/// @brief Implementation details of the `std-format-spec` parsing/formatting utilities.
namespace detail {

/**
 * @brief Converts a character to ASCII.
 * @param value the character to convert
 * @return the ASCII value of @p value, or a number greater than 127 on conversion failure
 */
template<std::integral Char>
[[nodiscard]] constexpr Char to_ascii(Char value)
{
  return value;
}

/**
 * @brief Converts a scoped/unscoped enumeration character type to its underlying ASCII value.
 * @param value the character to convert
 * @return the ASCII value of @p value, or a number greater than 127 on conversion failure
 */
template<typename Char>
  requires std::is_enum_v<Char>
[[nodiscard]] constexpr std::underlying_type_t<Char> to_ascii(Char value)
{
  return value;
}

/**
 * @brief Casts a nonnegative integer to its unsigned counterpart.
 * @param value the nonnegative integer to cast
 * @return @p value cast to `std::make_unsigned_t<Int>`
 */
template<typename Int>
[[nodiscard]] constexpr std::make_unsigned_t<Int> to_unsigned(Int value)
{
  MP_UNITS_PRECONDITION(std::is_unsigned_v<Int> || value >= 0);
  return static_cast<std::make_unsigned_t<Int>>(value);
}

/**
 * @brief Resolves a dynamic (`{}`-style) spec value from a format argument.
 * @param arg the format argument to visit with `Handler`
 * @return the resolved spec value
 */
template<class Handler, typename FormatArg>
[[nodiscard]] constexpr int get_dynamic_spec(FormatArg arg)
{
  unsigned long long value = 0;
#if (defined MP_UNITS_USE_FMTLIB && FMT_VERSION >= 110000)
  // for some reason the requires expression below does not work with fmt
  value = arg.visit(Handler{});
#else
  if constexpr (requires { arg.visit(Handler{}); })
    value = arg.visit(Handler{});
  else
    value = MP_UNITS_STD_FMT::visit_format_arg(Handler{}, arg);
#endif
  if (value > detail::to_unsigned(std::numeric_limits<int>::max())) {
    MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("number is too big"));
  }
  return static_cast<int>(value);
}

/**
 * @brief Looks up the format argument identified by @p id in @p ctx.
 * @param ctx the format context to look up the argument in
 * @param id the identifier (index or name) of the argument
 * @return the format argument identified by @p id
 */
template<typename Context, typename ID>
[[nodiscard]] constexpr auto get_arg(Context& ctx, ID id) -> decltype(ctx.arg(id))
{
  auto arg = ctx.arg(MP_UNITS_FMT_TO_ARG_ID(id));
  if (!arg) MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("argument not found"));
  return arg;
}

}  // namespace detail

// TODO the below should be exposed by the C++ Standard Library (used in our examples)
MP_UNITS_EXPORT_BEGIN

/**
 * @brief Resolves a dynamic (`{}`-style) spec against the format arguments.
 *
 * If @p ref does not reference an argument (`kind == fmt_arg_id_kind::none`), @p value is left
 * unchanged.
 *
 * @param value the spec value to update when @p ref references an argument
 * @param ref the reference to the format argument holding the dynamic value, if any
 * @param ctx the format context providing access to the format arguments
 */
template<class Handler, typename Context>
constexpr void handle_dynamic_spec(int& value, fmt_arg_ref<typename Context::char_type> ref, Context& ctx)
{
  switch (ref.kind) {
    case fmt_arg_id_kind::none:
      break;
    case fmt_arg_id_kind::index:
      value = detail::get_dynamic_spec<Handler>(detail::get_arg(ctx, ref.val.index));
      break;
#if MP_UNITS_USE_FMTLIB
    case fmt_arg_id_kind::name:
      value = detail::get_dynamic_spec<Handler>(detail::get_arg(ctx, ref.val.name));
      break;
#endif
  }
}

MP_UNITS_DIAGNOSTIC_PUSH
MP_UNITS_DIAGNOSTIC_IGNORE_UNREACHABLE
/// @brief A `Handler` for @ref handle_dynamic_spec that validates and extracts a `width` argument.
struct width_checker {
  /**
   * @brief Validates that @p value is a nonnegative integer and returns it as `unsigned long long`.
   * @param value the resolved format argument to validate
   * @return @p value converted to `unsigned long long`
   */
  template<typename T>
  [[nodiscard]] constexpr unsigned long long operator()(T value) const
  {
    if constexpr (::mp_units::detail::is_integer<T>) {
      if constexpr (std::numeric_limits<T>::is_signed)
        if (value < 0) MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("negative width"));
      return static_cast<unsigned long long>(value);
    }
    MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("width is not integer"));
  }
};
MP_UNITS_DIAGNOSTIC_POP

/**
 * @brief Parses the range [begin, end) as an unsigned integer.
 *
 * The range must be non-empty and start with a digit.
 *
 * @param begin the beginning of the range to parse; advanced past the parsed digits
 * @param end the end of the range to parse
 * @param error_value the value to return if the parsed integer overflows `int`
 * @return the parsed integer, or @p error_value on overflow
 */
template<std::forward_iterator It>
[[nodiscard]] constexpr int parse_nonnegative_int(It& begin, It end, int error_value)
{
  MP_UNITS_PRECONDITION(begin != end && '0' <= *begin && *begin <= '9');
  unsigned value = 0, prev = 0;
  auto pos = begin;
  do {
    prev = value;
    value = value * 10 + unsigned(*pos - '0');
    ++pos;
  } while (pos != end && '0' <= *pos && *pos <= '9');
  auto num_digits = pos - begin;
  begin = pos;
  if (num_digits <= std::numeric_limits<int>::digits10) return static_cast<int>(value);
  // Check for overflow.
  const unsigned max = detail::to_unsigned((std::numeric_limits<int>::max)());
  return num_digits == std::numeric_limits<int>::digits10 + 1 && prev * 10ull + unsigned(pos[-1] - '0') <= max
           ? static_cast<int>(value)
           : error_value;
}

MP_UNITS_EXPORT_END
namespace detail {

/**
 * @brief Checks whether @p c can start a named argument identifier.
 * @param c the character to check
 * @return `true` if @p c is a letter or underscore, `false` otherwise
 */
template<typename Char>
[[nodiscard]] constexpr bool is_name_start(Char c)
{
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

/**
 * @brief Parses an `arg-id` (an index or a name) from [begin, end) and reports it to @p handler.
 * @param begin the beginning of the range to parse; must be non-empty
 * @param end the end of the range to parse
 * @param handler the handler notified via `on_index` or `on_name` with the parsed identifier
 * @return an iterator past the parsed `arg-id`
 */
template<std::forward_iterator It, typename Handler>
[[nodiscard]] constexpr const It do_parse_arg_id(It begin, It end, Handler& handler)
{
  auto ch = *begin;
  if (ch >= '0' && ch <= '9') {
    int index = 0;
    constexpr int max = (std::numeric_limits<int>::max)();
    if (ch != '0')
      index = parse_nonnegative_int(begin, end, max);
    else
      ++begin;
    if (begin == end || (*begin != '}' && *begin != ':'))
      MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("invalid format string"));
    handler.on_index(index);
    return begin;
  }
  if (ch == '%') return begin;  // mp-units extension
  if (!detail::is_name_start(ch)) {
    MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("invalid format string"));
  }
  auto it = begin;
  do {
    ++it;
  } while (it != end && (detail::is_name_start(*it) || ('0' <= *it && *it <= '9')));
#if MP_UNITS_USE_FMTLIB
  handler.on_name({begin, detail::to_unsigned(it - begin)});
#else
  MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("named arguments are not supported in the C++ standard facilities"));
#endif

  return it;
}

/**
 * @brief Parses an `arg-id` from [begin, end), or reports an automatic index if none is given.
 *
 * An `arg-id` is absent when the range starts with `}` or `:`, in which case @p handler is
 * notified via `on_auto` instead of `on_index`/`on_name`.
 *
 * @param begin the beginning of the range to parse; must be non-empty
 * @param end the end of the range to parse
 * @param handler the handler notified with the parsed (or automatic) identifier
 * @return an iterator past the parsed `arg-id`, or @p begin when none was present
 */
template<std::forward_iterator It, typename Handler>
[[nodiscard]] constexpr It parse_arg_id(It begin, It end, Handler& handler)
{
  MP_UNITS_PRECONDITION(begin != end);
  auto ch = *begin;
  if (ch != '}' && ch != ':') return detail::do_parse_arg_id(begin, end, handler);
  handler.on_auto();
  return begin;
}

/// @brief A `parse_arg_id` handler that resolves a dynamic (`{}`-style) spec's argument reference.
template<typename Char>
struct dynamic_spec_id_handler {
  /// The format parse context used to validate and allocate the argument identifier.
  MP_UNITS_STD_FMT::basic_format_parse_context<Char>& ctx;
  /// The argument reference updated with the resolved identifier.
  fmt_arg_ref<Char>& ref;

  /// @brief Resolves an automatic (next-in-sequence) argument index and stores it in `ref`.
  constexpr void on_auto()
  {
    const int id = MP_UNITS_FMT_FROM_ARG_ID(ctx.next_arg_id());
    ref = fmt_arg_ref<Char>(id);
#if MP_UNITS_USE_FMTLIB
    ctx.check_dynamic_spec(id);
#elif __cpp_lib_format >= 202305L
    ctx.check_dynamic_spec_integral(MP_UNITS_FMT_TO_ARG_ID(id));
#endif
  }

  /**
   * @brief Validates argument index @p id and stores it in `ref`.
   * @param id the explicit argument index parsed from the format spec
   */
  constexpr void on_index(int id)
  {
    ref = fmt_arg_ref<Char>(id);
    ctx.check_arg_id(MP_UNITS_FMT_TO_ARG_ID(id));
#if MP_UNITS_USE_FMTLIB
    ctx.check_dynamic_spec(id);
#elif __cpp_lib_format >= 202305L
    ctx.check_dynamic_spec_integral(MP_UNITS_FMT_TO_ARG_ID(id));
#endif
  }

#if MP_UNITS_USE_FMTLIB
  constexpr void on_name([[maybe_unused]] std::basic_string_view<Char> id)
  {
    ref = fmt_arg_ref<Char>(id);
    ctx.check_arg_id(id);
  }
#endif
};

}  // namespace detail

/**
 * @brief Parses a spec value that may be a literal integer or a `{}`-style dynamic argument reference.
 * @param begin the beginning of the range to parse; must be non-empty
 * @param end the end of the range to parse
 * @param value updated with the parsed value when it is a literal integer
 * @param ref updated with the referenced argument when the value is `{}`-style dynamic
 * @param ctx the format parse context, used to validate a dynamic argument reference
 * @return an iterator past the parsed spec value, or @p begin when neither form is present
 */
MP_UNITS_EXPORT template<std::forward_iterator It, typename Char = std::iter_value_t<It>>
[[nodiscard]] constexpr It parse_dynamic_spec(It begin, It end, int& value, fmt_arg_ref<Char>& ref,
                                              MP_UNITS_STD_FMT::basic_format_parse_context<Char>& ctx)
{
  MP_UNITS_PRECONDITION(begin != end);
  if ('0' <= *begin && *begin <= '9') {
    const int val = parse_nonnegative_int(begin, end, -1);
    if (val != -1)
      value = val;
    else
      MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("number is too big"));
  } else if (*begin == '{') {
    ++begin;
    if (*begin == '%') return begin - 1;  // mp-units extension
    auto handler = detail::dynamic_spec_id_handler<Char>{ctx, ref};
    if (begin != end) begin = detail::parse_arg_id(begin, end, handler);
    if (begin != end && *begin == '}') return ++begin;
    MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("invalid format string"));
  }
  return begin;
}

namespace detail {

/**
 * @brief Returns the length, in code units, of the code point starting at @p begin.
 * @param begin an iterator to the first code unit of the code point
 * @return the number of code units the code point occupies
 */
template<std::input_iterator It>
constexpr int code_point_length(It begin)
{
  if constexpr (sizeof(std::iter_value_t<It>) != 1) return 1;
  constexpr std::array lengths = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                  0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0};
  const int len = lengths[static_cast<unsigned char>(*begin) >> 3];

  // Compute the pointer to the next character early so that the next
  // iteration can start working on the next character. Neither Clang
  // nor GCC figure out this reordering on their own.
  return len + !len;
}

}  // namespace detail

/**
 * @brief Finds the single modifier from @p modifiers present in [begin, end).
 *
 * Throws when more than one is used, which is how a `format-spec` states that a group of
 * modifier letters is mutually exclusive.
 *
 * @param begin the beginning of the range to search
 * @param end the end of the range to search
 * @param modifiers the set of mutually exclusive modifier characters to look for
 * @return the position of the single modifier present, or @p end when there is none
 */
MP_UNITS_EXPORT template<std::forward_iterator It>
[[nodiscard]] constexpr It at_most_one_of(It begin, It end, std::string_view modifiers)
{
  const It it = ::mp_units::detail::find_first_of(begin, end, modifiers.begin(), modifiers.end());
  if (it != end && ::mp_units::detail::find_first_of(it + 1, end, modifiers.begin(), modifiers.end()) != end)
    throw MP_UNITS_STD_FMT::format_error("only one of '" + std::string(modifiers) +
                                         "' unit modifiers may be used in the format spec");
  return it;
}

/**
 * @brief Parses the fill and alignment part of a `std-format-spec` into @p specs.
 * @param begin the beginning of the range to parse; must be non-empty
 * @param end the end of the range to parse
 * @param specs the specs object to update with the parsed fill and alignment
 * @param default_align the alignment to use when the format spec does not request one
 * @return an iterator past the parsed fill/align part of the format spec
 */
MP_UNITS_EXPORT template<std::forward_iterator It, typename Specs>
[[nodiscard]] constexpr It parse_align(It begin, It end, Specs& specs, fmt_align default_align = fmt_align::none)
{
  MP_UNITS_PRECONDITION(begin != end);
  auto align = fmt_align::none;
  auto pos = begin + detail::code_point_length(begin);
  if (end - pos <= 0) pos = begin;
  for (;;) {
    switch (detail::to_ascii(*pos)) {
      case '<':
        align = fmt_align::left;
        break;
      case '>':
        align = fmt_align::right;
        break;
      case '^':
        align = fmt_align::center;
        break;
    }
    if (align != fmt_align::none) {
      if (pos != begin) {
        auto ch = *begin;
        if (ch == '}') return begin;
        if (ch == '{') MP_UNITS_THROW(MP_UNITS_STD_FMT::format_error("invalid fill character '{'"));
        specs.fill = {begin, pos};
        begin = pos + 1;
      } else {
        ++begin;
      }
      break;
    }
    if (pos == begin) break;
    pos = begin;
  }
  if (align == fmt_align::none) align = default_align;  // mp-units extension
  specs.align = align;
  return begin;
}

// The fill/align/width part of a `std-format-spec` and the padding it drives.
// TODO the below should be exposed by the C++ Standard Library (used in our examples)
MP_UNITS_EXPORT_BEGIN

/**
 * @brief The fill/align/width part of a `std-format-spec`, as parsed by @ref parse_fill_align_width.
 */
template<typename Char>
struct fill_align_width_format_specs {
  fill_t<Char> fill;                        ///< The fill character(s), used to pad to `width`
  fmt_align align : 4 = fmt_align::none;     ///< The requested alignment
  int width = 0;                            ///< The resolved field width
  fmt_arg_ref<Char> width_ref;               ///< A reference to a `{}`-style dynamic width argument, if any
};

/**
 * @brief Parses the fill/align/width part of a `std-format-spec` into @p specs.
 * @param ctx the format parse context, used to resolve a dynamic (`{}`-style) width
 * @param begin the beginning of the range to parse
 * @param end the end of the range to parse
 * @param specs the specs object to update with the parsed fill, alignment, and width
 * @param default_align the alignment to use when the format spec does not request one
 * @return an iterator past the parsed fill/align/width part of the format spec
 */
template<std::forward_iterator It, typename Specs>
[[nodiscard]] constexpr It parse_fill_align_width(
  MP_UNITS_STD_FMT::basic_format_parse_context<std::iter_value_t<It>>& ctx, It begin, It end, Specs& specs,
  fmt_align default_align = fmt_align::none)
{
  auto it = begin;
  if (it == end || *it == '}') return it;

  it = mp_units::utility::parse_align(it, end, specs, default_align);
  if (it == end) return it;

  return mp_units::utility::parse_dynamic_spec(it, end, specs.width, specs.width_ref, ctx);
}

/**
 * @brief Writes a string to an output iterator with fill/align/width padding.
 *
 * Avoids instantiating format_to/vformat_to infrastructure for the common padding use-case
 * inside formatter<Unit>, formatter<Dimension>, and formatter<quantity> specializations.
 *
 * @param out the output iterator to write to
 * @param s the string to write
 * @param width the minimum field width; @p s is padded with @p fill to reach it
 * @param align how @p s is aligned within the padded field
 * @param fill the character(s) used to pad @p s up to @p width
 * @return the output iterator after writing the padded string
 */
template<typename Char, std::output_iterator<Char> Out>
constexpr Out write_padded(Out out, std::basic_string_view<Char> s, int width, fmt_align align,
                           const fill_t<Char>& fill)
{
  const int len = static_cast<int>(s.size());
  const int pad = (width > len) ? width - len : 0;
  const int lpad = (align == fmt_align::center) ? pad / 2 : (align == fmt_align::right) ? pad : 0;
  const int rpad = pad - lpad;
  auto write_fill = [&](int n) {
    for (int i = 0; i < n; ++i)
      for (std::size_t j = 0; j < fill.size(); ++j) *out++ = fill[j];
  };
  write_fill(lpad);
  MP_UNITS_DIAGNOSTIC_PUSH
  // Loop variable shadows a `si::unit_symbols` short name when `using namespace si::unit_symbols`
  // is in scope at the call site (MSVC C4459); the local variable here is the documented
  // canonical name for this trivial loop.
  MP_UNITS_DIAGNOSTIC_IGNORE_SHADOW
  for (Char c : s) *out++ = c;
  MP_UNITS_DIAGNOSTIC_POP
  write_fill(rpad);
  return out;
}

MP_UNITS_EXPORT_END
}  // namespace mp_units::utility

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-union-access)
