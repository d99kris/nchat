// uitopview.cpp
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uitopview.h"

#include "appconfig.h"
#include "apputil.h"
#include "status.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uimodel.h"

UiTopView::UiTopView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiTopView::Draw()
{
  static const bool awayStatusIndication = UiConfig::GetBool("away_status_indication");
  static const uint32_t fullMask = ~static_cast<uint32_t>(0);
  static const uint32_t statusMask = fullMask & (awayStatusIndication ? fullMask : ~Status::FlagAway);

  static uint32_t lastStatus = 0;
  uint32_t status = Status::Get(statusMask);
  m_Dirty |= (status != lastStatus);
  lastStatus = status;

  if (!m_Dirty) return;
  m_Dirty = false;

  m_Model->OnStatusUpdateLocked(status);

  if (!m_Enabled) return;

  curs_set(0);

  int topPadLeft = 1;
  int topPadRight = 1;
  static int colorPair = UiColorConfig::GetColorPair("top_color");
  static int attribute = UiColorConfig::GetAttribute("top_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  // simple proxy config check
  static const std::string statusSuffixStr = []()
  {
    const std::string proxyHost = AppConfig::GetStr("proxy_host");
    const int proxyPort = AppConfig::GetNum("proxy_port");
    const bool proxyEnabled = (!proxyHost.empty() && (proxyPort != 0));
    if (proxyEnabled)
    {
      std::string proxyIndicator = UiConfig::GetStr("proxy_indicator");
      return " " + proxyIndicator;
    }

    return std::string("");
  }();

  static const bool topShowVersion = UiConfig::GetBool("top_show_version");
  static const std::string appNameVersion = AppUtil::GetAppName(topShowVersion);
  const std::string statusStr = Status::ToString(status) + statusSuffixStr;
  std::wstring topWStrLeft = StrUtil::ToWString(std::string(topPadLeft, ' ') + appNameVersion);
  std::wstring topWStrRight = StrUtil::ToWString(statusStr + std::string(topPadRight, ' '));
  int topStrLeftWidth = StrUtil::WStringWidth(topWStrLeft);
  int topStrRightWidth = StrUtil::WStringWidth(topWStrRight);

  std::wstring topWStr = topWStrLeft + std::wstring(m_W - topStrLeftWidth - topStrRightWidth, ' ') + topWStrRight;
  mvwaddnwstr(m_Win, 0, 0, topWStr.c_str(), topWStr.size());

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
