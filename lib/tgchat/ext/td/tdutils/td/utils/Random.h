//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {

class Random {
 public:
#if TD_HAVE_OPENSSL
  static void secure_bytes(MutableSlice dest);
  static void secure_bytes(unsigned char *ptr, size_t size);
  static int32 secure_int32();
  static int64 secure_int64();
  static uint32 secure_uint32();
  static uint64 secure_uint64();

  // works only for current thread
  static void add_seed(Slice bytes, double entropy = 0);
#endif

  static uint32 fast_uint32();
  static uint64 fast_uint64();

  // distribution is not uniform, min and max are included
  static int fast(int min, int max);

  class Xorshift128plus {
   public:
    explicit Xorshift128plus(uint64 seed);
    Xorshift128plus(uint64 seed_a, uint64 seed_b);
    uint64 operator()();
    int fast(int min, int max);

   private:
    uint64 seed_[2];
  };
};

}  // namespace td
