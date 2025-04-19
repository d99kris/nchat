// apputil.h
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include "log.h"

#define nc_assert(cond) \
        do { if (!cond) { LOG_ERROR("Assertion failed: %s", #cond); AppUtil::AssertionFailed(); } } while (0)

class AppUtil
{
public:
  static void AssertionFailed();
  static std::string GetAppName(bool p_WithVersion);
  static std::string GetAppVersion();
  static void SetDeveloperMode(bool p_DeveloperMode);
  static bool GetDeveloperMode();
  static void InitCoredump();
  static void InitSignalHandler();
  static void SignalHandler(int p_Signal);

private:
  static bool m_DeveloperMode;
};
