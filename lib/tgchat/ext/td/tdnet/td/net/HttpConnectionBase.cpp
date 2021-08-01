//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/HttpConnectionBase.h"

#include "td/net/HttpHeaderCreator.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/detail/PollableFd.h"

namespace td {
namespace detail {

HttpConnectionBase::HttpConnectionBase(State state, SocketFd fd, SslStream ssl_stream, size_t max_post_size,
                                       size_t max_files, int32 idle_timeout)
    : state_(state)
    , fd_(std::move(fd))
    , ssl_stream_(std::move(ssl_stream))
    , max_post_size_(max_post_size)
    , max_files_(max_files)
    , idle_timeout_(idle_timeout) {
  CHECK(state_ != State::Close);

  if (ssl_stream_) {
    read_source_ >> ssl_stream_.read_byte_flow() >> read_sink_;
    write_source_ >> ssl_stream_.write_byte_flow() >> write_sink_;
  } else {
    read_source_ >> read_sink_;
    write_source_ >> write_sink_;
  }
}

void HttpConnectionBase::live_event() {
  if (idle_timeout_ != 0) {
    set_timeout_in(idle_timeout_);
  }
}

void HttpConnectionBase::start_up() {
  Scheduler::subscribe(fd_.get_poll_info().extract_pollable_fd(this));
  reader_.init(read_sink_.get_output(), max_post_size_, max_files_);
  if (state_ == State::Read) {
    current_query_ = make_unique<HttpQuery>();
  }
  live_event();
  yield();
}
void HttpConnectionBase::tear_down() {
  Scheduler::unsubscribe_before_close(fd_.get_poll_info().get_pollable_fd_ref());
  fd_.close();
}

void HttpConnectionBase::write_next(BufferSlice buffer) {
  CHECK(state_ == State::Write);
  write_buffer_.append(std::move(buffer));
  loop();
}

void HttpConnectionBase::write_ok() {
  CHECK(state_ == State::Write);
  current_query_ = make_unique<HttpQuery>();
  state_ = State::Read;
  live_event();
  loop();
}

void HttpConnectionBase::write_error(Status error) {
  CHECK(state_ == State::Write);
  LOG(WARNING) << "Close HTTP connection: " << error;
  state_ = State::Close;
  loop();
}

void HttpConnectionBase::timeout_expired() {
  LOG(INFO) << "Idle timeout expired";

  if (fd_.need_flush_write()) {
    on_error(Status::Error("Write timeout expired"));
  } else if (state_ == State::Read) {
    on_error(Status::Error("Read timeout expired"));
  }

  stop();
}
void HttpConnectionBase::loop() {
  if (can_read(fd_)) {
    LOG(DEBUG) << "Can read from the connection";
    auto r = fd_.flush_read();
    if (r.is_error()) {
      if (!begins_with(r.error().message(), "SSL error {336134278")) {  // if error is not yet outputed
        LOG(INFO) << "Receive flush_read error: " << r.error();
      }
      on_error(Status::Error(r.error().public_message()));
      return stop();
    }
  }
  read_source_.wakeup();

  // TODO: read_next even when state_ == State::Write

  bool want_read = false;
  if (state_ == State::Read) {
    auto res = reader_.read_next(current_query_.get());
    if (res.is_error()) {
      live_event();
      state_ = State::Write;
      LOG(INFO) << res.error();
      HttpHeaderCreator hc;
      hc.init_status_line(res.error().code());
      hc.set_content_size(0);
      write_buffer_.append(hc.finish().ok());
      close_after_write_ = true;
      on_error(Status::Error(res.error().public_message()));
    } else if (res.ok() == 0) {
      state_ = State::Write;
      LOG(DEBUG) << "Send query to handler";
      live_event();
      on_query(std::move(current_query_));
    } else {
      want_read = true;
    }
  }

  write_source_.wakeup();

  if (can_write(fd_)) {
    LOG(DEBUG) << "Can write to the connection";
    auto r = fd_.flush_write();
    if (r.is_error()) {
      LOG(INFO) << "Receive flush_write error: " << r.error();
      on_error(Status::Error(r.error().public_message()));
    }
    if (close_after_write_ && !fd_.need_flush_write()) {
      return stop();
    }
  }

  Status pending_error;
  if (fd_.get_poll_info().get_flags().has_pending_error()) {
    pending_error = fd_.get_pending_error();
  }
  if (pending_error.is_ok() && write_sink_.status().is_error()) {
    pending_error = std::move(write_sink_.status());
  }
  if (pending_error.is_ok() && read_sink_.status().is_error()) {
    pending_error = std::move(read_sink_.status());
  }
  if (pending_error.is_error()) {
    LOG(INFO) << pending_error;
    if (!close_after_write_) {
      on_error(Status::Error(pending_error.public_message()));
    }
    state_ = State::Close;
  }

  if (can_close(fd_)) {
    LOG(DEBUG) << "Can close the connection";
    state_ = State::Close;
  }
  if (state_ == State::Close) {
    LOG_IF(INFO, fd_.need_flush_write()) << "Close nonempty connection";
    LOG_IF(INFO, want_read && (fd_.input_buffer().size() > 0 || current_query_->type_ != HttpQuery::Type::EMPTY))
        << "Close connection while reading request/response";
    return stop();
  }
}

}  // namespace detail
}  // namespace td
