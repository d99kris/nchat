// apputil.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "apputil.h"
#include "version.h"

bool AppUtil::m_DeveloperMode = false;

std::string AppUtil::GetAppNameVersion()
{
  static std::string nameVersion = "nchat v" + GetAppVersion();
  return nameVersion;
}

std::string AppUtil::GetAppVersion()
{
  static std::string version = NCHAT_VERSION;
  return version;
}

void AppUtil::SetDeveloperMode(bool p_DeveloperMode)
{
  m_DeveloperMode = p_DeveloperMode;
}

bool AppUtil::GetDeveloperMode()
{
  return m_DeveloperMode;
}
