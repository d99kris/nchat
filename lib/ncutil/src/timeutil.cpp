// timeutil.cpp
//
// Copyright (c) 2020-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "timeutil.h"

#include <ctime>

#include <unistd.h>

#include <sys/time.h>

int64_t TimeUtil::GetCurrentTimeMSec()
{
  struct timeval now;
  gettimeofday(&now, NULL);
  return static_cast<int64_t>((now.tv_sec * 1000) + (now.tv_usec / 1000));
}

std::string TimeUtil::GetTimeString(int64_t p_TimeSent, bool p_Short)
{
  time_t timeSent = (time_t)(p_TimeSent / 1000);
  struct tm tmSent;
  localtime_r(&timeSent, &tmSent);
  time_t timeNow = time(NULL);
  struct tm tmNow;
  localtime_r(&timeNow, &tmNow);
  char tmpstr[32] = { 0 };
  static int64_t useWeekdayMaxAge = (6 * 24 * 3600);

  if (p_Short)
  {
    if ((tmSent.tm_year == tmNow.tm_year) && (tmSent.tm_mon == tmNow.tm_mon) && (tmSent.tm_mday == tmNow.tm_mday))
    {
      strftime(tmpstr, sizeof(tmpstr), "%H:%M", &tmSent);
    }
    else if ((timeNow - timeSent) <= useWeekdayMaxAge)
    {
      strftime(tmpstr, sizeof(tmpstr), "%a %H:%M", &tmSent);
    }
    else if (tmSent.tm_year == tmNow.tm_year)
    {
      int dlen = snprintf(tmpstr, sizeof(tmpstr), "%d ", tmSent.tm_mday);
      strftime(tmpstr + dlen, sizeof(tmpstr) - dlen, "%b %H:%M", &tmSent);
    }
    else
    {
      int dlen = snprintf(tmpstr, sizeof(tmpstr), "%d ", tmSent.tm_mday);
      strftime(tmpstr + dlen, sizeof(tmpstr) - dlen, "%b %Y %H:%M", &tmSent);
    }
  }
  else
  {
    int dlen = snprintf(tmpstr, sizeof(tmpstr), "%d ", tmSent.tm_mday);
    strftime(tmpstr + dlen, sizeof(tmpstr) - dlen, "%b %Y %H:%M", &tmSent);
  }

  return std::string(tmpstr);
}

std::string TimeUtil::GetYearString(int64_t p_TimeSent)
{
  time_t timeSent = (time_t)(p_TimeSent / 1000);
  struct tm tmSent;
  localtime_r(&timeSent, &tmSent);
  time_t timeNow = time(NULL);
  struct tm tmNow;
  localtime_r(&timeNow, &tmNow);
  char tmpstr[32] = { 0 };

  strftime(tmpstr, sizeof(tmpstr), "%Y", &tmSent);

  return std::string(tmpstr);
}

void TimeUtil::Sleep(double p_Sec)
{
  usleep(static_cast<useconds_t>(p_Sec * 1000000));
}
