// status.h
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

class Status
{
public:
  // keep in sync with constants in gowm.go
  enum Flag
  {
    FlagNone = 0,               // 0x00
    FlagOffline = (1 << 0),     // 0x01
    FlagConnecting = (1 << 1),  // 0x02
    FlagOnline = (1 << 2),      // 0x04
    FlagFetching = (1 << 3),    // 0x08
    FlagSending = (1 << 4),     // 0x10
    FlagUpdating = (1 << 5),    // 0x20
    FlagSyncing = (1 << 6),     // 0x40
    FlagAway = (1 << 7),        // 0x80
  };

  static uint32_t Get(uint32_t p_Mask);
  static void Set(const std::string& p_ProfileId, uint32_t p_Flags);
  static void Clear(const std::string& p_ProfileId, uint32_t p_Flags);
  static std::string ToString(uint32_t p_Mask);

private:
  static void UpdateCombined();

private:
  static uint32_t m_Flags;
  static std::map<std::string, uint32_t> m_ProfileFlags;
  static std::mutex m_Mutex;
};
