// setuputil.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "setuputil.h"

#include <cstdlib>

#include "appconfig.h"
#include "log.h"
#include "strutil.h"

bool SetupUtil::GetConfigOrEnvFlag(const std::string& p_EnvName)
{
  const std::string configName = StrUtil::ToLower(p_EnvName);

  if (AppConfig::GetBool(configName)) return true;

  const char* envVal = getenv(p_EnvName.c_str());
  if (envVal != nullptr)
  {
    AppConfig::SetBool(configName, true);
    return true;
  }

  return false;
}

bool SetupUtil::HasGui()
{
  if (GetConfigOrEnvFlag("USE_QR_TERMINAL"))
  {
    LOG_DEBUG("has gui 0 (use_qr_terminal)");
    return false;
  }

#ifdef __APPLE__
  bool rv = true;
  LOG_DEBUG("has gui %d (macOS)", rv);
#elif defined(__linux__)
  bool rv = (getenv("DISPLAY") != nullptr) || (getenv("WAYLAND_DISPLAY") != nullptr);
  LOG_DEBUG("has gui %d (linux)", rv);
#else
  bool rv = false;
  LOG_DEBUG("has gui %d (other)", rv);
#endif

  return rv;
}

void SetupUtil::ShowImage(const std::string& p_Path)
{
#ifdef __APPLE__
  std::string cmd = "open \"" + p_Path + "\" &";
#else
  std::string cmd = "xdg-open \"" + p_Path + "\" &";
#endif

  int rv = system(cmd.c_str());
  if (rv != 0)
  {
    LOG_WARNING("show image '%s' failed %d", cmd.c_str(), rv);
  }
  else
  {
    LOG_DEBUG("show image '%s' ok", cmd.c_str());
  }
}
