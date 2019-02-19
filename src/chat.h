// chat.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include "protocol.h"

struct Chat
{
  std::int64_t m_Id;
  std::string m_Name;
  Protocol* m_Protocol = nullptr;
  bool m_IsUnread = false;

  std::string GetUniqueId()
  {
    return m_Protocol->GetName() + std::string("_") + std::to_string(m_Id);
  }
};
