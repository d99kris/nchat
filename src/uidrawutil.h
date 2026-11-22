// uidrawutil.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include <ncurses.h>

class UiDrawUtil
{
public:
  // Draws p_Str at y, x, cluster by cluster as given by TermUtil, up to p_MaxWidth
  // columns (-1 = no limit). Returns the width drawn.
  static int AddWStr(WINDOW* p_Win, int p_Y, int p_X, const std::wstring& p_Str, int p_MaxWidth = -1);

  // Checks that curses uses our wcwidth (static / interposed ncurses), else disables
  // emoji width handling. Call after initscr.
  static void SelfCheck();
};
