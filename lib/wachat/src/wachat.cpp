// wachat.cpp
//
// Copyright (c) 2020-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "wachat.h"

#include <iostream>

#include <sys/stat.h>

#include "libcgowa.h"
#include "log.h"
#include "messagecache.h"
#include "protocolutil.h"
#include "status.h"
#include "timeutil.h"

std::mutex WaChat::s_ConnIdMapMutex;
std::map<int, WaChat*> WaChat::s_ConnIdMap;

extern "C" WaChat* CreateWaChat()
{
  return new WaChat();
}

WaChat::WaChat()
{
  m_ProfileId = WaChat::GetName();
}

WaChat::~WaChat()
{
}

std::string WaChat::GetProfileId() const
{
  return m_ProfileId;
}

bool WaChat::HasFeature(ProtocolFeature p_ProtocolFeature) const
{
  ProtocolFeature customFeatures = FeatureAutoGetChatsOnLogin;
  return (p_ProtocolFeature & customFeatures);
}

bool WaChat::SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId)
{
  std::cout << "\n";
  std::cout << "WARNING:\n";
  std::cout << "This functionality is in no way affiliated with, authorized, maintained,\n";
  std::cout << "sponsored or endorsed by WhatsApp or any of its affiliates or subsidiaries.\n";
  std::cout << "WhatsApp may disable or suspend accounts violating their Terms of Service.\n";
  std::cout << "Use at your own risk. You may abort this setup wizard by pressing CTRL-C.\n";
  std::cout << "\n";

  std::cout << "Enter phone number (ex. +6511111111): ";
  std::string phoneNumber;
  std::getline(std::cin, phoneNumber);

  m_ProfileId = m_ProfileId + "_" + phoneNumber;
  std::string profileDir = p_ProfilesDir + "/" + m_ProfileId;

  mkdir(profileDir.c_str(), 0777);

  p_ProfileId = m_ProfileId;

  MessageCache::AddProfile(m_ProfileId);

  int connId = CInit(const_cast<char*>(profileDir.c_str()));
  if (connId == -1) return false;

  int rv = CLogin(connId);
  if (rv != 0) return false;

  TimeUtil::Sleep(0.05);

  rv = CLogout(connId);
  if (rv != 0) return false;

  rv = CCleanup(connId);
  if (rv != 0) return false;

  return true;
}

bool WaChat::LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId)
{
  m_ProfileDir = p_ProfilesDir + "/" + p_ProfileId;
  m_ProfileId = p_ProfileId;

  MessageCache::AddProfile(m_ProfileId);

  m_ConnId = CInit(const_cast<char*>(m_ProfileDir.c_str()));
  if (m_ConnId == -1) return false;

  AddInstance(m_ConnId, this);

  return true;
}

bool WaChat::CloseProfile()
{
  int rv = CCleanup(m_ConnId);
  RemoveInstance(m_ConnId);
  m_ConnId = -1;
  m_ProfileDir = "";
  m_ProfileId = "";
  return (rv == 0);
}

bool WaChat::Login()
{
  int rv = 0;
  if (!m_Running)
  {
    m_Running = true;
    m_Thread = std::thread(&WaChat::Process, this);

    rv = CLogin(m_ConnId);
    Status::Set(Status::FlagOnline);

    {
      std::shared_ptr<ConnectNotify> connectNotify = std::make_shared<ConnectNotify>(m_ProfileId);
      connectNotify->success = (rv == 0);

      std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
      deferNotifyRequest->serviceMessage = connectNotify;
      SendRequest(deferNotifyRequest);
    }
  }
  return (rv == 0);
}

bool WaChat::Logout()
{
  int rv = 0;
  if (m_Running)
  {
    rv = CLogout(m_ConnId);
    Status::Clear(Status::FlagOnline);

    std::unique_lock<std::mutex> lock(m_ProcessMutex);
    m_Running = false;
    m_ProcessCondVar.notify_one();
  }

  if (m_Thread.joinable())
  {
    m_Thread.join();
  }
  return (rv == 0);
}

void WaChat::Process()
{
  while (m_Running)
  {
    std::shared_ptr<RequestMessage> requestMessage;

    {
      std::unique_lock<std::mutex> lock(m_ProcessMutex);
      while (m_RequestsQueue.empty() && m_Running)
      {
        m_ProcessCondVar.wait(lock);
      }

      if (!m_Running)
      {
        break;
      }

      requestMessage = m_RequestsQueue.front();
      m_RequestsQueue.pop_front();
    }

    PerformRequest(requestMessage);
  }
}

void WaChat::SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  std::unique_lock<std::mutex> lock(m_ProcessMutex);
  m_RequestsQueue.push_back(p_RequestMessage);
  m_ProcessCondVar.notify_one();
}

void WaChat::SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler)
{
  m_MessageHandler = p_MessageHandler;
}

void WaChat::CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  MessageCache::AddFromServiceMessage(m_ProfileId, p_ServiceMessage);

  if (!m_MessageHandler) return;

  m_MessageHandler(p_ServiceMessage);
}

void WaChat::PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  bool requestIntervalDelay = true;

  switch (p_RequestMessage->GetMessageType())
  {
    case GetContactsRequestType:
      {
        LOG_DEBUG("get contacts");
        std::shared_ptr<GetContactsRequest> getContactsRequest = std::static_pointer_cast<GetContactsRequest>(
          p_RequestMessage);
        std::shared_ptr<NewContactsNotify> newContactsNotify = std::make_shared<NewContactsNotify>(m_ProfileId);
        newContactsNotify->contactInfos = std::vector<ContactInfo>();
        CallMessageHandler(newContactsNotify);
      }
      break;

    case GetChatsRequestType:
      {
        LOG_DEBUG("get chats");
        std::shared_ptr<GetChatsRequest> getChatsRequest = std::static_pointer_cast<GetChatsRequest>(p_RequestMessage);
        std::shared_ptr<NewChatsNotify> newChatsNotify = std::make_shared<NewChatsNotify>(m_ProfileId);
        newChatsNotify->success = true;
        newChatsNotify->chatInfos = std::vector<ChatInfo>();
        CallMessageHandler(newChatsNotify);
      }
      break;

    case GetMessageRequestType:
      {
        LOG_DEBUG("Get message");
        std::shared_ptr<GetMessageRequest> getMessageRequest =
          std::static_pointer_cast<GetMessageRequest>(p_RequestMessage);
        MessageCache::FetchOneMessage(m_ProfileId, getMessageRequest->chatId, getMessageRequest->msgId, false /*p_Sync*/);
      }
      break;

    case GetMessagesRequestType:
      {
        LOG_DEBUG("get messages");
        std::shared_ptr<GetMessagesRequest> getMessagesRequest =
          std::static_pointer_cast<GetMessagesRequest>(p_RequestMessage);

        if (!getMessagesRequest->fromMsgId.empty() || (getMessagesRequest->limit == std::numeric_limits<int>::max()))
        {
          if (MessageCache::FetchMessagesFrom(m_ProfileId, getMessagesRequest->chatId,
                                              getMessagesRequest->fromMsgId, getMessagesRequest->limit, false /* p_Sync */))
          {
            return;
          }
        }

        Status::Set(Status::FlagFetching);
        std::string chatId = getMessagesRequest->chatId;
        int32_t limit = getMessagesRequest->limit;
        std::string fromMsgId = getMessagesRequest->fromMsgId;
        int32_t owner = getMessagesRequest->fromIsOutgoing ? 1 : 0;
        int cnt = CGetMessages(m_ConnId, const_cast<char*>(chatId.c_str()), limit,
                               const_cast<char*>(fromMsgId.c_str()), owner);
        Status::Clear(Status::FlagFetching);

        if (cnt == 0)
        {
          std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(m_ProfileId);
          newMessagesNotify->success = true;
          newMessagesNotify->chatId = chatId;
          newMessagesNotify->fromMsgId = fromMsgId;
          CallMessageHandler(newMessagesNotify);
        }
      }
      break;

    case SendMessageRequestType:
      {
        LOG_DEBUG("send message");
        Status::Set(Status::FlagSending);
        std::shared_ptr<SendMessageRequest> sendMessageRequest =
          std::static_pointer_cast<SendMessageRequest>(p_RequestMessage);
        std::string chatId = sendMessageRequest->chatId;
        std::string text = sendMessageRequest->chatMessage.text;
        std::string quotedId = sendMessageRequest->chatMessage.quotedId;
        std::string quotedText = sendMessageRequest->chatMessage.quotedText;
        std::string quotedSender = sendMessageRequest->chatMessage.quotedSender;
        std::string filePath;
        std::string fileType;
        if (!sendMessageRequest->chatMessage.fileInfo.empty())
        {
          FileInfo fileInfo =
            ProtocolUtil::FileInfoFromHex(sendMessageRequest->chatMessage.fileInfo);
          filePath = fileInfo.filePath;
          fileType = fileInfo.fileType;
        }

        int rv =
          CSendMessage(m_ConnId, const_cast<char*>(chatId.c_str()), const_cast<char*>(text.c_str()),
                       const_cast<char*>(quotedId.c_str()),
                       const_cast<char*>(quotedText.c_str()), const_cast<char*>(quotedSender.c_str()),
                       const_cast<char*>(filePath.c_str()), const_cast<char*>(fileType.c_str()));
        Status::Clear(Status::FlagSending);

        std::shared_ptr<SendMessageNotify> sendMessageNotify = std::make_shared<SendMessageNotify>(m_ProfileId);
        sendMessageNotify->success = (rv == 0);
        sendMessageNotify->chatId = sendMessageRequest->chatId;
        sendMessageNotify->chatMessage = sendMessageRequest->chatMessage;
        CallMessageHandler(sendMessageNotify);
      }
      break;

    case MarkMessageReadRequestType:
      {
        LOG_DEBUG("mark message read");
        std::shared_ptr<MarkMessageReadRequest> markMessageReadRequest =
          std::static_pointer_cast<MarkMessageReadRequest>(p_RequestMessage);
        std::string chatId = markMessageReadRequest->chatId;
        std::string msgId = markMessageReadRequest->msgId;

        int rv = CMarkMessageRead(m_ConnId, const_cast<char*>(chatId.c_str()), const_cast<char*>(msgId.c_str()));

        std::shared_ptr<MarkMessageReadNotify> markMessageReadNotify =
          std::make_shared<MarkMessageReadNotify>(m_ProfileId);
        markMessageReadNotify->success = (rv == 0);
        markMessageReadNotify->chatId = markMessageReadRequest->chatId;
        markMessageReadNotify->msgId = markMessageReadRequest->msgId;
        CallMessageHandler(markMessageReadNotify);
      }
      break;

    case DeleteMessageRequestType:
      {
        LOG_DEBUG("delete message");
        Status::Set(Status::FlagUpdating);
        std::shared_ptr<DeleteMessageRequest> deleteMessageRequest =
          std::static_pointer_cast<DeleteMessageRequest>(p_RequestMessage);
        std::string chatId = deleteMessageRequest->chatId;
        std::string msgId = deleteMessageRequest->msgId;

        int rv = CDeleteMessage(m_ConnId, const_cast<char*>(chatId.c_str()), const_cast<char*>(msgId.c_str()));
        Status::Clear(Status::FlagUpdating);

        std::shared_ptr<DeleteMessageNotify> deleteMessageNotify = std::make_shared<DeleteMessageNotify>(m_ProfileId);
        deleteMessageNotify->success = (rv == 0);
        deleteMessageNotify->chatId = deleteMessageRequest->chatId;
        deleteMessageNotify->msgId = deleteMessageRequest->msgId;
        CallMessageHandler(deleteMessageNotify);
      }
      break;

    case SendTypingRequestType:
      {
        LOG_DEBUG("send typing");
        std::shared_ptr<SendTypingRequest> sendTypingRequest =
          std::static_pointer_cast<SendTypingRequest>(p_RequestMessage);
        std::string chatId = sendTypingRequest->chatId;
        int32_t isTyping = sendTypingRequest->isTyping ? 1 : 0;

        int rv = CSendTyping(m_ConnId, const_cast<char*>(chatId.c_str()), isTyping);

        std::shared_ptr<SendTypingNotify> sendTypingNotify = std::make_shared<SendTypingNotify>(m_ProfileId);
        sendTypingNotify->success = (rv == 0);
        sendTypingNotify->chatId = sendTypingRequest->chatId;
        sendTypingNotify->isTyping = sendTypingRequest->isTyping;
        CallMessageHandler(sendTypingNotify);
      }
      break;

    case SetStatusRequestType:
      {
        LOG_DEBUG("set status");
        std::shared_ptr<SetStatusRequest> setStatusRequest =
          std::static_pointer_cast<SetStatusRequest>(p_RequestMessage);
        int32_t isOnline = setStatusRequest->isOnline ? 1 : 0;

        int rv = CSetStatus(m_ConnId, isOnline);

        std::shared_ptr<SetStatusNotify> setStatusNotify = std::make_shared<SetStatusNotify>(m_ProfileId);
        setStatusNotify->success = (rv == 0);
        setStatusNotify->isOnline = setStatusRequest->isOnline;
        CallMessageHandler(setStatusNotify);
      }
      break;

    case CreateChatRequestType:
      {
        LOG_DEBUG("create chat");

        std::shared_ptr<CreateChatRequest> createChatRequest =
          std::static_pointer_cast<CreateChatRequest>(p_RequestMessage);
        std::string userId = createChatRequest->userId;

        std::shared_ptr<CreateChatNotify> createChatNotify = std::make_shared<CreateChatNotify>(m_ProfileId);
        createChatNotify->success = true;
        createChatNotify->chatInfo.id = userId;
        CallMessageHandler(createChatNotify);
      }
      break;

    case DeferNotifyRequestType:
      {
        std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
          std::static_pointer_cast<DeferNotifyRequest>(p_RequestMessage);
        CallMessageHandler(deferNotifyRequest->serviceMessage);
        requestIntervalDelay = false;
      }
      break;

    case SetCurrentChatRequestType:
      {
        // No handling needed
      }
      break;

    default:
      LOG_DEBUG("unknown request %d", p_RequestMessage->GetMessageType());
      break;
  }

  if (requestIntervalDelay)
  {
    TimeUtil::Sleep(0.050);
  }
}

void WaChat::AddInstance(int p_ConnId, WaChat* p_Instance)
{
  std::unique_lock<std::mutex> lock(s_ConnIdMapMutex);
  s_ConnIdMap[p_ConnId] = p_Instance;
}

void WaChat::RemoveInstance(int p_ConnId)
{
  std::unique_lock<std::mutex> lock(s_ConnIdMapMutex);
  s_ConnIdMap.erase(p_ConnId);
}

WaChat* WaChat::GetInstance(int p_ConnId)
{
  std::unique_lock<std::mutex> lock(s_ConnIdMapMutex);
  auto it = s_ConnIdMap.find(p_ConnId);
  return (it != s_ConnIdMap.end()) ? it->second : nullptr;
}

void WaNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, int p_IsSelf)
{
  WaChat* instance = WaChat::GetInstance(p_ConnId);
  if (instance == nullptr) return;

  ContactInfo contactInfo;
  contactInfo.id = std::string(p_ChatId);
  contactInfo.name = std::string(p_Name);
  contactInfo.isSelf = (p_IsSelf == 1) ? true : false;

  std::shared_ptr<NewContactsNotify> newContactsNotify = std::make_shared<NewContactsNotify>(instance->GetProfileId());
  newContactsNotify->contactInfos = std::vector<ContactInfo>({ contactInfo });

  std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
  deferNotifyRequest->serviceMessage = newContactsNotify;
  instance->SendRequest(deferNotifyRequest);

  free(p_ChatId);
  free(p_Name);
}

void WaNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_LastMessageTime)
{
  WaChat* instance = WaChat::GetInstance(p_ConnId);
  if (instance == nullptr) return;

  ChatInfo chatInfo;
  chatInfo.id = std::string(p_ChatId);
  chatInfo.isUnread = (p_IsUnread == 1);
  chatInfo.isUnreadMention = false; // not supported in wa
  chatInfo.isMuted = (p_IsMuted == 1);
  chatInfo.lastMessageTime = ((int64_t)p_LastMessageTime) * 1000;

  std::shared_ptr<NewChatsNotify> newChatsNotify = std::make_shared<NewChatsNotify>(instance->GetProfileId());
  newChatsNotify->success = true;
  newChatsNotify->chatInfos = std::vector<ChatInfo>({ chatInfo });

  std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
  deferNotifyRequest->serviceMessage = newChatsNotify;
  instance->SendRequest(deferNotifyRequest);

  free(p_ChatId);
}

void WaNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe,
                         char* p_QuotedId, char* p_FilePath, int p_FileStatus, int p_TimeSent, int p_IsRead)
{
  LOG_DEBUG("WaNewMessagesNotify");

  WaChat* instance = WaChat::GetInstance(p_ConnId);
  if (instance == nullptr) return;

  std::string fileInfoStr;
  std::string filePath = std::string(p_FilePath);
  if (!filePath.empty())
  {
    FileInfo fileInfo;
    fileInfo.fileStatus = (FileStatus)p_FileStatus;
    fileInfo.filePath = p_FilePath;
    fileInfoStr = ProtocolUtil::FileInfoToHex(fileInfo);
  }

  ChatMessage chatMessage;
  chatMessage.id = std::string(p_MsgId);
  chatMessage.senderId = std::string(p_SenderId);
  chatMessage.text = std::string(p_Text);
  chatMessage.isOutgoing = (p_FromMe == 1);
  chatMessage.quotedId = std::string(p_QuotedId);
  chatMessage.fileInfo = fileInfoStr;
  chatMessage.timeSent = (((int64_t)p_TimeSent) * 1000) + (std::hash<std::string>{ } (chatMessage.id) % 256);
  chatMessage.isRead = (p_IsRead == 1);

  std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(instance->GetProfileId());
  newMessagesNotify->success = true;
  newMessagesNotify->chatId = std::string(p_ChatId);
  newMessagesNotify->chatMessages = std::vector<ChatMessage>({ chatMessage });

  std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
  deferNotifyRequest->serviceMessage = newMessagesNotify;
  instance->SendRequest(deferNotifyRequest);

  free(p_ChatId);
  free(p_MsgId);
  free(p_SenderId);
  free(p_Text);
  free(p_QuotedId);
  free(p_FilePath);
}

void WaNewStatusNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsOnline, int p_IsTyping)
{
  WaChat* instance = WaChat::GetInstance(p_ConnId);
  if (instance == nullptr) return;

  {
    std::shared_ptr<ReceiveStatusNotify> receiveStatusNotify =
      std::make_shared<ReceiveStatusNotify>(instance->GetProfileId());
    receiveStatusNotify->userId = std::string(p_UserId);
    receiveStatusNotify->isOnline = (p_IsOnline == 1);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
      std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = receiveStatusNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  {
    std::shared_ptr<ReceiveTypingNotify> receiveTypingNotify =
      std::make_shared<ReceiveTypingNotify>(instance->GetProfileId());
    receiveTypingNotify->chatId = std::string(p_ChatId);
    receiveTypingNotify->userId = std::string(p_UserId);
    receiveTypingNotify->isTyping = (p_IsTyping == 1);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
      std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = receiveTypingNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_UserId);
}

void WaNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead)
{
  WaChat* instance = WaChat::GetInstance(p_ConnId);
  if (instance == nullptr) return;

  {
    std::shared_ptr<NewMessageStatusNotify> newMessageStatusNotify =
      std::make_shared<NewMessageStatusNotify>(instance->GetProfileId());
    newMessageStatusNotify->chatId = std::string(p_ChatId);
    newMessageStatusNotify->msgId = std::string(p_MsgId);
    newMessageStatusNotify->isRead = (p_IsRead == 1);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = newMessageStatusNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_MsgId);
}

void WaLogTrace(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Trace(p_Filename, p_LineNo, p_Message);
}

void WaLogDebug(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Debug(p_Filename, p_LineNo, p_Message);
}

void WaLogInfo(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Info(p_Filename, p_LineNo, p_Message);
}

void WaLogWarning(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Warning(p_Filename, p_LineNo, p_Message);
}

void WaLogError(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Error(p_Filename, p_LineNo, p_Message);
}
