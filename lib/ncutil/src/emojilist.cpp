// emojilist.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "emojilist.h"

#include <map>
#include <utility>

#include <emoji.h>
#include <sqlite_modern_cpp.h>

#include "log.h"
#include "fileutil.h"

std::mutex EmojiList::m_Mutex;
std::unique_ptr<sqlite::database> EmojiList::m_Db;

void EmojiList::Init()
{
  std::lock_guard<std::mutex> lock(m_Mutex);

  static const int dirVersion = 1;
  const std::string& emojisDir = FileUtil::GetApplicationDir() + "/emojis";
  FileUtil::InitDirVersion(emojisDir, dirVersion);

  const std::string& dbPath = emojisDir + "/db.sqlite";
  m_Db.reset(new sqlite::database(dbPath));

  if (!m_Db) return;

  // create table if not exists
  *m_Db << "CREATE TABLE IF NOT EXISTS emojis (name TEXT PRIMARY KEY NOT NULL, emoji TEXT, usages INT);";

  // populate table if empty
  int rowCount = 0;
  *m_Db << "SELECT COUNT(emoji) FROM emojis;" >> rowCount;
  if (rowCount == 0)
  {
    LOG_DEBUG("populate emoji db");
    *m_Db << "BEGIN;";
    const std::map<std::string, std::string>& emojis = emojicpp::EMOJIS;
    for (const auto& emoji : emojis)
    {
      *m_Db << "INSERT INTO emojis (name, emoji, usages) VALUES (?,?,0);" << emoji.first << emoji.second;
    }
    *m_Db << "COMMIT;";
  }
}

void EmojiList::Cleanup()
{
  std::lock_guard<std::mutex> lock(m_Mutex);

  if (!m_Db) return;

  m_Db.reset();
}

std::vector<std::pair<std::string, std::string>> EmojiList::Get(const std::string& p_Filter)
{
  std::lock_guard<std::mutex> lock(m_Mutex);

  if (!m_Db) return std::vector<std::pair<std::string, std::string>>();

  std::vector<std::pair<std::string, std::string>> emojis;
  if (p_Filter.empty())
  {
    *m_Db << "SELECT name, emoji FROM emojis ORDER BY usages DESC, name ASC;" >>
      [&](const std::string& name, const std::string& emoji) { emojis.push_back(std::make_pair(name, emoji)); };
  }
  else
  {
    *m_Db << "SELECT name, emoji FROM emojis WHERE name LIKE ? ORDER BY usages DESC, name ASC;" <<
      ("%" + p_Filter + "%") >>
      [&](const std::string& name, const std::string& emoji) { emojis.push_back(std::make_pair(name, emoji)); };
  }

  return emojis;
}

void EmojiList::AddUsage(const std::string& p_Name)
{
  std::lock_guard<std::mutex> lock(m_Mutex);

  if (!m_Db) return;

  *m_Db << "UPDATE emojis SET usages = usages + 1 WHERE name = ?;" << p_Name;
}
