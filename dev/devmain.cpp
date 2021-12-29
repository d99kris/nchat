// devmain.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>

#include <path.hpp>

#include "apputil.h"
#include "fileutil.h"
#include "log.h"

#include "tgchat.h"

#ifdef HAS_DUMMY
#include "duchat.h"
#endif

#ifdef HAS_WHATSAPP
#include "wachat.h"
#endif

bool SetupProfile();
void ShowHelp();
void ShowPrompt();

static std::mutex m_StdoutMutex;
static std::map<std::string, std::shared_ptr<Protocol>> s_Protocols;
static std::map<std::string, std::set<std::string>> s_Chats;
static std::map<std::string, ChatInfo> s_ChatInfos;
static std::string currentProfileId = s_Protocols.begin()->first;
static std::string currentChatId;

static std::vector<std::shared_ptr<Protocol>> GetProtocols()
{
  std::vector<std::shared_ptr<Protocol>> protocols =
  {
#ifdef HAS_DUMMY
    std::make_shared<DuChat>(),
#endif
    std::make_shared<TgChat>(),
#ifdef HAS_WHATSAPP
    std::make_shared<WaChat>(),
#endif
  };

  return protocols;
}

void MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
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
            s_Chats[newChatsNotify->profileId].insert(chatInfo.id);
            s_ChatInfos[chatInfo.id] = chatInfo;

            if (currentChatId.empty())
            {
              currentChatId = chatInfo.id;
              std::cout << "Current chat auto-set to " << currentChatId << "\n";
            }
          }

          for (auto& chat : s_Chats[newChatsNotify->profileId])
          {
            const ChatInfo& chatInfo = s_ChatInfos[chat]; 
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
              std::cout << "-- id: " << chatMessage.id << " " << chatMessage.isOutgoing << " quotedId: " << chatMessage.quotedId << " filePath: " <<
                chatMessage.filePath << " time: " << chatMessage.timeSent <<
                " isRead: " << chatMessage.isRead <<  "\n";
              std::cout << chatMessage.senderId << ": " << chatMessage.text << "\n";
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
        bool isTyping = receiveTypingNotify->isTyping;
        std::cout << "Received is " << (isTyping ? "typing" : "idle") << "\n";
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

          if (!s_Protocols[connectNotify->profileId]->HasFeature(FeatureAutoGetChatsOnLogin))
          {
            std::shared_ptr<GetChatsRequest> getChatsRequest = std::make_shared<GetChatsRequest>();
            s_Protocols[connectNotify->profileId]->SendRequest(getChatsRequest);
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

int main(int argc, char *argv[])
{
  // Defaults
  umask(S_IRWXG | S_IRWXO);
  FileUtil::SetApplicationDir(std::string(getenv("HOME")) + std::string("/.nchat"));
  Log::SetVerboseLevel(Log::INFO_LEVEL);

  // Argument handling
  std::vector<std::string> args(argv + 1, argv + argc);
  for (auto it = args.begin(); it != args.end(); ++it)
  {
    if (((*it == "-d") || (*it == "--configdir")) && (std::distance(it + 1, args.end()) > 0))
    {
      ++it;
      FileUtil::SetApplicationDir(*it);
    }
    else if ((*it == "-e") || (*it == "--verbose"))
    {
      Log::SetVerboseLevel(Log::DEBUG_LEVEL);
    }
    else if ((*it == "-ee") || (*it == "--extra-verbose"))
    {
      Log::SetVerboseLevel(Log::TRACE_LEVEL);
    }
    else if ((*it == "-h") || (*it == "--help"))
    {
      //ShowHelp();
      return 0;
    }
    else if ((*it == "-s") || (*it == "--setup"))
    {
      SetupProfile();
      return 0;
    }
    else if ((*it == "-v") || (*it == "--version"))
    {
      //ShowVersion();
      return 0;
    }
    else
    {
      //ShowHelp();
      return 1;
    }
  }

  // Ensure application dir exists
  if (!apathy::Path(FileUtil::GetApplicationDir()).exists())
  {
    apathy::Path::makedirs(FileUtil::GetApplicationDir());
  }

  // Ensure application profiles dir exists
  std::string profilesDir = FileUtil::GetApplicationDir() + std::string("/profiles");
  if (!apathy::Path(profilesDir).exists())
  {
    apathy::Path::makedirs(profilesDir);
  }

  // Init logging
  const std::string& logPath = FileUtil::GetApplicationDir() + std::string("/log.txt");
  Log::SetPath(logPath);
  std::string appNameVersion = AppUtil::GetAppNameVersion();
  LOG_INFO("starting %s", appNameVersion.c_str());
  

  // Load Profiles
  const std::vector<apathy::Path>& profilePaths = apathy::Path::listdir(profilesDir);
  for (auto& profilePath : profilePaths)
  {
    std::stringstream ss(profilePath.filename());
    std::string protocolName;
    if (!std::getline(ss, protocolName, '_'))
    {
      std::cout << "some err!\n";
      return 1;
    }

    std::vector<std::shared_ptr<Protocol>> allProtocols = GetProtocols();
    for (auto& protocol : allProtocols)
    {
      if (protocol->GetProfileId() == protocolName)
      {
        std::cout << "Loading " << profilePath.filename() << "\n";
        protocol->LoadProfile(profilesDir, profilePath.filename());
        s_Protocols[protocol->GetProfileId()] = protocol;
      }
    }
  }

  if (s_Protocols.empty())
  {
    std::cout << "No profiles set up, exiting.\n";
    return 1;
  }

  // Login
  for (auto& protocol : s_Protocols)
  {
    protocol.second->SetMessageHandler(MessageHandler);
    std::cout << "Login " << protocol.second->GetProfileId() << "\n";
    protocol.second->Login();
  }
  
  // Main loop
  ShowHelp();
  ShowPrompt();
  currentProfileId = s_Protocols.begin()->first;
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
      for (auto& protocol : s_Protocols)
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
      auto it = s_Protocols.find(id);
      if (it != s_Protocols.end())
      {
        currentProfileId = it->first;
        std::cout << "Set current profile " << currentProfileId << "\n";
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
      s_Protocols[currentProfileId]->SendRequest(getChatsRequest);
    }
    // Get Contact List
    else if (cmd == "gl")
    {
      std::shared_ptr<GetContactsRequest> getContactsRequest = std::make_shared<GetContactsRequest>();
      s_Protocols[currentProfileId]->SendRequest(getContactsRequest);
    }
    // Select Chat
    else if (cmd == "sc")
    {
      std::string id;
      cmdss >> id;
      auto it = s_Chats[currentProfileId].find(id);
      if (it != s_Chats[currentProfileId].end())
      {
        currentChatId = id;
        std::cout << "Set current chat " << currentChatId << "\n";
      }
      else
      {
        std::cout << "Invalid chat id, creating new chat\n";

        ChatInfo chatInfo;
        chatInfo.id = id;
        s_Chats[currentProfileId].insert(id);
        s_ChatInfos[id] = chatInfo;
        currentChatId = id;
      }

      ShowPrompt();
    }
    // Get Messages
    else if (cmd == "gm")
    {
      std::string fromId;
      cmdss >> fromId;
      std::string isOutgoing;
      cmdss >> isOutgoing;
      std::shared_ptr<GetMessagesRequest> getMessagesRequest = std::make_shared<GetMessagesRequest>();
      getMessagesRequest->chatId = currentChatId;
      getMessagesRequest->fromMsgId = fromId;
      getMessagesRequest->limit = 5;
      getMessagesRequest->fromIsOutgoing = (isOutgoing == "1");
      s_Protocols[currentProfileId]->SendRequest(getMessagesRequest);
    }
    // Send Message
    else if (cmd == "sm")
    {
      std::string text;
      getline(cmdss, text);
      text = std::regex_replace(text, std::regex("^ +"), "");
      std::shared_ptr<SendMessageRequest> sendMessageRequest = std::make_shared<SendMessageRequest>();
      sendMessageRequest->chatId = currentChatId;
      sendMessageRequest->chatMessage.text = text;
      s_Protocols[currentProfileId]->SendRequest(sendMessageRequest);
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
      sendMessageRequest->chatId = currentChatId;
      sendMessageRequest->chatMessage.text = text;
      //sendMessageRequest->chatMessage.quotedId = quotedId;
      //sendMessageRequest->chatMessage.quotedText = "Text"; // only used by wa
      //sendMessageRequest->chatMessage.quotedSender = "6511111111@s.whatsapp.net"; // only used by wa
      s_Protocols[currentProfileId]->SendRequest(sendMessageRequest);
    }
    // Send File
    else if (cmd == "sf")
    {
      std::string path;
      getline(cmdss, path);
      path = std::regex_replace(path, std::regex("^ +"), "");
      std::shared_ptr<SendMessageRequest> sendMessageRequest = std::make_shared<SendMessageRequest>();
      sendMessageRequest->chatId = currentChatId;
      sendMessageRequest->chatMessage.filePath = path;
      sendMessageRequest->chatMessage.fileType = FileUtil::GetMimeType(path);
      s_Protocols[currentProfileId]->SendRequest(sendMessageRequest);
    }
    // Mark Message Read
    else if (cmd == "mr")
    {
      std::string msgId;
      getline(cmdss, msgId);
      msgId = std::regex_replace(msgId, std::regex("^ +"), "");
      std::shared_ptr<MarkMessageReadRequest> markMessageReadRequest = std::make_shared<MarkMessageReadRequest>();
      markMessageReadRequest->chatId = currentChatId;
      markMessageReadRequest->msgId = msgId;
      s_Protocols[currentProfileId]->SendRequest(markMessageReadRequest);
    }
    // Delete Message
    else if (cmd == "dm")
    {
      std::string msgId;
      getline(cmdss, msgId);
      msgId = std::regex_replace(msgId, std::regex("^ +"), "");
      std::shared_ptr<DeleteMessageRequest> deleteMessageRequest = std::make_shared<DeleteMessageRequest>();
      deleteMessageRequest->chatId = currentChatId;
      deleteMessageRequest->msgId = msgId;
      s_Protocols[currentProfileId]->SendRequest(deleteMessageRequest);
    }
    // Send Typing
    else if (cmd == "ty")
    {
      std::string status;
      getline(cmdss, status);
      status = std::regex_replace(status, std::regex("^ +"), "");
      std::shared_ptr<SendTypingRequest> sendTypingRequest = std::make_shared<SendTypingRequest>();
      sendTypingRequest->chatId = currentChatId;
      sendTypingRequest->isTyping = (status == "1");
      s_Protocols[currentProfileId]->SendRequest(sendTypingRequest);
    }
    // Set Status Online
    else if (cmd == "st")
    {
      std::string status;
      getline(cmdss, status);
      status = std::regex_replace(status, std::regex("^ +"), "");
      std::shared_ptr<SetStatusRequest> setStatusRequest = std::make_shared<SetStatusRequest>();
      setStatusRequest->isOnline = (status == "1");
      s_Protocols[currentProfileId]->SendRequest(setStatusRequest);
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

  // Logout
  for (auto& protocol : s_Protocols)
  {
    std::cout << "Logout " << protocol.second->GetProfileId() << "\n";
    protocol.second->Logout();
    protocol.second->CloseProfile();
  }

  return 0;  
}

void ShowPrompt()
{
  std::cout << "> ";
  fflush(stdout);
}

void ShowHelp()
{
  std::cout <<
    "gp          - get profiles\n"
    "sp N        - select profile\n"
    "gl          - get contacts list\n"
    "gc          - get chats\n"
    "sc N        - select/get chat\n"
    "gm [id] [o] - get messages\n"
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


bool SetupProfile()
{
  std::vector<std::shared_ptr<Protocol>> p_Protocols = GetProtocols();

  std::cout << "Protocols:" << std::endl;
  size_t idx = 0;
  for (auto it = p_Protocols.begin(); it != p_Protocols.end(); ++it, ++idx)
  {
    std::cout << idx << ". " << (*it)->GetProfileId() << std::endl;
  }
  std::cout << idx << ". Exit setup" << std::endl;
  
  size_t selectidx = idx;
  std::cout << "Select protocol (" << selectidx << "): ";
  std::string line;
  std::getline(std::cin, line);

  if (!line.empty())
  {
    selectidx = stoi(line);
  }

  if (selectidx >= p_Protocols.size())
  {
    std::cout << "Setup aborted, exiting." << std::endl;
    return false;
  }

  std::string profileId;
  std::string profilesDir = FileUtil::GetApplicationDir() + std::string("/profiles");
  bool rv = p_Protocols.at(selectidx)->SetupProfile(profilesDir, profileId);
  if (rv)
  {
    std::cout << "Succesfully set up profile " << profileId << "\n";
  }

  return rv;
}
