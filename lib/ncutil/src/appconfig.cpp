// appconfig.cpp
//
// Copyright (c) 2021-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "appconfig.h"

#include <map>

#include "fileutil.h"
#include "strutil.h"

std::shared_ptr<Config> AppConfig::m_Config;

void AppConfig::Init()
{
  const std::map<std::string, std::string> defaultConfig =
  {
    { "assert_abort", "0" },
    { "attachment_prefetch", "1" },
    { "attachment_send_type", "1" },
    { "cache_enabled", "1" },
    { "cache_read_only", "0" },
    { "clipboard_copy_command", "" },
    { "clipboard_has_image_command", "" },
    { "clipboard_paste_command", "" },
    { "clipboard_paste_image_command", "" },
    { "coredump_enabled", "0" },
    { "downloads_dir", "" },
    { "emoji_list_all", "0" },
    { "link_send_preview", "1" },
    { "logdump_enabled", "0" },
    { "mentions_quoted", "1" },
    { "message_delete", "1" },
    { "proxy_host", "" },
    { "proxy_pass", "" },
    { "proxy_port", "" },
    { "proxy_user", "" },
    { "timestamp_iso", "0" },
    { "use_pairing_code", "0" },
    { "use_qr_terminal", "0" },
  };

  const std::string configPath(FileUtil::GetApplicationDir() + std::string("/app.conf"));
  m_Config.reset(new Config(configPath, defaultConfig));
}

void AppConfig::Cleanup()
{
  m_Config->Save();
  m_Config.reset();
}

bool AppConfig::GetBool(const std::string& p_Param)
{
  return m_Config->Get(p_Param) == "1";
}

void AppConfig::SetBool(const std::string& p_Param, const bool& p_Value)
{
  m_Config->Set(p_Param, p_Value ? "1" : "0");
}

int AppConfig::GetNum(const std::string& p_Param)
{
  const std::string value = m_Config->Get(p_Param);
  if (!StrUtil::IsInteger(value)) return 0;

  return StrUtil::ToInteger(value);
}

void AppConfig::SetNum(const std::string& p_Param, const int& p_Value)
{
  m_Config->Set(p_Param, std::to_string(p_Value));
}

std::string AppConfig::GetStr(const std::string& p_Param)
{
  return m_Config->Get(p_Param);
}

void AppConfig::SetStr(const std::string& p_Param, const std::string& p_Value)
{
  m_Config->Set(p_Param, p_Value);
}
