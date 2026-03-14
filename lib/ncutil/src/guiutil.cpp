// guiutil.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "guiutil.h"

#include <cstdlib>

#include "appconfig.h"
#include "log.h"

bool GuiUtil::HasGui()
{
  if (AppConfig::GetConfigOrEnvFlag("use_qr_terminal", "USE_QR_TERMINAL"))
  {
    LOG_DEBUG("has gui: false (use_qr_terminal)");
    return false;
  }

#ifdef __APPLE__
  LOG_DEBUG("has gui: true (macOS)");
  return true;
#elif defined(__linux__)
  bool rv = (getenv("DISPLAY") != nullptr) || (getenv("WAYLAND_DISPLAY") != nullptr);
  LOG_DEBUG("has gui: %s (linux)", rv ? "true" : "false");
  return rv;
#else
  LOG_DEBUG("has gui: false (unknown os)");
  return false;
#endif
}

void GuiUtil::ShowImage(const std::string& p_Path)
{
  LOG_DEBUG("show image %s", p_Path.c_str());
#ifdef __APPLE__
  system(("open \"" + p_Path + "\" &").c_str());
#elif defined(__linux__)
  system(("xdg-open \"" + p_Path + "\" &").c_str());
#endif
}
