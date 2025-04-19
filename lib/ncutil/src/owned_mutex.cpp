// owned_mutex.cpp
//
// Copyright (c) 2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "owned_mutex.h"

#include "apputil.h"

void owned_mutex::lock()
{
  m_Mutex.lock();
  m_Owner = std::this_thread::get_id();
}

void owned_mutex::unlock()
{
  m_Owner = std::thread::id();
  m_Mutex.unlock();
}

bool owned_mutex::try_lock()
{
  if (m_Mutex.try_lock())
  {
    m_Owner = std::this_thread::get_id();
    return true;
  }

  return false;
}

bool owned_mutex::owns_lock() const
{
  return (m_Owner == std::this_thread::get_id());
}
