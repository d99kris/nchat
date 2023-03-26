// uientryview.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uientryview.h"

#include <algorithm>

#include "strutil.h"
#include "uicolorconfig.h"
#include "uimodel.h"

UiEntryView::UiEntryView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiEntryView::Draw()
{
  if (!m_Enabled) return;

  if (!m_Dirty)
  {
    wmove(m_Win, m_CursY, m_CursX);
    wrefresh(m_Win);
    return;
  }

  m_Dirty = false;

  curs_set(0);

  std::wstring input = m_Model->GetEntryStr();
  const int inputPos = m_Model->GetEntryPos();
  std::wstring line;
  std::vector<std::wstring> lines;
  int cx = 0;
  int cy = 0;
  lines = StrUtil::WordWrap(input, m_W, false, false, false, 2, inputPos, cy, cx);

  static int colorPair = UiColorConfig::GetColorPair("entry_color");
  static int attribute = UiColorConfig::GetAttribute("entry_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  int yoffs = (cy < (m_H - 1)) ? 0 : (cy - (m_H - 1));

  for (int i = 0; i < m_H; ++i)
  {
    if ((i + yoffs) < (int)lines.size())
    {
      line = lines.at(i + yoffs).c_str();
      line.erase(std::remove(line.begin(), line.end(), EMOJI_PAD), line.end());
      mvwaddwstr(m_Win, i, 0, line.c_str());
    }
  }

  wattroff(m_Win, attribute | colorPair);

  m_CursX = cx;
  m_CursY = (cy - yoffs);

  wmove(m_Win, m_CursY, m_CursX);
  wrefresh(m_Win);
}
