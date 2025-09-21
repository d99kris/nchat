// tgchat.h
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <set>
#include <thread>

#include "config.h"
#include "protocol.h"

class TgChat : public Protocol
{
  class Impl;
  std::unique_ptr<Impl> m_Impl;

public:
  TgChat();
  virtual ~TgChat();
  static std::string GetName() { return "Telegram"; }
  static std::string GetLibName() { return "libtgchat"; }
  static std::string GetCreateFunc() { return "CreateTgChat"; }
  static std::string GetSetupMessage() { return ""; }
  std::string GetProfileId() const;
  std::string GetProfileDisplayName() const;
  bool HasFeature(ProtocolFeature p_ProtocolFeature) const;
  std::string GetSelfId() const;

  bool SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId);
  bool LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId);
  bool CloseProfile();

  bool Login();
  bool Logout();

  void Process();

  void SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage);
  void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler);
};

extern "C" TgChat* CreateTgChat();
