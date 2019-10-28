// lockfile.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

class DirLock
{
public:
  DirLock(const std::string& p_DirPath);
  ~DirLock();

  bool IsLocked();

private:
  bool TryLock();
  void Unlock();

private:
  int m_DirFd = -1;
  std::string m_DirPath;
  bool m_IsLocked = false;
};
