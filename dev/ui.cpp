// devui.cpp
//
// Copyright (c) 2019-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "ui.h"

#include <iostream>
#include <regex>
#include <sstream>

#include <unistd.h>

#include "fileutil.h"
#include "protocolutil.h"

Ui::Ui()
{
}

Ui::~Ui()
{
}

static void ShowPrompt()
{
  std::cout << "> ";
  fflush(stdout);
}

static void ShowHelp()
{
  std::cout <<
    "gp          - get profiles\n"
    "sp N        - select profile\n"
    "gl          - get contacts list\n"
    "gc          - get chats\n"
    "sc N        - select/get chat\n"
    "gm [id]     - get messages\n"
    "sm text     - send message\n"
    "rm id text  - reply message\n"
    "sf path     - send file\n"
    "mr id       - mark read\n"
    "dm id       - delete message\n"
    "ty 1/0      - typing enable/disable\n"
    "st 1/0      - status online enable/disable\n"
    "h           - help\n"
    "q           - quit\n";
}

void Ui::Run()
{
  // Main loop
  ShowHelp();
  ShowPrompt();
  m_CurrentProfileId = m_Protocols.begin()->first;
  bool running = true;
  while (running)
  {
    std::string cmdline;
    std::getline(std::cin, cmdline);
    std::stringstream cmdss(cmdline);  
    std::string cmd;
    cmdss >> cmd;
    std::unique_lock<std::mutex> lock(m_StdoutMutex);
    
    // Get Profiles
    if (cmd == "gp")
    {
      for (auto& protocol : m_Protocols)
      {
        std::cout << protocol.second->GetProfileId() << "\n";
      }

      ShowPrompt();
    }
    // Select Profile
    else if (cmd == "sp")
    {
      std::string id;
      cmdss >> id;
      auto it = m_Protocols.find(id);
      if (it != m_Protocols.end())
      {
        m_CurrentProfileId = it->first;
        std::cout << "Set current profile " << m_CurrentProfileId << "\n";
      }
      else
      {
        std::cout << "Invalid profile id\n";
      }

      ShowPrompt();
    }
    // Get Chats
    else if (cmd == "gc")
    {
      std::shared_ptr<GetChatsRequest> getChatsRequest = std::make_shared<GetChatsRequest>();
      m_Protocols[m_CurrentProfileId]->SendRequest(getChatsRequest);
    }
    // Get Contact List
    else if (cmd == "gl")
    {
      std::shared_ptr<GetContactsRequest> getContactsRequest = std::make_shared<GetContactsRequest>();
      m_Protocols[m_CurrentProfileId]->SendRequest(getContactsRequest);
    }
    // Select Chat
    else if (cmd == "sc")
    {
      std::string id;
      cmdss >> id;
      auto it = m_Chats[m_CurrentProfileId].find(id);
      if (it != m_Chats[m_CurrentProfileId].end())
      {
        m_CurrentChatId = id;
        std::cout << "Set current chat " << m_CurrentChatId << "\n";
      }
      else
      {
        std::cout << "Invalid chat id, creating new chat\n";

        ChatInfo chatInfo;
        chatInfo.id = id;
        m_Chats[m_CurrentProfileId].insert(id);
        m_ChatInfos[id] = chatInfo;
        m_CurrentChatId = id;
      }

      ShowPrompt();
    }
    // Get Messages
    else if (cmd == "gm")
    {
      std::string fromId;
      cmdss >> fromId;
      std::shared_ptr<GetMessagesRequest> getMessagesRequest = std::make_shared<GetMessagesRequest>();
      getMessagesRequest->chatId = m_CurrentChatId;
      getMessagesRequest->fromMsgId = fromId;
      getMessagesRequest->limit = 5;
      m_Protocols[m_CurrentProfileId]->SendRequest(getMessagesRequest);
    }
    // Send Message
    else if (cmd == "sm")
    {
      std::string text;
      getline(cmdss, text);
      text = std::regex_replace(text, std::regex("^ +"), "");
      std::shared_ptr<SendMessageRequest> sendMessageRequest = std::make_shared<SendMessageRequest>();
      sendMessageRequest->chatId = m_CurrentChatId;
      sendMessageRequest->chatMessage.text = text;
      m_Protocols[m_CurrentProfileId]->SendRequest(sendMessageRequest);
    }
    // Reply Message
    else if (cmd == "rm")
    {
      std::string quotedId;
      cmdss >> quotedId;
      std::string text;
      getline(cmdss, text);
      text = std::regex_replace(text, std::regex("^ +"), "");
      std::shared_ptr<SendMessageRequest> sendMessageRequest = std::make_shared<SendMessageRequest>();
      sendMessageRequest->chatId = m_CurrentChatId;
      sendMessageRequest->chatMessage.text = text;
      //sendMessageRequest->chatMessage.quotedId = quotedId;
      //sendMessageRequest->chatMessage.quotedText = "Text"; // only used by wa
      //sendMessageRequest->chatMessage.quotedSender = "6511111111@s.whatsapp.net"; // only used by wa
      m_Protocols[m_CurrentProfileId]->SendRequest(sendMessageRequest);
    }
    // Send File
    else if (cmd == "sf")
    {
      std::string path;
      getline(cmdss, path);
      path = std::regex_replace(path, std::regex("^ +"), "");

      FileInfo fileInfo;
      fileInfo.filePath = path;
      fileInfo.fileType = FileUtil::GetMimeType(path);

      std::shared_ptr<SendMessageRequest> sendMessageRequest = std::make_shared<SendMessageRequest>();
      sendMessageRequest->chatId = m_CurrentChatId;
      sendMessageRequest->chatMessage.fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);;
      m_Protocols[m_CurrentProfileId]->SendRequest(sendMessageRequest);
    }
    // Mark Message Read
    else if (cmd == "mr")
    {
      std::string msgId;
      getline(cmdss, msgId);
      msgId = std::regex_replace(msgId, std::regex("^ +"), "");
      std::shared_ptr<MarkMessageReadRequest> markMessageReadRequest = std::make_shared<MarkMessageReadRequest>();
      markMessageReadRequest->chatId = m_CurrentChatId;
      markMessageReadRequest->msgId = msgId;
      m_Protocols[m_CurrentProfileId]->SendRequest(markMessageReadRequest);
    }
    // Delete Message
    else if (cmd == "dm")
    {
      std::string msgId;
      getline(cmdss, msgId);
      msgId = std::regex_replace(msgId, std::regex("^ +"), "");
      std::shared_ptr<DeleteMessageRequest> deleteMessageRequest = std::make_shared<DeleteMessageRequest>();
      deleteMessageRequest->chatId = m_CurrentChatId;
      deleteMessageRequest->msgId = msgId;
      m_Protocols[m_CurrentProfileId]->SendRequest(deleteMessageRequest);
    }
    // Send Typing
    else if (cmd == "ty")
    {
      std::string status;
      getline(cmdss, status);
      status = std::regex_replace(status, std::regex("^ +"), "");
      std::shared_ptr<SendTypingRequest> sendTypingRequest = std::make_shared<SendTypingRequest>();
      sendTypingRequest->chatId = m_CurrentChatId;
      sendTypingRequest->isTyping = (status == "1");
      m_Protocols[m_CurrentProfileId]->SendRequest(sendTypingRequest);
    }
    // Set Status Online
    else if (cmd == "st")
    {
      std::string status;
      getline(cmdss, status);
      status = std::regex_replace(status, std::regex("^ +"), "");
      std::shared_ptr<SetStatusRequest> setStatusRequest = std::make_shared<SetStatusRequest>();
      setStatusRequest->isOnline = (status == "1");
      m_Protocols[m_CurrentProfileId]->SendRequest(setStatusRequest);
    }
    // Help
    else if (cmd == "h")
    {
      ShowHelp();
      ShowPrompt();
    }
    // Quit
    else if (cmd == "q")
    {
      running = false;
    }
    // Empty
    else if (cmd == "")
    {
      ShowPrompt();
    }
    // Unknown Command
    else
    {
      std::cout << "Unknown command \"" << cmd << "\"\n";
      ShowPrompt();
    }
  }
}

void Ui::AddProtocol(std::shared_ptr<Protocol> p_Protocol)
{
  m_Protocols[p_Protocol->GetProfileId()] = p_Protocol;
}

std::unordered_map<std::string, std::shared_ptr<Protocol>>& Ui::GetProtocols()
{
  return m_Protocols;
}

void Ui::MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  std::unique_lock<std::mutex> lock(m_StdoutMutex);
  
  switch (p_ServiceMessage->GetMessageType())
  {
    case NewContactsNotifyType:
      {
        std::shared_ptr<NewContactsNotify> newContactsNotify = std::static_pointer_cast<NewContactsNotify>(p_ServiceMessage);
        {
          const std::vector<ContactInfo>& contactInfos = newContactsNotify->contactInfos;
          for (auto& contactInfo : contactInfos)
          {
            std::cout << "gl id " << contactInfo.id << " name " << contactInfo.name << "\n";
          }
        }
      }
      break;
      
    case NewChatsNotifyType:
      {
        std::shared_ptr<NewChatsNotify> newChatsNotify = std::static_pointer_cast<NewChatsNotify>(p_ServiceMessage);
        if (newChatsNotify->success)
        {
          const std::vector<ChatInfo>& chatInfos = newChatsNotify->chatInfos;
          for (auto& chatInfo : chatInfos)
          {
            m_Chats[newChatsNotify->profileId].insert(chatInfo.id);
            m_ChatInfos[chatInfo.id] = chatInfo;

            if (m_CurrentChatId.empty())
            {
              m_CurrentChatId = chatInfo.id;
              std::cout << "Current chat auto-set to " << m_CurrentChatId << "\n";
            }
          }

          for (auto& chat : m_Chats[newChatsNotify->profileId])
          {
            const ChatInfo& chatInfo = m_ChatInfos[chat]; 
            std::cout << chatInfo.id << " un=" << (int)chatInfo.isUnread << " unm=" <<
              (int)chatInfo.isUnreadMention << " mut=" << (int)chatInfo.isMuted << " t=" << chatInfo.lastMessageTime << "\n";
          }
        }
      }
      break;

    case NewMessagesNotifyType:
      {
        std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::static_pointer_cast<NewMessagesNotify>(p_ServiceMessage);
        if (newMessagesNotify->success)
        {
          {
            const std::vector<ChatMessage>& chatMessages = newMessagesNotify->chatMessages;
            for (auto& chatMessage : chatMessages)
            {
              std::cout << "-- id: " << chatMessage.id << " " << chatMessage.isOutgoing << " qt: " << chatMessage.quotedId << " time: " << chatMessage.timeSent <<
                " isRead: " << chatMessage.isRead <<  "\n";
              std::cout << chatMessage.senderId << ": " << chatMessage.text;

              if (!chatMessage.fileInfo.empty())
              {
                FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(chatMessage.fileInfo);
                std::cout << " (attachment: " << fileInfo.filePath << " " <<
                  (int)fileInfo.fileStatus << ")";
              }

              std::cout << "\n";
            }
          }
        }
      }
      break;

    case SendMessageNotifyType:
      {
        std::shared_ptr<SendMessageNotify> sendMessageNotify = std::static_pointer_cast<SendMessageNotify>(p_ServiceMessage);
        if (sendMessageNotify->success)
        {
          std::cout << "Send ok\n";
        }
        else
        {
          const ChatMessage& chatMessage = sendMessageNotify->chatMessage;
          std::cout << "Send failed (" << chatMessage.text << ")\n";
        }
      }
      break;

    case MarkMessageReadNotifyType:
      {
        std::shared_ptr<MarkMessageReadNotify> markMessageReadNotify = std::static_pointer_cast<MarkMessageReadNotify>(p_ServiceMessage);
        if (markMessageReadNotify->success)
        {
          std::cout << "Mark read ok\n";
        }
        else
        {
          std::cout << "Mark read failed\n";
        }
      }
      break;

    case DeleteMessageNotifyType:
      {
        std::shared_ptr<DeleteMessageNotify> deleteMessageNotify = std::static_pointer_cast<DeleteMessageNotify>(p_ServiceMessage);
        if (deleteMessageNotify->success)
        {
          std::cout << "Delete ok\n";
        }
        else
        {
          std::cout << "Delete failed\n";
        }
      }
      break;

    case SendTypingNotifyType:
      {
        std::shared_ptr<SendTypingNotify> sendTypingNotify = std::static_pointer_cast<SendTypingNotify>(p_ServiceMessage);
        if (sendTypingNotify->success)
        {
          std::cout << "Send typing ok\n";
        }
        else
        {
          std::cout << "Send typing failed\n";
        }
      }
      break;

    case SetStatusNotifyType:
      {
        std::shared_ptr<SetStatusNotify> setStatusNotify = std::static_pointer_cast<SetStatusNotify>(p_ServiceMessage);
        if (setStatusNotify->success)
        {
          std::cout << "Set status ok\n";
        }
        else
        {
          std::cout << "Set status failed\n";
        }
      }
      break;

    case ReceiveTypingNotifyType:
      {
        std::shared_ptr<ReceiveTypingNotify> receiveTypingNotify = std::static_pointer_cast<ReceiveTypingNotify>(p_ServiceMessage);
        std::string userId = receiveTypingNotify->userId;
        std::string chatId = receiveTypingNotify->chatId;
        bool isTyping = receiveTypingNotify->isTyping;
        std::cout << "Received " << userId << " in " << chatId << " is " << (isTyping ? "typing" : "idle") << "\n";
      }
      break;

    case ReceiveStatusNotifyType:
      {
        std::shared_ptr<ReceiveStatusNotify> receiveStatusNotify = std::static_pointer_cast<ReceiveStatusNotify>(p_ServiceMessage);
        std::string userId = receiveStatusNotify->userId;
        bool isOnline = receiveStatusNotify->isOnline;
        std::cout << "Received " << userId << " is " << (isOnline ? "online" : "offline") << "\n";
      }
      break;

    case NewMessageStatusNotifyType:
      {
        std::shared_ptr<NewMessageStatusNotify> newMessageStatusNotify = std::static_pointer_cast<NewMessageStatusNotify>(p_ServiceMessage);
        std::string chatId = newMessageStatusNotify->chatId;
        std::string msgId = newMessageStatusNotify->msgId;
        bool isRead = newMessageStatusNotify->isRead;
        std::cout << "New message status from " << chatId << " msg " << msgId << " is " <<
          (isRead ? "read" : "unread") << "\n";
      }
      break;

    case ConnectNotifyType:
      {
        std::shared_ptr<ConnectNotify> connectNotify = std::static_pointer_cast<ConnectNotify>(p_ServiceMessage);
        if (connectNotify->success)
        {
          std::cout << "Connected " << connectNotify->profileId << "\n";

          if (!m_Protocols[connectNotify->profileId]->HasFeature(FeatureAutoGetChatsOnLogin))
          {
            std::shared_ptr<GetChatsRequest> getChatsRequest = std::make_shared<GetChatsRequest>();
            m_Protocols[connectNotify->profileId]->SendRequest(getChatsRequest);
          }
        }
        else
        {
          std::cout << "Connect failed " << connectNotify->profileId << "\n";
        }
      }
      break;
      
    default:
      std::cout << "Unknown ServiceMessage type " << p_ServiceMessage->GetMessageType() << "\n";
      break;
  }

  ShowPrompt();
}

void Ui::RunKeyDump()
{
  std::cout << "Key dump mode is not supported in dev app\n";
}
