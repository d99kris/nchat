// status.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>

class Status
{
public:
  enum Flag
  {
    FlagNone = 0,
    FlagOffline = (1 << 0),
    FlagOnline = (1 << 1),
    FlagFetching = (1 << 2),
    FlagSending = (1 << 3),
    FlagUpdating = (1 << 4),
    FlagMax = FlagUpdating,
  };

  static void Set(uint32_t p_Flags);
  static void Clear(uint32_t p_Flags);
  static std::string ToString();

private:
  static uint32_t m_Flags;
  static std::mutex m_Mutex;
};
