// uistatusview.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nmail is distributed under the MIT license, see LICENSE for details.

#include "uistatusview.h"

#include "apputil.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uimodel.h"

UiStatusView::UiStatusView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiStatusView::Draw()
{
  std::unique_lock<std::mutex> lock(m_ViewMutex);

  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  std::pair<std::string, std::string>& currentChat = m_Model->GetCurrentChat();
  std::string name = m_Model->GetContactListName(currentChat.first, currentChat.second);
  if (!m_Model->GetEmojiEnabled())
  {
    name = StrUtil::Textize(name);
  }

  int statusVPad = 1;
  static int colorPair = UiColorConfig::GetColorPair("status_color");
  static int attribute = UiColorConfig::GetAttribute("status_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  std::string chatStatus = m_Model->GetChatStatus(currentChat.first, currentChat.second);
  std::wstring wstatus = std::wstring(statusVPad, ' ') +
    StrUtil::ToWString(name).substr(0, m_W / 2) + L" " + StrUtil::ToWString(chatStatus);

  static const bool developerMode = AppUtil::GetDeveloperMode();
  if (developerMode)
  {
    wstatus = wstatus + L" " + StrUtil::ToWString(currentChat.second);
  }

  wstatus = StrUtil::TrimPadWString(wstatus, m_W);

  mvwaddnwstr(m_Win, 0, 0, wstatus.c_str(), wstatus.size());

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
