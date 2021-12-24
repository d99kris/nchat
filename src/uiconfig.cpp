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
    { "attachment_indicator", "\xF0\x9F\x93\x8E" },
    { "confirm_deletion", "1" },
    { "emoji_enabled", "1" },
    { "help_enabled", "1" },
    { "home_fetch_all", "0" },
    { "list_enabled", "1" },
    { "muted_indicate_unread", "1" },
    { "muted_notify_unread", "0" },
    { "muted_position_by_timestamp", "1" },
    { "read_indicator", "\xe2\x9c\x93" },
    { "terminal_bell", "1" },
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

std::string UiConfig::GetText(const std::string& p_Param)
{
  return m_Config.Get(p_Param);
}
