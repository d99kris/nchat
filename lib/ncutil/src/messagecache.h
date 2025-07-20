// messagecache.h
//
// Copyright (c) 2020-2024 Kristofer Berggren
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
    AddMessagesRequestType,
    AddChatsRequestType,
    AddContactsRequestType,
    FetchChatsRequestType,
    FetchContactsRequestType,
    FetchMessagesFromRequestType,
    FetchOneMessageRequestType,
    FindCachedMessageRequestType,
    DeleteOneMessageRequestType,
    DeleteOneChatRequestType,
    UpdateMessageIsReadRequestType,
    UpdateMessageFileInfoRequestType,
    UpdateMessageReactionsRequestType,
    UpdateMuteRequestType,
    UpdatePinRequestType,
  };

  class Request
  {
  public:
    virtual ~Request() { }
    virtual RequestType GetRequestType() const { return UnknownRequestType; }
  };

  class AddMessagesRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return AddMessagesRequestType; }
    std::string profileId;
    std::string chatId;
    std::string fromMsgId;
    std::vector<ChatMessage> chatMessages;
  };

  class AddChatsRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return AddChatsRequestType; }
    std::string profileId;
    std::vector<ChatInfo> chatInfos;
  };

  class AddContactsRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return AddContactsRequestType; }
    std::string profileId;
    bool fullSync = false;
    std::vector<ContactInfo> contactInfos;
  };

  class FetchChatsRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return FetchChatsRequestType; }
    std::string profileId;
    std::unordered_set<std::string> chatIds; // optionally fetch only specified chats
  };

  class FetchContactsRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return FetchContactsRequestType; }
    std::string profileId;
  };

  class FetchMessagesFromRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return FetchMessagesFromRequestType; }
    std::string profileId;
    std::string chatId;
    std::string fromMsgId;
    int limit = 0;
  };

  class FetchOneMessageRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return FetchOneMessageRequestType; }
    std::string profileId;
    std::string chatId;
    std::string msgId;
  };

  class FindCachedMessageRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return FindCachedMessageRequestType; }
    std::string profileId;
    std::string chatId;
    std::string fromMsgId;
    std::string lastMsgId;
    std::string findText;
    std::string findMsgId;
  };

  class DeleteOneMessageRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return DeleteOneMessageRequestType; }
    std::string profileId;
    std::string chatId;
    std::string msgId;
  };

  class DeleteOneChatRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return DeleteOneChatRequestType; }
    std::string profileId;
    std::string chatId;
  };

  class UpdateMessageIsReadRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return UpdateMessageIsReadRequestType; }
    std::string profileId;
    std::string chatId;
    std::string msgId;
    bool isRead = false;
  };

  class UpdateMessageFileInfoRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return UpdateMessageFileInfoRequestType; }
    std::string profileId;
    std::string chatId;
    std::string msgId;
    std::string fileInfo;
  };

  class UpdateMessageReactionsRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return UpdateMessageReactionsRequestType; }
    std::string profileId;
    std::string chatId;
    std::string msgId;
    Reactions reactions;
  };

  class UpdateMuteRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return UpdateMuteRequestType; }
    std::string profileId;
    std::string chatId;
    bool isMuted = false;
  };

  class UpdatePinRequest : public Request
  {
  public:
    virtual RequestType GetRequestType() const { return UpdatePinRequestType; }
    std::string profileId;
    std::string chatId;
    bool isPinned = false;
    int64_t timePinned = -1;
  };

public:
  static void Init();
  static void Cleanup();
  static void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler);

  static void AddFromServiceMessage(const std::string& p_ProfileId, std::shared_ptr<ServiceMessage> p_ServiceMessage);

  static void AddProfile(const std::string& p_ProfileId, bool p_CheckSync, int p_DirVersion, bool p_IsSetup,
                         bool* p_IsRemoved = nullptr);
  static void AddMessages(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_FromMsgId,
                          const std::vector<ChatMessage>& p_ChatMessages);
  static void AddChats(const std::string& p_ProfileId, const std::vector<ChatInfo>& p_ChatInfos);
  static void AddContacts(const std::string& p_ProfileId, bool p_FullSync,
                          const std::vector<ContactInfo>& p_ContactInfos);
  static bool FetchChats(const std::string& p_ProfileId, const std::unordered_set<std::string>& p_ChatIds);
  static bool FetchContacts(const std::string& p_ProfileId);
  static bool FetchMessagesFrom(const std::string& p_ProfileId, const std::string& p_ChatId,
                                const std::string& p_FromMsgId,
                                const int p_Limit, const bool p_Sync);
  static bool FetchOneMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                              const std::string& p_MsgId, const bool p_Sync);
  static bool GetOneMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                            const std::string& p_MsgId, std::vector<ChatMessage>& p_ChatMessages);
  static void FindMessage(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_FromMsgId,
                          const std::string& p_LastMsgId, const std::string& p_FindText,
                          const std::string& p_FindMsgId);
  static void DeleteOneMessage(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId);
  static void DeleteChat(const std::string& p_ProfileId, const std::string& p_ChatId);
  static void UpdateMessageIsRead(const std::string& p_ProfileId, const std::string& p_ChatId,
                                  const std::string& p_MsgId,
                                  bool p_IsRead);
  static void UpdateMessageFileInfo(const std::string& p_ProfileId, const std::string& p_ChatId,
                                    const std::string& p_MsgId, const std::string& p_FileInfo);
  static void UpdateMessageReactions(const std::string& p_ProfileId, const std::string& p_ChatId,
                                     const std::string& p_MsgId, const Reactions& p_Reactions);
  static void UpdateMute(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_IsMuted);
  static void UpdatePin(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_IsPinned,
                        int64_t p_TimePinned);
  static void Export(const std::string& p_ExportDir);

private:
  static void Process();
  static void EnqueueRequest(std::shared_ptr<Request> p_Request);
  static void PerformRequest(std::shared_ptr<Request> p_Request);
  static void PerformFetchMessagesFrom(const std::string& p_ProfileId, const std::string& p_ChatId,
                                       const int64_t p_FromMsgIdTimeSent, const int p_Limit,
                                       std::vector<ChatMessage>& p_ChatMessages);
  static void PerformFetchOneMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                                     const std::string& p_MsgId, std::vector<ChatMessage>& p_ChatMessages);

  static void CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);

private:
  static std::function<void(std::shared_ptr<ServiceMessage>)> m_MessageHandler;

  static std::mutex m_DbMutex;
  static std::map<std::string, std::unique_ptr<sqlite::database>> m_Dbs;
  static std::unordered_map<std::string, std::unordered_map<std::string, bool>> m_InSync;
  static std::unordered_map<std::string, bool> m_CheckSync;

  static bool m_Running;
  static std::thread m_Thread;
  static std::mutex m_QueueMutex;
  static std::condition_variable m_CondVar;
  static std::deque<std::shared_ptr<Request>> m_Queue;

  static std::string m_HistoryDir;
  static bool m_CacheEnabled;
};
