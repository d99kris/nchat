// uilistborderview.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uilistborderview.h"

#include <ncursesw/ncurses.h>

#include "uicolorconfig.h"

UiListBorderView::UiListBorderView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiListBorderView::Draw()
{
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  static int colorPair = UiColorConfig::GetColorPair("listborder_color");
  static int attribute = UiColorConfig::GetAttribute("listborder_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  mvwvline(m_Win, 0, 0, ACS_VLINE, m_H);

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
