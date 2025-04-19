// owned_mutex.h
//
// Copyright (c) 2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <mutex>
#include <thread>

class owned_mutex
{
public:
  owned_mutex() = default;

  void lock();
  void unlock();
  bool try_lock();
  bool owns_lock() const;

private:
  std::mutex m_Mutex;
  std::thread::id m_Owner;
};
