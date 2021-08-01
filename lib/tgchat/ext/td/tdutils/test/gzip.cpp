//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/buffer.h"
#include "td/utils/ByteFlow.h"
#include "td/utils/Gzip.h"
#include "td/utils/GzipByteFlow.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"
#include "td/utils/tests.h"

static void encode_decode(td::string s) {
  auto r = td::gzencode(s, 2);
  ASSERT_TRUE(!r.empty());
  if (r.empty()) {
    return;
  }
  auto new_s = td::gzdecode(r.as_slice());
  ASSERT_TRUE(!new_s.empty());
  if (new_s.empty()) {
    return;
  }
  ASSERT_EQ(s, new_s.as_slice().str());
}

TEST(Gzip, gzencode_gzdecode) {
  auto str = td::rand_string(0, 127, 1000);
  encode_decode(str);
  str = td::rand_string('a', 'z', 1000000);
  encode_decode(str);
  str = td::string(1000000, 'a');
  encode_decode(str);
}

TEST(Gzip, flow) {
  auto str = td::rand_string('a', 'z', 1000000);
  auto parts = td::rand_split(str);

  td::ChainBufferWriter input_writer;
  auto input = input_writer.extract_reader();
  td::ByteFlowSource source(&input);
  td::GzipByteFlow gzip_flow(td::Gzip::Mode::Encode);
  gzip_flow = td::GzipByteFlow(td::Gzip::Mode::Encode);
  td::ByteFlowSink sink;

  source >> gzip_flow >> sink;

  ASSERT_TRUE(!sink.is_ready());
  for (auto &part : parts) {
    input_writer.append(part);
    source.wakeup();
  }
  ASSERT_TRUE(!sink.is_ready());
  source.close_input(td::Status::OK());
  ASSERT_TRUE(sink.is_ready());
  ASSERT_TRUE(sink.status().is_ok());
  auto res = sink.result()->move_as_buffer_slice().as_slice().str();
  ASSERT_TRUE(!res.empty());
  ASSERT_EQ(td::gzencode(str, 2).as_slice().str(), res);
}
TEST(Gzip, flow_error) {
  auto str = td::rand_string('a', 'z', 1000000);
  auto zip = td::gzencode(str).as_slice().str();
  zip.resize(zip.size() - 1);
  auto parts = td::rand_split(zip);

  auto input_writer = td::ChainBufferWriter();
  auto input = input_writer.extract_reader();
  td::ByteFlowSource source(&input);
  td::GzipByteFlow gzip_flow(td::Gzip::Mode::Decode);
  td::ByteFlowSink sink;

  source >> gzip_flow >> sink;

  ASSERT_TRUE(!sink.is_ready());
  for (auto &part : parts) {
    input_writer.append(part);
    source.wakeup();
  }
  ASSERT_TRUE(!sink.is_ready());
  source.close_input(td::Status::OK());
  ASSERT_TRUE(sink.is_ready());
  ASSERT_TRUE(!sink.status().is_ok());
}

TEST(Gzip, encode_decode_flow) {
  auto str = td::rand_string('a', 'z', 1000000);
  auto parts = td::rand_split(str);
  td::ChainBufferWriter input_writer;
  auto input = input_writer.extract_reader();
  td::ByteFlowSource source(&input);
  td::GzipByteFlow gzip_encode_flow(td::Gzip::Mode::Encode);
  td::GzipByteFlow gzip_decode_flow(td::Gzip::Mode::Decode);
  td::GzipByteFlow gzip_encode_flow2(td::Gzip::Mode::Encode);
  td::GzipByteFlow gzip_decode_flow2(td::Gzip::Mode::Decode);
  td::ByteFlowSink sink;
  source >> gzip_encode_flow >> gzip_decode_flow >> gzip_encode_flow2 >> gzip_decode_flow2 >> sink;

  ASSERT_TRUE(!sink.is_ready());
  for (auto &part : parts) {
    input_writer.append(part);
    source.wakeup();
  }
  ASSERT_TRUE(!sink.is_ready());
  source.close_input(td::Status::OK());
  ASSERT_TRUE(sink.is_ready());
  LOG_IF(ERROR, sink.status().is_error()) << sink.status();
  ASSERT_TRUE(sink.status().is_ok());
  ASSERT_EQ(str, sink.result()->move_as_buffer_slice().as_slice().str());
}
