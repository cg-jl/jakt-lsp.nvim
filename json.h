#pragma once
#include "numbers.h"
#include <cctype>
#include <concepts>
#include <fmt/format.h>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace json {
namespace types {
class value;

using array = std::vector<value>;
using string = std::u16string;
class object {
  using assoc_type = std::vector<std::pair<std::u16string, value>>;
  assoc_type m_assoc_array;

public:
  constexpr assoc_type const &assocs() const noexcept { return m_assoc_array; }
  // Returns whether adding was successful or not. Adding can fail
  // if the key already exists.
  bool set(std::u16string key, value value) noexcept;
  [[nodiscard]] bool has_key(std::u16string_view key) const noexcept;
  [[nodiscard]] value const &expect(std::u16string_view key) const;
  [[nodiscard]] value &expect(std::u16string_view key);
  [[nodiscard]] std::optional<value> remove(std::u16string_view key) noexcept;
  [[nodiscard]] value remove_expect(std::u16string_view key);
};
struct null {};

class value {
  std::variant<object, array, f64, bool, std::u16string, null> m_variant;

public:
  constexpr value() : m_variant{} {}
  constexpr value(bool v) : m_variant(v) {}
  value(object obj) : m_variant(std::move(obj)) {}
  constexpr value(array arr) : m_variant(std::move(arr)) {}
  constexpr value(f64 v) : m_variant(v) {}
  constexpr value(std::u16string str) : m_variant(std::move(str)) {}
  constexpr value(null) : m_variant(null{}) {}
  constexpr object const &as_object() const {
    return std::get<object>(m_variant);
  }
  constexpr auto as_object() -> object & { return std::get<object>(m_variant); }
  constexpr array const &as_array() const { return std::get<array>(m_variant); }
  constexpr array &as_array() { return std::get<array>(m_variant); }
  constexpr f64 as_number() const { return std::get<f64>(m_variant); }
  constexpr f64 &as_number() { return std::get<f64>(m_variant); }
  constexpr std::u16string_view as_string() const {
    return std::get<std::u16string>(m_variant);
  }
  constexpr std::u16string &as_string() {
    return std::get<std::u16string>(m_variant);
  }
  constexpr bool as_bool() const { return std::get<bool>(m_variant); }
  constexpr bool &as_bool() { return std::get<bool>(m_variant); }

  constexpr bool is_null() const noexcept {
    return m_variant.valueless_by_exception();
  }
  constexpr bool is_object() const noexcept {
    return std::holds_alternative<object>(m_variant);
  }
  constexpr bool is_array() const noexcept {
    return std::holds_alternative<array>(m_variant);
  }
  constexpr bool is_number() const noexcept {
    return std::holds_alternative<f64>(m_variant);
  }
  constexpr bool is_bool() const noexcept {
    return std::holds_alternative<bool>(m_variant);
  }
  constexpr bool is_string() const noexcept {
    return std::holds_alternative<std::u16string>(m_variant);
  }
  // Checks if number is an integer, using a comparison tolerance
  constexpr std::optional<i64> try_integer(f64 tolerance) const noexcept {
    if (!is_number())
      return std::nullopt;
    auto const value = as_number();
    if ((value - std::floor(value)) <= tolerance) {
      return static_cast<i64>(value);
    } else {
      return std::nullopt;
    }
  }
  friend struct fmt::formatter<value>;
};

} // namespace types
using namespace types;

// JSON Parser that bails on first encountered error.
// any method whose result is wrapped in `std::optional`
// (except current_char) means they bail on error.
class Parser {
  std::string_view m_source;
  u64 m_index;

  constexpr bool is_eof() const noexcept { return m_index >= m_source.size(); }
  constexpr char unchecked_char() const noexcept { return m_source[m_index]; }
  constexpr std::optional<char> current_char() const noexcept {
    if (is_eof())
      return std::nullopt;
    return unchecked_char();
  }

  constexpr void accept_current() noexcept { ++m_index; }
  constexpr static bool is_whitespace(char value) noexcept {
    return value == ' ' || value == '\n' || value == '\r' || value == '\t';
  }
  constexpr static u8 from_hex(char v) noexcept {
    if ('A' <= v && 'F' >= v)
      return v - 'A' + 10;
    if ('a' <= v && 'f' >= v)
      return v - 'a' + 10;
    return v - '0';
  }

  void skip_whitespace() noexcept;
  u64 parse_digits() noexcept;
  std::optional<f64> parse_number() noexcept;
  std::optional<u16> parse_four_hex() noexcept;
  // assumes '\\' was just accepted
  std::optional<u16> parse_escape() noexcept;
  // assumes first '"' has been accepted
  std::optional<std::u16string> parse_string() noexcept;
  // assumes first '[' has been accepted
  std::optional<types::array> parse_array() noexcept;
  // assumes first '{' has been accepted
  std::optional<types::object> parse_object() noexcept;

public:
  Parser(std::string_view source) : m_source(source), m_index(0) {}
  std::optional<types::value> parse_value() noexcept;
};

auto parse_single(std::string_view source) -> std::optional<types::value>;

namespace __fmt_helpers {
struct debug_u16_string {
  std::u16string_view view;
};
} // namespace __fmt_helpers

} // namespace json

template <> struct fmt::formatter<json::__fmt_helpers::debug_u16_string> {
  constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
    auto const begin = ctx.begin(), end = ctx.end();
    if (begin != end && *begin != '}')
      throw format_error("only basic format supported");

    return begin;
  }
  template <typename format_ctx>
  auto format(json::__fmt_helpers::debug_u16_string &str, format_ctx &ctx)
      -> decltype(ctx.out()) {
    format_to(ctx.out(), "\"");
    for (auto const value : str.view) {
      switch (value) {
      case '"':
        format_to(ctx.out(), "\\\"");
        break;
      case '\\':
        format_to(ctx.out(), "\\\\");
        break;
      case '/':
        format_to(ctx.out(), "\\/");
        break;
      case '\b':
        format_to(ctx.out(), "\\b");
        break;
      case '\f':
        format_to(ctx.out(), "\\f");
        break;
      case '\n':
        format_to(ctx.out(), "\\n");
        break;
      case '\r':
        format_to(ctx.out(), "\\r");
        break;
      case '\t':
        format_to(ctx.out(), "\\t");
        break;
      default:
        if (std::iswprint(value)) {
          format_to(ctx.out(), "{}", static_cast<char>(value));
        } else {
          u16 nibbles = value;
          format_to(ctx.out(), "\\u{}{}{}{}", (nibbles >> 24),
                    (nibbles >> 16) & 0xf, (nibbles >> 8) & 0xf, nibbles & 0xf);
        }
        break;
      }
    }
    return format_to(ctx.out(), "\"");
  }
};
template <> struct fmt::formatter<json::value> {
  // TODO: alternate form :# for objects/arrays?
  constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
    auto const begin = ctx.begin(), end = ctx.end();
    if (begin != end && *begin != '}')
      throw format_error("only basic format supported");

    return begin;
  }
  template <typename format_ctx>
  auto format(json::value const &v, format_ctx &ctx) -> decltype(ctx.out()) {
    if (v.is_null()) {
      return format_to(ctx.out(), "null");
    }
    if (v.is_array()) {
      auto const &arr = v.as_array();
      format_to(ctx.out(), "[");
      if (!arr.empty()) {
        format_to(ctx.out(), "{}", arr[0]);
        for (u64 i = 1; i != arr.size(); ++i)
          format_to(ctx.out(), ",{}", arr[i]);
      }
      return format_to(ctx.out(), "]");
    }
    if (v.is_object()) {
      auto const &assocs = v.as_object().assocs();
      format_to(ctx.out(), "{{");
      if (!assocs.empty()) {
        format_to(ctx.out(), "{}:{}",
                  json::__fmt_helpers::debug_u16_string{assocs[0].first},
                  assocs[0].second);
        for (u64 i = 1; i != assocs.size(); ++i) {
          format_to(ctx.out(), ",{}:{}",
                    json::__fmt_helpers::debug_u16_string{assocs[i].first},
                    assocs[i].second);
        }
      }
      return format_to(ctx.out(), "}}");
    }
    if (v.is_string()) {
      return format_to(ctx.out(), "{}",
                       json::__fmt_helpers::debug_u16_string{v.as_string()});
    }
    if (v.is_number())
      return format_to(ctx.out(), "{}", v.as_number());
    return format_to(ctx.out(), "{}", v.as_bool());
  }
};
