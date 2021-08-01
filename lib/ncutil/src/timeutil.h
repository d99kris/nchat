// timeutil.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstdint>

class TimeUtil
{
public:
  static int64_t GetCurrentTimeMSec();
  static void Sleep(double p_Sec);
};
