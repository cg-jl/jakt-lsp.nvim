#include "json.h"
#include <algorithm>
#include <cmath>

using namespace std::string_view_literals;

namespace json {

std::optional<value> object::remove(std::u16string_view key) noexcept {
  auto removed = std::remove_if(m_assoc_array.begin(), m_assoc_array.end(),
                                [&](auto const &p) { return p.first == key; });
  if (removed == m_assoc_array.end())
    return std::nullopt;
  auto moved = std::move(removed->second);
  m_assoc_array.erase(removed);
  return moved;
}

value object::remove_expect(std::u16string_view key) {
  auto removed = std::remove_if(m_assoc_array.begin(), m_assoc_array.end(),
                                [&](auto const &p) { return p.first == key; });
  auto moved = std::move(removed->second);
  m_assoc_array.erase(removed);
  return moved;
}

bool object::set(std::u16string key, value value) noexcept {
  // try finding where it exists
  if (has_key(key))
    return false;
  m_assoc_array.emplace_back(std::move(key), std::move(value));
  return true;
}

bool object::has_key(std::u16string_view key) const noexcept {
  return std::find_if(m_assoc_array.begin(), m_assoc_array.end(),
                      [&](auto const &value) { return value.first == key; }) !=
         m_assoc_array.end();
}

value &object::expect(std::u16string_view key) {
  return std::find_if(m_assoc_array.begin(), m_assoc_array.end(),
                      [&](auto &pair) { return pair.first == key; })
      ->second;
}

value const &object::expect(std::u16string_view key) const {
  return std::find_if(m_assoc_array.begin(), m_assoc_array.end(),
                      [&](auto &pair) { return pair.first == key; })
      ->second;
}

void Parser::skip_whitespace() noexcept {
  while (!is_eof() && Parser::is_whitespace(unchecked_char())) {
    accept_current();
  }
}
u64 Parser::parse_digits() noexcept {
  u64 value = 0;
  while (!is_eof() && '0' <= unchecked_char() && unchecked_char() <= '9') {
    value = value * 10 + (unchecked_char() - '0');
    accept_current();
  }
  return value;
}
std::optional<f64> Parser::parse_number() noexcept {
  auto is_negative = false;
  if (current_char() == '-') {
    is_negative = true;
    accept_current();
  }
  u64 integral = 0;
  // no leading zeroes on a number, so if it's zero
  // then it's just a zero.
  if (current_char() == '0') {
    accept_current();
  } else if (current_char() <= '9') {
    integral = parse_digits();
  } else {
    return std::nullopt;
  }

  u64 fraction = 0;
  u64 fraction_digits = 1; // default should be 1 due to /1 == id
  if (current_char() == '.') {
    accept_current();
    if (is_eof() || unchecked_char() < '0' || '9' < unchecked_char()) {
      return std::nullopt;
    }
    auto const start = m_index;
    fraction = parse_digits();
    fraction_digits = m_index - start;
  }

  i64 exponent = 1; // default exponent should be 1 due to ^1 ==  id
  if (current_char() == 'e' || current_char() == 'E') {
    accept_current();
    if (current_char() == '-') {
      exponent *= -1;
      accept_current();
    } else if (current_char() == '+') {
      accept_current();
    }

    if (is_eof() || unchecked_char() < '0' || '9' < unchecked_char()) {
      return std::nullopt;
    }
    exponent = parse_digits();
  }

  u64 fraction_div = 1;
  while (--fraction_digits)
    fraction_div *= 10;

  f64 const final =
      std::pow(static_cast<f64>(integral) +
                   static_cast<f64>(fraction) / static_cast<f64>(fraction_div),
               static_cast<f64>(exponent)) *
      (is_negative ? -1 : 1);

  return final;
}
std::optional<u16> Parser::parse_four_hex() noexcept {
  if (is_eof() || !std::isxdigit(unchecked_char()))
    return std::nullopt;
  u16 value = from_hex(unchecked_char());
  accept_current();
  if (is_eof() || !std::isxdigit(unchecked_char()))
    return std::nullopt;
  value = value << 4 | from_hex(unchecked_char());
  accept_current();
  if (is_eof() || !std::isxdigit(unchecked_char()))
    return std::nullopt;
  value = value << 4 | from_hex(unchecked_char());
  accept_current();
  if (is_eof() || !std::isxdigit(unchecked_char()))
    return std::nullopt;
  value = value << 4 | from_hex(unchecked_char());
  accept_current();
  return value;
}
std::optional<u16> Parser::parse_escape() noexcept {
  if (is_eof())
    return std::nullopt;
  switch (unchecked_char()) {
  case '"':
    accept_current();
    return '"';
  case '\\':
    accept_current();
    return '\\';
  case '/':
    accept_current();
    return '/';
  case 'b':
    accept_current();
    return '\b';
  case 'f':
    accept_current();
    return '\f';
  case 'n':
    accept_current();
    return '\n';
  case 'r':
    accept_current();
    return '\r';
  case 't':
    accept_current();
    return '\t';
  case 'u':
    accept_current();
    return parse_four_hex();
  default:
    return std::nullopt;
  }
}
std::optional<std::u16string> Parser::parse_string() noexcept {
  std::u16string value;

  for (; !is_eof() && unchecked_char() != '"';) {
    if (unchecked_char() == '\\') {
      accept_current();
      auto const escaped = parse_escape();
      // invalid escape
      if (!escaped)
        return std::nullopt;
      value.push_back(*escaped);
    } else {
      value.push_back(unchecked_char());
      accept_current();
    }
  }

  if (is_eof() || unchecked_char() != '"')
    return {}; // unfinished string
  accept_current();

  return value;
}
std::optional<types::array> Parser::parse_array() noexcept {
  types::array values;

  skip_whitespace();
  while (!is_eof() && unchecked_char() != ']') {
    auto value = parse_value();
    if (!value)
      return std::nullopt;
    values.emplace_back(std::move(*value));
    if (current_char() != ',')
      break;
    accept_current();
    skip_whitespace();
  }

  if (current_char() != ']')
    return std::nullopt;
  accept_current();

  return values;
}
std::optional<types::object> Parser::parse_object() noexcept {
  types::object kvpairs;

  skip_whitespace();
  while (!is_eof() && unchecked_char() != '}') {
    if (unchecked_char() != '"')
      return std::nullopt;
    accept_current();
    auto key = parse_string();
    if (!key)
      return std::nullopt;
    skip_whitespace();
    if (current_char() != ':')
      return std::nullopt;
    accept_current();
    auto value = parse_value();
    if (!value)
      return std::nullopt;
    if (!kvpairs.set(std::move(*key), std::move(*value)))
      return std::nullopt;
    if (current_char() != ',')
      break;
    accept_current();
    skip_whitespace();
  }
  skip_whitespace();

  if (current_char() != '}')
    return std::nullopt;

  accept_current();

  return kvpairs;
}
std::optional<types::value> Parser::parse_value() noexcept {
  std::optional<types::value> final = std::nullopt;

  skip_whitespace();
  if (!is_eof()) {
    if (m_source.substr(m_index).starts_with("false"sv)) {
      m_index += "false"sv.size();
      final = false;
    } else if (m_source.substr(m_index).starts_with("true"sv)) {
      m_index += "true"sv.size();
      final = true;
    } else if (m_source.substr(m_index).starts_with("null"sv)) {
      m_index += "null"sv.size();
      final = types::null();
    } else if (unchecked_char() == '-' || std::isdigit(unchecked_char())) {
      final = parse_number();
    } else {
      switch (unchecked_char()) {
      case '{':
        accept_current();
        final = parse_object();
        break;
      case '[':
        accept_current();
        final = parse_array();
        break;
      case '"':
        accept_current();
        final = parse_string();
        break;
      default:
        return std::nullopt;
      }
    }
    skip_whitespace();
  }
  return final;
}
auto parse_single(std::string_view source) -> std::optional<types::value> {
  Parser p(source);
  return p.parse_value();
}
} // namespace json
