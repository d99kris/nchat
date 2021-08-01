//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

#if TD_MSVC
#include <intrin.h>
#endif

#ifdef bswap32
#undef bswap32
#endif

#ifdef bswap64
#undef bswap64
#endif

namespace td {

int32 count_leading_zeroes32(uint32 x);
int32 count_leading_zeroes64(uint64 x);
int32 count_trailing_zeroes32(uint32 x);
int32 count_trailing_zeroes64(uint64 x);
uint32 bswap32(uint32 x);
uint64 bswap64(uint64 x);
int32 count_bits32(uint32 x);
int32 count_bits64(uint64 x);

inline uint32 bits_negate32(uint32 x) {
  return ~x + 1;
}

inline uint64 bits_negate64(uint64 x) {
  return ~x + 1;
}

inline uint32 lower_bit32(uint32 x) {
  return x & bits_negate32(x);
}

inline uint64 lower_bit64(uint64 x) {
  return x & bits_negate64(x);
}

//TODO: optimize
inline int32 count_leading_zeroes_non_zero32(uint32 x) {
  DCHECK(x != 0);
  return count_leading_zeroes32(x);
}
inline int32 count_leading_zeroes_non_zero64(uint64 x) {
  DCHECK(x != 0);
  return count_leading_zeroes64(x);
}
inline int32 count_trailing_zeroes_non_zero32(uint32 x) {
  DCHECK(x != 0);
  return count_trailing_zeroes32(x);
}
inline int32 count_trailing_zeroes_non_zero64(uint64 x) {
  DCHECK(x != 0);
  return count_trailing_zeroes64(x);
}

//
// Platform specific implementation
//
#if TD_MSVC

inline int32 count_leading_zeroes32(uint32 x) {
  unsigned long res = 0;
  if (_BitScanReverse(&res, x)) {
    return 31 - res;
  }
  return 32;
}

inline int32 count_leading_zeroes64(uint64 x) {
#if defined(_M_X64)
  unsigned long res = 0;
  if (_BitScanReverse64(&res, x)) {
    return 63 - res;
  }
  return 64;
#else
  if ((x >> 32) == 0) {
    return count_leading_zeroes32(static_cast<uint32>(x)) + 32;
  } else {
    return count_leading_zeroes32(static_cast<uint32>(x >> 32));
  }
#endif
}

inline int32 count_trailing_zeroes32(uint32 x) {
  unsigned long res = 0;
  if (_BitScanForward(&res, x)) {
    return res;
  }
  return 32;
}

inline int32 count_trailing_zeroes64(uint64 x) {
#if defined(_M_X64)
  unsigned long res = 0;
  if (_BitScanForward64(&res, x)) {
    return res;
  }
  return 64;
#else
  if (static_cast<uint32>(x) == 0) {
    return count_trailing_zeroes32(static_cast<uint32>(x >> 32)) + 32;
  } else {
    return count_trailing_zeroes32(static_cast<uint32>(x));
  }
#endif
}

inline uint32 bswap32(uint32 x) {
  return _byteswap_ulong(x);
}

inline uint64 bswap64(uint64 x) {
  return _byteswap_uint64(x);
}

inline int32 count_bits32(uint32 x) {
  return __popcnt(x);
}

inline int32 count_bits64(uint64 x) {
#if defined(_M_X64)
  return static_cast<int32>(__popcnt64(x));
#else
  return count_bits32(static_cast<uint32>(x >> 32)) + count_bits32(static_cast<uint32>(x));
#endif
}

#elif TD_INTEL

inline int32 count_leading_zeroes32(uint32 x) {
  unsigned __int32 res = 0;
  if (_BitScanReverse(&res, x)) {
    return 31 - res;
  }
  return 32;
}

inline int32 count_leading_zeroes64(uint64 x) {
#if defined(_M_X64) || defined(__x86_64__)
  unsigned __int32 res = 0;
  if (_BitScanReverse64(&res, x)) {
    return 63 - res;
  }
  return 64;
#else
  if ((x >> 32) == 0) {
    return count_leading_zeroes32(static_cast<uint32>(x)) + 32;
  } else {
    return count_leading_zeroes32(static_cast<uint32>(x >> 32));
  }
#endif
}

inline int32 count_trailing_zeroes32(uint32 x) {
  unsigned __int32 res = 0;
  if (_BitScanForward(&res, x)) {
    return res;
  }
  return 32;
}

inline int32 count_trailing_zeroes64(uint64 x) {
#if defined(_M_X64) || defined(__x86_64__)
  unsigned __int32 res = 0;
  if (_BitScanForward64(&res, x)) {
    return res;
  }
  return 64;
#else
  if (static_cast<uint32>(x) == 0) {
    return count_trailing_zeroes32(static_cast<uint32>(x >> 32)) + 32;
  } else {
    return count_trailing_zeroes32(static_cast<uint32>(x));
  }
#endif
}

inline uint32 bswap32(uint32 x) {
  return _bswap(static_cast<int>(x));
}

inline uint64 bswap64(uint64 x) {
  return _bswap64(static_cast<__int64>(x));
}

inline int32 count_bits32(uint32 x) {
  return _popcnt32(static_cast<int>(x));
}

inline int32 count_bits64(uint64 x) {
  return _popcnt64(static_cast<__int64>(x));
}

#else

inline int32 count_leading_zeroes32(uint32 x) {
  if (x == 0) {
    return 32;
  }
  return __builtin_clz(x);
}

inline int32 count_leading_zeroes64(uint64 x) {
  if (x == 0) {
    return 64;
  }
  return __builtin_clzll(x);
}

inline int32 count_trailing_zeroes32(uint32 x) {
  if (x == 0) {
    return 32;
  }
  return __builtin_ctz(x);
}

inline int32 count_trailing_zeroes64(uint64 x) {
  if (x == 0) {
    return 64;
  }
  return __builtin_ctzll(x);
}

inline uint32 bswap32(uint32 x) {
  return __builtin_bswap32(x);
}

inline uint64 bswap64(uint64 x) {
  return __builtin_bswap64(x);
}

inline int32 count_bits32(uint32 x) {
  return __builtin_popcount(x);
}

inline int32 count_bits64(uint64 x) {
  return __builtin_popcountll(x);
}

#endif

}  // namespace td
