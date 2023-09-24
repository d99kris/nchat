// tgchat.cpp
//
// Copyright (c) 2020-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "tgchat.h"

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <td/telegram/Client.h>
#include <td/telegram/Log.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <sys/stat.h>

#include <td/telegram/Client.h>

#include "appconfig.h"
#include "apputil.h"
#include "config.h"
#include "log.h"
#include "messagecache.h"
#include "path.hpp"
#include "protocolutil.h"
#include "status.h"
#include "strutil.h"
#include "timeutil.h"

// #define SIMULATED_SPONSORED_MESSAGES

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


class TgChat::Impl
{
public:
  Impl()
  {
    m_ProfileId = TgChat::GetName();
  }

  virtual ~Impl()
  {
  }

  std::string GetProfileId() const;
  bool HasFeature(ProtocolFeature p_ProtocolFeature) const;

  bool SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId);
  bool LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId);
  bool CloseProfile();

  bool Login();
  bool Logout();

  void Process();

  void SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage);
  void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler);

private:
  std::string m_ProfileId;
  std::string m_ProfileDir;
  std::function<void(std::shared_ptr<ServiceMessage>)> m_MessageHandler;

  bool m_Running = false;
  std::thread m_Thread;
  std::deque<std::shared_ptr<RequestMessage>> m_RequestsQueue;
  std::mutex m_ProcessMutex;
  std::condition_variable m_ProcessCondVar;

private:
  enum ChatType
  {
    ChatPrivate = 0,
    ChatBasicGroup,
    ChatSuperGroup,
    ChatSuperGroupChannel,
    ChatSecret,
  };

private:
  void CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);
  void PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage);

  using Object = td::td_api::object_ptr<td::td_api::Object>;
  void Init();
  void InitProxy();
  void Cleanup();
  void ProcessService();
  void ProcessResponse(td::Client::Response response);
  void ProcessUpdate(td::td_api::object_ptr<td::td_api::Object> update);
  void ProcessStatusUpdate(int64_t p_UserId,
                           td::td_api::object_ptr<td::td_api::UserStatus> p_Status);
  std::function<void(Object)> CreateAuthQueryHandler();
  void OnAuthStateUpdate();
  void SendQuery(td::td_api::object_ptr<td::td_api::Function> f, std::function<void(Object)> handler);
  void CheckAuthError(Object object);
  void CreateChat(Object p_Object);
  std::string GetRandomString(size_t p_Len);
  std::uint64_t GetNextQueryId();
  std::int64_t GetSenderId(const td::td_api::message& p_TdMessage);
  std::string GetText(td::td_api::object_ptr<td::td_api::formattedText>&& p_FormattedText);
  void TdMessageContentConvert(td::td_api::MessageContent& p_TdMessageContent, int64_t p_SenderId,
                               std::string& p_Text, std::string& p_FileInfo);
  void TdMessageConvert(td::td_api::message& p_TdMessage, ChatMessage& p_ChatMessage);
  void DownloadFile(std::string p_ChatId, std::string p_MsgId, std::string p_FileId, std::string p_DownloadId,
                    DownloadFileAction p_DownloadFileAction);
  void RequestSponsoredMessagesIfNeeded();
  void GetSponsoredMessages(const std::string& p_ChatId);
  void ViewSponsoredMessage(const std::string& p_ChatId, const std::string& p_MsgId);
  bool IsSponsoredMessageId(const std::string& p_MsgId);
  bool IsGroup(int64_t p_UserId);
  bool IsSelf(int64_t p_UserId);
  std::string GetContactName(int64_t p_UserId);
  void GetChatHistory(int64_t p_ChatId, int64_t p_FromMsgId, int32_t p_Offset, int32_t p_Limit, bool p_Sequence);
  td::td_api::object_ptr<td::td_api::inputMessageText> GetMessageText(const std::string& p_Text);
  std::string ConvertMarkdownV2ToV1(const std::string& p_Str);

private:
  std::thread m_ServiceThread;
  std::string m_SetupPhoneNumber;
  Config m_Config;
  std::unique_ptr<td::Client> m_Client;
  std::map<std::uint64_t, std::function<void(Object)>> m_Handlers;
  td::td_api::object_ptr<td::td_api::AuthorizationState> m_AuthorizationState;
  bool m_IsSetup = false;
  bool m_Authorized = false;
  bool m_WasAuthorized = false;
  std::int64_t m_SelfUserId = 0;
  std::uint64_t m_AuthQueryId = 0;
  std::uint64_t m_CurrentQueryId = 0;
  std::map<int64_t, int64_t> m_LastReadInboxMessage;
  std::map<int64_t, int64_t> m_LastReadOutboxMessage;
  std::map<int64_t, std::set<int64_t>> m_UnreadOutboxMessages;
  std::map<int64_t, ContactInfo> m_ContactInfos;
  std::map<int64_t, ChatType> m_ChatTypes;
  int64_t m_CurrentChat = 0;
  const char m_SponsoredMessageMsgIdPrefix = '+';
  std::map<std::string, std::set<std::string>> m_SponsoredMessageIds;
  static const int s_CacheDirVersion = 2;
};

// Public interface
extern "C" TgChat* CreateTgChat()
{
  return new TgChat();
}

TgChat::TgChat()
  : m_Impl(std::make_unique<Impl>())
{
}

TgChat::~TgChat()
{
}

std::string TgChat::GetProfileId() const
{
  return m_Impl->GetProfileId();
}

bool TgChat::HasFeature(ProtocolFeature p_ProtocolFeature) const
{
  return m_Impl->HasFeature(p_ProtocolFeature);
}

bool TgChat::SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId)
{
  return m_Impl->SetupProfile(p_ProfilesDir, p_ProfileId);
}

bool TgChat::LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId)
{
  return m_Impl->LoadProfile(p_ProfilesDir, p_ProfileId);
}

bool TgChat::CloseProfile()
{
  return m_Impl->CloseProfile();
}

bool TgChat::Login()
{
  return m_Impl->Login();
}

bool TgChat::Logout()
{
  return m_Impl->Logout();
}

void TgChat::Process()
{
  m_Impl->Process();
}

void TgChat::SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  m_Impl->SendRequest(p_RequestMessage);
}

void TgChat::SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler)
{
  m_Impl->SetMessageHandler(p_MessageHandler);
}

// Implementation
std::string TgChat::Impl::GetProfileId() const
{
  return m_ProfileId;
}

bool TgChat::Impl::HasFeature(ProtocolFeature p_ProtocolFeature) const
{
  static int customFeatures = FeatureTypingTimeout | FeatureEditMessagesWithinTwoDays;
  return (p_ProtocolFeature & customFeatures);
}

bool TgChat::Impl::SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId)
{
  std::cout << "Enter phone number (ex. +6511111111): ";
  std::getline(std::cin, m_SetupPhoneNumber);

  m_ProfileId = m_ProfileId + "_" + m_SetupPhoneNumber;
  m_ProfileDir = p_ProfilesDir + "/" + m_ProfileId;

  apathy::Path::rmdirs(apathy::Path(m_ProfileDir));
  apathy::Path::makedirs(m_ProfileDir);

  MessageCache::AddProfile(m_ProfileId, true, s_CacheDirVersion);

  p_ProfileId = m_ProfileId;
  m_IsSetup = true;
  m_Running = true;

  Init();

  ProcessService();

  Cleanup();

  bool rv = m_IsSetup;
  if (rv)
  {
    m_IsSetup = false;
  }
  else
  {
    apathy::Path::rmdirs(apathy::Path(m_ProfileDir));
  }

  return rv;
}

bool TgChat::Impl::LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId)
{
  m_ProfileDir = p_ProfilesDir + "/" + p_ProfileId;
  m_ProfileId = p_ProfileId;
  MessageCache::AddProfile(m_ProfileId, true, s_CacheDirVersion);
  return true;
}

bool TgChat::Impl::CloseProfile()
{
  m_ProfileDir = "";
  m_ProfileId = "";
  return true;
}

bool TgChat::Impl::Login()
{
  Status::Set(Status::FlagOnline);

  if (!m_Running)
  {
    m_Running = true;
    m_Thread = std::thread(&TgChat::Impl::Process, this);

    Init();
    m_ServiceThread = std::thread(&TgChat::Impl::ProcessService, this);
  }

  return true;
}

bool TgChat::Impl::Logout()
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

void TgChat::Impl::Process()
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
        LOG_DEBUG("postpone request handling");
        m_ProcessCondVar.wait(lock);
        continue;
      }

      requestMessage = m_RequestsQueue.front();
      m_RequestsQueue.pop_front();
    }

    PerformRequest(requestMessage);
  }
}

void TgChat::Impl::SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  std::unique_lock<std::mutex> lock(m_ProcessMutex);
  m_RequestsQueue.push_back(p_RequestMessage);
  m_ProcessCondVar.notify_one();
}

void TgChat::Impl::SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler)
{
  m_MessageHandler = p_MessageHandler;
}

void TgChat::Impl::CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  MessageCache::AddFromServiceMessage(m_ProfileId, p_ServiceMessage);

  if (!m_MessageHandler)
  {
    LOG_DEBUG("message handler not set");
    return;
  }

  m_MessageHandler(p_ServiceMessage);
}

void TgChat::Impl::PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  // *INDENT-OFF*
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
            userIds.push_back(userIdStr);
          }

          std::shared_ptr<DeferGetUserDetailsRequest> deferGetUserDetailsRequest =
            std::make_shared<DeferGetUserDetailsRequest>();
          deferGetUserDetailsRequest->userIds = userIds;
          SendRequest(deferGetUserDetailsRequest);
        });
      }
      break;

    case GetChatsRequestType:
      {
        LOG_DEBUG("Get chats");
        Status::Set(Status::FlagFetching);
        int32_t limit = std::numeric_limits<int32_t>::max(); // no limit

        SendQuery(td::td_api::make_object<td::td_api::getChats>(nullptr, limit),
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
          }

          std::shared_ptr<DeferGetChatDetailsRequest> deferGetChatDetailsRequest =
            std::make_shared<DeferGetChatDetailsRequest>();
          deferGetChatDetailsRequest->chatIds = chatIds;
          SendRequest(deferGetChatDetailsRequest);
        });
      }
      break;

    case GetStatusRequestType:
      {
        LOG_DEBUG("Get status");

        std::shared_ptr<GetStatusRequest> getStatusRequest =
          std::static_pointer_cast<GetStatusRequest>(p_RequestMessage);

        std::int64_t userId = StrUtil::NumFromHex<int64_t>(getStatusRequest->userId);

        if (IsGroup(userId) || IsSelf(userId)) return;

        Status::Set(Status::FlagFetching);

        auto get_user = td::td_api::make_object<td::td_api::getUser>();
        get_user->user_id_ = userId;
        SendQuery(std::move(get_user),
                  [this](Object object)
        {
          Status::Clear(Status::FlagFetching);

          if (object->get_id() == td::td_api::error::ID) return;

          auto tuser = td::move_tl_object_as<td::td_api::user>(object);

          if (!tuser) return;

          ProcessStatusUpdate(tuser->id_, std::move(tuser->status_));
        });
      }
      break;

    case DeferGetChatDetailsRequestType:
      {
        LOG_DEBUG("Get chat details");

        std::shared_ptr<DeferGetChatDetailsRequest> deferGetChatDetailsRequest =
          std::static_pointer_cast<DeferGetChatDetailsRequest>(p_RequestMessage);

        const bool isGetTypeOnly = deferGetChatDetailsRequest->isGetTypeOnly;
        const std::vector<std::string>& chatIds = deferGetChatDetailsRequest->chatIds;
        for (auto& chatId : chatIds)
        {
          Status::Set(Status::FlagFetching);
          std::int64_t chatIdNum = StrUtil::NumFromHex<int64_t>(chatId);

          auto get_chat = td::td_api::make_object<td::td_api::getChat>();
          get_chat->chat_id_ = chatIdNum;
          SendQuery(std::move(get_chat),
                    [this, chatId, isGetTypeOnly](Object object)
          {
            Status::Clear(Status::FlagFetching);

            if (object->get_id() == td::td_api::error::ID)
            {
              LOG_WARNING("get chat details failed %s", chatId.c_str());
              return;
            }

            auto tchat = td::move_tl_object_as<td::td_api::chat>(object);

            if (!tchat) return;

            if (tchat->type_->get_id() == td::td_api::chatTypePrivate::ID)
            {
              m_ChatTypes[tchat->id_] = ChatPrivate;
            }
            else if (tchat->type_->get_id() == td::td_api::chatTypeSupergroup::ID)
            {
              auto typeSupergroup =
                td::move_tl_object_as<td::td_api::chatTypeSupergroup>(tchat->type_);
              if (typeSupergroup->is_channel_)
              {
                m_ChatTypes[tchat->id_] = ChatSuperGroupChannel;
              }
              else
              {
                m_ChatTypes[tchat->id_] = ChatSuperGroup;
              }
            }
            else if (tchat->type_->get_id() == td::td_api::chatTypeBasicGroup::ID)
            {
              m_ChatTypes[tchat->id_] = ChatBasicGroup;
            }
            else if (tchat->type_->get_id() == td::td_api::chatTypeSecret::ID)
            {
              m_ChatTypes[tchat->id_] = ChatSecret;
            }
            else
            {
              LOG_WARNING("unknown chat type %d", tchat->type_->get_id());
            }

            if (isGetTypeOnly) return;

            ChatInfo chatInfo;
            chatInfo.id = StrUtil::NumToHex(tchat->id_);
            chatInfo.isUnread = (tchat->unread_count_ > 0);
            chatInfo.isUnreadMention = (tchat->unread_mention_count_ > 0);
            chatInfo.isMuted = (tchat->notification_settings_->mute_for_ > 0);
            int64_t lastMessageTimeSec =
              (tchat->last_message_ != nullptr) ? tchat->last_message_->date_ : 0;
            int64_t lastMessageHash =
              (tchat->last_message_ != nullptr) ? (std::hash<std::string>{ } (StrUtil::NumToHex(tchat->last_message_->id_)) % 256) : 0;
            chatInfo.lastMessageTime = (lastMessageTimeSec * 1000) + lastMessageHash;

            std::vector<ChatInfo> chatInfos;
            chatInfos.push_back(chatInfo);

            std::shared_ptr<NewChatsNotify> newChatsNotify =
              std::make_shared<NewChatsNotify>(m_ProfileId);
            newChatsNotify->success = true;
            newChatsNotify->chatInfos = chatInfos;
            CallMessageHandler(newChatsNotify);

            m_LastReadInboxMessage[tchat->id_] = tchat->last_read_inbox_message_id_;
            m_LastReadOutboxMessage[tchat->id_] = tchat->last_read_outbox_message_id_;

            std::set<int64_t>& unreadMessages = m_UnreadOutboxMessages[tchat->id_];
            if (!unreadMessages.empty())
            {
              for (auto it = unreadMessages.begin(); it != unreadMessages.end(); /* inc in loop */)
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

            const int64_t contactId = tuser->id_;
            ContactInfo contactInfo;
            contactInfo.id = StrUtil::NumToHex(contactId);
            contactInfo.name =
              tuser->first_name_ + (tuser->last_name_.empty() ? "" : " " + tuser->last_name_);
            contactInfo.isSelf = IsSelf(contactId);
            m_ContactInfos[contactId] = contactInfo;

            std::vector<ContactInfo> contactInfos;
            contactInfos.push_back(contactInfo);

            std::shared_ptr<NewContactsNotify> newContactsNotify =
              std::make_shared<NewContactsNotify>(m_ProfileId);
            newContactsNotify->contactInfos = contactInfos;
            CallMessageHandler(newContactsNotify);
          });
        }
      }
      break;

    case GetMessageRequestType:
      {
        LOG_DEBUG("Get message");
        std::shared_ptr<GetMessageRequest> getMessageRequest =
          std::static_pointer_cast<GetMessageRequest>(p_RequestMessage);

        if (getMessageRequest->cached)
        {
          if (MessageCache::FetchOneMessage(m_ProfileId, getMessageRequest->chatId,
                                            getMessageRequest->msgId, false /*p_Sync*/))
          {
            return;
          }
        }

        int64_t chatId = StrUtil::NumFromHex<int64_t>(getMessageRequest->chatId);
        int64_t fromMsgId = StrUtil::NumFromHex<int64_t>(getMessageRequest->msgId);
        int32_t offset = -1; // to get fromMsgId itself
        int32_t limit = 1;
        bool sequence = false; // out-of-sequence single message
        GetChatHistory(chatId, fromMsgId, offset, limit, sequence);
      }
      break;

    case GetMessagesRequestType:
      {
        LOG_DEBUG("Get messages");
        std::shared_ptr<GetMessagesRequest> getMessagesRequest =
          std::static_pointer_cast<GetMessagesRequest>(p_RequestMessage);

        if (!getMessagesRequest->fromMsgId.empty() ||
            (getMessagesRequest->limit == std::numeric_limits<int>::max()))
        {
          if (MessageCache::FetchMessagesFrom(m_ProfileId, getMessagesRequest->chatId,
                                              getMessagesRequest->fromMsgId,
                                              getMessagesRequest->limit, false /* p_Sync */))
          {
            return;
          }
        }

        int64_t chatId = StrUtil::NumFromHex<int64_t>(getMessagesRequest->chatId);
        int64_t fromMsgId = StrUtil::NumFromHex<int64_t>(getMessagesRequest->fromMsgId);
        int32_t offset = 0;
        int32_t limit = getMessagesRequest->limit;
        bool sequence = true; // in-sequence history request
        GetChatHistory(chatId, fromMsgId, offset, limit, sequence);
      }
      break;

    case SendMessageRequestType:
      {
        LOG_DEBUG("Send message");
        Status::Set(Status::FlagSending);
        std::shared_ptr<SendMessageRequest> sendMessageRequest =
          std::static_pointer_cast<SendMessageRequest>(
          p_RequestMessage);

        auto send_message = td::td_api::make_object<td::td_api::sendMessage>();
        send_message->chat_id_ = StrUtil::NumFromHex<int64_t>(sendMessageRequest->chatId);

        if (sendMessageRequest->chatMessage.fileInfo.empty())
        {
          auto message_content = GetMessageText(sendMessageRequest->chatMessage.text);
          send_message->input_message_content_ = std::move(message_content);
          send_message->reply_to_message_id_ =
            StrUtil::NumFromHex<int64_t>(sendMessageRequest->chatMessage.quotedId);
        }
        else
        {
          FileInfo fileInfo =
            ProtocolUtil::FileInfoFromHex(sendMessageRequest->chatMessage.fileInfo);
          auto message_content = td::td_api::make_object<td::td_api::inputMessageDocument>();
          message_content->document_ = td::td_api::make_object<td::td_api::inputFileLocal>(
            fileInfo.filePath);
          send_message->input_message_content_ = std::move(message_content);
        }

        SendQuery(std::move(send_message),
                  [this, sendMessageRequest](Object object)
        {
          Status::Clear(Status::FlagSending);

          if (object->get_id() == td::td_api::error::ID) return;

          std::shared_ptr<SendMessageNotify> sendMessageNotify =
            std::make_shared<SendMessageNotify>(m_ProfileId);
          sendMessageNotify->success = true;

          sendMessageNotify->chatId = sendMessageRequest->chatId;
          sendMessageNotify->chatMessage = sendMessageRequest->chatMessage;
          CallMessageHandler(sendMessageNotify);
        });
      }
      break;

    case EditMessageRequestType:
      {
        LOG_DEBUG("Edit message");
        Status::Set(Status::FlagSending);
        std::shared_ptr<EditMessageRequest> editMessageRequest =
          std::static_pointer_cast<EditMessageRequest>(p_RequestMessage);

        auto edit_message = td::td_api::make_object<td::td_api::editMessageText>();
        edit_message->chat_id_ = StrUtil::NumFromHex<int64_t>(editMessageRequest->chatId);
        edit_message->message_id_ = StrUtil::NumFromHex<int64_t>(editMessageRequest->msgId);

        auto message_content = GetMessageText(editMessageRequest->chatMessage.text);
        edit_message->input_message_content_ = std::move(message_content);

        SendQuery(std::move(edit_message),
                  [](Object object)
        {
          Status::Clear(Status::FlagSending);

          if (object->get_id() == td::td_api::error::ID) return;
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

        if (IsSponsoredMessageId(markMessageReadRequest->msgId))
        {
          ViewSponsoredMessage(markMessageReadRequest->chatId, markMessageReadRequest->msgId);
          return;
        }

        std::vector<std::int64_t> msgIds =
          { StrUtil::NumFromHex<int64_t>(markMessageReadRequest->msgId) };
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
        std::shared_ptr<DeleteMessageRequest> deleteMessageRequest =
          std::static_pointer_cast<DeleteMessageRequest>(
          p_RequestMessage);
        int64_t chatId = StrUtil::NumFromHex<int64_t>(deleteMessageRequest->chatId);
        std::vector<std::int64_t> msgIds =
          { StrUtil::NumFromHex<int64_t>(deleteMessageRequest->msgId) };

        auto delete_messages = td::td_api::make_object<td::td_api::deleteMessages>();
        delete_messages->chat_id_ = chatId;
        delete_messages->message_ids_ = msgIds;
        delete_messages->revoke_ = true; // delete for all (if possible)

        SendQuery(std::move(delete_messages),
                  [this, deleteMessageRequest](Object object)
        {
          Status::Clear(Status::FlagUpdating);

          std::shared_ptr<DeleteMessageNotify> deleteMessageNotify =
            std::make_shared<DeleteMessageNotify>(m_ProfileId);
          deleteMessageNotify->success = (object->get_id() != td::td_api::error::ID);
          deleteMessageNotify->chatId = deleteMessageRequest->chatId;
          deleteMessageNotify->msgId = deleteMessageRequest->msgId;
          CallMessageHandler(deleteMessageNotify);
        });
      }
      break;

    case SendTypingRequestType:
      {
        LOG_DEBUG("Send typing");
        std::shared_ptr<SendTypingRequest> sendTypingRequest =
          std::static_pointer_cast<SendTypingRequest>(
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

          std::shared_ptr<SendTypingNotify> sendTypingNotify =
            std::make_shared<SendTypingNotify>(m_ProfileId);
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

        auto option_value = td::td_api::make_object<td::td_api::optionValueBoolean>(isOnline);
        auto set_option =
          td::td_api::make_object<td::td_api::setOption>("online", std::move(option_value));
        SendQuery(std::move(set_option),
                  [this, setStatusRequest](Object object)
        {
          if (object->get_id() == td::td_api::error::ID) return;

          std::shared_ptr<SetStatusNotify> setStatusNotify =
            std::make_shared<SetStatusNotify>(m_ProfileId);
          setStatusNotify->success = true;
          setStatusNotify->isOnline = setStatusRequest->isOnline;
          CallMessageHandler(setStatusNotify);
        });
      }
      break;

    case CreateChatRequestType:
      {
        Status::Set(Status::FlagUpdating);

        std::shared_ptr<CreateChatRequest> createChatRequest =
          std::static_pointer_cast<CreateChatRequest>(p_RequestMessage);

        ChatType chatType = ChatPrivate;
        int64_t rawUserId = StrUtil::NumFromHex<int64_t>(createChatRequest->userId);
        auto typeIt = m_ChatTypes.find(rawUserId);
        if (typeIt != m_ChatTypes.end())
        {
          chatType = typeIt->second;
        }

        if (chatType == ChatPrivate)
        {
          int64_t userId = StrUtil::NumFromHex<int64_t>(createChatRequest->userId);
          LOG_DEBUG("create chat private %s %lld", createChatRequest->userId.c_str(), userId);
          auto createChat = td::td_api::make_object<td::td_api::createPrivateChat>();
          createChat->user_id_ = userId;
          SendQuery(std::move(createChat), [this](Object obj) { CreateChat(std::move(obj)); });
        }
        else if (chatType == ChatBasicGroup)
        {
          std::string userIdStr = createChatRequest->userId.substr(8); // remove "-100" prefix
          int64_t userId = StrUtil::NumFromHex<int64_t>(userIdStr);
          LOG_DEBUG("create chat basic group %s %lld", createChatRequest->userId.c_str(), userId);
          auto createChat = td::td_api::make_object<td::td_api::createBasicGroupChat>();
          createChat->basic_group_id_ = userId;
          SendQuery(std::move(createChat), [this](Object obj) { CreateChat(std::move(obj)); });
        }
        else if ((chatType == ChatSuperGroup) || (chatType == ChatSuperGroupChannel))
        {
          std::string userIdStr = createChatRequest->userId.substr(8); // remove "-100" prefix
          int64_t userId = StrUtil::NumFromHex<int64_t>(userIdStr);
          LOG_DEBUG("create chat super group %s %lld", createChatRequest->userId.c_str(), userId);
          auto createChat = td::td_api::make_object<td::td_api::createSupergroupChat>();
          createChat->supergroup_id_ = userId;
          SendQuery(std::move(createChat), [this](Object obj) { CreateChat(std::move(obj)); });
        }
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

        auto get_remote_file = td::td_api::make_object<td::td_api::getRemoteFile>();
        get_remote_file->remote_file_id_ = fileId;
        get_remote_file->file_type_ = nullptr;
        SendQuery(std::move(get_remote_file),
                  [this, chatId, msgId, fileId, downloadFileAction](Object object)
        {
          if (object->get_id() == td::td_api::error::ID) return;

          if (object->get_id() == td::td_api::file::ID)
          {
            auto file_ = td::move_tl_object_as<td::td_api::file>(object);
            std::string downloadId = StrUtil::NumToHex(file_->id_);

            std::shared_ptr<DeferDownloadFileRequest> deferDownloadFileRequest =
              std::make_shared<DeferDownloadFileRequest>();
            deferDownloadFileRequest->chatId = chatId;
            deferDownloadFileRequest->msgId = msgId;
            deferDownloadFileRequest->fileId = fileId;
            deferDownloadFileRequest->downloadId = downloadId;
            deferDownloadFileRequest->downloadFileAction = downloadFileAction;
            SendRequest(deferDownloadFileRequest);
          }
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
        std::string downloadId = deferDownloadFileRequest->downloadId;
        DownloadFileAction downloadFileAction = deferDownloadFileRequest->downloadFileAction;
        DownloadFile(chatId, msgId, fileId, downloadId, downloadFileAction);
      }
      break;

    case SetCurrentChatRequestType:
      {
        std::shared_ptr<SetCurrentChatRequest> setCurrentChatRequest =
          std::static_pointer_cast<SetCurrentChatRequest>(p_RequestMessage);
        int64_t chatId = StrUtil::NumFromHex<int64_t>(setCurrentChatRequest->chatId);
        m_CurrentChat = chatId;
        RequestSponsoredMessagesIfNeeded();
      }
      break;

    case DeferGetSponsoredMessagesRequestType:
      {
        std::shared_ptr<DeferGetSponsoredMessagesRequest> deferGetSponsoredMessagesRequest =
          std::static_pointer_cast<DeferGetSponsoredMessagesRequest>(p_RequestMessage);
        std::string chatId = deferGetSponsoredMessagesRequest->chatId;
        GetSponsoredMessages(chatId);
      }
      break;

    default:
      LOG_DEBUG("unknown request message %d", p_RequestMessage->GetMessageType());
      break;
  }
  // *INDENT-ON*
}

void TgChat::Impl::Init()
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
  InitProxy();
}

void TgChat::Impl::InitProxy()
{
  SendQuery(td::td_api::make_object<td::td_api::getProxies>(),
            [this](Object proxiesObject)
  {
    if (!proxiesObject) return;

    if (proxiesObject->get_id() == td::td_api::error::ID) return;

    auto proxies = td::move_tl_object_as<td::td_api::proxies>(proxiesObject);
    if (!proxies) return;

    for (const td::td_api::object_ptr<td::td_api::proxy> &proxy : proxies->proxies_)
    {
      if (proxy)
      {
        const int32_t proxyId = proxy->id_;
        SendQuery(td::td_api::make_object<td::td_api::removeProxy>(proxyId),
                  [proxyId](Object object)
        {
          if (object->get_id() == td::td_api::error::ID) return;

          LOG_TRACE("removed proxy %d", proxyId);
        });
      }
    }

    const std::string proxyHost = AppConfig::GetStr("proxy_host");
    const int proxyPort = AppConfig::GetNum("proxy_port");
    if (!proxyHost.empty() && (proxyPort != 0))
    {
      const std::string proxyUser = AppConfig::GetStr("proxy_user");
      const std::string proxyPass = AppConfig::GetStr("proxy_pass");
      const bool proxyEnable = true;
      auto proxyType = (!proxyUser.empty()) ? td::make_tl_object<td::td_api::proxyTypeSocks5>(proxyUser, proxyPass)
                                            : td::td_api::make_object<td::td_api::proxyTypeSocks5>();
      SendQuery(td::td_api::make_object<td::td_api::addProxy>(proxyHost, proxyPort, proxyEnable, std::move(proxyType)),
                [](Object object)
      {
        if (object->get_id() == td::td_api::error::ID) return;

        LOG_TRACE("added proxy");
      });
    }
  });
}

void TgChat::Impl::Cleanup()
{
  m_Config.Save();
  td::td_api::close();
}

void TgChat::Impl::ProcessService()
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

void TgChat::Impl::ProcessResponse(td::Client::Response response)
{
  if (!response.object) return;

  if (response.id == 0) return ProcessUpdate(std::move(response.object));

  auto it = m_Handlers.find(response.id);
  if (it != m_Handlers.end())
  {
    it->second(std::move(response.object));
  }
}

void TgChat::Impl::ProcessUpdate(td::td_api::object_ptr<td::td_api::Object> update)
{
  // *INDENT-OFF*
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

    const int64_t contactId = update_new_chat.chat_->id_;
    ContactInfo contactInfo;
    contactInfo.id = StrUtil::NumToHex(contactId);
    contactInfo.name = update_new_chat.chat_->title_;
    contactInfo.isSelf = IsSelf(contactId);
    m_ContactInfos[contactId] = contactInfo;

    std::shared_ptr<NewContactsNotify> newContactsNotify =
      std::make_shared<NewContactsNotify>(m_ProfileId);
    newContactsNotify->contactInfos = std::vector<ContactInfo>({ contactInfo });
    CallMessageHandler(newContactsNotify);
  },
  [this](td::td_api::updateChatTitle& update_chat_title)
  {
    LOG_TRACE("chat title update");

    const int64_t contactId = update_chat_title.chat_id_;
    ContactInfo contactInfo;
    contactInfo.id = StrUtil::NumToHex(contactId);
    contactInfo.name = update_chat_title.title_;
    contactInfo.isSelf = IsSelf(contactId);
    m_ContactInfos[contactId] = contactInfo;

    std::shared_ptr<NewContactsNotify> newContactsNotify =
      std::make_shared<NewContactsNotify>(m_ProfileId);
    newContactsNotify->contactInfos = std::vector<ContactInfo>({ contactInfo });
    CallMessageHandler(newContactsNotify);
  },
  [this](td::td_api::updateUser& update_user)
  {
    LOG_TRACE("user update");

    td::td_api::object_ptr<td::td_api::user> user = std::move(update_user.user_);

    const int64_t contactId = user->id_;
    ContactInfo contactInfo;
    contactInfo.id = StrUtil::NumToHex(contactId);
    contactInfo.name =
      user->first_name_ + (user->last_name_.empty() ? "" : " " + user->last_name_);;
    contactInfo.isSelf = IsSelf(contactId);
    m_ContactInfos[contactId] = contactInfo;

    std::shared_ptr<NewContactsNotify> newContactsNotify =
      std::make_shared<NewContactsNotify>(m_ProfileId);
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

      std::shared_ptr<NewMessagesNotify> newMessagesNotify =
        std::make_shared<NewMessagesNotify>(m_ProfileId);
      newMessagesNotify->success = true;
      newMessagesNotify->chatId = StrUtil::NumToHex(message->chat_id_);
      newMessagesNotify->chatMessages = chatMessages;
      newMessagesNotify->cached = false;
      newMessagesNotify->sequence = true;
      CallMessageHandler(newMessagesNotify);
    }
  },
  [this](td::td_api::updateMessageSendSucceeded& update_message_send_succeeded)
  {
    LOG_TRACE("msg send update");

    auto message =
      td::move_tl_object_as<td::td_api::message>(update_message_send_succeeded.message_);
    ChatMessage chatMessage;
    TdMessageConvert(*message, chatMessage);

    std::vector<ChatMessage> chatMessages;
    chatMessages.push_back(chatMessage);

    std::shared_ptr<NewMessagesNotify> newMessagesNotify =
      std::make_shared<NewMessagesNotify>(m_ProfileId);
    newMessagesNotify->success = true;
    newMessagesNotify->chatId = StrUtil::NumToHex(message->chat_id_);
    newMessagesNotify->chatMessages = chatMessages;
    newMessagesNotify->cached = false;
    newMessagesNotify->sequence = true;
    CallMessageHandler(newMessagesNotify);
  },
  [this](td::td_api::updateChatAction& user_chat_action)
  {
    LOG_TRACE("user chat action update");

    int64_t chatId = user_chat_action.chat_id_;

    if (user_chat_action.sender_id_->get_id() == td::td_api::messageSenderUser::ID)
    {
      auto& message_sender_user =
        static_cast<const td::td_api::messageSenderUser&>(*user_chat_action.sender_id_);
      int64_t userId = message_sender_user.user_id_;

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

      std::shared_ptr<ReceiveTypingNotify> receiveTypingNotify =
        std::make_shared<ReceiveTypingNotify>(m_ProfileId);
      receiveTypingNotify->chatId = StrUtil::NumToHex(chatId);
      receiveTypingNotify->userId = StrUtil::NumToHex(userId);
      receiveTypingNotify->isTyping = isTyping;
      CallMessageHandler(receiveTypingNotify);
    }
  },
  [this](td::td_api::updateUserStatus& user_status)
  {
    LOG_TRACE("user status update");

    ProcessStatusUpdate(user_status.user_id_, std::move(user_status.status_));
  },
  [this](td::td_api::updateChatReadOutbox& chat_read_outbox)
  {
    LOG_TRACE("chat read outbox update");

    m_LastReadOutboxMessage[chat_read_outbox.chat_id_] =
      chat_read_outbox.last_read_outbox_message_id_;

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
  [this](td::td_api::updateDeleteMessages& delete_messages)
  {
    if (!delete_messages.is_permanent_ || delete_messages.from_cache_) return;

    LOG_TRACE("delete messages update");

    std::string chatId = StrUtil::NumToHex(delete_messages.chat_id_);
    std::vector<std::int64_t> msgIds = delete_messages.message_ids_;
    for (const auto& msgId : msgIds)
    {
      std::shared_ptr<DeleteMessageNotify> deleteMessageNotify =
        std::make_shared<DeleteMessageNotify>(m_ProfileId);
      deleteMessageNotify->success = true;
      deleteMessageNotify->chatId = chatId;
      deleteMessageNotify->msgId = StrUtil::NumToHex(msgId);
      CallMessageHandler(deleteMessageNotify);
    }
  },
  [](td::td_api::updateConnectionState& connection_state)
  {
    LOG_TRACE("update connection state");

    if (!connection_state.state_) return;

    if (connection_state.state_->get_id() == td::td_api::connectionStateReady::ID)
    {
      Status::Set(Status::FlagOnline);
      Status::Clear(Status::FlagOffline);
    }
    else
    {
      Status::Set(Status::FlagOffline);
      Status::Clear(Status::FlagOnline);
    }
  },
  [this](td::td_api::updateMessageContent& update_message_content)
  {
    LOG_TRACE("update message content");

    int64_t chatId = update_message_content.chat_id_;
    int64_t msgId = update_message_content.message_id_;

    std::shared_ptr<GetMessageRequest> getMessageRequest =
      std::make_shared<GetMessageRequest>();
    getMessageRequest->chatId = StrUtil::NumToHex(chatId);
    getMessageRequest->msgId = StrUtil::NumToHex(msgId);
    getMessageRequest->cached = false;
    SendRequest(getMessageRequest);
  },
  [this](td::td_api::updateMessageEdited& update_message_edited)
  {
    LOG_TRACE("update message edited");

    int64_t chatId = update_message_edited.chat_id_;
    int64_t msgId = update_message_edited.message_id_;

    std::shared_ptr<GetMessageRequest> getMessageRequest =
      std::make_shared<GetMessageRequest>();
    getMessageRequest->chatId = StrUtil::NumToHex(chatId);
    getMessageRequest->msgId = StrUtil::NumToHex(msgId);
    getMessageRequest->cached = false;
    SendRequest(getMessageRequest);
  },
  [](td::td_api::updateRecentStickers&)
  {
    LOG_TRACE("update recent stickers");
  },
  [](td::td_api::updateFavoriteStickers&)
  {
    LOG_TRACE("update favorite stickers");
  },
  [](td::td_api::updateInstalledStickerSets&)
  {
    LOG_TRACE("update installed sticker sets");
  },
  [](td::td_api::updateTrendingStickerSets&)
  {
    LOG_TRACE("update trending sticker sets");
  },
  [](td::td_api::updateOption&)
  {
    LOG_TRACE("update option");
  },
  [](td::td_api::updateSelectedBackground&)
  {
    LOG_TRACE("update selected background");
  },
  [](td::td_api::updateScopeNotificationSettings&)
  {
    LOG_TRACE("update scope notification settings");
  },
  [](td::td_api::updateUnreadChatCount&)
  {
    LOG_TRACE("update unread chat count");
  },
  [](td::td_api::updateHavePendingNotifications&)
  {
    LOG_TRACE("update have pending notifications");
  },
  [](td::td_api::updateChatLastMessage&)
  {
    LOG_TRACE("update chat last message");
  },
  [](td::td_api::updateDiceEmojis&)
  {
    LOG_TRACE("update dice emojis");
  },
  [](td::td_api::updateChatPosition&)
  {
    LOG_TRACE("update chat position");
  },
  [](td::td_api::updateSupergroup&)
  {
    LOG_TRACE("update supergroup");
  },
  [](td::td_api::updateChatThemes&)
  {
    LOG_TRACE("update chat themes");
  },
  [](td::td_api::updateUnreadMessageCount&)
  {
    LOG_TRACE("update unread message count");
  },
  [](td::td_api::updateAnimationSearchParameters&)
  {
    LOG_TRACE("update animation search parameters");
  },
  [](td::td_api::updateBasicGroup&)
  {
    LOG_TRACE("update basic group");
  },
  [](td::td_api::updateSavedAnimations&)
  {
    LOG_TRACE("update saved animations");
  },
  [](td::td_api::updateFile&)
  {
    LOG_TRACE("update file");
  },
  [](auto& anyupdate)
  {
    LOG_TRACE("other update %d", anyupdate.get_id());
  }
  ));
  // *INDENT-ON*
}

void TgChat::Impl::ProcessStatusUpdate(int64_t p_UserId,
                                       td::td_api::object_ptr<td::td_api::UserStatus> p_Status)
{
  if (!p_Status) return;

  if (IsGroup(p_UserId) || IsSelf(p_UserId)) return;

  bool isOnline = false;
  int64_t timeSeen = TimeSeenNone;
  std::int32_t statusId = p_Status->get_id();
  switch (statusId)
  {
    case td::td_api::userStatusOnline::ID:
      isOnline = true;
      break;

    case td::td_api::userStatusLastMonth::ID:
      timeSeen = TimeSeenLastMonth;
      break;

    case td::td_api::userStatusLastWeek::ID:
      timeSeen = TimeSeenLastWeek;
      break;

    case td::td_api::userStatusOffline::ID:
      {
        auto& user_status_offline =
          static_cast<const td::td_api::userStatusOffline&>(*p_Status);
        timeSeen = static_cast<int64_t>(user_status_offline.was_online_) * 1000;
      }
      break;

    case td::td_api::userStatusEmpty::ID:
      break;

    case td::td_api::userStatusRecently::ID:
      break;

    default:
      break;
  }

  std::shared_ptr<ReceiveStatusNotify> receiveStatusNotify =
    std::make_shared<ReceiveStatusNotify>(m_ProfileId);
  receiveStatusNotify->userId = StrUtil::NumToHex(p_UserId);
  receiveStatusNotify->isOnline = isOnline;
  receiveStatusNotify->timeSeen = timeSeen;
  CallMessageHandler(receiveStatusNotify);
}

std::function<void(TgChat::Impl::Object)> TgChat::Impl::CreateAuthQueryHandler()
{
  return [this, id = m_AuthQueryId](Object object)
  {
    if (id == m_AuthQueryId)
    {
      CheckAuthError(std::move(object));
    }
  };
}

void TgChat::Impl::OnAuthStateUpdate()
{
  m_AuthQueryId++;
  // *INDENT-OFF*
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

        if (m_ContactInfos.count(m_SelfUserId))
        {
          // notify of (self) contact again, now that self user id is known
          m_ContactInfos[m_SelfUserId].isSelf = true;
          std::shared_ptr<NewContactsNotify> newContactsNotify =
            std::make_shared<NewContactsNotify>(m_ProfileId);
          newContactsNotify->contactInfos =
            std::vector<ContactInfo>({ m_ContactInfos[m_SelfUserId] });
          CallMessageHandler(newContactsNotify);
        }
      });

      std::shared_ptr<ConnectNotify> connectNotify = std::make_shared<ConnectNotify>(m_ProfileId);
      connectNotify->success = true;

      std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
        std::make_shared<DeferNotifyRequest>();
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
      SendQuery(td::td_api::make_object<td::td_api::setAuthenticationPhoneNumber>(phone_number,
                                                                                  nullptr),
                CreateAuthQueryHandler());
    }
    else
    {
      LOG_DEBUG("unexpected state");
      m_Running = false;
    }
  },
  [this](td::td_api::authorizationStateWaitTdlibParameters&)
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

    const std::string dbPath(m_ProfileDir + std::string("/tdlib"));
    auto set_parameters = td::td_api::make_object<td::td_api::setTdlibParameters>();
    set_parameters->use_test_dc_ = false;
    set_parameters->database_directory_ = dbPath;
    set_parameters->database_encryption_key_ = key;
    set_parameters->use_message_database_ = true;
    set_parameters->use_secret_chats_ = true;

    std::string apiId = getenv("TG_APIID") ? getenv("TG_APIID")
      : StrUtil::StrFromHex("3130343132303237");
    set_parameters->api_id_ = StrUtil::ToInteger(apiId);

    std::string apiHash = getenv("TG_APIHASH") ? getenv("TG_APIHASH")
      : StrUtil::StrFromHex("3536373261353832633265666532643939363232326636343237386563616163");
    set_parameters->api_hash_ = apiHash;

    set_parameters->system_language_code_ = "en";
    set_parameters->device_model_ = "Desktop";
#ifdef __linux__
    set_parameters->system_version_ = "Linux";
#elif __APPLE__
    set_parameters->system_version_ = "Darwin";
#else
    set_parameters->system_version_ = "Unknown";
#endif
    static std::string appVersion = AppUtil::GetAppVersion();
    set_parameters->application_version_ = appVersion.c_str();
    set_parameters->enable_storage_optimizer_ = true;
    SendQuery(std::move(set_parameters), CreateAuthQueryHandler());
  },
  [](td::td_api::authorizationStateWaitOtherDeviceConfirmation& state)
  {
    std::cout << "Confirm this login link on another device:\n" << state.link_ << "\n";
  },
  [this](auto& anystate)
  {
    LOG_DEBUG("unexpected authorization state %d", anystate.get_id());
    m_Running = false;
  }
  ));
  // *INDENT-ON*
}

void TgChat::Impl::SendQuery(td::td_api::object_ptr<td::td_api::Function> f,
                             std::function<void(Object)> handler)
{
  auto query_id = GetNextQueryId();
  if (handler)
  {
    m_Handlers.emplace(query_id, std::move(handler));
  }
  m_Client->send({ query_id, std::move(f) });
}

void TgChat::Impl::CheckAuthError(Object object)
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

void TgChat::Impl::CreateChat(Object p_Object)
{
  Status::Clear(Status::FlagUpdating);

  if (p_Object->get_id() == td::td_api::error::ID)
  {
    LOG_WARNING("create chat failed");
    return;
  }

  auto chat = td::move_tl_object_as<td::td_api::chat>(p_Object);

  ChatInfo chatInfo;
  chatInfo.id = StrUtil::NumToHex(chat->id_);

  std::shared_ptr<CreateChatNotify> createChatNotify = std::make_shared<CreateChatNotify>(m_ProfileId);
  createChatNotify->success = true;
  createChatNotify->chatInfo = chatInfo;

  CallMessageHandler(createChatNotify);
}

std::string TgChat::Impl::GetRandomString(size_t p_Len)
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

std::uint64_t TgChat::Impl::GetNextQueryId()
{
  return ++m_CurrentQueryId;
}

std::int64_t TgChat::Impl::GetSenderId(const td::td_api::message& p_TdMessage)
{
  std::int64_t senderId = 0;
  if (p_TdMessage.sender_id_->get_id() == td::td_api::messageSenderUser::ID)
  {
    auto& message_sender_user =
      static_cast<const td::td_api::messageSenderUser&>(*p_TdMessage.sender_id_);
    senderId = message_sender_user.user_id_;
  }
  else if (p_TdMessage.sender_id_->get_id() == td::td_api::messageSenderChat::ID)
  {
    auto& message_sender_chat =
      static_cast<const td::td_api::messageSenderChat&>(*p_TdMessage.sender_id_);
    senderId = message_sender_chat.chat_id_;
  }

  return senderId;
}

std::string TgChat::Impl::GetText(td::td_api::object_ptr<td::td_api::formattedText>&& p_FormattedText)
{
  std::string text = p_FormattedText->text_;
  static const bool markdownEnabled = (m_Config.Get("markdown_enabled") == "1");
  static const int32_t markdownVersion = (m_Config.Get("markdown_version") == "1") ? 1 : 2;
  if (markdownEnabled)
  {
    auto getMarkdownText = td::td_api::make_object<td::td_api::getMarkdownText>(std::move(p_FormattedText));
    td::Client::Request parseRequest{ 2, std::move(getMarkdownText) };
    auto parseResponse = td::Client::execute(std::move(parseRequest));
    if (parseResponse.object->get_id() == td::td_api::formattedText::ID)
    {
      auto formattedText = td::td_api::move_object_as<td::td_api::formattedText>(parseResponse.object);
      text = formattedText->text_;
      if (markdownVersion == 1)
      {
        text = ConvertMarkdownV2ToV1(text);
      }
    }
  }

  return text;
}

void TgChat::Impl::TdMessageContentConvert(td::td_api::MessageContent& p_TdMessageContent, int64_t p_SenderId,
                                           std::string& p_Text, std::string& p_FileInfo)
{
  if (p_TdMessageContent.get_id() == td::td_api::messageText::ID)
  {
    auto& messageText = static_cast<td::td_api::messageText&>(p_TdMessageContent);
    p_Text = GetText(std::move(messageText.text_));
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageAnimatedEmoji::ID)
  {
    auto& messageAnimatedEmoji = static_cast<td::td_api::messageAnimatedEmoji&>(p_TdMessageContent);
    p_Text = messageAnimatedEmoji.emoji_;
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageAnimation::ID)
  {
    p_Text = "[Animation]";
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageAudio::ID)
  {
    auto& messageAudio = static_cast<td::td_api::messageAudio&>(p_TdMessageContent);

    std::string id = messageAudio.audio_->audio_->remote_->id_;
    std::string path = messageAudio.audio_->audio_->local_->path_;
    std::string fileName = messageAudio.audio_->file_name_;
    p_Text = GetText(std::move(messageAudio.caption_));
    FileInfo fileInfo;
    fileInfo.fileId = id;
    if (!path.empty())
    {
      fileInfo.filePath = path;
      fileInfo.fileStatus = FileStatusDownloaded;
    }
    else
    {
      fileInfo.filePath = fileName;
      fileInfo.fileStatus = FileStatusNotDownloaded;
    }

    p_FileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageCall::ID)
  {
    p_Text = "[Call]";
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageContact::ID)
  {
    p_Text = "[Contact]";
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageLocation::ID)
  {
    auto& messageLocation = static_cast<td::td_api::messageLocation&>(p_TdMessageContent);
    if (messageLocation.live_period_ == 0)
    {
      p_Text = "[Location]";
    }
    else
    {
      p_Text = "[LiveLocation]";
    }
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageContactRegistered::ID)
  {
    p_Text = "[Joined Telegram]";
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageCustomServiceAction::ID)
  {
    p_Text = "[CustomServiceAction]";
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageDocument::ID)
  {
    auto& messageDocument = static_cast<td::td_api::messageDocument&>(p_TdMessageContent);

    std::string id = messageDocument.document_->document_->remote_->id_;
    std::string path = messageDocument.document_->document_->local_->path_;
    std::string fileName = messageDocument.document_->file_name_;
    p_Text = GetText(std::move(messageDocument.caption_));
    FileInfo fileInfo;
    fileInfo.fileId = id;
    if (!path.empty())
    {
      fileInfo.filePath = path;
      fileInfo.fileStatus = FileStatusDownloaded;
    }
    else
    {
      fileInfo.filePath = fileName;
      fileInfo.fileStatus = FileStatusNotDownloaded;
    }

    p_FileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messagePhoto::ID)
  {
    auto& messagePhoto = static_cast<td::td_api::messagePhoto&>(p_TdMessageContent);
    auto& photo = messagePhoto.photo_;
    auto& sizes = photo->sizes_;
    p_Text = GetText(std::move(messagePhoto.caption_));
    if (!sizes.empty())
    {
      auto& largestSize = sizes.back();
      auto& photoFile = largestSize->photo_;
      auto& localFile = photoFile->local_;
      auto& localPath = localFile->path_;
      FileInfo fileInfo;
      std::string id = photoFile->remote_->id_;
      fileInfo.fileId = id;
      if (!localPath.empty())
      {
        fileInfo.filePath = localPath;
        fileInfo.fileStatus = FileStatusDownloaded;
      }
      else
      {
        fileInfo.filePath = "[Photo]";
        fileInfo.fileStatus = FileStatusNotDownloaded;
      }

      p_FileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
    }
    else
    {
      p_Text = "[Photo Error]";
    }
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageSticker::ID)
  {
    auto& messageSticker = static_cast<td::td_api::messageSticker&>(p_TdMessageContent);
    auto& sticker = messageSticker.sticker_;
    p_Text = sticker->emoji_;

    auto& stickerFile = sticker->sticker_;
    auto& localFile = stickerFile->local_;
    auto& localPath = localFile->path_;
    FileInfo fileInfo;
    std::string id = stickerFile->remote_->id_;
    fileInfo.fileId = id;
    if (!localPath.empty())
    {
      fileInfo.filePath = localPath;
      fileInfo.fileStatus = FileStatusDownloaded;
    }
    else
    {
      fileInfo.filePath = "[Sticker]";
      fileInfo.fileStatus = FileStatusNotDownloaded;
    }

    p_FileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageVideo::ID)
  {
    auto& messageVideo = static_cast<td::td_api::messageVideo&>(p_TdMessageContent);
    p_Text = GetText(std::move(messageVideo.caption_));
    auto& video = messageVideo.video_;
    auto& videoFile = video->video_;

    auto& localFile = videoFile->local_;
    auto& localPath = localFile->path_;
    FileInfo fileInfo;
    std::string id = videoFile->remote_->id_;
    fileInfo.fileId = id;
    if (!localPath.empty())
    {
      fileInfo.filePath = localPath;
      fileInfo.fileStatus = FileStatusDownloaded;
    }
    else
    {
      fileInfo.filePath = "[Video]";
      fileInfo.fileStatus = FileStatusNotDownloaded;
    }

    p_FileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageVideoNote::ID)
  {
    auto& messageVideoNote = static_cast<td::td_api::messageVideoNote&>(p_TdMessageContent);
    auto& videoNote = messageVideoNote.video_note_;
    auto& videoFile = videoNote->video_;

    auto& localFile = videoFile->local_;
    auto& localPath = localFile->path_;
    FileInfo fileInfo;
    std::string id = videoFile->remote_->id_;
    fileInfo.fileId = id;
    if (!localPath.empty())
    {
      fileInfo.filePath = localPath;
      fileInfo.fileStatus = FileStatusDownloaded;
    }
    else
    {
      fileInfo.filePath = "[VideoNote]";
      fileInfo.fileStatus = FileStatusNotDownloaded;
    }

    p_FileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageVoiceNote::ID)
  {
    auto& messageVoiceNote = static_cast<td::td_api::messageVoiceNote&>(p_TdMessageContent);

    std::string id = messageVoiceNote.voice_note_->voice_->remote_->id_;
    std::string path = messageVoiceNote.voice_note_->voice_->local_->path_;
    p_Text = GetText(std::move(messageVoiceNote.caption_));
    FileInfo fileInfo;
    fileInfo.fileId = id;
    if (!path.empty())
    {
      fileInfo.filePath = path;
      fileInfo.fileStatus = FileStatusDownloaded;
    }
    else
    {
      fileInfo.filePath = "[VoiceNote]";
      fileInfo.fileStatus = FileStatusNotDownloaded;
    }

    p_FileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageChatJoinByLink::ID)
  {
    p_Text = "[Joined]";
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageChatAddMembers::ID)
  {
    auto& messageChatAddMembers = static_cast<td::td_api::messageChatAddMembers&>(p_TdMessageContent);
    auto ids = messageChatAddMembers.member_user_ids_;

    if ((ids.size() == 1) && (ids.at(0) == p_SenderId))
    {
      p_Text = "[Joined]";
    }
    else
    {
      std::string idsStr;
      for (auto& id : ids)
      {
        idsStr += (idsStr.empty() ? "" : ",") + GetContactName(id);
      }

      p_Text = "[Added " + idsStr + "]";
    }
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageChatDeleteMember::ID)
  {
    auto& messageChatDeleteMember = static_cast<td::td_api::messageChatDeleteMember&>(p_TdMessageContent);
    auto id = messageChatDeleteMember.user_id_;
    if (id == p_SenderId)
    {
      p_Text = "[Left]";
    }
    else
    {
      p_Text = "[Removed " + GetContactName(id) + "]";
    }
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageChatChangeTitle::ID)
  {
    auto& messageChatChangeTitle = static_cast<td::td_api::messageChatChangeTitle&>(p_TdMessageContent);
    auto title = messageChatChangeTitle.title_;
    p_Text = "[Changed group name to " + title + "]";
  }
  else if (p_TdMessageContent.get_id() == td::td_api::messageChatUpgradeFrom::ID)
  {
    p_Text = "[Created]";
  }
  else
  {
    p_Text = "[UnknownMessage " + std::to_string(p_TdMessageContent.get_id()) + "]";
  }
}

void TgChat::Impl::TdMessageConvert(td::td_api::message& p_TdMessage, ChatMessage& p_ChatMessage)
{
  const int64_t senderId = GetSenderId(p_TdMessage);
  TdMessageContentConvert(*p_TdMessage.content_, senderId, p_ChatMessage.text, p_ChatMessage.fileInfo);

  p_ChatMessage.id = StrUtil::NumToHex(p_TdMessage.id_);
  p_ChatMessage.senderId = StrUtil::NumToHex(senderId);
  p_ChatMessage.isOutgoing = p_TdMessage.is_outgoing_;
  p_ChatMessage.timeSent = (((int64_t)p_TdMessage.date_) * 1000) + (std::hash<std::string>{ }(p_ChatMessage.id) % 256);
  p_ChatMessage.quotedId =
    (p_TdMessage.reply_to_message_id_ != 0) ? StrUtil::NumToHex(p_TdMessage.reply_to_message_id_) : "";
  p_ChatMessage.hasMention = p_TdMessage.contains_unread_mention_;

  if (IsSelf(p_TdMessage.chat_id_))
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
}

void TgChat::Impl::DownloadFile(std::string p_ChatId, std::string p_MsgId, std::string p_FileId,
                                std::string p_DownloadId,
                                DownloadFileAction p_DownloadFileAction)
{
  LOG_DEBUG("download file %s %s", p_FileId.c_str(), p_DownloadId.c_str());
  try
  {
    auto download_file = td::td_api::make_object<td::td_api::downloadFile>();
    download_file->file_id_ = StrUtil::NumFromHex<std::int32_t>(p_DownloadId);
    download_file->priority_ = 32;
    download_file->synchronous_ = true;
    SendQuery(std::move(download_file),
              [this, p_ChatId, p_MsgId, p_FileId, p_DownloadFileAction](Object object)
    {
      if (object->get_id() == td::td_api::error::ID) return;

      if (object->get_id() == td::td_api::file::ID)
      {
        auto file_ = td::move_tl_object_as<td::td_api::file>(object);
        std::string path = file_->local_->path_;

        FileInfo fileInfo;
        fileInfo.fileStatus = FileStatusDownloaded;
        fileInfo.filePath = path;
        fileInfo.fileId = p_FileId;

        std::shared_ptr<NewMessageFileNotify> newMessageFileNotify =
          std::make_shared<NewMessageFileNotify>(m_ProfileId);
        newMessageFileNotify->chatId = std::string(p_ChatId);
        newMessageFileNotify->msgId = std::string(p_MsgId);
        newMessageFileNotify->fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
        newMessageFileNotify->downloadFileAction = p_DownloadFileAction;

        CallMessageHandler(newMessageFileNotify);
      }
    });
  }
  catch (...)
  {
  }
}

void TgChat::Impl::RequestSponsoredMessagesIfNeeded()
{
  if (m_ChatTypes[m_CurrentChat] != ChatSuperGroupChannel) return;

#ifdef SIMULATED_SPONSORED_MESSAGES
  const int64_t intervalTime = 10 * 1000; // 10 sec
#else
  const int64_t intervalTime = 5 * 60 * 1000; // 5 min
#endif
  static std::map<int64_t, int64_t> lastTime;
  const int64_t nowTime = TimeUtil::GetCurrentTimeMSec();

  if ((nowTime - lastTime[m_CurrentChat]) >= intervalTime)
  {
    lastTime[m_CurrentChat] = nowTime;
    std::shared_ptr<DeferGetSponsoredMessagesRequest> deferGetSponsoredMessagesRequest =
      std::make_shared<DeferGetSponsoredMessagesRequest>();
    deferGetSponsoredMessagesRequest->chatId = StrUtil::NumToHex(m_CurrentChat);
    SendRequest(deferGetSponsoredMessagesRequest);
  }
}

void TgChat::Impl::GetSponsoredMessages(const std::string& p_ChatId)
{
  LOG_DEBUG("get sponsored messages %s", p_ChatId.c_str());

  // delete previous sponsored message(s) for this chat
  for (auto it =
         m_SponsoredMessageIds[p_ChatId].begin(); it != m_SponsoredMessageIds[p_ChatId].end();
       /* incremented in loop */)
  {
    const std::string msgId = *it;
    std::shared_ptr<DeleteMessageNotify> deleteMessageNotify = std::make_shared<DeleteMessageNotify>(m_ProfileId);
    deleteMessageNotify->success = true;
    deleteMessageNotify->chatId = p_ChatId;
    deleteMessageNotify->msgId = msgId;
    CallMessageHandler(deleteMessageNotify);
    it = m_SponsoredMessageIds[p_ChatId].erase(it);
  }

#ifdef SIMULATED_SPONSORED_MESSAGES
  // fake/simulated sponsored messages for dev/testing
  static int64_t sponsoredMessageId = 0;
  std::vector<ChatMessage> chatMessages;
  int num = 1 + (rand() % 2);
  for (int i = 0; i < num; ++i)
  {
    ++sponsoredMessageId;

    ChatMessage chatMessage;
    chatMessage.id = StrUtil::NumAddPrefix(StrUtil::NumToHex(sponsoredMessageId), m_SponsoredMessageMsgIdPrefix);
    chatMessage.timeSent = std::numeric_limits<int64_t>::max();
    chatMessage.isOutgoing = false;
    if ((sponsoredMessageId % 3) == 0)
    {
      chatMessage.text = "This is a long sponsored message. In fact, it has the maximum length "
        "allowed on the platform – 160 characters\xF0\x9F\x98\xAC\xF0\x9F\x98\xAC. It's "
        "promoting a bot with a start parameter."
        "\n[https://t.me/QuizBot?start=GreatMinds]";
      chatMessage.senderId = "393833303030323332";
    }
    else if ((sponsoredMessageId % 3) == 1)
    {
      chatMessage.text = "This is a regular sponsored message, it is promoting a channel."
        "\n[https://t.me/c/1001997501]";
      chatMessage.senderId = "2D31303031303031393937353031";
    }
    else if ((sponsoredMessageId % 3) == 2)
    {
      chatMessage.text = "This sponsored message is promoting a particular post in a channel."
        "\n[https://t.me/c/1006503122/172]";
      chatMessage.senderId = "2D31303031303036353033313232";
    }

    chatMessage.link = chatMessage.senderId;
    chatMessages.push_back(chatMessage);
    m_SponsoredMessageIds[p_ChatId].insert(chatMessage.id);
    LOG_DEBUG("new sponsored message %s (%d)", chatMessage.id.c_str(), sponsoredMessageId);

    // request chat type for senders
    const std::vector<std::string> chatIds = { chatMessage.senderId };
    std::shared_ptr<DeferGetChatDetailsRequest> deferGetChatDetailsRequest =
      std::make_shared<DeferGetChatDetailsRequest>();
    deferGetChatDetailsRequest->isGetTypeOnly = true;
    deferGetChatDetailsRequest->chatIds = chatIds;
    SendRequest(deferGetChatDetailsRequest);
  }

  std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(m_ProfileId);
  newMessagesNotify->success = true;
  newMessagesNotify->chatId = p_ChatId;
  newMessagesNotify->chatMessages = chatMessages;
  newMessagesNotify->fromMsgId = "";
  newMessagesNotify->cached = true; // do not cache sponsored messages
  newMessagesNotify->sequence = false;
  CallMessageHandler(newMessagesNotify);
#else
  // sponsored messages from telegram
  const int64_t chatId = StrUtil::NumFromHex<int64_t>(p_ChatId);
  SendQuery(td::td_api::make_object<td::td_api::getChatSponsoredMessages>(chatId),
            [this, p_ChatId](Object object)
  {
    if (object->get_id() == td::td_api::error::ID) return;

    auto sponsoredMessages = td::move_tl_object_as<td::td_api::sponsoredMessages>(object);

    std::vector<ChatMessage> chatMessages;
    for (auto it = sponsoredMessages->messages_.begin(); it != sponsoredMessages->messages_.end(); ++it)
    {
      auto sponsoredMessage = td::move_tl_object_as<td::td_api::sponsoredMessage>(*it);

      const int64_t sponsoredMessageId = sponsoredMessage->message_id_;
      ChatMessage chatMessage;
      TdMessageContentConvert(*sponsoredMessage->content_, sponsoredMessage->sponsor_chat_id_, chatMessage.text, chatMessage.fileInfo);

      chatMessage.id = StrUtil::NumAddPrefix(StrUtil::NumToHex(sponsoredMessageId), m_SponsoredMessageMsgIdPrefix);
      chatMessage.timeSent = std::numeric_limits<int64_t>::max();
      chatMessage.isOutgoing = false;
      chatMessage.senderId = StrUtil::NumToHex(sponsoredMessage->sponsor_chat_id_);

      std::string url;
      if (sponsoredMessage->link_)
      {
        if (sponsoredMessage->link_->get_id() == td::td_api::internalLinkTypeMessage::ID)
        {
          auto internalLink = td::move_tl_object_as<td::td_api::internalLinkTypeMessage>(sponsoredMessage->link_);
          url = internalLink->url_;
        }
        else if (sponsoredMessage->link_->get_id() == td::td_api::internalLinkTypeBotStart::ID)
        {
          auto internalLink = td::move_tl_object_as<td::td_api::internalLinkTypeBotStart>(sponsoredMessage->link_);
          url = "https://t.me/" + internalLink->bot_username_ + "?start=" + internalLink->start_parameter_;
        }
        else
        {
          LOG_WARNING("unknown internal link type: %lld", sponsoredMessage->link_->get_id());
        }
      }
      else
      {
        url = "https://t.me/c/" + std::to_string(sponsoredMessage->sponsor_chat_id_).substr(4);
      }

      if (!url.empty())
      {
        chatMessage.text += "\n[" + url + "]";
      }

      chatMessage.link = chatMessage.senderId;
      chatMessages.push_back(chatMessage);
      m_SponsoredMessageIds[p_ChatId].insert(chatMessage.id);
      LOG_DEBUG("new sponsored message %s (%lld)", chatMessage.id.c_str(), sponsoredMessageId);

      // request chat type for senders
      const std::vector<std::string> chatIds = { chatMessage.senderId };
      std::shared_ptr<DeferGetChatDetailsRequest> deferGetChatDetailsRequest =
        std::make_shared<DeferGetChatDetailsRequest>();
      deferGetChatDetailsRequest->isGetTypeOnly = true;
      deferGetChatDetailsRequest->chatIds = chatIds;
      SendRequest(deferGetChatDetailsRequest);
    }

    std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(m_ProfileId);
    newMessagesNotify->success = true;
    newMessagesNotify->chatId = p_ChatId;
    newMessagesNotify->chatMessages = chatMessages;
    newMessagesNotify->fromMsgId = "";
    newMessagesNotify->cached = true; // do not cache sponsored messages
    newMessagesNotify->sequence = false;
    CallMessageHandler(newMessagesNotify);
  });
#endif
}

void TgChat::Impl::ViewSponsoredMessage(const std::string& p_ChatId, const std::string& p_MsgId)
{
  if (!m_SponsoredMessageIds[p_ChatId].count(p_MsgId)) return;

  const int64_t msgId = StrUtil::NumFromHex<std::int64_t>(p_MsgId);
  LOG_DEBUG("view sponsored message %s (%lld)", p_MsgId.c_str(), msgId);

#ifdef SIMULATED_SPONSORED_MESSAGES
  // fake/simulated sponsored messages for dev/testing
#else
  // sponsored messages from telegram
  const int64_t chatId = StrUtil::NumFromHex<int64_t>(p_ChatId);
  const std::vector<std::int64_t> msgIds = { msgId };
  auto view_messages = td::td_api::make_object<td::td_api::viewMessages>();
  view_messages->chat_id_ = chatId;
  view_messages->message_ids_ = msgIds;
  view_messages->force_read_ = true;
  view_messages->source_ = td::td_api::make_object<td::td_api::messageSourceChatHistory>();

  SendQuery(std::move(view_messages), [msgId](Object object)
  {
    if (object->get_id() == td::td_api::error::ID)
    {
      auto error = td::td_api::move_object_as<td::td_api::error>(object);
      LOG_WARNING("view sponsored message failed %lld code %d (%s)", msgId, error->code_, error->message_.c_str());
    }
    else
    {
      LOG_TRACE("view sponsored message ok %lld", msgId);
    }
  });
#endif
}

bool TgChat::Impl::IsSponsoredMessageId(const std::string& p_MsgId)
{
  return StrUtil::NumHasPrefix(p_MsgId, m_SponsoredMessageMsgIdPrefix);
}

bool TgChat::Impl::IsGroup(int64_t p_UserId)
{
  return (p_UserId < 0);
}

bool TgChat::Impl::IsSelf(int64_t p_UserId)
{
  return (p_UserId == m_SelfUserId);
}

std::string TgChat::Impl::GetContactName(int64_t p_UserId)
{
  auto it = m_ContactInfos.find(p_UserId);
  if (it != m_ContactInfos.end())
  {
    return it->second.name;
  }
  else
  {
    return std::to_string(p_UserId);
  }
}

void TgChat::Impl::GetChatHistory(int64_t p_ChatId, int64_t p_FromMsgId, int32_t p_Offset, int32_t p_Limit, bool p_Sequence)
{
  // *INDENT-OFF*
  Status::Set(Status::FlagFetching);
  SendQuery(td::td_api::make_object<td::td_api::getChatHistory>(p_ChatId, p_FromMsgId, p_Offset,
                                                                p_Limit, false),
  [this, p_ChatId, p_FromMsgId, p_Offset, p_Sequence](Object object)
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

    std::shared_ptr<NewMessagesNotify> newMessagesNotify =
      std::make_shared<NewMessagesNotify>(m_ProfileId);
    newMessagesNotify->success = true;
    newMessagesNotify->chatId = StrUtil::NumToHex(p_ChatId);
    newMessagesNotify->chatMessages = chatMessages;
    newMessagesNotify->fromMsgId = ((p_FromMsgId != 0) && (p_Offset == 0)) ? StrUtil::NumToHex(p_FromMsgId) : "";
    newMessagesNotify->sequence = p_Sequence;
    CallMessageHandler(newMessagesNotify);
  });
  // *INDENT-ON*
}

td::td_api::object_ptr<td::td_api::inputMessageText> TgChat::Impl::GetMessageText(const std::string& p_Text)
{
  auto message_content = td::td_api::make_object<td::td_api::inputMessageText>();

  static const bool markdownEnabled = (m_Config.Get("markdown_enabled") == "1");
  static const int32_t markdownVersion = (m_Config.Get("markdown_version") == "1") ? 1 : 2;
  if (markdownEnabled)
  {
    auto textParseMarkdown =
      td::td_api::make_object<td::td_api::textParseModeMarkdown>(markdownVersion);
    auto parseTextEntities =
      td::td_api::make_object<td::td_api::parseTextEntities>(p_Text,
                                                             std::move(textParseMarkdown));
    td::Client::Request parseRequest{ 1, std::move(parseTextEntities) };
    auto parseResponse = td::Client::execute(std::move(parseRequest));
    if (parseResponse.object->get_id() == td::td_api::formattedText::ID)
    {
      auto formattedText =
        td::td_api::move_object_as<td::td_api::formattedText>(parseResponse.object);
      message_content->text_ = std::move(formattedText);
    }
  }

  if (!message_content->text_)
  {
    message_content->text_ = td::td_api::make_object<td::td_api::formattedText>();
    message_content->text_->text_ = p_Text;
  }

  return message_content;
}

std::string TgChat::Impl::ConvertMarkdownV2ToV1(const std::string& p_Str)
{
  auto ReplaceV2Markup = [](const std::string& p_Text) -> std::string
  {
    std::string text = p_Text;
    StrUtil::ReplaceString(text, "**", "*");
    StrUtil::ReplaceString(text, "__", "_");
    StrUtil::ReplaceString(text, "~~", "~");
    return text;
  };

  std::string rv;
  std::string str = p_Str;
  std::regex rg("(http|https):\\/\\/([^\\s]+)");
  std::smatch sm;
  while (regex_search(str, sm, rg))
  {
    rv += ReplaceV2Markup(sm.prefix().str()); // text non-match
    rv += sm.str(); // url match
    str = sm.suffix().str();
  }

  rv += ReplaceV2Markup(str); // text non-match
  return rv;
}
