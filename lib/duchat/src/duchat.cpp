// duchat.cpp
//
// Copyright (c) 2020-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "duchat.h"

#include <algorithm>
#include <iostream>
#include <map>

#include <sys/stat.h>

#include "log.h"
#include "status.h"

extern "C" DuChat* CreateDuChat()
{
  return new DuChat();
}

DuChat::DuChat()
{
  m_ProfileId = GetName();
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

    std::shared_ptr<ConnectNotify> connectNotify =
      std::make_shared<ConnectNotify>(m_ProfileId);
    connectNotify->success = true;

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
      std::make_shared<DeferNotifyRequest>();
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
        std::shared_ptr<GetChatsRequest> getChatsRequest =
          std::static_pointer_cast<GetChatsRequest>(p_RequestMessage);
        std::shared_ptr<NewChatsNotify> newChatsNotify =
          std::make_shared<NewChatsNotify>(m_ProfileId);
        std::shared_ptr<NewContactsNotify> newContactsNotify =
          std::make_shared<NewContactsNotify>(m_ProfileId);
        newChatsNotify->success = true;

        static std::vector<std::pair<std::string, std::string>> messages =
        {
          { "Michael",
            "Would I rather be feared or loved? Easy. Both. I want people to "
            "be afraid of how much they love me." },
          { "Dwight",
            "Whenever I'm about to do something, I think, 'Would an idiot do "
            "that?' And if they would, I do not do that thing." },
          { "Jim",
            "Right now, this is just a job. If I advance any higher in this "
            "company, this would be my career. And, uh, if this were my "
            "career, I’d have to throw myself in front of a train." },
          { "Pam", "There's a lot of beauty in ordinary things. Isn't that "
            "kind of the point?" },
          /* { "Stanley", "" }, */
          { "Phyllis",
            "Andy sings beautifully. And he's really good at dancing. He's "
            "a good speaker. But there's just something there you don't want "
            "to look at." },
          { "Kevin",
            "Mini-cupcakes? As in the mini version of regular cupcakes? Which "
            "is already a mini version of cake? Honestly, where does it end "
            "with you people?" },
          { "Ryan", "I'd rather she be alone than with somebody. Is that love?" },
          { "Angela",
            "Malls are just awful and humiliating. They’re just store after "
            "store of these horrible salespeople making a big fuss out of an "
            "adult shopping in a junior’s section. There are petite adults "
            "who are sort of… smaller who need to wear… maybe a kids’ size 10." },
          { "Oscar",
            "Angela's engaged to a gay man. As a gay man, I'm horrified. As a "
            "friend of Angela's, horrified. As a lover of elegant weddings, "
            "I'm a little excited." },
          { "Kelly", "I have a lot of questions. Number one, how dare you?" },
          { "Meredith",
            "Stop fighting! Just on St. Patrick's Day okay? Just one, perfect "
            "day a year. No hassles. No problems. No kids." },
          { "Creed",
            "I am not offended by homosexuality, in the sixties I made love "
            "to many, many women, often outdoors in the mud & rain. It’s "
            "possible a man could’ve slipped in there. There’d be no way of "
            "knowing." },
          { "Darryl",
            "I've been meaning to join a gym for my health. I used to say I "
            "wanted to live long enough to see a black president. I didn't "
            "realize how easy that would be. So now I want to live long "
            "enough to see a really, really gay president. Or a supermodel "
            "president. I want to see all the different kinds of presidents." },
          { "Toby",
            "Oh, I went zip lining my third day in Costa Rica. I guess the "
            "harness wasn't strapped in exactly right. I broke my neck. And, "
            "I've been in the hospital five weeks now. I still haven't seen "
            "the beach. It's nice to have visitors." },
          { "Erin",
            "Whenever I'm sick, it goes away within a few hours. Except that "
            "once when I was in the hospital from age three to six." },
          { "Gabe",
            "Apparently, I bear a passing resemblance to Abraham Lincoln. "
            "Makes it kind of hard for me to go to places like museums, "
            "historical monuments, elementary schools... I don't see it." },
          { "Andy",
            "I went to Cornell. Ever heard of it? I graduated in four years, "
            "I never studied once, I was drunk the whole time, and I sang in "
            "the acapella group, 'Here Comes Treble'." },
        };

        static std::vector<std::pair<std::string, std::string>> groupMessages =
        {
          { "Stanley", "Maybe you should go into your office, close the door, "
            "and make some calls about jobs?" },
          { "Michael", "I have a job." },
          { "Andy", "For four more days." },
          { "Pam", "Do you have any leads on a job?" },
          { "Michael",
            "Pam, what you don't understand is that at my level you just don't "
            "look in the want-ads for a job. You are head-hunted." },
          { "Jim", "You called any headhunters?" },
          { "Michael", "Any good headhunter knows I am available." },
          { "Dwight",
            "Any really good headhunter would storm your village at sunset with "
            "overwhelming force and cut off your head with a ceremonial knife." },
        };
        std::reverse(groupMessages.begin(), groupMessages.end());

        int64_t t = 1237922000;
        s_Messages.clear();
        newChatsNotify->chatInfos.clear();

        // Individual chats
        for (auto& message : messages)
        {
          std::string name = message.first;
          std::string text = message.second;
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
          chatMessage.text = text;
          chatMessage.timeSent = (t * 1000);
          chatMessage.isOutgoing = false;
          chatMessage.isRead = true;
          t = t - 100;
          s_Messages[id].push_back(chatMessage);
        }

        // Self
        std::string sname = "Stanley";
        std::string sid = sname + "_0";
        {
          ContactInfo scontactInfo;
          scontactInfo.id = sid;
          scontactInfo.name = sname;
          scontactInfo.isSelf = true;
          newContactsNotify->contactInfos.push_back(scontactInfo);
        }

        // Group chat
        {
          t = 1237962000;
          std::string gname = "The Office";
          std::string gid = gname + "_0";

          ChatInfo chatInfo;
          chatInfo.id = gid;
          chatInfo.lastMessageTime = t;
          newChatsNotify->chatInfos.push_back(chatInfo);

          ContactInfo contactInfo;
          contactInfo.id = gid;
          contactInfo.name = gname;
          newContactsNotify->contactInfos.push_back(contactInfo);

          // From others
          for (auto& message : groupMessages)
          {
            std::string name = message.first;
            std::string id = name + "_0";

            ChatMessage chatMessage;
            chatMessage.id = id + "_" + std::to_string(t);
            chatMessage.senderId = id;
            chatMessage.text = message.second;
            chatMessage.timeSent = (t * 1000);
            chatMessage.isOutgoing = (id == sid);
            chatMessage.isRead = true;
            t = t - 100;
            s_Messages[gid].push_back(chatMessage);
          }
        }

        m_MessageHandler(newChatsNotify);
        m_MessageHandler(newContactsNotify);
      }
      break;

    case GetMessagesRequestType:
      {
        std::shared_ptr<GetMessagesRequest> getMessagesRequest =
          std::static_pointer_cast<GetMessagesRequest>(p_RequestMessage);
        std::shared_ptr<NewMessagesNotify> newMessagesNotify =
          std::make_shared<NewMessagesNotify>(m_ProfileId);
        newMessagesNotify->success = true;
        newMessagesNotify->chatId = getMessagesRequest->chatId;

        newMessagesNotify->chatMessages = s_Messages[getMessagesRequest->chatId];
        m_MessageHandler(newMessagesNotify);
      }
      break;

    case SendMessageRequestType:
      {
        std::shared_ptr<SendMessageRequest> sendMessageRequest =
          std::static_pointer_cast<SendMessageRequest>(p_RequestMessage);
        std::shared_ptr<SendMessageNotify> sendMessageNotify =
          std::make_shared<SendMessageNotify>(m_ProfileId);
        sendMessageNotify->success = true;
        sendMessageNotify->chatId = sendMessageRequest->chatId;
        sendMessageNotify->chatMessage = sendMessageRequest->chatMessage;
        m_MessageHandler(sendMessageNotify);
      }
      break;

    case DeferNotifyRequestType:
      {
        std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
          std::static_pointer_cast<DeferNotifyRequest>(p_RequestMessage);
        m_MessageHandler(deferNotifyRequest->serviceMessage);
      }
      break;

    default:
      LOG_DEBUG("unknown request message %d", p_RequestMessage->GetMessageType());
      break;
  }
}
