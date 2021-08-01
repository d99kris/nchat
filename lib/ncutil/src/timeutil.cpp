// timeutil.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "timeutil.h"

#include <unistd.h>

#include <sys/time.h>

int64_t TimeUtil::GetCurrentTimeMSec()
{
  struct timeval now;
  gettimeofday(&now, NULL);
  return static_cast<int64_t>((now.tv_sec * 1000) + (now.tv_usec / 1000));
}

void TimeUtil::Sleep(double p_Sec)
{
  usleep(static_cast<useconds_t>(p_Sec * 1000000));
}
