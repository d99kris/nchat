// tgchat.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <condition_variable>
#include <deque>
#include <set>
#include <thread>

#include <td/telegram/Client.h>

#include "config.h"
#include "protocol.h"

class TgChat : public Protocol
{
public:
  TgChat();
  virtual ~TgChat();
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
  void CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);
  void PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage);

  using Object = td::td_api::object_ptr<td::td_api::Object>;
  void Init();
  void Cleanup();
  void ProcessService();
  void ProcessResponse(td::Client::Response response);
  void ProcessUpdate(td::td_api::object_ptr<td::td_api::Object> update);
  std::function<void(TgChat::Object)> CreateAuthQueryHandler();
  void OnAuthStateUpdate();
  void SendQuery(td::td_api::object_ptr<td::td_api::Function> f, std::function<void(Object)> handler);
  void CheckAuthError(Object object);
  std::string GetRandomString(size_t p_Len);
  std::uint64_t GetNextQueryId();
  std::int64_t GetSenderId(const td::td_api::message& p_TdMessage);
  std::string GetText(td::td_api::object_ptr<td::td_api::formattedText>&& p_FormattedText);
  void TdMessageConvert(td::td_api::message& p_TdMessage, ChatMessage& p_ChatMessage);
  std::string GetUserName(std::int32_t user_id, std::int64_t chat_id);
  void DownloadFile(std::string p_ChatId, std::string p_MsgId, std::string p_FileId);

private:
  std::string m_ProfileId = "Telegram";
  std::string m_ProfileDir;
  std::function<void(std::shared_ptr<ServiceMessage>)> m_MessageHandler;

  bool m_Running = false;
  std::thread m_Thread;
  std::deque<std::shared_ptr<RequestMessage>> m_RequestsQueue;
  std::mutex m_ProcessMutex;
  std::condition_variable m_ProcessCondVar;

  std::thread m_ServiceThread;
  std::string m_SetupPhoneNumber;
  Config m_Config;
  std::unique_ptr<td::Client> m_Client;
  std::map<std::uint64_t, std::function<void(Object)>> m_Handlers;
  td::td_api::object_ptr<td::td_api::AuthorizationState> m_AuthorizationState;
  std::map<std::int32_t, td::td_api::object_ptr<td::td_api::user>> m_Users;
  std::map<int32_t, std::set<int64_t>> m_UserToChats;
  bool m_IsSetup = false;
  bool m_Authorized = false;
  bool m_WasAuthorized = false;
  std::int32_t m_SelfUserId = 0;
  std::uint64_t m_AuthQueryId = 0;
  std::uint64_t m_CurrentQueryId = 0;
  std::map<int64_t, int64_t> m_LastReadInboxMessage;
  std::map<int64_t, int64_t> m_LastReadOutboxMessage;
  std::map<int64_t, std::set<int64_t>> m_UnreadOutboxMessages;
};
