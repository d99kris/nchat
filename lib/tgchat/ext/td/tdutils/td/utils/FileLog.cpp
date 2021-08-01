//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/FileLog.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/port/StdStreams.h"
#include "td/utils/Slice.h"

#include <limits>

namespace td {

Status FileLog::init(string path, int64 rotate_threshold, bool redirect_stderr) {
  if (path.empty()) {
    return Status::Error("Log file path can't be empty");
  }
  if (path == path_) {
    set_rotate_threshold(rotate_threshold);
    return Status::OK();
  }

  TRY_RESULT(fd, FileFd::open(path, FileFd::Create | FileFd::Write | FileFd::Append));

  fd_.close();
  fd_ = std::move(fd);
  if (!Stderr().empty() && redirect_stderr) {
    fd_.get_native_fd().duplicate(Stderr().get_native_fd()).ignore();
  }

  auto r_path = realpath(path, true);
  if (r_path.is_error()) {
    path_ = std::move(path);
  } else {
    path_ = r_path.move_as_ok();
  }
  TRY_RESULT_ASSIGN(size_, fd_.get_size());
  rotate_threshold_ = rotate_threshold;
  redirect_stderr_ = redirect_stderr;
  return Status::OK();
}

Slice FileLog::get_path() const {
  return path_;
}

vector<string> FileLog::get_file_paths() {
  vector<string> result;
  if (!path_.empty()) {
    result.push_back(path_);
    result.push_back(PSTRING() << path_ << ".old");
  }
  return result;
}

void FileLog::set_rotate_threshold(int64 rotate_threshold) {
  rotate_threshold_ = rotate_threshold;
}

int64 FileLog::get_rotate_threshold() const {
  return rotate_threshold_;
}

void FileLog::append(CSlice cslice, int log_level) {
  Slice slice = cslice;
  while (!slice.empty()) {
    auto r_size = fd_.write(slice);
    if (r_size.is_error()) {
      process_fatal_error(PSLICE() << r_size.error() << " in " << __FILE__ << " at " << __LINE__);
    }
    auto written = r_size.ok();
    size_ += static_cast<int64>(written);
    slice.remove_prefix(written);
  }
  if (log_level == VERBOSITY_NAME(FATAL)) {
    process_fatal_error(cslice);
  }

  if (size_ > rotate_threshold_) {
    auto status = rename(path_, PSLICE() << path_ << ".old");
    if (status.is_error()) {
      process_fatal_error(PSLICE() << status.error() << " in " << __FILE__ << " at " << __LINE__);
    }
    do_rotate();
  }
}

void FileLog::rotate() {
  if (path_.empty()) {
    return;
  }
  do_rotate();
}

void FileLog::do_rotate() {
  auto current_verbosity_level = GET_VERBOSITY_LEVEL();
  SET_VERBOSITY_LEVEL(std::numeric_limits<int>::min());  // to ensure that nothing will be printed to the closed log
  CHECK(!path_.empty());
  fd_.close();
  auto r_fd = FileFd::open(path_, FileFd::Create | FileFd::Truncate | FileFd::Write);
  if (r_fd.is_error()) {
    process_fatal_error(PSLICE() << r_fd.error() << " in " << __FILE__ << " at " << __LINE__);
  }
  fd_ = r_fd.move_as_ok();
  if (!Stderr().empty() && redirect_stderr_) {
    fd_.get_native_fd().duplicate(Stderr().get_native_fd()).ignore();
  }
  size_ = 0;
  SET_VERBOSITY_LEVEL(current_verbosity_level);
}

}  // namespace td
