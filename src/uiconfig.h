// uiconfig.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include "config.h"

enum AttachmentPrefetch
{
  AttachmentPrefetchNone = 0,
  AttachmentPrefetchSelected = 1,
  AttachmentPrefetchAll = 2,
};

class UiConfig
{
public:
  static void Init();
  static void Cleanup();
  static bool GetBool(const std::string& p_Param);
  static void SetBool(const std::string& p_Param, const bool& p_Value);
  static std::string GetStr(const std::string& p_Param);
  static int GetNum(const std::string& p_Param);

private:
  static Config m_Config;
};
