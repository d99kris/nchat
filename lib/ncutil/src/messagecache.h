// messagecache.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "protocol.h"

namespace sqlite
{
  class database;
}

class MessageCache
{
private:
  enum RequestType
  {
    UnknownRequestType = 0,
    AddRequestType,
    AddContactsRequestType,
    FetchRequestType,
    DeleteRequestType,
    UpdateIsReadRequestType,
    UpdateFileInfoRequestType,
  };

  class Request
  {
  public:
    virtual ~Request() { }
    virtual RequestType GetRequestType() const { return UnknownRequestType; }
  };

  class AddRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return AddRequestType; }
    std::string profileId;
    std::string chatId;
    std::string fromMsgId;
    std::vector<ChatMessage> chatMessages;
  };

  class AddContactsRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return AddContactsRequestType; }
    std::string profileId;
    std::vector<ContactInfo> contactInfos;
  };

  class FetchRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return FetchRequestType; }
    std::string profileId;
    std::string chatId;
    std::string fromMsgId;
    int limit = 0;
  };

  class DeleteRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return DeleteRequestType; }
    std::string profileId;
    std::string chatId;
    std::string msgId;
  };

  class UpdateIsReadRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return UpdateIsReadRequestType; }
    std::string profileId;
    std::string chatId;
    std::string msgId;
    bool isRead = false;
  };

  class UpdateFileInfoRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return UpdateFileInfoRequestType; }
    std::string profileId;
    std::string chatId;
    std::string msgId;
    std::string fileInfo;
  };

public:
  static void Init(const bool p_CacheEnabled,
                   const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler);
  static void Cleanup();

  static void AddProfile(const std::string& p_ProfileId);
  static void Add(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_FromMsgId,
                  const std::vector<ChatMessage>& p_ChatMessages);
  static void AddContacts(const std::string& p_ProfileId, const std::vector<ContactInfo>& p_ContactInfos);
  static bool Fetch(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_FromMsgId,
                    const int p_Limit, const bool p_Sync);
  static void Delete(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId);
  static void UpdateIsRead(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId,
                           bool p_IsRead);
  static void UpdateFileInfo(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId,
                             const std::string& p_FileInfo);

  static void Export(const std::string& p_ExportDir);

private:
  static void Process();
  static void EnqueueRequest(std::shared_ptr<Request> p_Request);
  static void PerformRequest(std::shared_ptr<Request> p_Request);
  static void PerformFetch(const std::string& p_ProfileId, const std::string& p_ChatId,
                           const int64_t p_FromMsgIdTimeSent, const int p_Limit,
                           std::vector<ChatMessage>& p_ChatMessages);

  static void CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);

private:
  static std::function<void(std::shared_ptr<ServiceMessage>)> m_MessageHandler;

  static std::mutex m_DbMutex;
  static std::map<std::string, std::unique_ptr<sqlite::database>> m_Dbs;
  static std::unordered_map<std::string, std::unordered_map<std::string, bool>> m_InSync;

  static bool m_Running;
  static std::thread m_Thread;
  static std::mutex m_QueueMutex;
  static std::condition_variable m_CondVar;
  static std::deque<std::shared_ptr<Request>> m_Queue;

  static std::string m_HistoryDir;
  static bool m_CacheEnabled;
};
