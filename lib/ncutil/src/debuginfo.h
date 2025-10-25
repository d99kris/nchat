// debuginfo.h
//
// Copyright (c) 2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <memory>
#include <string>

#include "config.h"

class DebugInfo
{
public:
  static void Init();
  static void Cleanup();
  static std::string GetStr(const std::string& p_Param);
  static void SetStr(const std::string& p_Param, const std::string& p_Value);

private:
  static std::shared_ptr<Config> m_Config;
};
