// apputil.cpp
//
// Copyright (c) 2020-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "apputil.h"

#include <unistd.h>

#include <sys/resource.h>

#include "log.h"
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

void AppUtil::InitCoredump()
{
  struct rlimit lim;
  int rv = 0;
  rv = getrlimit(RLIMIT_CORE, &lim);
  if (rv != 0)
  {
    LOG_WARNING("getrlimit failed %d errno %d", rv, errno);
  }
  else
  {
    lim.rlim_cur = lim.rlim_max;
    rv = setrlimit(RLIMIT_CORE, &lim);
    if (rv != 0)
    {
      LOG_WARNING("setrlimit failed %d errno %d", rv, errno);
    }
    else
    {
      LOG_DEBUG("setrlimit cur %llu max %llu", lim.rlim_cur, lim.rlim_max);
    }
  }

#ifdef __APPLE__
  rv = access("/cores", W_OK);
  if (rv == -1)
  {
    LOG_WARNING("/cores is not writable");
  }
#endif
}
