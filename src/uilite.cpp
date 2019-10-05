// uilite.cpp
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uilite.h"

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

UiLite::UiLite()
  : UiCommon("uilite")
{
}

UiLite::~UiLite()
{
}

std::map<std::string, std::string> UiLite::GetPrivateConfig()
{
  const std::map<std::string, std::string> privateConfig =
  {
    // general
    {"highlight_bold", "0"},
    {"show_emoji", "0"},
  };

  return privateConfig;
}

void UiLite::PrivateInit()
{
}

void UiLite::SetupWin()
{
  getmaxyx(stdscr, m_ScreenHeight, m_ScreenWidth);
  wclear(stdscr);
  wrefresh(stdscr);

  int sepHeight = 1;
  m_OutHeight = m_ScreenHeight - m_InHeight - sepHeight;
  m_OutWidth = m_ScreenWidth;
  int outY = 0;
  int outX = 0;
  m_OutWin = newwin(m_OutHeight, m_OutWidth, outY, outX);
  wrefresh(m_OutWin);

  if (sepHeight > 0)
  {
    int sepWidth = m_ScreenWidth;
    int sepY = m_OutHeight;
    int sepX = 0;
    m_StatusWin = newwin(m_OutHeight, m_OutWidth, sepY, sepX);
    mvwhline(m_StatusWin, 0, 0, 0, sepWidth);
    wrefresh(m_StatusWin);
  }

  m_InWidth = m_ScreenWidth;
  int inY = m_OutHeight + sepHeight;
  int inX = 0;
  m_InWin = newwin(m_InHeight, m_InWidth, inY, inX);;
  wrefresh(m_InWin);
}

void UiLite::CleanupWin()
{
  delwin(m_InWin);
  m_InWin = NULL;
  delwin(m_OutWin);
  m_OutWin = NULL;
  delwin(m_StatusWin);
  m_StatusWin = NULL;
}

void UiLite::RedrawContactWin()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  mvwhline(m_StatusWin, 0, 0, 0, m_ScreenWidth);

  if (m_Chats.find(m_CurrentChat) != m_Chats.end())
  {
    const Chat& chat = m_Chats.at(m_CurrentChat);
    const std::string& rawName = chat.m_Name;
    const std::string& name = m_ShowEmoji ? rawName : emojicpp::textize(rawName);
    mvwprintw(m_StatusWin, 0, 5, " %s ", name.c_str());
  }
  
  bool isAnyUnread = false;
  for (auto& chat : m_Chats)
  {
    if (chat.second.m_IsUnread)
    {
      isAnyUnread = true;
      break;
    }
  }

  if (isAnyUnread)
  {
    mvwprintw(m_StatusWin, 0, m_ScreenWidth - 8, " * ");
  }
  
  wrefresh(m_StatusWin);
}
