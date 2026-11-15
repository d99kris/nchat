// theme.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

class Theme
{
public:
  static bool Install(const std::string& p_Name);

private:
  static bool Select(size_t& p_Index);
  static bool Find(const std::string& p_Name, size_t& p_Index);
  static bool WriteConf(const std::string& p_Path, const unsigned char* p_Data, size_t p_Size);
};
