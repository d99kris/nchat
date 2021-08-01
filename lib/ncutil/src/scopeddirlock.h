// scopeddirlock.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

class ScopedDirLock
{
public:
  ScopedDirLock(const std::string& p_DirPath);
  ~ScopedDirLock();

  bool IsLocked();

private:
  bool TryLock();
  void Unlock();

private:
  int m_DirFd = -1;
  std::string m_DirPath;
  bool m_IsLocked = false;
};

class PathLock
{
public:
  static int Lock(const std::string& p_Path);
  static bool Unlock(int p_Fd);
  static int TryLock(const std::string& p_Path);
  static bool TryUnlock(int p_Fd);
};
