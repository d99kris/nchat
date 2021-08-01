// apputil.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "apputil.h"

std::string AppUtil::GetAppNameVersion()
{
  static std::string nameVersion = "nchat v" + GetAppVersion();
  return nameVersion;
}

std::string AppUtil::GetAppVersion()
{
#ifdef NCHAT_PROJECT_VERSION
  static std::string version = "" NCHAT_PROJECT_VERSION;
#else
  static std::string version = "0.00";
#endif
  return version;
}
