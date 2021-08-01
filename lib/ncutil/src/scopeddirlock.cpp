// scopeddirlock.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "scopeddirlock.h"

#include <fcntl.h>
#include <unistd.h>

#include <sys/file.h>

ScopedDirLock::ScopedDirLock(const std::string& p_DirPath)
  : m_DirPath(p_DirPath)
{
  TryLock();
}

ScopedDirLock::~ScopedDirLock()
{
  Unlock();
}

bool ScopedDirLock::IsLocked()
{
  return m_IsLocked;
}

bool ScopedDirLock::TryLock()
{
  m_DirFd = open(m_DirPath.c_str(), O_RDONLY | O_NOCTTY);
  if (m_DirFd != -1)
  {
    m_IsLocked = (flock(m_DirFd, LOCK_EX | LOCK_NB) == 0);
  }

  return m_IsLocked;
}

void ScopedDirLock::Unlock()
{
  if (m_IsLocked)
  {
    flock(m_DirFd, LOCK_UN | LOCK_NB);
    m_IsLocked = false;
    close(m_DirFd);
    m_DirFd = -1;
  }
}

int PathLock::Lock(const std::string& p_Path)
{
  if (p_Path.empty()) return -1;

  int fd = open(p_Path.c_str(), O_RDONLY | O_NOCTTY);
  if (fd >= 0)
  {
    if (flock(fd, LOCK_EX) == 0)
    {
      return fd;
    }

    close(fd);
  }

  return -1;
}

bool PathLock::Unlock(int p_Fd)
{
  bool rv = false;
  if (p_Fd != -1)
  {
    rv = (flock(p_Fd, LOCK_UN) == 0);
    close(p_Fd);
  }

  return rv;
}

int PathLock::TryLock(const std::string& p_Path)
{
  if (p_Path.empty()) return -1;

  int fd = open(p_Path.c_str(), O_RDONLY | O_NOCTTY);
  if (fd >= 0)
  {
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
    {
      return fd;
    }

    close(fd);
  }

  return -1;
}

bool PathLock::TryUnlock(int p_Fd)
{
  bool rv = false;
  if (p_Fd != -1)
  {
    rv = (flock(p_Fd, LOCK_UN | LOCK_NB) == 0);
    close(p_Fd);
  }

  return rv;
}
