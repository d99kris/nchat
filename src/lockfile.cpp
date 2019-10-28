// lockfile.cpp
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "lockfile.h"

#include <unistd.h>

#include <sys/file.h>

DirLock::DirLock(const std::string& p_DirPath)
  : m_DirPath(p_DirPath)
{
  TryLock();
}

DirLock::~DirLock()
{
  Unlock();
}

bool DirLock::IsLocked()
{
  return m_IsLocked;
}

bool DirLock::TryLock()
{
  m_DirFd = open(m_DirPath.c_str(), O_RDONLY | O_NOCTTY);
  if (m_DirFd != -1)
  {
    m_IsLocked = (flock(m_DirFd, LOCK_EX | LOCK_NB) == 0);
  }

  return m_IsLocked;
}

void DirLock::Unlock()
{
  if (m_IsLocked)
  {
    flock(m_DirFd, LOCK_UN | LOCK_NB);
    m_IsLocked = false;
    close(m_DirFd);
    m_DirFd = -1;
  }
}
