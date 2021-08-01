//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/net/HttpQuery.h"
#include "td/net/HttpReader.h"
#include "td/net/SslStream.h"

#include "td/utils/buffer.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/ByteFlow.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"

namespace td {

namespace detail {

class HttpConnectionBase : public Actor {
 public:
  void write_next(BufferSlice buffer);
  void write_ok();
  void write_error(Status error);

 protected:
  enum class State { Read, Write, Close };
  HttpConnectionBase(State state, SocketFd fd, SslStream ssl_stream, size_t max_post_size, size_t max_files,
                     int32 idle_timeout);

 private:
  State state_;

  BufferedFd<SocketFd> fd_;
  SslStream ssl_stream_;

  ByteFlowSource read_source_{&fd_.input_buffer()};
  ByteFlowSink read_sink_;

  ChainBufferWriter write_buffer_;
  ChainBufferReader write_buffer_reader_ = write_buffer_.extract_reader();
  ByteFlowSource write_source_{&write_buffer_reader_};
  ByteFlowMoveSink write_sink_{&fd_.output_buffer()};

  size_t max_post_size_;
  size_t max_files_;
  int32 idle_timeout_;
  HttpReader reader_;
  unique_ptr<HttpQuery> current_query_;
  bool close_after_write_ = false;

  void live_event();

  void start_up() override;
  void tear_down() override;
  void timeout_expired() override;
  void loop() override;

  virtual void on_query(unique_ptr<HttpQuery> query) = 0;
  virtual void on_error(Status error) = 0;
};

}  // namespace detail
}  // namespace td
