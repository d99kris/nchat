// debuginfo.cpp
//
// Copyright (c) 2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "debuginfo.h"

#include <map>

#include "fileutil.h"

std::shared_ptr<Config> DebugInfo::m_Config;

void DebugInfo::Init()
{
  const std::map<std::string, std::string> defaultConfig =
  {
    { "version_used", "" },
  };

  const std::string configPath(FileUtil::GetApplicationDir() + std::string("/debug.info"));
  m_Config.reset(new Config(configPath, defaultConfig));
}

void DebugInfo::Cleanup()
{
  m_Config->Save();
  m_Config.reset();
}

std::string DebugInfo::GetStr(const std::string& p_Param)
{
  return m_Config->Get(p_Param);
}

void DebugInfo::SetStr(const std::string& p_Param, const std::string& p_Value)
{
  m_Config->Set(p_Param, p_Value);
}
