// uikeyconfig.h
//
// Copyright (c) 2019-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include "config.h"

#define KEY_TAB 9
#define KEY_RETURN 10
#define KEY_SPACE 32
#define KEY_DELETE 127

class UiKeyConfig
{
public:
  static void Init();
  static void Cleanup();
  static int GetKey(const std::string& p_Param);

private:
  static int GetKeyCode(const std::string& p_KeyName);
  static int ReserveVirtualKeyCode();

private:
  static Config m_Config;
};
