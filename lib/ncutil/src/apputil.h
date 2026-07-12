// apputil.h
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include <signal.h>

#include "log.h"

#define nc_assert(cond) \
        do { if (!(cond)) { LOG_ERROR("Assertion failed: %s", #cond); AppUtil::AssertionFailed(); } } while (0)

class AppUtil
{
public:
  static void AssertionFailed();
  static std::string GetAppName(bool p_WithVersion, bool p_WithBranch = false);
  static std::string GetAppVersion();
  static void SetDeveloperMode(bool p_DeveloperMode);
  static bool GetDeveloperMode();
  static void InitCoredump();
  static void InitSignalHandler();
  static void SignalHandler(int p_Signal, siginfo_t* p_SigInfo, void* p_Context);
  static int GetCallstack(void** p_Callstack, int p_MaxSize, void* p_Context);

private:
  static bool m_DeveloperMode;
};
