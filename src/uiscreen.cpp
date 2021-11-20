// uiscreen.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nmail is distributed under the MIT license, see LICENSE for details.

#include "uiscreen.h"

#include <ncurses.h>

UiScreen::UiScreen()
{
  wclear(stdscr);
  wrefresh(stdscr);
  getmaxyx(stdscr, m_H, m_W);
}

int UiScreen::W()
{
  return m_W;
}

int UiScreen::H()
{
  return m_H;
}
