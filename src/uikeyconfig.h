// uikeyconfig.h
//
// Copyright (c) 2019-2023 Kristofer Berggren
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
  static void Init(bool p_MapKeys);
  static void Cleanup();
  static int GetKey(const std::string& p_Param);
  static int GetKeyCode(const std::string& p_KeyName);
  static std::string GetKeyName(int p_KeyCode);
  static int GetOffsettedKeyCode(int p_KeyCode, bool p_IsFunctionKey);
  static std::map<std::string, std::string> GetMap();

private:
  static void InitKeyCodes(bool p_MapKeys);
  static int GetOffsettedKeyCode(int p_KeyCode);
  static int GetVirtualKeyCodeFromOct(const std::string& p_KeyOct);
  static int ReserveVirtualKeyCode();
  static int GetFunctionKeyOffset();

private:
  static Config m_Config;
  static std::map<std::string, int> m_KeyCodes;
};
