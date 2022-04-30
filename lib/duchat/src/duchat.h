// duchat.h
//
// Copyright (c) 2020-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <condition_variable>
#include <deque>
#include <thread>

#include "protocol.h"

class DuChat : public Protocol
{
public:
  DuChat();
  virtual ~DuChat();
  static std::string GetName() { return "Dummy"; }
  static std::string GetLibName() { return "libduchat"; }
  static std::string GetCreateFunc() { return "CreateDuChat"; }
  std::string GetProfileId() const;
  bool HasFeature(ProtocolFeature p_ProtocolFeature) const;

  bool SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId);
  bool LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId);
  bool CloseProfile();

  bool Login();
  bool Logout();

  void Process();

  void SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage);
  void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler);

private:
  void PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage);

private:
  std::string m_ProfileId;
  std::function<void(std::shared_ptr<ServiceMessage>)> m_MessageHandler;

  bool m_Running = false;
  std::thread m_Thread;
  std::deque<std::shared_ptr<RequestMessage>> m_RequestsQueue;
  std::mutex m_ProcessMutex;
  std::condition_variable m_ProcessCondVar;
};

extern "C" DuChat* CreateDuChat();
