// uientryview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
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

  std::wstring input = m_Model->GetEntryStrLocked();
  const int inputPos = m_Model->GetEntryPosLocked();
  std::wstring line;
  std::vector<std::wstring> lines;
  int cx = 0;
  int cy = 0;
  lines = StrUtil::WordWrap(input, m_W, false, false, false, 2, inputPos, cy, cx);

  // Vim visual selection: compute anchor screen coords + selection ordering
  bool selActive = m_Model->GetVimModeLocked() && m_Model->GetVimVisualLocked();
  int selStartY = 0, selStartX = 0, selEndY = 0, selEndX = 0;
  if (selActive)
  {
    int ax = 0, ay = 0;
    std::vector<int> dummyJunk; (void)dummyJunk;
    int anchorPos = m_Model->GetVimVisualAnchorLocked();
    int tcx = 0, tcy = 0;
    StrUtil::WordWrap(input, m_W, false, false, false, 2, anchorPos, tcy, tcx);
    ay = tcy; ax = tcx;
    // order anchor vs cursor in reading order; selection is inclusive of both cells
    if ((ay < cy) || ((ay == cy) && (ax <= cx)))
    {
      selStartY = ay; selStartX = ax; selEndY = cy; selEndX = cx;
    }
    else
    {
      selStartY = cy; selStartX = cx; selEndY = ay; selEndX = ax;
    }
  }

  static int colorPair = UiColorConfig::GetColorPair("entry_color");
  static int attribute = UiColorConfig::GetAttribute("entry_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  int yoffs = (cy < (m_H - 1)) ? 0 : (cy - (m_H - 1));

  for (int i = 0; i < m_H; ++i)
  {
    int lineIdx = i + yoffs;
    if (lineIdx < (int)lines.size())
    {
      line = lines.at(lineIdx).c_str();
      line.erase(std::remove(line.begin(), line.end(), EMOJI_PAD), line.end());
      mvwaddwstr(m_Win, i, 0, line.c_str());

      // Overdraw the selected span on this row with reverse video
      if (selActive && (lineIdx >= selStartY) && (lineIdx <= selEndY))
      {
        int len = (int)line.size();
        int a = (lineIdx == selStartY) ? selStartX : 0;
        int b = (lineIdx == selEndY) ? selEndX : (len - 1);
        a = std::max(0, std::min(a, len - 1));
        b = std::max(0, std::min(b, len - 1));
        if (len > 0 && b >= a)
        {
          std::wstring seg = line.substr(a, b - a + 1);
          wattron(m_Win, attribute | colorPair | A_REVERSE);
          mvwaddnwstr(m_Win, i, a, seg.c_str(), seg.size());
          wattroff(m_Win, A_REVERSE);
        }
      }
    }
  }

  wattroff(m_Win, attribute | colorPair);

  m_CursX = cx;
  m_CursY = (cy - yoffs);

  wmove(m_Win, m_CursY, m_CursX);
  wrefresh(m_Win);
}
