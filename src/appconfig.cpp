// appconfig.cpp
//
// Copyright (c) 2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "appconfig.h"

#include <map>

#include "fileutil.h"

Config AppConfig::m_Config;

void AppConfig::Init()
{
  const std::map<std::string, std::string> defaultConfig =
  {
    { "cache_enabled", "1" },
    { "confirm_deletion", "1" },
  };

  const std::string configPath(FileUtil::GetApplicationDir() + std::string("/app.conf"));
  m_Config = Config(configPath, defaultConfig);
}

void AppConfig::Cleanup()
{
  m_Config.Save();
}

bool AppConfig::GetBool(const std::string& p_Param)
{
  return m_Config.Get(p_Param) == "1";
}

void AppConfig::SetBool(const std::string& p_Param, const bool& p_Value)
{
  m_Config.Set(p_Param, p_Value ? "1" : "0");
}
