// status.cpp
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "status.h"

uint32_t Status::m_Flags = 0;
std::map<std::string, uint32_t> Status::m_ProfileFlags;
std::mutex Status::m_Mutex;

uint32_t Status::Get(uint32_t p_Mask)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  return m_Flags & p_Mask;
}

void Status::Set(const std::string& p_ProfileId, uint32_t p_Flags)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  m_ProfileFlags[p_ProfileId] |= p_Flags;
  UpdateCombined();
}

void Status::Clear(const std::string& p_ProfileId, uint32_t p_Flags)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  m_ProfileFlags[p_ProfileId] &= ~p_Flags;
  UpdateCombined();
}

std::string Status::ToString(uint32_t p_Flags)
{
  if (p_Flags & FlagSyncing) return "Syncing";
  if (p_Flags & FlagFetching) return "Fetching";
  if (p_Flags & FlagSending) return "Sending";
  if (p_Flags & FlagUpdating) return "Updating";
  if (p_Flags & FlagAway) return "Away";
  if (p_Flags & FlagOnline) return "Online";
  if (p_Flags & FlagConnecting) return "Connecting";

  return "Offline";
}

void Status::UpdateCombined()
{
  m_Flags = m_ProfileFlags.begin()->second;
  if (m_ProfileFlags.size() > 1)
  {
    for (auto it = std::next(m_ProfileFlags.begin()); it != m_ProfileFlags.end(); ++it)
    {
      m_Flags |= it->second;
    }
  }
}
