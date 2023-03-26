// uitopview.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uitopview.h"

#include "apputil.h"
#include "status.h"
#include "uicolorconfig.h"

UiTopView::UiTopView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiTopView::Draw()
{
  static std::string lastStatus;
  std::string status = Status::ToString();
  m_Dirty |= (status != lastStatus);
  lastStatus = status;

  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  curs_set(0);

  int topPadLeft = 1;
  int topPadRight = 1;
  static int colorPair = UiColorConfig::GetColorPair("top_color");
  static int attribute = UiColorConfig::GetAttribute("top_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  static const std::string appNameVersion = AppUtil::GetAppNameVersion();
  std::string topStrLeft = std::string(topPadLeft, ' ') + appNameVersion;
  std::string topStrRight = status + std::string(topPadRight, ' ');
  std::string topStr = topStrLeft + std::string(m_W - topStrLeft.size() - topStrRight.size(), ' ') + topStrRight;
  mvwprintw(m_Win, 0, 0, "%s", topStr.c_str());

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
