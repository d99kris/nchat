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

std::string TimeUtil::GetTimeString(int64_t p_TimeSent, bool p_ShortToday)
{
  time_t rawtime = (time_t)(p_TimeSent / 1000);
  struct tm* timeinfo;
  timeinfo = localtime(&rawtime);

  char senttimestr[64];
  strftime(senttimestr, sizeof(senttimestr), "%H:%M", timeinfo);
  std::string senttime(senttimestr);

  char sentdatestr[64];
  strftime(sentdatestr, sizeof(sentdatestr), "%Y-%m-%d", timeinfo);
  std::string sentdate(sentdatestr);

  if (p_ShortToday)
  {
    time_t nowtime = time(NULL);
    struct tm* nowtimeinfo = localtime(&nowtime);
    char nowdatestr[64];
    strftime(nowdatestr, sizeof(nowdatestr), "%Y-%m-%d", nowtimeinfo);
    std::string nowdate(nowdatestr);

    return (sentdate == nowdate) ? senttime : sentdate + std::string(" ") + senttime;
  }
  else
  {
    return sentdate + std::string(" ") + senttime;
  }
}

void TimeUtil::Sleep(double p_Sec)
{
  usleep(static_cast<useconds_t>(p_Sec * 1000000));
}
