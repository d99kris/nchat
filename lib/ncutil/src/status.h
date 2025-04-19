// status.h
//
// Copyright (c) 2020-2025 Kristofer Berggren
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
  // keep in sync with constants in gowm.go
  enum Flag
  {
    FlagNone = 0,
    FlagOffline = (1 << 0),
    FlagConnecting = (1 << 1),
    FlagOnline = (1 << 2),
    FlagFetching = (1 << 3),
    FlagSending = (1 << 4),
    FlagUpdating = (1 << 5),
    FlagSyncing = (1 << 6),
    FlagAway = (1 << 7),
  };

  static uint32_t Get(uint32_t p_Mask);
  static void Set(uint32_t p_Flags);
  static void Clear(uint32_t p_Flags);
  static std::string ToString(uint32_t p_Mask);

private:
  static uint32_t m_Flags;
  static std::mutex m_Mutex;
};
