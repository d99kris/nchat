// ui.cpp
//
// Copyright (c) 2019-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "ui.h"

#include <locale.h>
#include <unistd.h>

#include <ncurses.h>

#include "emojilist.h"
#include "log.h"
#include "messagecache.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uicontroller.h"
#include "uikeyconfig.h"
#include "uikeydump.h"
#include "uimodel.h"

Ui::Ui()
{
  UiConfig::Init();

  m_TerminalTitle = UiConfig::GetStr("terminal_title");
  if (!m_TerminalTitle.empty())
  {
    printf("\033]0;%s\007", m_TerminalTitle.c_str());
  }

  printf("\033[?1004h"); // enable terminal focus in/out event

  setlocale(LC_ALL, "");
  initscr();
  noecho();
  cbreak();
  raw();
  keypad(stdscr, TRUE);
  curs_set(0);
  timeout(0);
  EmojiList::Init();
  UiKeyConfig::Init();
  UiColorConfig::Init();

  m_Controller = std::make_shared<UiController>();
  m_Model = std::make_shared<UiModel>();
}

Ui::~Ui()
{
  m_Model.reset();
  m_Controller.reset();

  UiColorConfig::Cleanup();
  UiKeyConfig::Cleanup();
  EmojiList::Cleanup();
  UiConfig::Cleanup();
  wclear(stdscr);
  endwin();

  printf("\033[?1004l"); // disable terminal focus in/out event

  if (!m_TerminalTitle.empty())
  {
    printf("\033]0;%s\007", "");
  }
}

void Ui::Run()
{
  std::unordered_map<std::string, std::shared_ptr<Protocol>>& protocols = m_Model->GetProtocols();

  // retrieve cached contacts for use until receiving latest from chat service
  for (auto& protocol : protocols)
  {
    MessageCache::FetchContacts(protocol.first);
  }

  LOG_INFO("entering ui loop");

  curs_set(1);
  while (m_Model->Process())
  {
    wint_t key = UiController::GetKey(50);
    if (key != 0)
    {
      m_Model->KeyHandler(key);
    }
  }

  LOG_INFO("exiting ui loop");

  // set as offline before logging off
  for (auto& protocol : protocols)
  {
    m_Model->SetStatusOnline(protocol.first, false);
  }

  usleep(100000);
}

void Ui::AddProtocol(std::shared_ptr<Protocol> p_Protocol)
{
  m_Model->AddProtocol(p_Protocol);
}

std::unordered_map<std::string, std::shared_ptr<Protocol>>& Ui::GetProtocols()
{
  return m_Model->GetProtocols();
}

void Ui::MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  m_Model->MessageHandler(p_ServiceMessage);
}

void Ui::RunKeyDump()
{
  UiKeyDump::Run();
}
