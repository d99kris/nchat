// uicolorconfig.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uicolorconfig.h"

#include <algorithm>
#include <map>
#include <string>

#include <ncurses.h>

#include "fileutil.h"
#include "log.h"
#include "strutil.h"

const static std::string userColor = "usercolor";
static int colorPairId = 0;

void UiColorConfig::Init()
{
  if (has_colors())
  {
    start_color();
    assume_default_colors(-1, -1);
  }

  const std::string defaultSentColor = (COLORS > 8) ? "gray" : "";
  const std::string defaultQuotedColor = (COLORS > 8) ? "gray" : "";
  const std::string defaultAttachmentColor = (COLORS > 8) ? "gray" : "";
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
    { "history_text_quoted_color_bg", "" },
    { "history_text_quoted_color_fg", defaultQuotedColor },
    { "history_text_attachment_color_bg", "" },
    { "history_text_attachment_color_fg", defaultAttachmentColor },
    { "history_text_recv_group_color_bg", "" },
    { "history_text_recv_group_color_fg", "" },

    { "history_name_attr", "bold" },
    { "history_name_attr_selected", "reverse" },
    { "history_name_sent_color_bg", "" },
    { "history_name_sent_color_fg", defaultSentColor },
    { "history_name_recv_color_bg", "" },
    { "history_name_recv_color_fg", "" },
    { "history_name_recv_group_color_bg", "" },
    { "history_name_recv_group_color_fg", "" },

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

  ++colorPairId;
  const int id = colorPairId;
  const int fg = GetColorId(m_Config.Get(p_Param + "_fg"));
  const int bg = GetColorId(m_Config.Get(p_Param + "_bg"));
  init_pair(id, fg, bg);

  LOG_TRACE("color %s id %d fg %d bg %d", p_Param.c_str(), id, fg, bg);

  colorPairs[p_Param] = COLOR_PAIR(id);
  return colorPairs[p_Param];
}

int UiColorConfig::GetUserColorPair(const std::string& p_Param, const std::string& p_UserId)
{
  if (!has_colors()) return 0;

  // palette is a subset of: https://lospec.com/palette-list/st-64-natural
  static std::vector<std::string> defaultUserColors =
  {
    "0x313199", "0x543fe0", "0x8463e0", "0xb896eb", "0xd9baf5", "0xf3e3e3",
    "0xf5d7f3", "0xf5c4f2", "0xe48deb", "0xe063d8", "0xb842a0", "0x8f3370",
    "0x991f2f", "0xe53737", "0xf56d58", "0xf59f7f", "0xf5ccb0", "0xfae7d2",
    "0xf5db93", "0xf5be6c", "0xeb9b54", "0xcc7041", "0x8f4a39", "0x855d30",
    "0xb88c33", "0xe0c03f", "0xebdf42", "0xecf56c", "0xf7fac8", "0xcbf558",
    "0x45e02d", "0x2cb82c", "0x227a2e", "0x338f49", "0x42b86d", "0x51e099",
    "0x7ff5ca", "0xbaf5ef", "0x7ff1f5", "0x42ceeb", "0x258cb8", "0x28628f",
    "0x33408f", "0x496ccc", "0x5897f5", "0x7fbef5",
  };

  static std::vector<std::string> userColors = [&]()
  {
    static std::string path = FileUtil::GetApplicationDir() + std::string("/usercolor.conf");
    std::string data = FileUtil::ReadFile(path);
    if (!data.empty())
    {
      std::vector<std::string> lines = StrUtil::Split(data, '\n');
      lines.erase(std::remove_if(lines.begin(), lines.end(),
                                 [](const std::string& line) { return line.empty(); }),
                  lines.end());
      return lines;
    }
    else
    {
      data = StrUtil::Join(defaultUserColors, "\n");
      FileUtil::WriteFile(path, data);
      return defaultUserColors;
    }
  }();

  static size_t userColorCount = userColors.size();
  if (userColorCount == 0) return 0;

  std::size_t userIdColor = CalcChecksum(p_UserId) % userColorCount;

  static std::map<int, int> colorPairs;

  auto colorPair = colorPairs.find(userIdColor);
  if (colorPair != colorPairs.end()) return colorPair->second;

  ++colorPairId;
  const int id = colorPairId;
  const int fg = GetColorId(userColors.at(userIdColor));
  const int bg = GetColorId(m_Config.Get(p_Param + "_bg"));
  init_pair(id, fg, bg);

  LOG_TRACE("user color %s %d id %d fg %d bg %d", p_UserId.c_str(), userIdColor, id, fg, bg);

  colorPairs[userIdColor] = COLOR_PAIR(id);
  return colorPairs[userIdColor];
}

bool UiColorConfig::IsUserColor(const std::string& p_Param)
{
  return (m_Config.Get(p_Param + "_fg") == userColor) ||
         (m_Config.Get(p_Param + "_bg") == userColor);
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

size_t UiColorConfig::CalcChecksum(const std::string& p_Str)
{
  size_t sum = 0;
  std::for_each(p_Str.begin(), p_Str.end(), [&](char ch) { sum += (size_t)ch; });
  return sum;
}

Config UiColorConfig::m_Config;
