// uiscreen.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiscreen.h"

#include <algorithm>

#include <ncurses.h>

UiScreen::UiScreen()
{
  wclear(stdscr);
  wrefresh(stdscr);
  getmaxyx(stdscr, m_H, m_W);
  m_H = std::max(m_H, 11);
  m_W = std::max(m_W, 29);
}

int UiScreen::W()
{
  return m_W;
}

int UiScreen::H()
{
  return m_H;
}
