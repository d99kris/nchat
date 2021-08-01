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
    FetchRequestType,
    DeleteRequestType,
    UpdateIsReadRequestType,
    UpdateFilePathRequestType,
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

  class UpdateFilePathRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return UpdateFilePathRequestType; }
    std::string profileId;
    std::string chatId;
    std::string msgId;
    std::string filePath;
  };

public:
  static void Init(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler);
  static void Cleanup();

  static void AddProfile(const std::string& p_ProfileId);
  static void Add(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_FromMsgId, const std::vector<ChatMessage>& p_ChatMessages);
  static bool Fetch(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_FromMsgId, int p_Limit);
  static void Delete(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId);
  static void UpdateIsRead(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId, bool p_IsRead);
  static void UpdateFilePath(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId, const std::string& p_FilePath);

private:
  static void Process();
  static void EnqueueRequest(std::shared_ptr<Request> p_Request);
  static void PerformRequest(std::shared_ptr<Request> p_Request);
  static void CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);

private:
  static std::function<void(std::shared_ptr<ServiceMessage>)> m_MessageHandler;

  static std::mutex m_DbMutex;
  static std::map<std::string, std::unique_ptr<sqlite::database>> m_Dbs;

  static bool m_Running;
  static std::thread m_Thread;
  static std::mutex m_QueueMutex;
  static std::condition_variable m_CondVar;
  static std::deque<std::shared_ptr<Request>> m_Queue;

  static std::string m_HistoryDir;
};
