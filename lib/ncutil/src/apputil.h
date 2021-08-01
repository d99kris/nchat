// apputil.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
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
};
