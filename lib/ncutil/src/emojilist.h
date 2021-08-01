// emojilist.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sqlite
{
  class database;
}

class EmojiList
{
public:
  static void Init();
  static void Cleanup();

  static std::vector<std::pair<std::string, std::string>> Get(const std::string& p_Filter);
  static void AddUsage(const std::string& p_Name);

private:
  static std::mutex m_Mutex;
  static std::unique_ptr<sqlite::database> m_Db;
};
