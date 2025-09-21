// appconfig.h
//
// Copyright (c) 2021-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <memory>
#include <string>

#include "config.h"

enum AttachmentPrefetchType
{
  AttachmentPrefetchNone = 0,
  AttachmentPrefetchSelected = 1,
  AttachmentPrefetchAll = 2,
};

enum MessageDeleteType
{
  MessageDeleteErase = 1,
  MessageDeleteReplace = 2,
  MessageDeletePrefix = 3,
};

class AppConfig
{
public:
  static void Init();
  static void Cleanup();
  static bool GetBool(const std::string& p_Param);
  static void SetBool(const std::string& p_Param, const bool& p_Value);
  static int GetNum(const std::string& p_Param);
  static void SetNum(const std::string& p_Param, const int& p_Value);
  static std::string GetStr(const std::string& p_Param);
  static void SetStr(const std::string& p_Param, const std::string& p_Value);

private:
  static std::shared_ptr<Config> m_Config;
};
