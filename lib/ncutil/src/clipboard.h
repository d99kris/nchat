// clipboard.h
//
// Copyright (c) 2022-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

class Clipboard
{
public:
  static void SetText(const std::string& p_Text);
  static std::string GetText();

  static bool HasImage();
  static bool GetImage(const std::string& p_Path);
};
