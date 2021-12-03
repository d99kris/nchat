// tgchat.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "tgchat.h"

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <td/telegram/Client.h>
#include <td/telegram/Log.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <sys/stat.h>

#include "apputil.h"
#include "config.h"
#include "log.h"
#include "path.hpp"
#include "status.h"
#include "strutil.h"

namespace detail
{
  template<class... Fs>
  struct overload;

  template<class F>
  struct overload<F>: public F
  {
    explicit overload(F f) :
      F(f)
    {
    }
  };

  template<class F, class... Fs>
  struct overload<F, Fs...>: public overload<F>
    , overload<Fs...>
  {
    overload(F f, Fs... fs) :
      overload<F>(f), overload<Fs...>(fs...)
    {
    }

    using overload<F>::operator();
    using overload<Fs...>::operator();
  };
}

template<class... F>
auto overloaded(F... f)
{
  return detail::overload<F...>(f...);
}


TgChat::TgChat()
{
}

TgChat::~TgChat()
{
}

std::string TgChat::GetProfileId() const
{
  return m_ProfileId;
}

bool TgChat::HasFeature(ProtocolFeature p_ProtocolFeature) const
{
  ProtocolFeature customFeatures = TypingTimeout;
  return (p_ProtocolFeature & customFeatures);
}

bool TgChat::SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId)
{
  std::cout << "Enter phone number (ex. +6511111111): ";
  std::getline(std::cin, m_SetupPhoneNumber);

  m_ProfileId = m_ProfileId + "_" + m_SetupPhoneNumber;
  m_ProfileDir = p_ProfilesDir + "/" + m_ProfileId;

  apathy::Path::rmdirs(apathy::Path(m_ProfileDir));
  apathy::Path::makedirs(m_ProfileDir);

  p_ProfileId = m_ProfileId;
  m_IsSetup = true;
  m_Running = true;

  Init();

  ProcessService();

  Cleanup();

  if (!m_IsSetup)
  {
    apathy::Path::rmdirs(apathy::Path(m_ProfileDir));
  }

  return m_IsSetup;
}

bool TgChat::LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId)
{
  m_ProfileDir = p_ProfilesDir + "/" + p_ProfileId;
  m_ProfileId = p_ProfileId;
  return true;
}

bool TgChat::CloseProfile()
{
  m_ProfileDir = "";
  m_ProfileId = "";
  return true;
}

bool TgChat::Login()
{
  Status::Set(Status::FlagOnline);

  if (!m_Running)
  {
    m_Running = true;
    m_Thread = std::thread(&TgChat::Process, this);

    Init();
    m_ServiceThread = std::thread(&TgChat::ProcessService, this);
  }

  return true;
}

bool TgChat::Logout()
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

  if (m_ServiceThread.joinable())
  {
    m_ServiceThread.join();
  }

  Cleanup();

  return true;
}

void TgChat::Process()
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

void TgChat::SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  std::unique_lock<std::mutex> lock(m_ProcessMutex);
  m_RequestsQueue.push_back(p_RequestMessage);
  m_ProcessCondVar.notify_one();
}

void TgChat::SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler)
{
  m_MessageHandler = p_MessageHandler;
}

void TgChat::CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  if (!m_MessageHandler) return;

  m_MessageHandler(p_ServiceMessage);
}

void TgChat::PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  switch (p_RequestMessage->GetMessageType())
  {
    case GetContactsRequestType:
      {
        LOG_DEBUG("Get contacts");
        Status::Set(Status::FlagFetching);

        SendQuery(td::td_api::make_object<td::td_api::getContacts>(),
                  [this](Object object)
        {
          Status::Clear(Status::FlagFetching);

          if (object->get_id() == td::td_api::error::ID) return;

          auto users = td::move_tl_object_as<td::td_api::users>(object);
          if (users->user_ids_.size() == 0) return;

          std::vector<std::string> userIds;
          for (auto userId : users->user_ids_)
          {
            std::string userIdStr = StrUtil::NumToHex(userId);
            userIds.push_back(
                              userIdStr);
          }

          std::shared_ptr<DeferGetUserDetailsRequest> deferGetUserDetailsRequest = std::make_shared<DeferGetUserDetailsRequest>();
          deferGetUserDetailsRequest->userIds = userIds;
          SendRequest(deferGetUserDetailsRequest);
        });
      }
      break;

    case GetChatsRequestType:
      {
        LOG_DEBUG("Get chats");
        Status::Set(Status::FlagFetching);
        int64_t order = (std::numeric_limits<int64_t>::max() - 1); // offset order, from beginning
        int64_t offset = 0; // offset chat id, from first
        int32_t limit = std::numeric_limits<int32_t>::max(); // no limit

        SendQuery(td::td_api::make_object<td::td_api::getChats>(nullptr, order, offset, limit),
                  [this](Object object)
        {
          Status::Clear(Status::FlagFetching);

          if (object->get_id() == td::td_api::error::ID) return;

          auto chats = td::move_tl_object_as<td::td_api::chats>(object);
          if (chats->chat_ids_.size() == 0) return;

          std::vector<std::string> chatIds;
          std::vector<ChatInfo> chatInfos;
          for (auto chatId : chats->chat_ids_)
          {
            std::string chatIdStr = StrUtil::NumToHex(chatId);
            chatIds.push_back(chatIdStr);

            ChatInfo chatInfo;
            chatInfo.id = chatIdStr;
            chatInfos.push_back(chatInfo);
          }

          std::shared_ptr<NewChatsNotify> newChatsNotify = std::make_shared<NewChatsNotify>(m_ProfileId);
          newChatsNotify->success = true;
          newChatsNotify->chatInfos = chatInfos;
          CallMessageHandler(
                             newChatsNotify);

          std::shared_ptr<DeferGetChatDetailsRequest> deferGetChatDetailsRequest = std::make_shared<DeferGetChatDetailsRequest>();
          deferGetChatDetailsRequest->chatIds = chatIds;
          SendRequest(deferGetChatDetailsRequest);
        });
      }
      break;

    case DeferGetChatDetailsRequestType:
      {
        LOG_DEBUG("Get chat details");

        std::shared_ptr<DeferGetChatDetailsRequest> deferGetChatDetailsRequest =
          std::static_pointer_cast<DeferGetChatDetailsRequest>(p_RequestMessage);

        const std::vector<std::string>& chatIds = deferGetChatDetailsRequest->chatIds;
        for (auto& chatId : chatIds)
        {
          Status::Set(Status::FlagFetching);
          std::int64_t chatIdNum = StrUtil::NumFromHex<int64_t>(chatId);

          auto get_chat = td::td_api::make_object<td::td_api::getChat>();
          get_chat->chat_id_ = chatIdNum;
          SendQuery(std::move(get_chat),
                    [this](Object object)
          {
            Status::Clear(Status::FlagFetching);

            if (object->get_id() == td::td_api::error::ID) return;

            auto tchat = td::move_tl_object_as<td::td_api::chat>(object);

            if (!tchat) return;

            ChatInfo chatInfo;
            chatInfo.id = StrUtil::NumToHex(tchat->id_);
            chatInfo.isUnread = (tchat->unread_count_ > 0);
            chatInfo.isUnreadMention = (tchat->unread_mention_count_ > 0);
            chatInfo.isMuted = (tchat->notification_settings_->mute_for_ > 0);
            int64_t lastMessageTimeSec = (tchat->last_message_ != nullptr) ? tchat->last_message_->date_ : 0;
            int64_t lastMessageHash =
              (tchat->last_message_ !=
               nullptr) ? (std::hash<std::string>{ } (StrUtil::NumToHex(tchat->last_message_->id_)) % 256) : 0;
            chatInfo.lastMessageTime = (lastMessageTimeSec * 1000) + lastMessageHash;

            std::vector<ChatInfo> chatInfos;
            chatInfos.push_back(chatInfo);

            std::shared_ptr<NewChatsNotify> newChatsNotify = std::make_shared<NewChatsNotify>(m_ProfileId);
            newChatsNotify->success = true;
            newChatsNotify->chatInfos = chatInfos;
            CallMessageHandler(newChatsNotify);

            m_LastReadInboxMessage[tchat->id_] = tchat->last_read_inbox_message_id_;
            m_LastReadOutboxMessage[tchat->id_] = tchat->last_read_outbox_message_id_;

            std::set<int64_t>& unreadMessages = m_UnreadOutboxMessages[tchat->id_];
            if (!unreadMessages.empty())
            {
              for (auto it = unreadMessages.begin(); it != unreadMessages.end(); /* increment in loop */)
              {
                if (*it <= tchat->last_read_outbox_message_id_)
                {
                  std::shared_ptr<NewMessageStatusNotify> newMessageStatusNotify =
                    std::make_shared<NewMessageStatusNotify>(m_ProfileId);
                  newMessageStatusNotify->chatId = StrUtil::NumToHex(tchat->id_);
                  newMessageStatusNotify->msgId = StrUtil::NumToHex(*it);
                  newMessageStatusNotify->isRead = true;
                  CallMessageHandler(newMessageStatusNotify);

                  it = unreadMessages.erase(it);
                }
                else
                {
                  it = std::next(it);
                }
              }
            }
          });
        }
      }
      break;

    case DeferGetUserDetailsRequestType:
      {
        LOG_DEBUG("Get user details");

        std::shared_ptr<DeferGetUserDetailsRequest> deferGetUserDetailsRequest =
          std::static_pointer_cast<DeferGetUserDetailsRequest>(p_RequestMessage);

        const std::vector<std::string>& userIds = deferGetUserDetailsRequest->userIds;
        for (auto& userId : userIds)
        {
          Status::Set(Status::FlagFetching);
          std::int64_t userIdNum = StrUtil::NumFromHex<int64_t>(userId);

          auto get_user = td::td_api::make_object<td::td_api::getUser>();
          get_user->user_id_ = userIdNum;
          SendQuery(std::move(get_user),
                    [this](Object object)
          {
            Status::Clear(Status::FlagFetching);

            if (object->get_id() == td::td_api::error::ID) return;

            auto tuser = td::move_tl_object_as<td::td_api::user>(object);

            if (!tuser) return;

            ContactInfo contactInfo;
            contactInfo.id = StrUtil::NumToHex(tuser->id_);
            contactInfo.name = tuser->first_name_ + (tuser->last_name_.empty() ? "" : " " + tuser->last_name_);;
            contactInfo.isSelf = (tuser->id_ == m_SelfUserId);

            std::vector<ContactInfo> contactInfos;
            contactInfos.push_back(contactInfo);

            std::shared_ptr<NewContactsNotify> newContactsNotify = std::make_shared<NewContactsNotify>(m_ProfileId);
            newContactsNotify->contactInfos = contactInfos;
            CallMessageHandler(newContactsNotify);
          });
        }
      }
      break;

    case GetMessagesRequestType:
      {
        LOG_DEBUG("Get messages");
        Status::Set(Status::FlagFetching);
        std::shared_ptr<GetMessagesRequest> getMessagesRequest =
          std::static_pointer_cast<GetMessagesRequest>(p_RequestMessage);
        int64_t chatId = StrUtil::NumFromHex<int64_t>(getMessagesRequest->chatId);
        int64_t fromMsgId = StrUtil::NumFromHex<int64_t>(getMessagesRequest->fromMsgId);
        int32_t limit = getMessagesRequest->limit;

        SendQuery(td::td_api::make_object<td::td_api::getChatHistory>(chatId, fromMsgId, 0, limit, false),
                  [this, chatId, fromMsgId](Object object)
        {
          Status::Clear(Status::FlagFetching);

          if (object->get_id() == td::td_api::error::ID) return;

          auto messages = td::move_tl_object_as<td::td_api::messages>(object);

          std::vector<ChatMessage> chatMessages;
          for (auto it = messages->messages_.begin(); it != messages->messages_.end(); ++it)
          {
            auto message = td::move_tl_object_as<td::td_api::message>(*it);
            ChatMessage chatMessage;
            TdMessageConvert(*message, chatMessage);
            chatMessages.push_back(chatMessage);
          }

          std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(m_ProfileId);
          newMessagesNotify->success = true;
          newMessagesNotify->chatId = StrUtil::NumToHex(chatId);
          newMessagesNotify->chatMessages = chatMessages;
          newMessagesNotify->fromMsgId = (fromMsgId != 0) ? StrUtil::NumToHex(fromMsgId) : "";
          CallMessageHandler(newMessagesNotify);
        });
      }
      break;

    case SendMessageRequestType:
      {
        LOG_DEBUG("Send message");
        Status::Set(Status::FlagSending);
        std::shared_ptr<SendMessageRequest> sendMessageRequest = std::static_pointer_cast<SendMessageRequest>(
          p_RequestMessage);

        auto send_message = td::td_api::make_object<td::td_api::sendMessage>();
        send_message->chat_id_ = StrUtil::NumFromHex<int64_t>(sendMessageRequest->chatId);

        if (sendMessageRequest->chatMessage.filePath.empty())
        {
          auto message_content = td::td_api::make_object<td::td_api::inputMessageText>();

          static const bool markdownEnabled = (m_Config.Get("markdown_enabled") == "1");
          static const int markdownVersion = (m_Config.Get("markdown_version") == "1") ? 1 : 2;
          if (markdownEnabled)
          {
            const std::string text = sendMessageRequest->chatMessage.text;
            auto textParseMarkdown = td::td_api::make_object<td::td_api::textParseModeMarkdown>(markdownVersion);
            auto parseTextEntities = td::td_api::make_object<td::td_api::parseTextEntities>(text, std::move(textParseMarkdown));
            td::Client::Request parseRequest{ 1, std::move(parseTextEntities) };
            auto parseResponse = td::Client::execute(std::move(parseRequest));
            if (parseResponse.object->get_id()  == td::td_api::formattedText::ID)
            {
              auto formattedText = td::td_api::move_object_as<td::td_api::formattedText>(parseResponse.object);
              message_content->text_ = std::move(formattedText);
            }
          }

          if (!message_content->text_)
          {
            message_content->text_ = td::td_api::make_object<td::td_api::formattedText>();
            message_content->text_->text_ = sendMessageRequest->chatMessage.text;
          }

          send_message->input_message_content_ = std::move(message_content);
          send_message->reply_to_message_id_ = StrUtil::NumFromHex<int64_t>(sendMessageRequest->chatMessage.quotedId);
        }
        else
        {
          auto message_content = td::td_api::make_object<td::td_api::inputMessageDocument>();
          message_content->document_ = td::td_api::make_object<td::td_api::inputFileLocal>(
            sendMessageRequest->chatMessage.filePath);
          send_message->input_message_content_ = std::move(message_content);
        }

        SendQuery(std::move(send_message),
                  [this, sendMessageRequest](Object object)
        {
          Status::Clear(Status::FlagSending);

          if (object->get_id() == td::td_api::error::ID) return;

          std::shared_ptr<SendMessageNotify> sendMessageNotify = std::make_shared<SendMessageNotify>(m_ProfileId);
          sendMessageNotify->success = true;

          sendMessageNotify->chatId = sendMessageRequest->chatId;
          sendMessageNotify->chatMessage = sendMessageRequest->chatMessage;
          CallMessageHandler(sendMessageNotify);
        });
      }
      break;

    case DeferNotifyRequestType:
      {
        std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
          std::static_pointer_cast<DeferNotifyRequest>(p_RequestMessage);
        CallMessageHandler(deferNotifyRequest->serviceMessage);
      }
      break;

    case MarkMessageReadRequestType:
      {
        LOG_DEBUG("Mark message read");
        std::shared_ptr<MarkMessageReadRequest> markMessageReadRequest =
          std::static_pointer_cast<MarkMessageReadRequest>(p_RequestMessage);
        int64_t chatId = StrUtil::NumFromHex<int64_t>(markMessageReadRequest->chatId);
        std::vector<std::int64_t> msgIds = { StrUtil::NumFromHex<int64_t>(markMessageReadRequest->msgId) };

        auto view_messages = td::td_api::make_object<td::td_api::viewMessages>();
        view_messages->chat_id_ = chatId;
        view_messages->message_ids_ = msgIds;
        view_messages->force_read_ = true;

        SendQuery(std::move(view_messages),
                  [this, markMessageReadRequest](Object object)
        {
          if (object->get_id() == td::td_api::error::ID) return;

          std::shared_ptr<MarkMessageReadNotify> markMessageReadNotify =
            std::make_shared<MarkMessageReadNotify>(m_ProfileId);
          markMessageReadNotify->success = true;
          markMessageReadNotify->chatId = markMessageReadRequest->chatId;
          markMessageReadNotify->msgId = markMessageReadRequest->msgId;
          CallMessageHandler(markMessageReadNotify);
        });
      }
      break;

    case DeleteMessageRequestType:
      {
        LOG_DEBUG("Delete message");
        Status::Set(Status::FlagUpdating);
        std::shared_ptr<DeleteMessageRequest> deleteMessageRequest = std::static_pointer_cast<DeleteMessageRequest>(
          p_RequestMessage);
        int64_t chatId = StrUtil::NumFromHex<int64_t>(deleteMessageRequest->chatId);
        std::vector<std::int64_t> msgIds = { StrUtil::NumFromHex<int64_t>(deleteMessageRequest->msgId) };

        auto delete_messages = td::td_api::make_object<td::td_api::deleteMessages>();
        delete_messages->chat_id_ = chatId;
        delete_messages->message_ids_ = msgIds;
        delete_messages->revoke_ = true; // delete for all (if possible)

        SendQuery(std::move(delete_messages),
                  [this, deleteMessageRequest](Object object)
        {
          Status::Clear(Status::FlagUpdating);

          if (object->get_id() == td::td_api::error::ID) return;

          std::shared_ptr<DeleteMessageNotify> deleteMessageNotify = std::make_shared<DeleteMessageNotify>(m_ProfileId);
          deleteMessageNotify->success = true;
          deleteMessageNotify->chatId = deleteMessageRequest->chatId;
          deleteMessageNotify->msgId = deleteMessageRequest->msgId;
          CallMessageHandler(deleteMessageNotify);
        });
      }
      break;

    case SendTypingRequestType:
      {
        LOG_DEBUG("Send typing");
        std::shared_ptr<SendTypingRequest> sendTypingRequest = std::static_pointer_cast<SendTypingRequest>(
          p_RequestMessage);
        int64_t chatId = StrUtil::NumFromHex<int64_t>(sendTypingRequest->chatId);
        bool isTyping = sendTypingRequest->isTyping;

        auto send_chat_action = td::td_api::make_object<td::td_api::sendChatAction>();
        send_chat_action->chat_id_ = chatId;
        if (isTyping)
        {
          send_chat_action->action_ = td::td_api::make_object<td::td_api::chatActionTyping>();
        }
        else
        {
          send_chat_action->action_ = td::td_api::make_object<td::td_api::chatActionCancel>();
        }

        SendQuery(std::move(send_chat_action),
                  [this, sendTypingRequest](Object object)
        {
          if (object->get_id() == td::td_api::error::ID) return;

          std::shared_ptr<SendTypingNotify> sendTypingNotify = std::make_shared<SendTypingNotify>(m_ProfileId);
          sendTypingNotify->success = true;
          sendTypingNotify->chatId = sendTypingRequest->chatId;
          sendTypingNotify->isTyping = sendTypingRequest->isTyping;
          CallMessageHandler(sendTypingNotify);
        });
      }
      break;

    case SetStatusRequestType:
      {
        LOG_DEBUG("Set status");
        std::shared_ptr<SetStatusRequest> setStatusRequest =
          std::static_pointer_cast<SetStatusRequest>(p_RequestMessage);
        bool isOnline = setStatusRequest->isOnline;

        auto set_option = td::td_api::make_object<td::td_api::setOption>("online",
                                                                         td::td_api::make_object<td::td_api::optionValueBoolean>(
                                                                           isOnline));
        SendQuery(std::move(set_option),
                  [this, setStatusRequest](Object object)
        {
          if (object->get_id() == td::td_api::error::ID) return;

          std::shared_ptr<SetStatusNotify> setStatusNotify = std::make_shared<SetStatusNotify>(m_ProfileId);
          setStatusNotify->success = true;
          setStatusNotify->isOnline = setStatusRequest->isOnline;
          CallMessageHandler(setStatusNotify);
        });
      }
      break;

    case CreateChatRequestType:
      {
        LOG_DEBUG("Create chat");
        Status::Set(Status::FlagUpdating);
        std::shared_ptr<CreateChatRequest> createChatRequest = std::static_pointer_cast<CreateChatRequest>(
          p_RequestMessage);
        int32_t userId = StrUtil::NumFromHex<int32_t>(createChatRequest->userId);

        auto create_chat = td::td_api::make_object<td::td_api::createPrivateChat>();
        create_chat->user_id_ = userId;

        SendQuery(std::move(create_chat),
                  [this](Object object)
        {
          Status::Clear(Status::FlagUpdating);

          if (object->get_id() == td::td_api::error::ID) return;

          auto chat = td::move_tl_object_as<td::td_api::chat>(object);

          ChatInfo chatInfo;
          chatInfo.id = StrUtil::NumToHex(chat->id_);

          std::shared_ptr<CreateChatNotify> createChatNotify = std::make_shared<CreateChatNotify>(m_ProfileId);
          createChatNotify->success = true;
          createChatNotify->chatInfo = chatInfo;

          CallMessageHandler(createChatNotify);
        });
      }
      break;

    case DeferDownloadFileRequestType:
      {
        std::shared_ptr<DeferDownloadFileRequest> deferDownloadFileRequest =
          std::static_pointer_cast<DeferDownloadFileRequest>(p_RequestMessage);
        std::string chatId = deferDownloadFileRequest->chatId;
        std::string msgId = deferDownloadFileRequest->msgId;
        std::string fileId = deferDownloadFileRequest->fileId;
        DownloadFile(chatId, msgId, fileId);
      }
      break;

    default:
      LOG_DEBUG("unknown request message %d", p_RequestMessage->GetMessageType());
      break;
  }
}

void TgChat::Init()
{
  const std::map<std::string, std::string> defaultConfig =
  {
    { "local_key", "" },
    { "markdown_enabled", "1" },
    { "markdown_version", "1" },
  };
  const std::string configPath(m_ProfileDir + std::string("/telegram.conf"));
  m_Config = Config(configPath, defaultConfig);

  td::Log::set_verbosity_level(Log::GetDebugEnabled() ? 5 : 1);
  const std::string logPath(m_ProfileDir + std::string("/td.log"));
  td::Log::set_file_path(logPath);
  td::Log::set_max_file_size(1024 * 1024);
  m_Client = std::make_unique<td::Client>();
}

void TgChat::Cleanup()
{
  m_Config.Save();
  td::td_api::close();
}

void TgChat::ProcessService()
{
  while (m_Running)
  {
    auto response = m_Client->receive(0.1);
    if (response.object)
    {
      ProcessResponse(std::move(response));
    }
  }
}

void TgChat::ProcessResponse(td::Client::Response response)
{
  if (!response.object) return;

  if (response.id == 0) return ProcessUpdate(std::move(response.object));

  auto it = m_Handlers.find(response.id);
  if (it != m_Handlers.end())
  {
    it->second(std::move(response.object));
  }
}

void TgChat::ProcessUpdate(td::td_api::object_ptr<td::td_api::Object> update)
{
  td::td_api::downcast_call(*update, overloaded(
  [this](td::td_api::updateAuthorizationState& update_authorization_state)
  {
    LOG_TRACE("auth update");

    m_AuthorizationState = std::move(update_authorization_state.authorization_state_);
    OnAuthStateUpdate();
  },
  [this](td::td_api::updateNewChat& update_new_chat)
  {
    LOG_TRACE("new chat update");

    ContactInfo contactInfo;
    contactInfo.id = StrUtil::NumToHex(update_new_chat.chat_->id_);
    contactInfo.name = update_new_chat.chat_->title_;
    contactInfo.isSelf = (update_new_chat.chat_->id_ == m_SelfUserId);

    std::shared_ptr<NewContactsNotify> newContactsNotify = std::make_shared<NewContactsNotify>(m_ProfileId);
    newContactsNotify->contactInfos = std::vector<ContactInfo>({ contactInfo });
    CallMessageHandler(newContactsNotify);
  },
  [this](td::td_api::updateChatTitle& update_chat_title)
  {
    LOG_TRACE("chat title update");

    ContactInfo contactInfo;
    contactInfo.id = StrUtil::NumToHex(update_chat_title.chat_id_);
    contactInfo.name = update_chat_title.title_;
    contactInfo.isSelf = (update_chat_title.chat_id_ == m_SelfUserId);

    std::shared_ptr<NewContactsNotify> newContactsNotify = std::make_shared<NewContactsNotify>(m_ProfileId);
    newContactsNotify->contactInfos = std::vector<ContactInfo>({ contactInfo });
    CallMessageHandler(newContactsNotify);
  },
  [this](td::td_api::updateUser& update_user)
  {
    LOG_TRACE("user update");

    td::td_api::object_ptr<td::td_api::user> user = std::move(update_user.user_);
    ContactInfo contactInfo;

    contactInfo.id = StrUtil::NumToHex(user->id_);
    contactInfo.name = user->first_name_ + (user->last_name_.empty() ? "" : " " + user->last_name_);;
    contactInfo.isSelf = (user->id_ == m_SelfUserId);

    std::shared_ptr<NewContactsNotify> newContactsNotify = std::make_shared<NewContactsNotify>(m_ProfileId);
    newContactsNotify->contactInfos = std::vector<ContactInfo>({ contactInfo });
    CallMessageHandler(newContactsNotify);
  },
  [this](td::td_api::updateNewMessage& update_new_message)
  {
    LOG_TRACE("new msg update");

    auto message = td::move_tl_object_as<td::td_api::message>(update_new_message.message_);
    ChatMessage chatMessage;
    TdMessageConvert(*message, chatMessage);

    bool isPending = (message->sending_state_ != nullptr);
    if (!isPending) // ignore pending messages as their ids change once sent
    {
      std::vector<ChatMessage> chatMessages;
      chatMessages.push_back(chatMessage);

      std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(m_ProfileId);
      newMessagesNotify->success = true;
      newMessagesNotify->chatId = StrUtil::NumToHex(message->chat_id_);
      newMessagesNotify->chatMessages = chatMessages;
      CallMessageHandler(newMessagesNotify);
    }
  },
  [this](td::td_api::updateMessageSendSucceeded& update_message_send_succeeded)
  {
    LOG_TRACE("msg send update");

    auto message = td::move_tl_object_as<td::td_api::message>(update_message_send_succeeded.message_);
    ChatMessage chatMessage;
    TdMessageConvert(*message, chatMessage);

    std::vector<ChatMessage> chatMessages;
    chatMessages.push_back(chatMessage);

    std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(m_ProfileId);
    newMessagesNotify->success = true;
    newMessagesNotify->chatId = StrUtil::NumToHex(message->chat_id_);
    newMessagesNotify->chatMessages = chatMessages;
    CallMessageHandler(newMessagesNotify);
  },
  [this](td::td_api::updateUserChatAction& user_chat_action)
  {
    LOG_TRACE("user chat action update");

    int64_t chatId = user_chat_action.chat_id_;
    int64_t userId = user_chat_action.user_id_;
    bool isTyping = false;

    if (user_chat_action.action_->get_id() == td::td_api::chatActionTyping::ID)
    {
      LOG_TRACE("user %d in chat %d is typing", userId, chatId);
      isTyping = true;
    }
    else
    {
      LOG_TRACE("user %d in chat %d is not typing", userId, chatId);
    }

    std::shared_ptr<ReceiveTypingNotify> receiveTypingNotify = std::make_shared<ReceiveTypingNotify>(m_ProfileId);
    receiveTypingNotify->chatId = StrUtil::NumToHex(chatId);
    receiveTypingNotify->userId = StrUtil::NumToHex(userId);
    receiveTypingNotify->isTyping = isTyping;
    CallMessageHandler(receiveTypingNotify);
  },
  [this](td::td_api::updateUserStatus& user_status)
  {
    LOG_TRACE("user status update");

    int64_t userId = user_status.user_id_;
    bool isOnline = false;

    if (user_status.status_->get_id() == td::td_api::userStatusOnline::ID)
    {
      isOnline = true;
    }

    std::shared_ptr<ReceiveStatusNotify> receiveStatusNotify = std::make_shared<ReceiveStatusNotify>(m_ProfileId);
    receiveStatusNotify->userId = StrUtil::NumToHex(userId);
    receiveStatusNotify->isOnline = isOnline;
    CallMessageHandler(receiveStatusNotify);
  },
  [this](td::td_api::updateChatReadOutbox& chat_read_outbox)
  {
    LOG_TRACE("chat read outbox update");

    m_LastReadOutboxMessage[chat_read_outbox.chat_id_] = chat_read_outbox.last_read_outbox_message_id_;

    std::set<int64_t>& unreadMessages = m_UnreadOutboxMessages[chat_read_outbox.chat_id_];
    if (!unreadMessages.empty())
    {
      for (auto it = unreadMessages.begin(); it != unreadMessages.end(); /* increment in loop */)
      {
        if (*it <= chat_read_outbox.last_read_outbox_message_id_)
        {
          std::shared_ptr<NewMessageStatusNotify> newMessageStatusNotify =
            std::make_shared<NewMessageStatusNotify>(m_ProfileId);
          newMessageStatusNotify->chatId = StrUtil::NumToHex(chat_read_outbox.chat_id_);
          newMessageStatusNotify->msgId = StrUtil::NumToHex(*it);
          newMessageStatusNotify->isRead = true;
          CallMessageHandler(newMessageStatusNotify);

          it = unreadMessages.erase(it);
        }
        else
        {
          it = std::next(it);
        }
      }
    }
  },
  [this](td::td_api::updateChatReadInbox& chat_read_inbox)
  {
    LOG_TRACE("chat read inbox update");

    m_LastReadInboxMessage[chat_read_inbox.chat_id_] = chat_read_inbox.last_read_inbox_message_id_;
  },
  [](auto& anyupdate)
  {
    LOG_TRACE("other update %d", anyupdate.get_id());
  }
  ));
}

std::function<void(TgChat::Object)> TgChat::CreateAuthQueryHandler()
{
  return [this, id = m_AuthQueryId](Object object)
  {
    if (id == m_AuthQueryId)
    {
      CheckAuthError(std::move(object));
    }
  };
}

void TgChat::OnAuthStateUpdate()
{
  m_AuthQueryId++;
  td::td_api::downcast_call(*m_AuthorizationState, overloaded(
  [this](td::td_api::authorizationStateReady&)
  {
    m_Authorized = true;
    m_WasAuthorized = true;
    if (m_IsSetup)
    {
      m_Running = false;
    }
    else
    {
      SendQuery(td::td_api::make_object<td::td_api::getMe>(),
      [this](Object object)
      {
        if (object->get_id() == td::td_api::error::ID) return;

        auto user_ = td::move_tl_object_as<td::td_api::user>(object);
        m_SelfUserId = user_->id_;
      });

      std::shared_ptr<ConnectNotify> connectNotify = std::make_shared<ConnectNotify>(m_ProfileId);
      connectNotify->success = true;

      std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
      deferNotifyRequest->serviceMessage = connectNotify;
      SendRequest(deferNotifyRequest);
    }
  },
  [this](td::td_api::authorizationStateLoggingOut&)
  {
    m_Authorized = false;
    LOG_DEBUG("logging out");
  },
  [](td::td_api::authorizationStateClosing&)
  {
    LOG_DEBUG("closing");
  },
  [this](td::td_api::authorizationStateClosed&)
  {
    m_Authorized = false;
    m_Running = false;
    LOG_DEBUG("closed");
  },
  [this](td::td_api::authorizationStateWaitCode&)
  {
    if (m_IsSetup)
    {
      std::cout << "Enter authentication code: ";
      std::string code;
      std::getline(std::cin, code);
      SendQuery(td::td_api::make_object<td::td_api::checkAuthenticationCode>(code),
                CreateAuthQueryHandler());
    }
    else
    {
      LOG_DEBUG("unexpected state");
      m_Running = false;
    }
  },
  [this](td::td_api::authorizationStateWaitRegistration&)
  {
    if (m_IsSetup)
    {
      std::string first_name;
      std::string last_name;
      std::cout << "Enter your first name: ";
      std::getline(std::cin, first_name);
      std::cout << "Enter your last name: ";
      std::getline(std::cin, last_name);
      SendQuery(td::td_api::make_object<td::td_api::registerUser>(first_name, last_name),
                CreateAuthQueryHandler());
    }
    else
    {
      LOG_DEBUG("Unexpected state");
      m_Running = false;
    }
  },
  [this](td::td_api::authorizationStateWaitPassword&)
  {
    if (m_IsSetup)
    {
      std::cout << "Enter authentication password: ";
      std::string password = StrUtil::GetPass();
      SendQuery(td::td_api::make_object<td::td_api::checkAuthenticationPassword>(password),
                CreateAuthQueryHandler());
    }
    else
    {
      LOG_DEBUG("Unexpected state");
      m_Running = false;
    }
  },
  [this](td::td_api::authorizationStateWaitPhoneNumber&)
  {
    if (m_IsSetup)
    {
      std::string phone_number = m_SetupPhoneNumber;
      SendQuery(td::td_api::make_object<td::td_api::setAuthenticationPhoneNumber>(phone_number, nullptr),
                CreateAuthQueryHandler());
    }
    else
    {
      LOG_DEBUG("unexpected state");
      m_Running = false;
    }
  },
  [this](td::td_api::authorizationStateWaitEncryptionKey&)
  {
    std::string key;
    if (m_IsSetup)
    {
      // generate new key during setup
      key = GetRandomString(16);
      m_Config.Set("local_key", key);
    }
    else
    {
      // use saved local encryption key
      key = m_Config.Get("local_key");
    }

    if (key.empty())
    {
      LOG_ERROR("Empty key");
      m_Running = false;
    }
    else
    {
      SendQuery(td::td_api::make_object<td::td_api::checkDatabaseEncryptionKey>(std::move(key)),
                CreateAuthQueryHandler());
    }
  },
  [this](td::td_api::authorizationStateWaitTdlibParameters&)
  {
    const std::string dbPath(m_ProfileDir + std::string("/tdlib"));
    auto parameters = td::td_api::make_object<td::td_api::tdlibParameters>();
    parameters->use_test_dc_ = false;
    parameters->database_directory_ = dbPath;
    parameters->use_message_database_ = true;
    parameters->use_secret_chats_ = true;
    parameters->api_id_ = 317904;
    parameters->api_hash_ = "ae116c4816db58b08fef5d2703bb5aff";
    parameters->system_language_code_ = "en";
    parameters->device_model_ = "Desktop";
#ifdef __linux__
    parameters->system_version_ = "Linux";
#elif __APPLE__
    parameters->system_version_ = "Darwin";
#else
    parameters->system_version_ = "Unknown";
#endif
    static std::string appVersion = AppUtil::GetAppVersion();
    parameters->application_version_ = appVersion.c_str();
    parameters->enable_storage_optimizer_ = true;
    SendQuery(td::td_api::make_object<td::td_api::setTdlibParameters>(std::move(parameters)),
              CreateAuthQueryHandler());
  },
  [](td::td_api::authorizationStateWaitOtherDeviceConfirmation& state)
  {
    std::cout << "Confirm this login link on another device:\n" << state.link_ << "\n";
  }
  ));
}

void TgChat::SendQuery(td::td_api::object_ptr<td::td_api::Function> f,
                       std::function<void(Object)> handler)
{
  auto query_id = GetNextQueryId();
  if (handler)
  {
    m_Handlers.emplace(query_id, std::move(handler));
  }
  m_Client->send({ query_id, std::move(f) });
}

void TgChat::CheckAuthError(Object object)
{
  if (object->get_id() == td::td_api::error::ID)
  {
    auto error = td::move_tl_object_as<td::td_api::error>(object);
    LOG_WARNING("auth error \"%s\"", to_string(error).c_str());
    if (m_IsSetup)
    {
      std::cout << "Authentication error: " << error->message_ << "\n";
      m_IsSetup = false;
    }

    m_Running = false;
    OnAuthStateUpdate();
  }
}

std::string TgChat::GetRandomString(size_t p_Len)
{
  srand(time(0));
  std::string str = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  std::string newstr;
  int pos;
  while (newstr.size() != p_Len)
  {
    pos = ((rand() % (str.size() - 1)));
    newstr += str.substr(pos, 1);
  }
  return newstr;
}

std::uint64_t TgChat::GetNextQueryId()
{
  return ++m_CurrentQueryId;
}

std::int64_t TgChat::GetSenderId(const td::td_api::message& p_TdMessage)
{
  std::int64_t senderId = 0;
  if (p_TdMessage.sender_->get_id() == td::td_api::messageSenderUser::ID)
  {
    auto& message_sender_user = static_cast<const td::td_api::messageSenderUser&>(*p_TdMessage.sender_);
    senderId = message_sender_user.user_id_;
  }
  else if (p_TdMessage.sender_->get_id() == td::td_api::messageSenderChat::ID)
  {
    auto& message_sender_chat = static_cast<const td::td_api::messageSenderChat&>(*p_TdMessage.sender_);
    senderId = message_sender_chat.chat_id_;
  }

  return senderId;
}

std::string TgChat::GetText(td::td_api::object_ptr<td::td_api::formattedText>&& p_FormattedText)
{
  std::string text = p_FormattedText->text_;
  static const bool markdownEnabled = (m_Config.Get("markdown_enabled") == "1");
  static const int markdownVersion = (m_Config.Get("markdown_version") == "1") ? 1 : 2;
  if (markdownEnabled)
  {
    auto getMarkdownText = td::td_api::make_object<td::td_api::getMarkdownText>(std::move(p_FormattedText));
    td::Client::Request parseRequest{ 2, std::move(getMarkdownText) };
    auto parseResponse = td::Client::execute(std::move(parseRequest));
    if (parseResponse.object->get_id()  == td::td_api::formattedText::ID)
    {
      auto formattedText = td::td_api::move_object_as<td::td_api::formattedText>(parseResponse.object);
      text = formattedText->text_;
      if (markdownVersion == 1)
      {
        StrUtil::ReplaceString(text, "**", "*");
        StrUtil::ReplaceString(text, "__", "_");
        StrUtil::ReplaceString(text, "~~", "~");
      }
    }
  }

  return text;
}

void TgChat::TdMessageConvert(td::td_api::message& p_TdMessage, ChatMessage& p_ChatMessage)
{
  std::string text;
  std::string filePath;
  int32_t downloadId = 0;
  if (p_TdMessage.content_->get_id() == td::td_api::messageText::ID)
  {
    auto& messageText = static_cast<td::td_api::messageText&>(*p_TdMessage.content_);
    text = GetText(std::move(messageText.text_));
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageAnimation::ID)
  {
    text = "[Animation]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageAudio::ID)
  {
    text = "[Audio]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageCall::ID)
  {
    text = "[Call]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageContact::ID)
  {
    text = "[Contact]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageContactRegistered::ID)
  {
    text = "[ContactRegistered]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageCustomServiceAction::ID)
  {
    text = "[CustomServiceAction]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageDocument::ID)
  {
    auto& messageDocument = static_cast<td::td_api::messageDocument&>(*p_TdMessage.content_);

    int32_t id = messageDocument.document_->document_->id_;
    std::string path = messageDocument.document_->document_->local_->path_;
    std::string fileName = messageDocument.document_->file_name_;
    text = GetText(std::move(messageDocument.caption_));
    if (!path.empty())
    {
      filePath = path;
    }
    else
    {
      filePath = " ";
      downloadId = id;
    }
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messagePhoto::ID)
  {
    auto& messagePhoto = static_cast<td::td_api::messagePhoto&>(*p_TdMessage.content_);
    auto& photo = messagePhoto.photo_;
    auto& sizes = photo->sizes_;
    text = GetText(std::move(messagePhoto.caption_));
    if (!sizes.empty())
    {
      auto& largestSize = sizes.back();
      auto& photoFile = largestSize->photo_;
      auto& localFile = photoFile->local_;
      auto& localPath = localFile->path_;
      if (!localPath.empty())
      {
        filePath = localPath;
      }
      else
      {
        int32_t id = photoFile->id_;
        filePath = " ";
        downloadId = id;
      }
    }
    else
    {
      text = "[Photo Error]";
    }
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageSticker::ID)
  {
    text = "[Sticker]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageVideo::ID)
  {
    text = "[Video]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageVideoNote::ID)
  {
    text = "[VideoNote]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageVoiceNote::ID)
  {
    text = "[VoiceNote]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messageChatAddMembers::ID)
  {
    text = "[ChatAddMembers]";
  }
  else
  {
    text = "[UnknownMessage " + std::to_string(p_TdMessage.content_->get_id()) + "]";
  }

  p_ChatMessage.id = StrUtil::NumToHex(p_TdMessage.id_);
  p_ChatMessage.senderId = StrUtil::NumToHex(GetSenderId(p_TdMessage));
  p_ChatMessage.isOutgoing = p_TdMessage.is_outgoing_;
  p_ChatMessage.timeSent = (((int64_t)p_TdMessage.date_) * 1000) + (std::hash<std::string>{ } (p_ChatMessage.id) % 256);
  p_ChatMessage.quotedId =
    (p_TdMessage.reply_to_message_id_ != 0) ? StrUtil::NumToHex(p_TdMessage.reply_to_message_id_) : "";
  p_ChatMessage.text = text;
  p_ChatMessage.filePath = filePath;
  p_ChatMessage.hasMention = p_TdMessage.contains_unread_mention_;

  if (p_TdMessage.chat_id_ == m_SelfUserId)
  {
    p_ChatMessage.isRead = true;
  }
  else if (p_TdMessage.is_outgoing_)
  {
    p_ChatMessage.isRead = (p_TdMessage.id_ <= m_LastReadOutboxMessage[p_TdMessage.chat_id_]);
    if (!p_ChatMessage.isRead)
    {
      m_UnreadOutboxMessages[p_TdMessage.chat_id_].insert(p_TdMessage.id_);
    }
  }
  else
  {
    if (m_LastReadInboxMessage.count(p_TdMessage.chat_id_) > 0)
    {
      p_ChatMessage.isRead = (p_TdMessage.id_ <= m_LastReadInboxMessage[p_TdMessage.chat_id_]);
    }
    else
    {
      p_ChatMessage.isRead = !p_TdMessage.contains_unread_mention_;
    }
  }

  if (downloadId != 0)
  {
    std::shared_ptr<DeferDownloadFileRequest> deferDownloadFileRequest = std::make_shared<DeferDownloadFileRequest>();
    deferDownloadFileRequest->chatId = StrUtil::NumToHex(p_TdMessage.chat_id_);
    deferDownloadFileRequest->msgId = StrUtil::NumToHex(p_TdMessage.id_);
    deferDownloadFileRequest->fileId = StrUtil::NumToHex(downloadId);
    SendRequest(deferDownloadFileRequest);
  }
}

std::string TgChat::GetUserName(std::int32_t user_id, std::int64_t chat_id)
{
  m_UserToChats[user_id].insert(chat_id);
  auto it = m_Users.find(user_id);
  if (it == m_Users.end())
  {
    return std::string("(") + std::to_string(user_id) + std::string(")");
  }

  return it->second->first_name_ + (it->second->last_name_.empty() ? "" : " " + it->second->last_name_);
}

void TgChat::DownloadFile(std::string p_ChatId, std::string p_MsgId, std::string p_FileId)
{
  LOG_DEBUG("download file");
  try
  {
    auto download_file = td::td_api::make_object<td::td_api::downloadFile>();
    download_file->file_id_ = StrUtil::NumFromHex<std::int32_t>(p_FileId);
    download_file->priority_ = 32;
    download_file->synchronous_ = true;
    SendQuery(std::move(download_file),
              [this, p_ChatId, p_MsgId, p_FileId](Object object)
    {
      if (object->get_id() == td::td_api::error::ID) return;

      if (object->get_id() == td::td_api::file::ID)
      {
        auto file_ = td::move_tl_object_as<td::td_api::file>(object);
        std::string path = file_->local_->path_;

        std::shared_ptr<NewMessageFileNotify> newMessageFileNotify =
          std::make_shared<NewMessageFileNotify>(m_ProfileId);
        newMessageFileNotify->chatId = std::string(p_ChatId);
        newMessageFileNotify->msgId = std::string(p_MsgId);
        newMessageFileNotify->filePath = path;

        CallMessageHandler(newMessageFileNotify);
      }
    });
  }
  catch (...)
  {
  }
}
