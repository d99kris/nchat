// profiles.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "profiles.h"

#include <string>

#include "fileutil.h"

void Profiles::Init()
{
  static const int dirVersion = 1;
  const std::string& profilesDir = FileUtil::GetApplicationDir() + "/profiles";
  FileUtil::InitDirVersion(profilesDir, dirVersion);
}

void Profiles::Cleanup()
{
}
