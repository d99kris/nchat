// sysutil.h
//
// Copyright (c) 2024-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#define UNUSED(x) SysUtil::Unused(x)

class SysUtil
{
public:
  static std::string GetCompiler();
  static std::string GetGo(const std::string& p_GoVersion);
  static std::string GetOsArch();
  static bool IsSupportedLibc();
  static int System(const std::string& p_Cmd);

  template<typename T>
  static inline void Unused(const T& p_Arg)
  {
    (void)p_Arg;
  }
};
