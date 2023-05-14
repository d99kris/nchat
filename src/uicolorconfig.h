// uicolorconfig.h
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstdint>
#include <string>

#include "config.h"

class UiColorConfig
{
public:
  static void Init();
  static void Cleanup();
  static int GetColorPair(const std::string& p_Param); // ex: "top_color"
  static int GetUserColorPair(const std::string& p_Param, const std::string& p_UserId);
  static bool IsUserColor(const std::string& p_Param); // ex: "top_color"
  static int GetAttribute(const std::string& p_Param); // ex: "top_attr"

private:
  static int GetColorId(const std::string& p_Str);
  static void HexToRGB(const std::string p_Str, uint32_t& p_R, uint32_t& p_G, uint32_t& p_B);
  static size_t CalcChecksum(const std::string& p_Str);

private:
  static Config m_Config;
};
