// uicolorconfig.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uicolorconfig.h"

#include <map>
#include <string>

#include <ncurses.h>

#include "fileutil.h"
#include "log.h"
#include "strutil.h"

void UiColorConfig::Init()
{
  if (has_colors())
  {
    start_color();
    assume_default_colors(-1, -1);
  }

  const std::string defaultSentColor = (COLORS > 8) ? "gray" : "";
  const std::map<std::string, std::string> defaultConfig =
  {
    { "top_attr", "reverse" },
    { "top_color_bg", "" },
    { "top_color_fg", "" },
    { "help_attr", "reverse" },
    { "help_color_bg", "black" },
    { "help_color_fg", "white" },
    { "entry_attr", "" },
    { "entry_color_bg", "" },
    { "entry_color_fg", "" },
    { "status_attr", "reverse" },
    { "status_color_bg", "" },
    { "status_color_fg", "" },
    { "list_attr", "" },
    { "list_attr_selected", "bold" },
    { "list_color_bg", "" },
    { "list_color_fg", "" },
    { "listborder_attr", "" },
    { "listborder_color_bg", "" },
    { "listborder_color_fg", "" },

    { "history_text_attr", "" },
    { "history_text_attr_selected", "reverse" },
    { "history_text_sent_color_bg", "" },
    { "history_text_sent_color_fg", defaultSentColor },
    { "history_text_recv_color_bg", "" },
    { "history_text_recv_color_fg", "" },

    { "history_name_attr", "bold" },
    { "history_name_attr_selected", "reverse" },
    { "history_name_sent_color_bg", "" },
    { "history_name_sent_color_fg", defaultSentColor },
    { "history_name_recv_color_bg", "" },
    { "history_name_recv_color_fg", "" },

    { "dialog_attr", "" },
    { "dialog_attr_selected", "reverse" },
    { "dialog_color_bg", "" },
    { "dialog_color_fg", "" },
  };

  const std::string configPath(FileUtil::GetApplicationDir() + std::string("/color.conf"));
  m_Config = Config(configPath, defaultConfig);
}

void UiColorConfig::Cleanup()
{
  m_Config.Save();
}

int UiColorConfig::GetColorPair(const std::string& p_Param)
{
  if (!has_colors()) return 0;

  static std::map<std::string, int> colorPairs;

  auto colorPair = colorPairs.find(p_Param);
  if (colorPair != colorPairs.end()) return colorPair->second;

  static int colorPairId = 0;
  ++colorPairId;
  const int id = colorPairId;
  const int fg = GetColorId(m_Config.Get(p_Param + "_fg"));
  const int bg = GetColorId(m_Config.Get(p_Param + "_bg"));
  init_pair(id, fg, bg);

  LOG_TRACE("color %s id %d fg %d bg %d", p_Param.c_str(), id, fg, bg);

  colorPairs[p_Param] = COLOR_PAIR(id);
  return colorPairs[p_Param];
}

int UiColorConfig::GetAttribute(const std::string& p_Param)
{
  static const std::map<std::string, int> attributes =
  {
    { "normal", A_NORMAL },
    { "underline", A_UNDERLINE },
    { "reverse", A_REVERSE },
    { "bold", A_BOLD },
    { "italic", A_ITALIC },
  };

  auto attribute = attributes.find(m_Config.Get(p_Param));
  if (attribute != attributes.end()) return attribute->second;

  return A_NORMAL;
}

int UiColorConfig::GetColorId(const std::string& p_Str)
{
  static const std::map<std::string, int> standardColors = []()
  {
    std::map<std::string, int> colors;
    const std::map<std::string, int> basicColors =
    {
      { "black", COLOR_BLACK },
      { "red", COLOR_RED },
      { "green", COLOR_GREEN },
      { "yellow", COLOR_YELLOW },
      { "blue", COLOR_BLUE },
      { "magenta", COLOR_MAGENTA },
      { "cyan", COLOR_CYAN },
      { "white", COLOR_WHITE },
    };
    colors.insert(basicColors.begin(), basicColors.end());

    if (COLORS > 8)
    {
      const int BRIGHT = 8;
      const std::map<std::string, int> extendedColors =
      {
        { "gray", BRIGHT | COLOR_BLACK },
        { "bright_black", BRIGHT | COLOR_BLACK },
        { "bright_red", BRIGHT | COLOR_RED },
        { "bright_green", BRIGHT | COLOR_GREEN },
        { "bright_yellow", BRIGHT | COLOR_YELLOW },
        { "bright_blue", BRIGHT | COLOR_BLUE },
        { "bright_magenta", BRIGHT | COLOR_MAGENTA },
        { "bright_cyan", BRIGHT | COLOR_CYAN },
        { "bright_white", BRIGHT | COLOR_WHITE },
      };
      colors.insert(extendedColors.begin(), extendedColors.end());
    }

    return colors;
  }();

  if (p_Str.empty()) return -1;

  // hex
  if ((p_Str.size() == 8) && (p_Str.substr(0, 2) == "0x"))
  {
    if (!can_change_color())
    {
      LOG_WARNING("terminal cannot set custom hex colors, skipping \"%s\"", p_Str.c_str());
      return -1;
    }

    uint32_t r = 0, g = 0, b = 0;
    HexToRGB(p_Str, r, g, b);
    if ((r <= 255) && (g <= 255) && (b <= 255))
    {
      static int colorId = 31;
      colorId++;
      if (colorId > COLORS)
      {
        LOG_WARNING("max number of colors (%d) already defined, skipping \"%s\"", p_Str.c_str());
        return -1;
      }

      init_color(colorId, ((r * 1000) / 255), ((g * 1000) / 255), ((b * 1000) / 255));
      return colorId;
    }

    LOG_WARNING("invalid color hex code \"%s\"", p_Str.c_str());
    return -1;
  }

  // name
  std::map<std::string, int>::const_iterator standardColor = standardColors.find(p_Str);
  if (standardColor != standardColors.end())
  {
    return standardColor->second;
  }

  // id
  if (StrUtil::IsInteger(p_Str))
  {
    int32_t id = StrUtil::ToInteger(p_Str);
    return id;
  }

  LOG_WARNING("unsupported color string \"%s\"", p_Str.c_str());
  return -1;
}

void UiColorConfig::HexToRGB(const std::string p_Str, uint32_t& p_R, uint32_t& p_G, uint32_t& p_B)
{
  std::stringstream ss(p_Str);
  int val;
  ss >> std::hex >> val;

  p_R = (val / 0x10000);
  p_G = (val / 0x100) % 0x100;
  p_B = (val % 0x100);
}

Config UiColorConfig::m_Config;
