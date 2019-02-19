// ui.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>

#include <ncursesw/ncurses.h>

class Config;
class Contact;
class Protocol;
struct Chat;
struct Message;

class Ui
{
public:
  Ui()
  {
  }

  virtual ~Ui()
  {
  }

  virtual void Init() = 0;
  virtual void Cleanup() = 0;
  virtual std::string GetName() = 0;
  virtual void Run() = 0;

  virtual void AddProtocol(Protocol* p_Protocol) = 0;
  virtual void RemoveProtocol(Protocol* p_Protocol) = 0;
  virtual void UpdateChat(Chat p_Chats) = 0;
  virtual void UpdateChats(std::vector<Chat> p_Chats) = 0;
  virtual void UpdateMessages(std::vector<Message> p_Messages, bool p_ClearChat = false) = 0;

private:
};
