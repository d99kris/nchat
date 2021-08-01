//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace td {

template <class T, T empty_val = T()>
class MovableValue {
 public:
  MovableValue() = default;
  MovableValue(T val) : val_(val) {
  }
  MovableValue(MovableValue &&other) : val_(other.val_) {
    other.clear();
  }
  MovableValue &operator=(MovableValue &&other) {
    val_ = other.val_;
    other.clear();
    return *this;
  }
  MovableValue(const MovableValue &) = delete;
  MovableValue &operator=(const MovableValue &) = delete;
  ~MovableValue() = default;

  void clear() {
    val_ = empty_val;
  }
  const T &get() const {
    return val_;
  }

 private:
  T val_ = empty_val;
};

}  // namespace td
