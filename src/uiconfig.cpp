// uiconfig.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiconfig.h"

#include <map>

#include "fileutil.h"

Config UiConfig::m_Config;

void UiConfig::Init()
{
  const std::map<std::string, std::string> defaultConfig =
  {
    { "emoji_enabled", "1" },
    { "help_enabled", "1" },
    { "list_enabled", "1" },
    { "top_enabled", "1" },
  };

  const std::string configPath(FileUtil::GetApplicationDir() + std::string("/ui.conf"));
  m_Config = Config(configPath, defaultConfig);
}

void UiConfig::Cleanup()
{
  m_Config.Save();
}

bool UiConfig::GetBool(const std::string& p_Param)
{
  return m_Config.Get(p_Param) == "1";
}

void UiConfig::SetBool(const std::string& p_Param, const bool& p_Value)
{
  m_Config.Set(p_Param, p_Value ? "1" : "0");
}
