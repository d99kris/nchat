// theme.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "theme.h"

#include <fstream>
#include <iostream>

#include "fileutil.h"
#include "strutil.h"
#include "themes.h"

static const size_t themeCount = sizeof(EmbeddedThemes) / sizeof(EmbeddedThemes[0]);

bool Theme::Install(const std::string& p_Name)
{
  size_t index = 0;
  if (p_Name.empty())
  {
    if (!Select(index)) return false;
  }
  else if (!Find(p_Name, index))
  {
    std::cerr << "error: theme \"" << p_Name << "\" does not exist, run \"nchat --theme\" for a list\n"
      "       of available themes.\n";
    return false;
  }

  const EmbeddedTheme& theme = EmbeddedThemes[index];
  const std::string appDir = FileUtil::GetApplicationDir();
  const std::string colorPath = appDir + "/color.conf";
  const std::string userColorPath = appDir + "/usercolor.conf";
  if (!WriteConf(colorPath, theme.colorData, theme.colorSize)) return false;

  if (!WriteConf(userColorPath, theme.userColorData, theme.userColorSize)) return false;

  std::cout << "Installed theme \"" << theme.name << "\" to " << appDir << "\n";
  return true;
}

bool Theme::Select(size_t& p_Index)
{
  // first theme in index.txt is the default selection
  std::cout << "Themes:\n";
  for (size_t i = 0; i < themeCount; ++i)
  {
    std::cout << (i + 1) << ". " << EmbeddedThemes[i].name << "\n";
  }

  std::cout << "Press ctrl-c to cancel.\n";

  std::cout << "Select theme (1): ";
  std::string line;
  if (!std::getline(std::cin, line))
  {
    std::cout << "\n";
    return false;
  }

  StrUtil::Trim(line);
  if (line.empty())
  {
    p_Index = 0;
    return true;
  }

  const bool isNumber = (line.find_first_not_of("0123456789") == std::string::npos) && (line.size() < 6);
  const size_t number = isNumber ? std::stoul(line) : 0;
  if ((number < 1) || (number > themeCount))
  {
    std::cerr << "error: invalid selection \"" << line << "\".\n";
    return false;
  }

  p_Index = number - 1;
  return true;
}

bool Theme::Find(const std::string& p_Name, size_t& p_Index)
{
  for (size_t i = 0; i < themeCount; ++i)
  {
    if (p_Name == EmbeddedThemes[i].name)
    {
      p_Index = i;
      return true;
    }
  }

  return false;
}

bool Theme::WriteConf(const std::string& p_Path, const unsigned char* p_Data, size_t p_Size)
{
  std::ofstream file(p_Path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(p_Data), p_Size);
  if (!file.good())
  {
    std::cerr << "error: failed to write " << p_Path << "\n";
    return false;
  }

  return true;
}
