//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"

#include <utility>

namespace td {

class DarwinHttp {
 public:
  static void get(CSlice url, Promise<std::pair<int32, BufferSlice>> promise);
  static void post(CSlice url, Slice data, Promise<std::pair<int32, BufferSlice>> promise);
};

}  // namespace td
