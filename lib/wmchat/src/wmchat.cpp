// wmchat.cpp
//
// Copyright (c) 2020-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "wmchat.h"

#include <iostream>

#include <sys/stat.h>

#include "appconfig.h"
#include "libcgowm.h"
#include "log.h"
#include "messagecache.h"
#include "protocolutil.h"
#include "status.h"
#include "timeutil.h"

std::mutex WmChat::s_ConnIdMapMutex;
std::map<int, WmChat*> WmChat::s_ConnIdMap;

extern "C" WmChat* CreateWmChat()
{
  return new WmChat();
}

WmChat::WmChat()
{
  m_ProfileId = GetName();
}

WmChat::~WmChat()
{
}

std::string WmChat::GetProfileId() const
{
  return m_ProfileId;
}

bool WmChat::HasFeature(ProtocolFeature p_ProtocolFeature) const
{
  ProtocolFeature customFeatures = FeatureEditMessagesWithinFifteenMins;
  return (p_ProtocolFeature & customFeatures);
}

bool WmChat::SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId)
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

  std::cout << "\n";
  std::cout << "Open WhatsApp on your phone, click the menu bar and select \"Linked deviced\".\n";
  std::cout << "Click on \"Link a device\", unlock the phone and aim its camera at the\n";
  std::cout << "Qr code displayed on the computer screen.\n";
  std::cout << "\n";


  m_ProfileId = m_ProfileId + "_" + phoneNumber;
  std::string profileDir = p_ProfilesDir + "/" + m_ProfileId;

  mkdir(profileDir.c_str(), 0777);

  p_ProfileId = m_ProfileId;

  std::string proxyUrl = GetProxyUrl();
  int connId = CWmInit(const_cast<char*>(profileDir.c_str()), const_cast<char*>(proxyUrl.c_str()));
  if (connId == -1) return false;

  m_ConnId = connId;
  AddInstance(m_ConnId, this);
  MessageCache::AddProfile(m_ProfileId, false, s_CacheDirVersion);

  bool rv = Login();
  if (!rv) return false;

  return true;
}

bool WmChat::LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId)
{
  m_ProfileDir = p_ProfilesDir + "/" + p_ProfileId;
  m_ProfileId = p_ProfileId;

  std::string proxyUrl = GetProxyUrl();
  m_ConnId = CWmInit(const_cast<char*>(m_ProfileDir.c_str()), const_cast<char*>(proxyUrl.c_str()));
  if (m_ConnId == -1) return false;

  AddInstance(m_ConnId, this);
  MessageCache::AddProfile(m_ProfileId, false, s_CacheDirVersion);

  return true;
}

bool WmChat::CloseProfile()
{
  int rv = CWmCleanup(m_ConnId);
  RemoveInstance(m_ConnId);
  m_ConnId = -1;
  m_ProfileDir = "";
  m_ProfileId = "";
  return (rv == 0);
}

bool WmChat::Login()
{
  int rv = 0;
  if (!m_Running)
  {
    m_Running = true;
    m_Thread = std::thread(&WmChat::Process, this);

    rv = CWmLogin(m_ConnId);
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

bool WmChat::Logout()
{
  int rv = 0;
  if (m_Running)
  {
    rv = CWmLogout(m_ConnId);
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

void WmChat::Process()
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

      if (!m_MessageHandler)
      {
        m_ProcessCondVar.wait(lock);
        continue;
      }

      requestMessage = m_RequestsQueue.front();
      m_RequestsQueue.pop_front();
    }

    PerformRequest(requestMessage);
  }
}

void WmChat::SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  std::unique_lock<std::mutex> lock(m_ProcessMutex);
  m_RequestsQueue.push_back(p_RequestMessage);
  m_ProcessCondVar.notify_one();
}

void WmChat::SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler)
{
  m_MessageHandler = p_MessageHandler;
  m_ProcessCondVar.notify_one();
}

void WmChat::CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  MessageCache::AddFromServiceMessage(m_ProfileId, p_ServiceMessage);

  if (!m_MessageHandler)
  {
    LOG_DEBUG("message handler not set");
    return;
  }

  m_MessageHandler(p_ServiceMessage);
}

void WmChat::PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  bool requestIntervalDelay = true;

  switch (p_RequestMessage->GetMessageType())
  {
    case GetContactsRequestType:
      {
        LOG_DEBUG("get contacts");
        std::shared_ptr<GetContactsRequest> getContactsRequest = std::static_pointer_cast<GetContactsRequest>(
          p_RequestMessage);
        MessageCache::FetchContacts(m_ProfileId);
      }
      break;

    case GetChatsRequestType:
      {
        LOG_DEBUG("get chats");
        std::shared_ptr<GetChatsRequest> getChatsRequest =
          std::static_pointer_cast<GetChatsRequest>(p_RequestMessage);
        MessageCache::FetchChats(m_ProfileId);
      }
      break;

    case GetStatusRequestType:
      {
        LOG_DEBUG("get status");
        std::shared_ptr<GetStatusRequest> getStatusRequest =
          std::static_pointer_cast<GetStatusRequest>(p_RequestMessage);
        std::string userId = getStatusRequest->userId;

        CWmGetStatus(m_ConnId, const_cast<char*>(userId.c_str()));
      }
      break;

    case GetMessageRequestType:
      {
        LOG_DEBUG("get message");
        std::shared_ptr<GetMessageRequest> getMessageRequest =
          std::static_pointer_cast<GetMessageRequest>(p_RequestMessage);
        MessageCache::FetchOneMessage(m_ProfileId, getMessageRequest->chatId, getMessageRequest->msgId,
                                      false /*p_Sync*/);
      }
      break;

    case GetMessagesRequestType:
      {
        LOG_DEBUG("get messages");
        std::shared_ptr<GetMessagesRequest> getMessagesRequest =
          std::static_pointer_cast<GetMessagesRequest>(p_RequestMessage);
        MessageCache::FetchMessagesFrom(m_ProfileId, getMessagesRequest->chatId,
                                        getMessagesRequest->fromMsgId,
                                        getMessagesRequest->limit, false /* p_Sync */);
      }
      break;

    case SendMessageRequestType:
      {
        LOG_DEBUG("send message");
        Status::Set(Status::FlagSending);
        std::shared_ptr<SendMessageRequest> sendMessageRequest =
          std::static_pointer_cast<SendMessageRequest>(p_RequestMessage);
        std::string chatId = sendMessageRequest->chatId;
        std::string editMsgId = "";
        std::string text = sendMessageRequest->chatMessage.text;
        std::string quotedId = sendMessageRequest->chatMessage.quotedId;
        std::string quotedText = sendMessageRequest->chatMessage.quotedText;
        std::string quotedSender = sendMessageRequest->chatMessage.quotedSender;
        std::string filePath;
        std::string fileType;
        int editMsgSent = 0;
        if (!sendMessageRequest->chatMessage.fileInfo.empty())
        {
          FileInfo fileInfo =
            ProtocolUtil::FileInfoFromHex(sendMessageRequest->chatMessage.fileInfo);
          filePath = fileInfo.filePath;
          fileType = fileInfo.fileType;
        }

        int rv =
          CWmSendMessage(m_ConnId, const_cast<char*>(chatId.c_str()), const_cast<char*>(text.c_str()),
                         const_cast<char*>(quotedId.c_str()), const_cast<char*>(quotedText.c_str()),
                         const_cast<char*>(quotedSender.c_str()), const_cast<char*>(filePath.c_str()),
                         const_cast<char*>(fileType.c_str()), const_cast<char*>(editMsgId.c_str()),
                         editMsgSent);
        Status::Clear(Status::FlagSending);

        std::shared_ptr<SendMessageNotify> sendMessageNotify = std::make_shared<SendMessageNotify>(m_ProfileId);
        sendMessageNotify->success = (rv == 0);
        sendMessageNotify->chatId = sendMessageRequest->chatId;
        sendMessageNotify->chatMessage = sendMessageRequest->chatMessage;
        CallMessageHandler(sendMessageNotify);
      }
      break;

    case EditMessageRequestType:
      {
        LOG_DEBUG("edit message");
        Status::Set(Status::FlagSending);
        std::shared_ptr<EditMessageRequest> editMessageRequest =
          std::static_pointer_cast<EditMessageRequest>(p_RequestMessage);
        std::string chatId = editMessageRequest->chatId;
        std::string editMsgId = editMessageRequest->msgId;
        std::string text = editMessageRequest->chatMessage.text;
        std::string quotedId = editMessageRequest->chatMessage.quotedId;
        std::string quotedText = editMessageRequest->chatMessage.quotedText;
        std::string quotedSender = editMessageRequest->chatMessage.quotedSender;
        std::string filePath;
        std::string fileType;
        int editMsgSent = editMessageRequest->chatMessage.timeSent / 1000;
        if (!editMessageRequest->chatMessage.fileInfo.empty())
        {
          FileInfo fileInfo =
            ProtocolUtil::FileInfoFromHex(editMessageRequest->chatMessage.fileInfo);
          filePath = fileInfo.filePath;
          fileType = fileInfo.fileType;
        }

        CWmSendMessage(m_ConnId, const_cast<char*>(chatId.c_str()), const_cast<char*>(text.c_str()),
                       const_cast<char*>(quotedId.c_str()), const_cast<char*>(quotedText.c_str()),
                       const_cast<char*>(quotedSender.c_str()), const_cast<char*>(filePath.c_str()),
                       const_cast<char*>(fileType.c_str()), const_cast<char*>(editMsgId.c_str()),
                       editMsgSent);
        Status::Clear(Status::FlagSending);
      }
      break;

    case MarkMessageReadRequestType:
      {
        LOG_DEBUG("mark message read");
        std::shared_ptr<MarkMessageReadRequest> markMessageReadRequest =
          std::static_pointer_cast<MarkMessageReadRequest>(p_RequestMessage);
        std::string chatId = markMessageReadRequest->chatId;
        std::string msgId = markMessageReadRequest->msgId;

        int rv = CWmMarkMessageRead(m_ConnId, const_cast<char*>(chatId.c_str()),
                                    const_cast<char*>(msgId.c_str()));

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

        int rv = CWmDeleteMessage(m_ConnId, const_cast<char*>(chatId.c_str()),
                                  const_cast<char*>(msgId.c_str()));
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

        int rv = CWmSendTyping(m_ConnId, const_cast<char*>(chatId.c_str()), isTyping);

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

        int rv = CWmSendStatus(m_ConnId, isOnline);

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

    case DownloadFileRequestType:
      {
        std::shared_ptr<DownloadFileRequest> downloadFileRequest =
          std::static_pointer_cast<DownloadFileRequest>(p_RequestMessage);
        std::string chatId = downloadFileRequest->chatId;
        std::string msgId = downloadFileRequest->msgId;
        std::string fileId = downloadFileRequest->fileId;
        DownloadFileAction downloadFileAction = downloadFileRequest->downloadFileAction;

        CWmDownloadFile(m_ConnId,
                        const_cast<char*>(chatId.c_str()),
                        const_cast<char*>(msgId.c_str()),
                        const_cast<char*>(fileId.c_str()),
                        downloadFileAction
                        );
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

std::string WmChat::GetProxyUrl() const
{
  const std::string proxyHost = AppConfig::GetStr("proxy_host");
  const int proxyPort = AppConfig::GetNum("proxy_port");
  if (!proxyHost.empty() && (proxyPort != 0))
  {
    std::string proxyUrl = "socks5://";
    const std::string proxyUser = AppConfig::GetStr("proxy_user");
    const std::string proxyPass = AppConfig::GetStr("proxy_pass");
    if (!proxyUser.empty())
    {
      proxyUrl += proxyUser + ":" + proxyPass + "@";
    }

    proxyUrl += proxyHost + ":" + std::to_string(proxyPort);
    return proxyUrl;
  }

  return "";
}

void WmChat::AddInstance(int p_ConnId, WmChat* p_Instance)
{
  std::unique_lock<std::mutex> lock(s_ConnIdMapMutex);
  s_ConnIdMap[p_ConnId] = p_Instance;
}

void WmChat::RemoveInstance(int p_ConnId)
{
  std::unique_lock<std::mutex> lock(s_ConnIdMapMutex);
  s_ConnIdMap.erase(p_ConnId);
}

WmChat* WmChat::GetInstance(int p_ConnId)
{
  std::unique_lock<std::mutex> lock(s_ConnIdMapMutex);
  auto it = s_ConnIdMap.find(p_ConnId);
  return (it != s_ConnIdMap.end()) ? it->second : nullptr;
}

void WmNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, int p_IsSelf)
{
  WmChat* instance = WmChat::GetInstance(p_ConnId);
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

void WmNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_LastMessageTime)
{
  WmChat* instance = WmChat::GetInstance(p_ConnId);
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

void WmNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe,
                         char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent,
                         int p_IsRead)
{
  LOG_DEBUG("WaNewMessagesNotify");

  WmChat* instance = WmChat::GetInstance(p_ConnId);
  if (instance == nullptr) return;

  std::string fileInfoStr;
  std::string fileId = std::string(p_FileId);
  if (!fileId.empty())
  {
    FileInfo fileInfo;
    fileInfo.fileStatus = (FileStatus)p_FileStatus;
    fileInfo.fileId = fileId;
    fileInfo.filePath = std::string(p_FilePath);
    fileInfoStr = ProtocolUtil::FileInfoToHex(fileInfo);
  }

  ChatMessage chatMessage;
  chatMessage.id = std::string(p_MsgId);
  chatMessage.senderId = std::string(p_SenderId);
  chatMessage.text = std::string(p_Text);
  chatMessage.isOutgoing = (p_FromMe == 1);
  chatMessage.quotedId = std::string(p_QuotedId);
  chatMessage.fileInfo = fileInfoStr;
  chatMessage.timeSent = (((int64_t)p_TimeSent) * 1000) + (std::hash<std::string>{ }(chatMessage.id) % 256);
  chatMessage.isRead = (p_IsRead == 1);

  std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(instance->GetProfileId());
  newMessagesNotify->success = true;
  newMessagesNotify->chatId = std::string(p_ChatId);
  newMessagesNotify->chatMessages = std::vector<ChatMessage>({ chatMessage });
  newMessagesNotify->cached = false;
  newMessagesNotify->sequence = true;

  std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
  deferNotifyRequest->serviceMessage = newMessagesNotify;
  instance->SendRequest(deferNotifyRequest);

  free(p_ChatId);
  free(p_MsgId);
  free(p_SenderId);
  free(p_Text);
  free(p_QuotedId);
  free(p_FileId);
}

void WmNewStatusNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsOnline, int p_IsTyping, int p_TimeSeen)
{
  WmChat* instance = WmChat::GetInstance(p_ConnId);
  if (instance == nullptr) return;

  std::string chatId(p_ChatId);
  std::string userId(p_UserId);

  {
    std::shared_ptr<ReceiveStatusNotify> receiveStatusNotify =
      std::make_shared<ReceiveStatusNotify>(instance->GetProfileId());
    receiveStatusNotify->userId = userId;
    receiveStatusNotify->isOnline = (p_IsOnline == 1);
    receiveStatusNotify->timeSeen = (p_TimeSeen > 0) ? (((int64_t)p_TimeSeen) * 1000) : -1;

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
      std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = receiveStatusNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  if (!chatId.empty())
  {
    std::shared_ptr<ReceiveTypingNotify> receiveTypingNotify =
      std::make_shared<ReceiveTypingNotify>(instance->GetProfileId());
    receiveTypingNotify->chatId = chatId;
    receiveTypingNotify->userId = userId;
    receiveTypingNotify->isTyping = (p_IsTyping == 1);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
      std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = receiveTypingNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_UserId);
}

void WmNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead)
{
  WmChat* instance = WmChat::GetInstance(p_ConnId);
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

void WmNewMessageFileNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_FilePath, int p_FileStatus,
                            int p_Action)
{
  WmChat* instance = WmChat::GetInstance(p_ConnId);
  if (instance == nullptr) return;

  {
    FileInfo fileInfo;
    fileInfo.fileStatus = static_cast<FileStatus>(p_FileStatus);
    fileInfo.filePath = std::string(p_FilePath);

    std::shared_ptr<NewMessageFileNotify> newMessageFileNotify =
      std::make_shared<NewMessageFileNotify>(instance->GetProfileId());
    newMessageFileNotify->chatId = std::string(p_ChatId);
    newMessageFileNotify->msgId = std::string(p_MsgId);
    newMessageFileNotify->fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
    newMessageFileNotify->downloadFileAction = static_cast<DownloadFileAction>(p_Action);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = newMessageFileNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_MsgId);
  free(p_FilePath);
}

void WmSetStatus(int p_Flags)
{
  Status::Set(p_Flags);
}

void WmClearStatus(int p_Flags)
{
  Status::Clear(p_Flags);
}

void WmLogTrace(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Trace(p_Filename, p_LineNo, "%s", p_Message);
}

void WmLogDebug(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Debug(p_Filename, p_LineNo, "%s", p_Message);
}

void WmLogInfo(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Info(p_Filename, p_LineNo, "%s", p_Message);
}

void WmLogWarning(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Warning(p_Filename, p_LineNo, "%s", p_Message);
}

void WmLogError(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Error(p_Filename, p_LineNo, "%s", p_Message);
}
