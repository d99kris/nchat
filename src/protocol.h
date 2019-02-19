// protocol.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <limits>
#include <string>

class Config;
class Contact;
class Ui;
struct Message;

class Protocol
{
public:
  Protocol()
  {
  }
  
  virtual ~Protocol()
  {
  }

  virtual std::string GetName() = 0;

  virtual void RequestChats(std::int32_t p_Limit, std::int64_t p_OffsetChat = 0,
                            std::int64_t p_OffsetOrder = (std::numeric_limits<std::int64_t>::max() - 1)) = 0;
  virtual void RequestChatUpdate(std::int64_t p_ChatId) = 0;
  virtual void RequestMessages(std::int64_t p_ChatId, std::int64_t p_FromMsg, std::int32_t p_Limit) = 0;
  virtual void SendMessage(std::int64_t p_ChatId, const std::string& p_Message) = 0;
  virtual void MarkRead(std::int64_t p_ChatId, const std::vector<std::int64_t>& p_MsgIds) = 0;

  virtual bool Setup() = 0;
  virtual void Start() = 0;
  virtual void Stop() = 0;

private:
};
