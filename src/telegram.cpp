// telegram.cpp
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.
//
// The tdlib interfacing in this file was originally based on the tdlib example
// ext/td/example/cpp/td_example.cpp

#include "telegram.h"

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

#include <path.hpp>

#include "chat.h"
#include "message.h"
#include "ui.h"
#include "util.h"

namespace detail
{
  template <class... Fs>
  struct overload;

  template <class F>
  struct overload<F> : public F
  {
    explicit overload(F f) : F(f)
    {
    }
  };

  template <class F, class... Fs>
  struct overload<F, Fs...>
    : public overload<F>
    , overload<Fs...>
  {
    overload(F f, Fs... fs) : overload<F>(f), overload<Fs...>(fs...)
    {
    }

    using overload<F>::operator();
    using overload<Fs...>::operator();
  };
}

template <class... F>
auto overloaded(F... f)
{
  return detail::overload<F...>(f...);
}

Telegram::Telegram(std::shared_ptr<Ui> p_Ui, bool p_IsSetup, bool p_IsVerbose)
  : m_Ui(p_Ui)
  , m_IsSetup(p_IsSetup)
  , m_IsVerbose(p_IsVerbose)
{
  // Init config
  const std::map<std::string, std::string> defaultConfig =
  {
    {"local_key", ""},
  };
  const std::string configPath(Util::GetConfigDir() + std::string("/telegram.conf"));
  m_Config = Config(configPath, defaultConfig);

  Init();
}

Telegram::~Telegram()
{
  m_Config.Save();
  Cleanup();
}

std::string Telegram::GetName()
{
  return std::string("telegram");
}

void Telegram::RequestChats(std::int32_t p_Limit, std::int64_t p_OffsetChat,
                            std::int64_t p_OffsetOrder)
{
  SendQuery(td::td_api::make_object<td::td_api::getChats>(p_OffsetOrder, p_OffsetChat, p_Limit),
            [this](Object object)
            {
              if (object->get_id() == td::td_api::error::ID) return;

              auto tchats = td::move_tl_object_as<td::td_api::chats>(object);
              if (tchats->chat_ids_.size() == 0) return;

              std::vector<Chat> chats;
              for (auto chatId : tchats->chat_ids_)
              {
                Chat chat;
                chat.m_Id = chatId;
                chat.m_Name = m_ChatTitle[chatId];
                chat.m_Protocol = this;
                chats.push_back(chat);
              }
               
              if (m_Ui.get() != nullptr)
              {
                m_Ui->UpdateChats(chats);
              }
            });
}

void Telegram::RequestChatUpdate(std::int64_t p_ChatId)
{
  auto get_chat = td::td_api::make_object<td::td_api::getChat>();
  get_chat->chat_id_ = p_ChatId;
  SendQuery(std::move(get_chat),
            [this](Object object)
            {
              if (object->get_id() == td::td_api::error::ID) return;
              
              auto tchat = td::move_tl_object_as<td::td_api::chat>(object);

              Chat chat;
              chat.m_Id = tchat->id_;
              chat.m_Name = tchat->title_;
              chat.m_Protocol = this;
              chat.m_IsUnread = (tchat->unread_count_ > 0);
               
              if (m_Ui.get() != nullptr)
              {
                m_Ui->UpdateChat(chat);
              }               
            });  
}

void Telegram::RequestMessages(std::int64_t p_ChatId, std::int64_t p_FromMsg, std::int32_t p_Limit)
{
  SendQuery(td::td_api::make_object<td::td_api::getChatHistory>(p_ChatId, p_FromMsg, 0, p_Limit, false),
            [this, p_ChatId, p_FromMsg, p_Limit](Object object)
            {
              if (object->get_id() == td::td_api::error::ID) return;

              auto msgs = td::move_tl_object_as<td::td_api::messages>(object);
              if (msgs->messages_.size() == 0) return;
               
              std::vector<Message> messages;
              for (auto it = msgs->messages_.begin(); it != msgs->messages_.end(); ++it)
              {
                auto msg = td::move_tl_object_as<td::td_api::message>(*it);
                Message message;
                TdMessageConvert(*msg, message);
                messages.push_back(message);
              }

              if (m_Ui.get() != nullptr)
              {
                m_Ui->UpdateMessages(messages, (p_FromMsg == 0));
              }

              // Recursively request additional until limit reached
              std::int32_t limit = p_Limit - msgs->messages_.size();
              if (limit > 0)
              {
                std::int64_t fromMsg = messages.back().m_Id;
                RequestMessages(p_ChatId, fromMsg, limit);
              }
            });
}

void Telegram::SendMessage(std::int64_t p_ChatId, const std::string& p_Message)
{
  auto send_message = td::td_api::make_object<td::td_api::sendMessage>();
  send_message->chat_id_ = p_ChatId;
  auto message_content = td::td_api::make_object<td::td_api::inputMessageText>();
  message_content->text_ = td::td_api::make_object<td::td_api::formattedText>();
  message_content->text_->text_ = p_Message;
  send_message->input_message_content_ = std::move(message_content);
  SendQuery(std::move(send_message), {});
}

void Telegram::MarkRead(std::int64_t p_ChatId, const std::vector<std::int64_t>& p_MsgIds)
{
  auto view_messages = td::td_api::make_object<td::td_api::viewMessages>();
  view_messages->chat_id_ = p_ChatId;
  view_messages->message_ids_ = p_MsgIds;
  view_messages->force_read_ = true;
  SendQuery(std::move(view_messages), {});
}

bool Telegram::Setup()
{
  const std::string dbPath(Util::GetConfigDir() + std::string("/tdlib"));
  apathy::Path::rmdirs(apathy::Path(dbPath));
  
  Process();

  return m_WasAuthorized;
}

void Telegram::Start()
{
  m_Thread = new std::thread(&Telegram::Process, this);
}

void Telegram::Stop()
{
  m_Running = false;
  if (m_Thread != NULL)
  {
    m_Thread->join();
  }
}

void Telegram::Init()
{
  td::Log::set_verbosity_level(m_IsVerbose ? 5 : 1);
  const std::string logPath(Util::GetConfigDir() + std::string("/td.log"));
  td::Log::set_file_path(logPath);
  td::Log::set_max_file_size(1024 * 1024);
  m_Client = std::make_unique<td::Client>();
}

void Telegram::Cleanup()
{
  td::td_api::close();
}
 
void Telegram::Process()
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

void Telegram::TdMessageConvert(const td::td_api::message& p_TdMessage, Message& p_Message)
{
  auto sender_user_name = GetUserName(p_TdMessage.sender_user_id_);
  std::string text;
  if (p_TdMessage.content_->get_id() == td::td_api::messageText::ID)
  {
    text = static_cast<td::td_api::messageText &>(*p_TdMessage.content_).text_->text_;
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
    text = "[Document]";
  }
  else if (p_TdMessage.content_->get_id() == td::td_api::messagePhoto::ID)
  {
    text = "[Photo]";
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

  p_Message.m_Id = p_TdMessage.id_;
  p_Message.m_Sender = sender_user_name;
  p_Message.m_ChatId = p_TdMessage.chat_id_;
  p_Message.m_IsOutgoing = p_TdMessage.is_outgoing_;
  p_Message.m_IsUnread = p_TdMessage.contains_unread_mention_;
  p_Message.m_TimeSent = p_TdMessage.date_;
  p_Message.m_ReplyToId = p_TdMessage.reply_to_message_id_;
  p_Message.m_Content = text;
  p_Message.m_Protocol = this;
}

void Telegram::SendQuery(td::td_api::object_ptr<td::td_api::Function> f,
                         std::function<void(Object)> handler)
{
  auto query_id = GetNextQueryId();
  if (handler)
  {
    m_Handlers.emplace(query_id, std::move(handler));
  }
  m_Client->send({query_id, std::move(f)});
}

void Telegram::ProcessResponse(td::Client::Response response)
{
  if (!response.object) return;

  if (response.id == 0) return ProcessUpdate(std::move(response.object));

  auto it = m_Handlers.find(response.id);
  if (it != m_Handlers.end())
  {
    it->second(std::move(response.object));
  }
}

std::string Telegram::GetUserName(std::int32_t user_id)
{
  auto it = m_Users.find(user_id);
  if (it == m_Users.end())
  {
    return std::string("(") + std::to_string(user_id) + std::string(")");
  }
  return it->second->first_name_ + " " + it->second->last_name_;
}

void Telegram::ProcessUpdate(td::td_api::object_ptr<td::td_api::Object> update)
{
  td::td_api::downcast_call(*update, overloaded(
    [this](td::td_api::updateAuthorizationState &update_authorization_state)
    {
      m_AuthorizationState = std::move(update_authorization_state.authorization_state_);
      OnAuthStateUpdate();
    },
    [this](td::td_api::updateNewChat &update_new_chat)
    {
      m_ChatTitle[update_new_chat.chat_->id_] = update_new_chat.chat_->title_;
    },
    [this](td::td_api::updateChatTitle &update_chat_title)
    {
      m_ChatTitle[update_chat_title.chat_id_] = update_chat_title.title_;
    },
    [this](td::td_api::updateUser &update_user)
    {
      auto user_id = update_user.user_->id_;
      m_Users[user_id] = std::move(update_user.user_);
    },
    [this](td::td_api::updateNewMessage &update_new_message)
    {
      std::vector<Message> messages;
      auto msg = td::move_tl_object_as<td::td_api::message>(update_new_message.message_);
      Message message;
      TdMessageConvert(*msg, message);
      messages.push_back(message);
      m_Ui->UpdateMessages(messages);
    },
    [](auto &anyupdate) { (void)anyupdate; }
    ));
}

auto Telegram::CreateAuthQueryHandler()
{
  return [this, id = m_AuthQueryId](Object object)
  {
    if (id == m_AuthQueryId)
    {
      CheckAuthError(std::move(object));
    }
  };
}

void Telegram::OnAuthStateUpdate()
{
  m_AuthQueryId++;
  td::td_api::downcast_call(*m_AuthorizationState, overloaded(
    [this](td::td_api::authorizationStateReady &)
    {      
      m_Authorized = true;
      m_WasAuthorized = true;
      std::cerr << "Got authorization" << std::endl;
      if (m_IsSetup)
      {
        m_Running = false;
      }
      else
      {
        RequestChats(100);
      }      
    },
    [this](td::td_api::authorizationStateLoggingOut &)
    {
      m_Authorized = false;
      std::cerr << "Logging out" << std::endl;
    },
    [](td::td_api::authorizationStateClosing &)
    {
      std::cerr << "Closing" << std::endl;
    },
    [this](td::td_api::authorizationStateClosed &)
    {
      m_Authorized = false;
      m_Running = false;
      std::cerr << "Terminated" << std::endl;
    },
    [this](td::td_api::authorizationStateWaitCode &wait_code)
    {
      if (m_IsSetup)
      {
        std::string first_name;
        std::string last_name;
        if (!wait_code.is_registered_)
        {
          std::cout << "Enter your first name: ";
          std::cin >> first_name;
          std::cout << "Enter your last name: ";
          std::cin >> last_name;
        }
        std::cout << "Enter authentication code: ";
        std::string code;
        std::cin >> code;
        SendQuery(td::td_api::make_object<td::td_api::checkAuthenticationCode>(code, first_name,
                                                                               last_name),
                  CreateAuthQueryHandler());
      }
      else
      {
        std::cerr << "Error: L" << __LINE__ << std::endl;
        m_Running = false;
      }
    },
    [this](td::td_api::authorizationStateWaitPassword &)
    {
      if (m_IsSetup)
      {
        std::cout << "Enter authentication password: ";
        std::string password;
        std::cin >> password;
        SendQuery(td::td_api::make_object<td::td_api::checkAuthenticationPassword>(password),
                  CreateAuthQueryHandler());
      }
      else
      {
        std::cerr << "Error: L" << __LINE__ << std::endl;
        m_Running = false;
      }
    },
    [this](td::td_api::authorizationStateWaitPhoneNumber &)
    {
      if (m_IsSetup)
      {
        std::cout << "Enter phone number: ";
        std::string phone_number;
        std::cin >> phone_number;
        SendQuery(td::td_api::make_object<td::td_api::setAuthenticationPhoneNumber>(phone_number,
                                                                                    false,
                                                                                    false),
                  CreateAuthQueryHandler());
      }
      else
      {
        std::cerr << "Error: L" << __LINE__ << std::endl;
        m_Running = false;
      }
    },
    [this](td::td_api::authorizationStateWaitEncryptionKey &)
    {
      std::string key;
      if (m_IsSetup)
      {
        // Generate fresh local encryption key during setup
        key = GetRandomString(16);
        m_Config.Set("local_key", key);
      }
      else
      {
        // Use saved local encryption key
        key = m_Config.Get("local_key");
      }

      if (key.empty())
      {
        m_Running = false;
      }
      else
      {
        std::cerr << "Using key: " << key << std::endl;
        SendQuery(td::td_api::make_object<td::td_api::checkDatabaseEncryptionKey>(std::move(key)),
                  CreateAuthQueryHandler());
      }
    },
    [this](td::td_api::authorizationStateWaitTdlibParameters &)
    {
      const std::string dbPath(Util::GetConfigDir() + std::string("/tdlib"));
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
      parameters->application_version_ = "1.0";
      parameters->enable_storage_optimizer_ = true;
      SendQuery(td::td_api::make_object<td::td_api::setTdlibParameters>(std::move(parameters)),
                CreateAuthQueryHandler());
    }
  ));
}

void Telegram::CheckAuthError(Object object)
{
  if (object->get_id() == td::td_api::error::ID)
  {
    auto error = td::move_tl_object_as<td::td_api::error>(object);
    std::cerr << "Error: " << to_string(error);
    OnAuthStateUpdate();
  }
}

std::uint64_t Telegram::GetNextQueryId()
{
  return ++m_CurrentQueryId;
}

std::string Telegram::GetRandomString(size_t p_Len)
{
  srand(time(0));
  std::string str = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  std::string newstr;
  int pos;
  while(newstr.size() != p_Len)
  {
    pos = ((rand() % (str.size() - 1)));
    newstr += str.substr(pos,1);
  }
  return newstr;
}



