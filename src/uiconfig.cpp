// uiconfig.cpp
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiconfig.h"

#include <map>

#include "fileutil.h"
#include "strutil.h"

Config UiConfig::m_Config;

void UiConfig::Init()
{
  const std::map<std::string, std::string> defaultConfig =
  {
    { "attachment_indicator", "\xF0\x9F\x93\x8E" },
    { "attachment_open_command", "" },
    { "confirm_deletion", "1" },
    { "desktop_notify_active", "0" },
    { "desktop_notify_inactive", "0" },
    { "desktop_notify_command", "" },
    { "downloadable_indicator", "+" },
    { "emoji_enabled", "1" },
    { "entry_height", "4" },
    { "failed_indicator", "\xe2\x9c\x97" },
    { "file_picker_command", "" },
    { "help_enabled", "1" },
    { "home_fetch_all", "0" },
    { "link_open_command", "" },
    { "list_enabled", "1" },
    { "list_width", "14" },
    { "mark_read_on_view", "1" },
    { "message_open_command", "" },
    { "muted_indicate_unread", "1" },
    { "muted_notify_unread", "0" },
    { "muted_position_by_timestamp", "1" },
    { "online_status_share", "1" },
    { "online_status_dynamic", "0" },
    { "proxy_indicator", "\xF0\x9F\x94\x92" },
    { "read_indicator", "\xe2\x9c\x93" },
    { "spell_check_command", "" },
    { "syncing_indicator", "\xe2\x87\x84" },
    { "terminal_bell_active", "0" },
    { "terminal_bell_inactive", "1" },
    { "terminal_title", "" },
    { "top_enabled", "1" },
    { "typing_status_share", "1" },
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

std::string UiConfig::GetStr(const std::string& p_Param)
{
  return m_Config.Get(p_Param);
}

int UiConfig::GetNum(const std::string& p_Param)
{
  const std::string value = m_Config.Get(p_Param);
  if (!StrUtil::IsInteger(value)) return 0;

  return StrUtil::ToInteger(value);
}

void UiConfig::SetNum(const std::string& p_Param, const int& p_Value)
{
  m_Config.Set(p_Param, std::to_string(p_Value));
}
