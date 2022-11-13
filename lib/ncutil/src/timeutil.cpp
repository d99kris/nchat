// timeutil.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
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

  if (p_Short)
  {
    if (tmSent.tm_year == tmNow.tm_year)
    {
      if (tmSent.tm_mon == tmNow.tm_mon)
      {
        if (tmSent.tm_mday == tmNow.tm_mday)
        {
          strftime(tmpstr, sizeof(tmpstr), "%H:%M", &tmSent);
        }
        else if ((tmNow.tm_mday - tmSent.tm_mday) <= 7)
        {
          strftime(tmpstr, sizeof(tmpstr), "%a %H:%M", &tmSent);
        }
        else
        {
          int dlen = snprintf(tmpstr, sizeof(tmpstr), "%d ", tmSent.tm_mday);
          strftime(tmpstr + dlen, sizeof(tmpstr) - dlen, "%b %H:%M", &tmSent);
        }
      }
      else
      {
        int dlen = snprintf(tmpstr, sizeof(tmpstr), "%d ", tmSent.tm_mday);
        strftime(tmpstr + dlen, sizeof(tmpstr) - dlen, "%b %H:%M", &tmSent);
      }
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

void TimeUtil::Sleep(double p_Sec)
{
  usleep(static_cast<useconds_t>(p_Sec * 1000000));
}
