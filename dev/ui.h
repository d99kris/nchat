// devui.h
//
// Copyright (c) 2019-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include "protocol.h"

class Protocol;
class ServiceMessage;

class Ui
{
public:
  Ui();
  virtual ~Ui();

  void Run();
  void AddProtocol(std::shared_ptr<Protocol> p_Protocol);
  std::unordered_map<std::string, std::shared_ptr<Protocol>>& GetProtocols();
  void MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);

  static void RunKeyDump();

private:
  std::mutex m_StdoutMutex;
  std::unordered_map<std::string, std::shared_ptr<Protocol>> m_Protocols;
  std::map<std::string, std::set<std::string>> m_Chats;
  std::map<std::string, ChatInfo> m_ChatInfos;
  std::string m_CurrentProfileId;
  std::string m_CurrentChatId;
};
