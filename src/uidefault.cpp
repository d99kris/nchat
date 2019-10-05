// uidefault.cpp
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uidefault.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <sstream>

#include <locale.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <emoji.h>
#include <ncursesw/ncurses.h>

#include "chat.h"
#include "config.h"
#include "message.h"
#include "protocol.h"
#include "util.h"

UiDefault::UiDefault()
  : UiCommon("uidefault")
{
}

UiDefault::~UiDefault()
{
}

std::map<std::string, std::string> UiDefault::GetPrivateConfig()
{
  const std::map<std::string, std::string> privateConfig =
  {
    // general
    {"highlight_bold", "1"},
    {"show_emoji", "1"},
    // layout
    {"list_width", "14"},
  };

  return privateConfig;
}

void UiDefault::PrivateInit()
{
  m_ListWidth = std::stoi(m_Config.Get("list_width"));
}

void UiDefault::SetupWin()
{
  getmaxyx(stdscr, m_ScreenHeight, m_ScreenWidth);
  wclear(stdscr);
  wrefresh(stdscr);

  m_ListBorderWin = newwin(m_ScreenHeight, m_ListWidth + 4, 0, 0);
  wborder(m_ListBorderWin, 0, 0, 0, 0, 0, 0, 0, 0);
  wrefresh(m_ListBorderWin);

  m_ListHeight = m_ScreenHeight - 2;
  m_ListWin = newwin(m_ListHeight, m_ListWidth, 1, 2);
  wrefresh(m_ListWin);

  m_OutHeight = m_ScreenHeight - m_InHeight - 3;
  m_OutWidth = m_ScreenWidth - m_ListWidth - 7;
  
  m_OutBorderWin = newwin(m_ScreenHeight - m_InHeight - 1, m_ScreenWidth - m_ListWidth - 3,
                          0, m_ListWidth + 3);
  wborder(m_OutBorderWin, 0, 0, 0, 0, 0, 0, 0, 0);  
  mvwaddch(m_OutBorderWin, 0, 0, ACS_TTEE);
  wrefresh(m_OutBorderWin);

  m_OutWin = newwin(m_ScreenHeight - m_InHeight - 3, m_ScreenWidth - m_ListWidth -7,
                    1, m_ListWidth + 5);
  wrefresh(m_OutWin);

  m_InWidth = m_ScreenWidth - m_ListWidth - 7;
  
  m_InBorderWin = newwin(m_InHeight + 2, m_ScreenWidth - m_ListWidth - 3,
                         m_ScreenHeight - m_InHeight - 2, m_ListWidth + 3);
  wborder(m_InBorderWin, 0, 0, 0, 0, 0, 0, 0, 0);  
  mvwaddch(m_InBorderWin, 0, 0, ACS_LTEE);
  mvwaddch(m_InBorderWin, 0, m_ScreenWidth - m_ListWidth - 4, ACS_RTEE);
  mvwaddch(m_InBorderWin, m_InHeight + 1, 0, ACS_BTEE);
  wrefresh(m_InBorderWin);

  m_InWin = newwin(m_InHeight, m_ScreenWidth - m_ListWidth - 7,
                   m_ScreenHeight - m_InHeight - 1, m_ListWidth + 5);
  wrefresh(m_InWin);
}  

void UiDefault::CleanupWin()
{
  delwin(m_InWin);
  m_InWin = NULL;
  delwin(m_OutWin);
  m_OutWin = NULL;
  delwin(m_ListWin);
  m_ListWin = NULL;
  delwin(m_InBorderWin);
  m_InBorderWin = NULL;
  delwin(m_OutBorderWin);
  m_OutBorderWin = NULL;
  delwin(m_ListBorderWin);
  m_ListBorderWin = NULL;  
}

void UiDefault::RedrawContactWin()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  werase(m_ListWin);

  int i = 0;
  int current = 0;
  for (auto& chat : m_Chats)
  {
    if (chat.first == m_CurrentChat)
    {
      current = i;
      break;
    }
    i++;
  }
  
  int viewmax = m_ListHeight;
  int chatcount = m_Chats.size();
  int offs = std::min(std::max(0, current - ((viewmax - 1) / 2)), chatcount - viewmax);

  i = 0;
  size_t y = 0;
  for (auto& chat : m_Chats)
  {
    if (i++ < offs)
    {
      continue;
    }
    
    wattron(m_ListWin, (chat.first == m_CurrentChat) ? A_REVERSE : A_NORMAL);
    const std::string& rawName = chat.second.m_Name;
    const std::string& name = m_ShowEmoji ? rawName : emojicpp::textize(rawName);
    
    std::wstring wname = Util::ToWString(name).substr(0, m_ListWidth);
    wname = Util::TrimPadWString(wname, m_ListWidth);

    mvwaddnwstr(m_ListWin, y, 0, wname.c_str(), wname.size());

    if (chat.second.m_IsUnread)
    {
      mvwprintw(m_ListWin, y, m_ListWidth-2, " *");
    }

    wattroff(m_ListWin, (chat.first == m_CurrentChat) ? A_REVERSE : A_NORMAL);
    
    ++y;
  }

  wrefresh(m_ListWin);
}
