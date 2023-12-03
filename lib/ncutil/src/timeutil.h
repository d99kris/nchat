// timeutil.h
//
// Copyright (c) 2020-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstdint>
#include <string>

class TimeUtil
{
public:
  static int64_t GetCurrentTimeMSec();
  static std::string GetTimeString(int64_t p_TimeSent, bool p_IsExport);
  static std::string GetYearString(int64_t p_TimeSent);
  static void Sleep(double p_Sec);
};
