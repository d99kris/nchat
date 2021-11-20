// ui.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "ui.h"

#include <locale.h>
#include <unistd.h>

#include <ncurses.h>

#include "emojilist.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uicontroller.h"
#include "uikeyconfig.h"
#include "uimodel.h"

Ui::Ui()
{
  setlocale(LC_ALL, "");
  initscr();
  noecho();
  cbreak();
  raw();
  keypad(stdscr, TRUE);
  curs_set(0);
  timeout(0);
  EmojiList::Init();
  UiConfig::Init();
  UiKeyConfig::Init();
  UiColorConfig::Init();

  m_Model = std::make_shared<UiModel>();
  m_Controller = std::make_shared<UiController>();
}

Ui::~Ui()
{
  m_Model.reset();
  m_Controller.reset();

  UiColorConfig::Cleanup();
  UiKeyConfig::Cleanup();
  UiConfig::Cleanup();
  EmojiList::Cleanup();
  wclear(stdscr);
  endwin();
}

void Ui::Run()
{
  curs_set(1);
  while (m_Model->Process())
  {
    wint_t key = UiController::GetKey(50);
    if (key != 0)
    {
      m_Model->KeyHandler(key);
    }
  }

  std::unordered_map<std::string, std::shared_ptr<Protocol>>& protocols = m_Model->GetProtocols();
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
