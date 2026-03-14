//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/InputCallId.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"

namespace td {

Result<InputCallId> InputCallId::get_input_call_id(const td_api::object_ptr<td_api::InputCall> &input_call) {
  if (input_call == nullptr) {
    return Status::Error(400, "Input call must be non-empty");
  }
  InputCallId result;
  switch (input_call->get_id()) {
    case td_api::inputCallDiscarded::ID: {
      auto call = static_cast<const td_api::inputCallDiscarded *>(input_call.get());
      result.type_ = Type::Call;
      result.call_id_ = CallId(call->call_id_);
      if (!result.call_id_.is_valid()) {
        return Status::Error(400, "Invalid call identifier specified");
      }
      break;
    }
    case td_api::inputCallFromMessage::ID: {
      auto call = static_cast<const td_api::inputCallFromMessage *>(input_call.get());
      auto dialog_id = DialogId(call->chat_id_);
      auto message_id = MessageId(call->message_id_);
      if (!dialog_id.is_valid()) {
        return Status::Error(400, "Invalid chat identifier specified");
      }
      if (!message_id.is_valid()) {
        return Status::Error(400, "Invalid message identifier specified");
      }
      if (!message_id.is_server()) {
        return Status::Error(400, "Wrong message identifier specified");
      }
      result.type_ = Type::Message;
      result.message_full_id_ = MessageFullId{dialog_id, message_id};
      break;
    }
    default:
      UNREACHABLE();
  }
  return std::move(result);
}

StringBuilder &operator<<(StringBuilder &string_builder, InputCallId input_call_id) {
  switch (input_call_id.type_) {
    case InputCallId::Type::Call:
      return string_builder << input_call_id.call_id_;
    case InputCallId::Type::Message:
      return string_builder << "call from " << input_call_id.message_full_id_;
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
