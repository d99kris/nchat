//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/HttpContentLengthByteFlow.h"

#include "td/utils/Status.h"

namespace td {

void HttpContentLengthByteFlow::loop() {
  auto ready_size = input_->size();
  if (ready_size > len_) {
    ready_size = len_;
  }
  auto need_size = min(MIN_UPDATE_SIZE, len_);
  if (ready_size < need_size) {
    set_need_size(need_size);
    return;
  }
  output_.append(input_->cut_head(ready_size));
  len_ -= ready_size;
  if (len_ == 0) {
    return finish(Status::OK());
  }
  if (!is_input_active_) {
    return finish(Status::Error("Unexpected end of stream"));
  }
  on_output_updated();
}

}  // namespace td
