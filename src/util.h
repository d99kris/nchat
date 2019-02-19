// util.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>

class Util
{
public:
  static std::string GetConfigDir();
  static int GetKeyCode(const std::string& p_KeyName);
  static std::vector<std::string> WordWrap(std::string p_Text, unsigned p_LineLength);
  static std::string ToString(const std::wstring& p_WStr);
  static std::wstring ToWString(const std::string& p_Str);
};

