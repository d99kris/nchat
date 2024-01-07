// apputil.h
//
// Copyright (c) 2020-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

class AppUtil
{
public:
  static std::string GetAppNameVersion();
  static std::string GetAppVersion();
  static void SetDeveloperMode(bool p_DeveloperMode);
  static bool GetDeveloperMode();
  static void InitCoredump();
  static void InitSignalHandler();
  static void SignalHandler(int p_Signal);

private:
  static bool m_DeveloperMode;
};
