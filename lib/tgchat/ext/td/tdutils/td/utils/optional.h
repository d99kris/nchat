//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <type_traits>
#include <utility>

namespace td {

template <class T, bool = std::is_copy_constructible<T>::value>
class optional {
 public:
  optional() = default;
  template <class T1,
            std::enable_if_t<!std::is_same<std::decay_t<T1>, optional>::value && std::is_constructible<T, T1>::value,
                             int> = 0>
  optional(T1 &&t) : impl_(std::forward<T1>(t)) {
  }

  optional(const optional &other) {
    if (other) {
      impl_ = Result<T>(other.value());
    }
  }

  optional &operator=(const optional &other) {
    if (other) {
      impl_ = Result<T>(other.value());
    } else {
      impl_ = Result<T>();
    }
    return *this;
  }

  optional(optional &&other) = default;
  optional &operator=(optional &&other) = default;
  ~optional() = default;

  explicit operator bool() const {
    return impl_.is_ok();
  }
  T &value() {
    return impl_.ok_ref();
  }
  const T &value() const {
    return impl_.ok_ref();
  }
  T &operator*() {
    return value();
  }
  T unwrap() {
    CHECK(*this);
    auto res = std::move(value());
    impl_ = {};
    return res;
  }

  template <class... ArgsT>
  void emplace(ArgsT &&... args) {
    impl_.emplace(std::forward<ArgsT>(args)...);
  }

 private:
  Result<T> impl_;
};

template <typename T>
struct optional<T, false> : optional<T, true> {
  optional() = default;

  using optional<T, true>::optional;

  optional(const optional &other) = delete;
  optional &operator=(const optional &other) = delete;
  optional(optional &&) = default;
  optional &operator=(optional &&) = default;
  ~optional() = default;
};

}  // namespace td
