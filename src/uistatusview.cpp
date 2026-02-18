// uistatusview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uistatusview.h"

#include "apputil.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uimodel.h"

UiStatusView::UiStatusView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiStatusView::Draw()
{
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  curs_set(0);

  std::pair<std::string, std::string>& currentChat = m_Model->GetCurrentChatLocked();
  std::string name = m_Model->GetContactListNameLocked(currentChat.first, currentChat.second, true /*p_AllowId*/,
                                                       true /*p_AllowAlias*/);
  if (!m_Model->GetEmojiEnabledLocked())
  {
    name = StrUtil::Textize(name);
  }

  int statusVPad = 1;
  static int colorPair = UiColorConfig::GetColorPair("status_color");
  static int attribute = UiColorConfig::GetAttribute("status_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  std::wstring wstatus;
  if (currentChat.first.empty() && currentChat.second.empty())
  {
    // Empty status bar until current chat is set
  }
  else
  {
    static bool isMultipleProfiles = m_Model->IsMultipleProfilesLocked();
    std::string profileDisplayName = isMultipleProfiles ? " @ " +
      m_Model->GetProfileDisplayNameLocked(currentChat.first) : "";

    std::string chatStatus = m_Model->GetChatStatusLocked(currentChat.first, currentChat.second);
    wstatus = std::wstring(statusVPad, ' ') +
      StrUtil::ToWString(name).substr(0, m_W / 2) +
      StrUtil::ToWString(profileDisplayName) +
      StrUtil::ToWString(chatStatus);

    static const std::string phoneNumberIndicator = UiConfig::GetStr("phone_number_indicator");
    if (!phoneNumberIndicator.empty())
    {
      static std::string placeholder = "%1";
      static const bool isDynamicIndicator = (phoneNumberIndicator.find(placeholder) != std::string::npos);
      if (isDynamicIndicator)
      {
        std::string dynamicIndicator = phoneNumberIndicator;
        std::string phone = m_Model->GetContactPhoneLocked(currentChat.first, currentChat.second);
        StrUtil::ReplaceString(dynamicIndicator, placeholder, phone);
        wstatus += L" " + StrUtil::ToWString(dynamicIndicator);
      }
      else
      {
        wstatus += L" " + StrUtil::ToWString(phoneNumberIndicator);
      }
    }

    static const bool developerMode = AppUtil::GetDeveloperMode();
    if (developerMode)
    {
      wstatus = wstatus + L" chat " + StrUtil::ToWString(currentChat.second);
      int64_t lastMessageTime = m_Model->GetLastMessageTimeLocked(currentChat.first, currentChat.second);
      wstatus = wstatus + L" time " + StrUtil::ToWString(std::to_string(lastMessageTime));
      std::string phone = m_Model->GetContactPhoneLocked(currentChat.first, currentChat.second);
      if (!phone.empty())
      {
        wstatus = wstatus + L" phone " + StrUtil::ToWString(phone);
      }
    }

    wstatus = StrUtil::TrimPadWString(wstatus, m_W);
  }

  mvwaddnwstr(m_Win, 0, 0, wstatus.c_str(), wstatus.size());

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
