#include "json.h"

// Base Protocol :
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#baseProtocol
namespace rpc::base {
// Abstract Message.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#abstractMessage
struct Message {
  static bool validate(json::value &) noexcept;
  static void dump(json::object &) noexcept;
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#requestMessage
struct RequestMessage {
  std::variant<json::string, i64> id;
  json::string method;
  std::optional<json::value> params;

private:
  constexpr RequestMessage() {}

public:
  // we need some way to differ RequestMessage and NotificationMessage.
  // RequestMessage has an "id", which is what this method checks.
  static bool identify(json::value const &) noexcept;
  static std::optional<RequestMessage> validate(json::value &) noexcept;
};

enum class ErrorCode : i64 {
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
  json::string message;
  std::optional<json::value> data;

  static void dump(ResponseError, json::object &) noexcept;
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#responseMessage
struct ResponseMessage {
  // The request id.
  std::variant<json::string, i64, json::null> id;
  // The result of a request. This member is REQUIRED on success.
  // This member MUST NOT exist if there was an error invoking the method.
  std::optional<json::value> result;

  // The error object in case a request fails.
  std::optional<ResponseError> error;

  static constexpr ResponseMessage
  ok(std::variant<json::string, i64, json::null> id,
     json::value result) noexcept {
    return ResponseMessage{std::move(id), std::move(result), std::nullopt};
  }
  static constexpr ResponseMessage
  err(std::variant<json::string, i64, json::null> id,
      ResponseError error) noexcept {
    return ResponseMessage{std::move(id), std::nullopt, std::move(error)};
  }
  static void dump(ResponseMessage, json::object &) noexcept;
};
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#notificationMessage
struct NotificationMessage {
  // The method to be invoked.
  json::string method;
  // The notification's params.
  std::optional<json::value> params;

  static std::optional<NotificationMessage> validate(json::value &) noexcept;
};

// NOTE: Notification and requests whose methods start with ‘$/’ are messages
// which are protocol implementation dependent and might not be implementable in
// all clients or servers. For example if the server implementation uses a
// single threaded synchronous programming language then there is little a
// server can do to react to a $/cancelRequest notification. If a server or
// client receives notifications starting with ‘$/’ it is free to ignore the
// notification. If a server or client receives a request starting with ‘$/’ it
// must error the request with error code MethodNotFound
//
// A request that got canceled still needs to return from the server and send a
// response back. It can not be left open / hanging. This is in line with the
// JSON-RPC protocol that requires that every request sends a response back. In
// addition it allows for returning partial results on cancel. If the request
// returns an error response on cancellation it is advised to set the error code
// to ErrorCodes.RequestCancelled.

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#cancelRequest
struct CancelParams {
  // The request id to cancel.
  std::variant<json::string, i64> id;

  static std::optional<CancelParams> validate(json::value &) noexcept;
};

} // namespace rpc::base
