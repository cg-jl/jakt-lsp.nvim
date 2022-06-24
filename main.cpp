#include "json.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/xchar.h> // for u16
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using u64 = std::uint64_t;
using i64 = std::int64_t;
using f64 = double;
using u16 = std::uint16_t;
using u8 = std::uint8_t;

using namespace std::string_view_literals;
namespace fs = std::filesystem;

void usage(char const *progname) {
  std::fprintf(stderr, "USAGE: %s [OPTIONS..]\n", progname);
  std::fputs("OPTIONS:\n", stderr);
  std::fputs(" -h,--help       Show this message and exit.\n", stderr);
  std::fputs(" -C PATH,--compiler=PATH\n\
               Where compiler is located\n\
               (default is $HOME/.cargo/bin/jakt)\n",
             stderr);
}

class PreConditionChecker {
  std::string m_precondition_name = "<unknown precondition>";

protected:
  constexpr void set_precondition_name(std::string name) noexcept {
    m_precondition_name = std::move(name);
  }

public:
  constexpr std::string_view name() const noexcept {
    return m_precondition_name;
  }
  virtual ~PreConditionChecker() {}
  virtual std::optional<std::string_view> perform_check() const noexcept = 0;
};

class CompilerPathChecker : public PreConditionChecker {
  fs::path m_path;

public:
  CompilerPathChecker(std::string_view path) noexcept : m_path(path) {
    std::stringstream ss;
    ss << "compiler path: " << m_path;
    set_precondition_name(ss.str());
  }
  virtual std::optional<std::string_view>
  perform_check() const noexcept override {
    auto const stat = fs::status(m_path);
    if (!fs::exists(stat))
      return "can't find compiler binary"sv;
    if (!fs::is_regular_file(stat))
      return "compiler binary must be a normal executable file"sv;
    static constexpr auto perms_needed =
        fs::perms::others_exec | fs::perms::others_read;
    if ((stat.permissions() & perms_needed) != perms_needed)
      return "cannot use compiler binary due to permissions. "
             "Check that o+w and o+r are there."sv;
    return std::nullopt;
  }
};

// Abstract compiler interface
class Compiler {
  std::string_view m_compiler_path;

public:
  constexpr Compiler(std::string_view compiler_path)
      : m_compiler_path(compiler_path) {}
};

static bool check_single_precondition(const PreConditionChecker &checker) {
  auto errored = false;
  std::cerr << "Checking " << checker.name();
  auto const error = checker.perform_check();
  if (error) {
    std::cerr << "  \x1b[1;31mERROR\x1b[m";
    errored = true;
  } else {
    std::cerr << "  \x1b[1;38;5;2mOK\x1b[m";
  }
  std::cerr << '\n';
  return !errored;
}

static bool check_preconditions(
    std::span<std::unique_ptr<const PreConditionChecker>> checkers) {
  auto errored = false;
  for (auto &checker : checkers) {
    errored |= !check_single_precondition(*checker);
  }
  return !errored;
}

namespace rpc {
// Base Protocol :
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#baseProtocol
namespace base {
// constructors made private so everyone that uses the interface must use its
// `from_json` spec
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#abstractMessage
class Message {
private:
  constexpr Message() {}

public:
  static constexpr Message create_for_dump() { return Message(); }
  static std::optional<Message> validate(json::value &value) {
    if (!value.is_object())
      return std::nullopt;
    auto &obj = value.as_object();
    auto version = obj.remove(u"jsonrpc");
    if (!version || !version->is_string() || version->as_string() != u"2.0")
      return std::nullopt;

    return Message();
  }

  static void dump([[maybe_unused]] Message _m, json::object &global) {
    global.set(u"jsonrpc", u"2.0");
  }
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#requestMessage
struct RequestMessage {
  std::variant<std::u16string, i64> id;
  std::u16string method;
  std::optional<json::value> params; // array | object

private:
  Message m_message_impl;
  constexpr RequestMessage(Message impl) : m_message_impl(impl){};

public:
  static bool identify(json::value const &value) {
    if (!value.is_object())
      return false;
    auto const &obj = value.as_object();
    return obj.has_key(u"id") && obj.has_key(u"method");
  }
  static std::optional<RequestMessage> validate(json::value &value) {
    auto inner = Message::validate(value);
    if (!inner)
      return std::nullopt;
    RequestMessage message(std::move(*inner));
    // RequestMessage extends Message

    auto &obj = value.as_object();

    // RequestMessage.id : string | number
    {
      auto id = obj.remove(u"id");
      if (!id)
        return std::nullopt;
      if (id->is_number()) {
        message.id = fmt::format(u"{}", id->as_number());
      } else if (id->is_string()) {
        message.id = std::move(id->as_string());
      }
    }

    // RequestMessage.method : string
    {
      auto method = obj.remove(u"method");
      if (!method || !method->is_string())
        return std::nullopt;
      message.method = std::move(method->as_string());
    }

    // RequestMessage.params : (array | object)?
    {
      auto params = obj.remove(u"params");
      if (params && !params->is_array() && !params->is_object())
        return std::nullopt;
      message.params = std::move(params);
    }

    return message;
  }
};

enum class ErrorCode : int {
  // defined by JSON-RPC
  ParseError = -32700,
  InvalidRequest = -32600,
  MethodNotFound = -32601,
  InvalidParams = -32602,
  InternalError = -32603,
  __jsonrpcReservedErrorRangeStart = -32099,
  // Error code indicating that a server received a notification or
  // request before the serevr has received the `initialize` request
  ServerNotInitialized = -32002,
  UnknownErrorCode = -32001,
  __jsonrpcReservedErrorRangeEnd = -32000,
  __lspReservedErrorRangeStart = -32899,
  // A request failed but it was syntactically correct, i.e the
  // method name was known and the parameters were valid. The error
  // message should contain human readable information about why the
  // request failed.
  RequestFailed = -32803,
  // The server cancelled the request. This error code sthould only
  // be used for requests that explicitly support being server cancellable.
  ServerCancelled = -32802,
  // The server detected that the contet of a document got modified outside
  // normal conditions. A server should NOT send this error code if it detects a
  // content change in it unprocessed messages The result even computed on an
  // older
  // state might still be useful for the client.
  ContentModified = -32801,
  // The client has canceled a request and the server has detected the cancel.
  RequestCancelled = -32800,
  __lspReservedErrorRangeEnd = -32800
};

struct ResponseError {
  ErrorCode code;

  std::u16string message;

  std::optional<json::value> data;

  static void dump(ResponseError error, json::object &target) {
    target.set(u"code", f64(error.code));
    target.set(u"message", std::move(error.message));
    if (error.data) {
      target.set(u"data", std::move(*error.data));
    }
  }
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#responseMessage
struct ResponseMessage {
  // The request id.
  std::variant<std::u16string, i64, json::null> id;
  // The result of a request. This member is REQUIRED on success.
  // This member MUST NOT exist if there was an error invoking the method.
  std::optional<json::value> result;

  // The error object in case a request fails.
  std::optional<ResponseError> error;

  static auto ok(std::variant<std::u16string, i64, json::null> id,
                 json::value result) -> ResponseMessage {
    return ResponseMessage{std::move(id), std::move(result), std::nullopt};
  }

  static auto err(std::variant<std::u16string, i64, json::null> id,
                  ResponseError error) -> ResponseMessage {
    return ResponseMessage{std::move(id), std::nullopt, std::move(error)};
  }

  static void dump(ResponseMessage message, json::object &target) {
    if (auto const str = std::get_if<std::u16string>(&message.id); str) {
      target.set(u"id", std::move(*str));
    } else if (std::get_if<json::null>(&message.id)) {
      target.set(u"id", json::null{});
    } else {
      target.set(u"id", static_cast<f64>(std::get<i64>(message.id)));
    }

    if (message.result) {
      target.set(u"result", std::move(*message.result));
    } else {
      json::object obj;
      ResponseError::dump(std::move(*message.error), obj);
      target.set(u"error", std::move(obj));
    }
  }
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#notificationMessage
struct NotificationMessage {
  // The method to be invoked.
  std::u16string method;
  // The notification's params.
  std::optional<json::value> params;

  static std::optional<NotificationMessage> validate(json::value &value) {
    NotificationMessage message;
    // NotificationMesssage extends Message
    if (!Message::validate(value))
      return std::nullopt;
    auto &obj = value.as_object();

    // NotificationMessage.method : string
    {
      auto method = obj.remove(u"method");
      if (!method || !method->is_string())
        return std::nullopt;

      message.method = std::move(method->as_string());
    }

    // NotificationMessage.params : (array | object)?
    {
      auto params = obj.remove(u"params");
      if (params && !params->is_array() && !params->is_object())
        return std::nullopt;
      message.params = std::move(params);
    }

    return message;
  }
};

// NOTE: Notification and requests whose methods start with ‘$/’ are messages
// which are protocol implementation dependent and might not be implementable in
// all clients or servers. For example if the server implementation uses a
// single threaded synchronous programming language then there is little a
// server can do to react to a $/cancelRequest notification. If a server or
// client receives notifications starting with ‘$/’ it is free to ignore the
// notification. If a server or client receives a request starting with ‘$/’ it
// must error the request with error code MethodNotFound

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#cancelRequest
struct CancelParams {
  // The request id to cancel.
  std::variant<i64, std::u16string> id;

  static std::optional<CancelParams> validate(json::value &value) {
    CancelParams params;
    if (!value.is_object())
      return std::nullopt;
    auto &obj = value.as_object();
    // CancelParams.id : integer | string
    {
      auto id = obj.remove(u"id");
      if (!id)
        return std::nullopt;
      if (id->is_number() && id->as_number() == std::floor(id->as_number())) {
        params.id = static_cast<i64>(std::as_const(id)->as_number());
      } else if (id->is_string()) {
        params.id = std::move(id->as_string());
      } else {
        return std::nullopt;
      }
    }

    return params;
  }
};
// A request that got canceled still needs to return from the server and send a
// response back. It can not be left open / hanging. This is in line with the
// JSON-RPC protocol that requires that every request sends a response back. In
// addition it allows for returning partial results on cancel. If the request
// returns an error response on cancellation it is advised to set the error code
// to ErrorCodes.RequestCancelled.
} // namespace base

} // namespace rpc

auto main(int const argc, char const *const *const argv,
          char const *const *envp) -> int {

  auto const progname = argv[0];

  std::string compiler_path = "";

  // search for environment variables like HOME
  for (; *envp; ++envp) {
    if (strncmp(*envp, "HOME=", 5) == 0) {
      auto home_dir = *envp + 5; // 5 is offset for <home> (envp == HOME=<home>)
      compiler_path = std::string(home_dir) + "/.cargo/bin/jakt";
      continue;
    }
    // windows: home is USERPROFILE
    if (strncmp(*envp, "USERPROFILE=", 12) == 0) {
      auto home_dir =
          *envp + 12; // 12 is offset for <home> (envp == USERPROFILE=<home>)
      compiler_path = std::string(home_dir) + "\\.cargo\\bin\\jakt.exe";
      continue;
    }
  }

  for (int i = 1; i != argc; ++i) {
    if (std::strcmp(argv[i], "-h") == 0 ||
        std::strcmp(argv[i], "--help") == 0) {
      usage(progname);
      return 0;
    }
    // -C PATH, --compiler PATH
    if (std::strcmp(argv[i], "-C") == 0 ||
        std::strcmp(argv[i], "--compiler") == 0) {
      ++i;
      if (i == argc) {
        std::fprintf(stderr, "error: used %s without an argument.\n",
                     argv[i - 1]);
        usage(progname);
        return 1;
      }
      compiler_path = std::string(argv[i]);
      continue;
    }

    // --compiler=PATH
    if (std::strncmp(argv[i], "--compiler=", 11) == 0) {
      auto const rest = argv[i] + 11;
      compiler_path = std::string(rest);
      continue;
    }
  }

  if (!check_single_precondition(CompilerPathChecker(compiler_path)))
    return 1;

  return 0;
}
