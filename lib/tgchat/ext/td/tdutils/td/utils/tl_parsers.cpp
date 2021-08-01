//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tl_parsers.h"

#include "td/utils/misc.h"

namespace td {

alignas(4) const unsigned char TlParser::empty_data[sizeof(UInt256)] = {};  // static zero-initialized

TlParser::TlParser(Slice slice) {
  data_len = left_len = slice.size();
  if (is_aligned_pointer<4>(slice.begin())) {
    data = slice.ubegin();
  } else {
    int32 *buf;
    if (data_len <= small_data_array.size() * sizeof(int32)) {
      buf = &small_data_array[0];
    } else {
      LOG(ERROR) << "Unexpected big unaligned data pointer of length " << slice.size() << " at " << slice.begin();
      data_buf = std::make_unique<int32[]>(1 + data_len / sizeof(int32));
      buf = data_buf.get();
    }
    std::memcpy(buf, slice.begin(), slice.size());
    data = reinterpret_cast<unsigned char *>(buf);
  }
}

void TlParser::set_error(const string &error_message) {
  if (error.empty()) {
    CHECK(!error_message.empty());
    error = error_message;
    error_pos = data_len - left_len;
    data = empty_data;
    left_len = 0;
    data_len = 0;
  } else {
    data = empty_data;
    CHECK(error_pos != std::numeric_limits<size_t>::max());
    LOG_CHECK(data_len == 0) << data_len << " " << left_len << " " << data << " " << &empty_data[0] << " " << error_pos
                             << " " << error;
    CHECK(left_len == 0);
  }
}

BufferSlice TlBufferParser::as_buffer_slice(Slice slice) {
  if (is_aligned_pointer<4>(slice.data())) {
    return parent_->from_slice(slice);
  }
  return BufferSlice(slice);
}

}  // namespace td
