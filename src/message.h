// message.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include "protocol.h"

struct Message
{
  std::int64_t m_Id;
  std::string m_Sender;
  std::int64_t m_ChatId;
  bool m_IsOutgoing;
  bool m_IsUnread;
  std::int32_t m_TimeSent;
  std::int64_t m_ReplyToId;
  std::string m_Content;
  Protocol* m_Protocol;

  std::string GetUniqueChatId()
  {
    return m_Protocol->GetName() + std::string("_") + std::to_string(m_ChatId);
  }
};
