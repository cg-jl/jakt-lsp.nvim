#include <rpc/base.h>

static constexpr f64 INT_CONVERSION_TOLERANCE = 0.000000001;

namespace rpc::base {
bool Message::validate(json::value &value) noexcept {
  // Message : object
  if (!value.is_object())
    return false;

  auto &obj = value.as_object();
  // Message.jsonrpc: string = "2.0"
  auto jsonrpc = obj.remove(u"jsonrpc");
  return jsonrpc && jsonrpc->is_string() && jsonrpc->as_string() == u"2.0";
}

void Message::dump(json::object &target) noexcept {
  target.set(u"jsonrpc", u"2.0");
}

bool RequestMessage::identify(json::value const &value) noexcept {
  return value.is_object() && value.as_object().has_key(u"id");
}

std::optional<RequestMessage>
RequestMessage::validate(json::value &input) noexcept {
  // RequestMessage extends Message
  if (!Message::validate(input))
    return std::nullopt;

  RequestMessage message;

  auto &obj = input.as_object();

  // RequestMessage.id : string | number
  {
    auto id = obj.remove(u"id");
    if (!id)
      return std::nullopt;
    if (id->is_string()) {
      message.id = std::move(id->as_string());
    } else if (auto const i = id->try_integer(INT_CONVERSION_TOLERANCE); i) {
      message.id = *i;
    } else {
      return std::nullopt;
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

void ResponseError::dump(ResponseError error, json::object &target) noexcept {
  target.set(u"code", static_cast<f64>(error.code));
  target.set(u"message", std::move(error.message));
  if (error.data) {
    target.set(u"data", std::move(*error.data));
  }
}

void ResponseMessage::dump(ResponseMessage message,
                           json::object &target) noexcept {
  // ResponseMessage extends Message
  Message::dump(target);
  json::value id;
  if (auto const str = std::get_if<json::string>(&message.id); str) {
    id = std::move(*str);
  } else if (std::holds_alternative<json::null>(message.id)) {
    id = json::null{};
  } else {
    id = static_cast<f64>(std::get<i64>(message.id));
  }
  target.set(u"id", std::move(id));

  if (message.result) {
    target.set(u"result", std::move(*message.result));
  } else {
    json::object error;
    ResponseError::dump(std::move(*message.error), error);
    target.set(u"error", std::move(error));
  }
}

std::optional<NotificationMessage> validate(json::value &input) noexcept {
  // NotificationMessage extends Message
  if (!Message::validate(input))
    return std::nullopt;
  NotificationMessage message;
  auto &obj = input.as_object();

  // NotificationMessage.method : string
  {
    auto method = obj.remove(u"method");
    if (!method || !method->is_string())
      return std::nullopt;
    message.method = std::move(method->as_string());
  }

  // NotificationMessage.params: (array | object)?
  {
    auto params = obj.remove(u"params");
    if (params && !params->is_array() && !params->is_object())
      return std::nullopt;
    message.params = std::move(params);
  }

  return message;
}

std::optional<CancelParams>
CancelParams::validate(json::value &input) noexcept {
  if (!input.is_object())
    return std::nullopt;
  CancelParams params;
  auto &obj = input.as_object();

  // CancelParams.id : integer | string
  {
    auto id = obj.remove(u"id");
    if (!id)
      return std::nullopt;
    if (id->is_string()) {
      params.id = std::move(id->as_string());
    } else if (auto const num = id->try_integer(INT_CONVERSION_TOLERANCE);
               num) {
      params.id = *num;
    } else {
      return std::nullopt;
    }
  }

  return params;
}

} // namespace rpc::base
