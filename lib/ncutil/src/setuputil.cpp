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
  LOG_DEBUG("has gui 1 (macOS)");
  return true;
#elif defined(__linux__)
  if (!IsCommandAvailable("xdg-open"))
  {
    LOG_DEBUG("has gui 0 (no xdg-open)");
    return false;
  }

  if (IsCommandAvailable("xset"))
  {
    const std::string xsetCmd =
      IsCommandAvailable("timeout") ? "timeout 1s xset q" : "xset q";
    if (system((xsetCmd + " > /dev/null 2>&1").c_str()) == 0)
    {
      LOG_DEBUG("has gui 1 (%s ok)", xsetCmd.c_str());
      return true;
    }

    // x11 not reachable, gui may still be present on a wayland-only session
    bool hasWayland = (getenv("WAYLAND_DISPLAY") != nullptr);
    LOG_DEBUG("has gui %d (%s failed)", hasWayland, xsetCmd.c_str());
    return hasWayland;
  }

  // xset not available for probing, fall back to environment check
  bool hasDisplay = (getenv("DISPLAY") != nullptr) || (getenv("WAYLAND_DISPLAY") != nullptr);
  LOG_DEBUG("has gui %d (env)", hasDisplay);
  return hasDisplay;
#else
  LOG_DEBUG("has gui 0 (other)");
  return false;
#endif
}

bool SetupUtil::IsCommandAvailable(const std::string& p_Cmd)
{
  const std::string cmd = "command -v " + p_Cmd + " > /dev/null 2>&1";
  return (system(cmd.c_str()) == 0);
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
