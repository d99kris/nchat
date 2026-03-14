//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CallId.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class InputCallId {
 public:
  enum class Type : int32 { Call, Message };

  static Result<InputCallId> get_input_call_id(const td_api::object_ptr<td_api::InputCall> &input_call);

  Type get_type() const {
    return type_;
  }

  CallId get_call_id() const {
    CHECK(type_ == Type::Call);
    return call_id_;
  }

  MessageFullId get_message_full_id() const {
    return message_full_id_;
  }

  friend StringBuilder &operator<<(StringBuilder &string_builder, InputCallId input_call_id);

 private:
  Type type_ = Type::Call;
  CallId call_id_;
  MessageFullId message_full_id_;
};

}  // namespace td
