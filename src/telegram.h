// telegram.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <functional>
#include <thread>

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>

#include "config.h"
#include "protocol.h"

class Ui;

class Telegram : public Protocol
{
public:
  Telegram(bool p_IsSetup, std::shared_ptr<Ui> p_Ui);
  virtual ~Telegram();
  virtual std::string GetName();  

  virtual void RequestChats(std::int32_t p_Limit, std::int64_t p_OffsetChat = 0,
                            std::int64_t p_OffsetOrder = (std::numeric_limits<std::int64_t>::max() - 1));
  virtual void RequestChatUpdate(std::int64_t p_ChatId);
  virtual void RequestMessages(std::int64_t p_ChatId, std::int64_t p_FromMsg, std::int32_t p_Limit);
  virtual void SendMessage(std::int64_t p_ChatId, const std::string& p_Message);
  virtual void MarkRead(std::int64_t p_ChatId, const std::vector<std::int64_t>& p_MsgIds);

  virtual bool Setup();
  virtual void Start();
  virtual void Stop();

private:
  using Object = td::td_api::object_ptr<td::td_api::Object>;
  
private:
  void Init();
  void Cleanup();
  void Process();
  void TdMessageConvert(const td::td_api::message& p_TdMessage, Message& p_Message);

  void SendQuery(td::td_api::object_ptr<td::td_api::Function> f, std::function<void(Object)> handler);
  void ProcessResponse(td::Client::Response response);
  std::string GetUserName(std::int32_t user_id);
  void ProcessUpdate(td::td_api::object_ptr<td::td_api::Object> update);
  auto CreateAuthQueryHandler();
  void OnAuthStateUpdate();
  void CheckAuthError(Object object);
  std::uint64_t GetNextQueryId();
  std::string GetRandomString(size_t p_Len);

private:
  Config m_Config;
  bool m_IsSetup;
  std::shared_ptr<Ui> m_Ui;
  std::unique_ptr<td::Client> m_Client;
  td::td_api::object_ptr<td::td_api::AuthorizationState> m_AuthorizationState;
  bool m_Authorized = false;
  bool m_WasAuthorized = false;
  bool m_Running = true;
  std::uint64_t m_CurrentQueryId = 0;
  std::uint64_t m_AuthQueryId = 0;
  std::map<std::uint64_t, std::function<void(Object)>> m_Handlers;
  std::map<std::int32_t, td::td_api::object_ptr<td::td_api::user>> m_Users;
  std::map<std::int64_t, std::string> m_ChatTitle;
  std::thread* m_Thread;
};
