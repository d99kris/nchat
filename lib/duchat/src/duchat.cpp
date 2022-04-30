// duchat.cpp
//
// Copyright (c) 2020-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "duchat.h"

#include <iostream>
#include <map>

#include <sys/stat.h>

#include "log.h"
#include "status.h"

DuChat::DuChat()
{
}

DuChat::~DuChat()
{
}

std::string DuChat::GetProfileId() const
{
  return m_ProfileId;
}

bool DuChat::HasFeature(ProtocolFeature p_ProtocolFeature) const
{
  ProtocolFeature customFeatures = FeatureNone;
  return (p_ProtocolFeature & customFeatures);
}

bool DuChat::SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId)
{
  std::cout << "Enter phone number: ";
  std::string phoneNumber;
  std::getline(std::cin, phoneNumber);

  m_ProfileId = m_ProfileId + "_" + phoneNumber;
  std::string profileDir = p_ProfilesDir + "/" + m_ProfileId;

  mkdir(profileDir.c_str(), 0777);

  p_ProfileId = m_ProfileId;

  return true;
}

bool DuChat::LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId)
{
  (void)p_ProfilesDir;
  m_ProfileId = p_ProfileId;
  return true;
}

bool DuChat::CloseProfile()
{
  m_ProfileId = "";
  return true;
}

bool DuChat::Login()
{
  Status::Set(Status::FlagOnline);

  if (!m_Running)
  {
    m_Running = true;
    m_Thread = std::thread(&DuChat::Process, this);

    std::shared_ptr<ConnectNotify> connectNotify = std::make_shared<ConnectNotify>(m_ProfileId);
    connectNotify->success = true;

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = connectNotify;
    SendRequest(deferNotifyRequest);
  }
  return true;
}

bool DuChat::Logout()
{
  Status::Clear(Status::FlagOnline);

  if (m_Running)
  {
    std::unique_lock<std::mutex> lock(m_ProcessMutex);
    m_Running = false;
    m_ProcessCondVar.notify_one();
  }

  if (m_Thread.joinable())
  {
    m_Thread.join();
  }
  return true;
}

void DuChat::Process()
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

void DuChat::SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  std::unique_lock<std::mutex> lock(m_ProcessMutex);
  m_RequestsQueue.push_back(p_RequestMessage);
  m_ProcessCondVar.notify_one();
}


void DuChat::SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler)
{
  m_MessageHandler = p_MessageHandler;
}

void DuChat::PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  if (!m_MessageHandler) return;

  static std::map<std::string, std::vector<ChatMessage>> s_Messages;

  switch (p_RequestMessage->GetMessageType())
  {
    case GetChatsRequestType:
      {
        std::shared_ptr<GetChatsRequest> getChatsRequest = std::static_pointer_cast<GetChatsRequest>(p_RequestMessage);
        std::shared_ptr<NewChatsNotify> newChatsNotify = std::make_shared<NewChatsNotify>(m_ProfileId);
        std::shared_ptr<NewContactsNotify> newContactsNotify = std::make_shared<NewContactsNotify>(m_ProfileId);
        newChatsNotify->success = true;

        static std::vector<std::string> names =
        {
          "Alice", "Bob", "Chuck", "Dave", "Eve", "Frank", "Grace", "Heidi", "Ivan",
          "Judy", "Kris", "Lars", "Mallory", "Niaj", "Olivia", "Pat", "Quentin",
          "Rupert", "Sybil", "Trent", "Ulf", "Victor", "Walter", "Xavier", "Yuki",
          "Zeke",
        };

        int64_t t = 1627728640;
        s_Messages.clear();
        newChatsNotify->chatInfos.clear();
        for (auto& name : names)
        {
          std::string id = name + "_0";

          ChatInfo chatInfo;
          chatInfo.id = id;
          chatInfo.lastMessageTime = t;
          newChatsNotify->chatInfos.push_back(chatInfo);

          ContactInfo contactInfo;
          contactInfo.id = id;
          contactInfo.name = name;
          newContactsNotify->contactInfos.push_back(contactInfo);

          ChatMessage chatMessage;
          chatMessage.id = id + "_" + std::to_string(t);
          chatMessage.senderId = id;
          chatMessage.text = "Hello world! \xF0\x9F\x8C\x8E";
          chatMessage.timeSent = (t * 1000);
          chatMessage.isOutgoing = false;
          chatMessage.isRead = true;
          t = t - 100;
          s_Messages[id].push_back(chatMessage);
        }

        m_MessageHandler(newChatsNotify);
        m_MessageHandler(newContactsNotify);
      }
      break;

    case GetMessagesRequestType:
      {
        std::shared_ptr<GetMessagesRequest> getMessagesRequest = std::static_pointer_cast<GetMessagesRequest>(
          p_RequestMessage);
        std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(m_ProfileId);
        newMessagesNotify->success = true;
        newMessagesNotify->chatId = getMessagesRequest->chatId;

        newMessagesNotify->chatMessages = s_Messages[getMessagesRequest->chatId];
        m_MessageHandler(newMessagesNotify);
      }
      break;

    case SendMessageRequestType:
      {
        std::shared_ptr<SendMessageRequest> sendMessageRequest = std::static_pointer_cast<SendMessageRequest>(
          p_RequestMessage);
        std::shared_ptr<SendMessageNotify> sendMessageNotify = std::make_shared<SendMessageNotify>(m_ProfileId);
        sendMessageNotify->success = true;
        sendMessageNotify->chatId = sendMessageRequest->chatId;
        sendMessageNotify->chatMessage = sendMessageRequest->chatMessage;
        m_MessageHandler(sendMessageNotify);
      }
      break;

    case DeferNotifyRequestType:
      {
        std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::static_pointer_cast<DeferNotifyRequest>(
          p_RequestMessage);
        m_MessageHandler(deferNotifyRequest->serviceMessage);
      }
      break;

    default:
      LOG_DEBUG("unknown request message %d", p_RequestMessage->GetMessageType());
      break;
  }
}
