// status.cpp
//
// Copyright (c) 2020-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "status.h"

uint32_t Status::m_Flags = 0;
std::mutex Status::m_Mutex;

uint32_t Status::Get()
{
  return m_Flags;
}

void Status::Set(uint32_t p_Flags)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  m_Flags |= p_Flags;
}

void Status::Clear(uint32_t p_Flags)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  m_Flags &= ~p_Flags;
}

std::string Status::ToString(uint32_t p_Mask)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  const uint32_t maskedFlags = m_Flags & p_Mask;

  if (maskedFlags & FlagSyncing) return "Syncing";
  if (maskedFlags & FlagFetching) return "Fetching";
  if (maskedFlags & FlagSending) return "Sending";
  if (maskedFlags & FlagUpdating) return "Updating";
  if (maskedFlags & FlagAway) return "Away";
  if (maskedFlags & FlagOnline) return "Online";
  if (maskedFlags & FlagConnecting) return "Connecting";

  return "Offline";
}
