// status.cpp
//
// Copyright (c) 2020-2023 Kristofer Berggren
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

std::string Status::ToString()
{
  std::unique_lock<std::mutex> lock(m_Mutex);

  if (m_Flags & FlagSyncing) return "Syncing";
  if (m_Flags & FlagFetching) return "Fetching";
  if (m_Flags & FlagSending) return "Sending";
  if (m_Flags & FlagUpdating) return "Updating";
  if (m_Flags & FlagOnline) return "Online";

  return "Offline";
}
