// guiutil.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

class GuiUtil
{
public:
  static bool HasGui();
  static void ShowImage(const std::string& p_Path);
};
