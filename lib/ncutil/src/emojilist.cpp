// emojilist.cpp
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "emojilist.h"

#include <map>
#include <utility>

#include <sqlite_modern_cpp.h>

#include "appconfig.h"
#include "log.h"
#include "emojiutil.h"
#include "fileutil.h"

std::mutex EmojiList::m_Mutex;
std::unique_ptr<sqlite::database> EmojiList::m_Db;

void EmojiList::Init()
{
  std::unique_lock<std::mutex> lock(m_Mutex);

  static const int dirVersion = 2;
  const std::string& emojisDir = FileUtil::GetApplicationDir() + "/emojis";
  FileUtil::InitDirVersion(emojisDir, dirVersion);

  const bool emojiListAll = AppConfig::GetBool("emoji_list_all");

  const std::string dbName = (emojiListAll ? "dball.sqlite" : "db.sqlite");
  const std::string dbPath = emojisDir + "/" + dbName;
  m_Db.reset(new sqlite::database(dbPath));

  if (!m_Db) return;

  *m_Db << "PRAGMA synchronous = FULL";
  *m_Db << "PRAGMA journal_mode = DELETE";

  // create table if not exists
  *m_Db << "CREATE TABLE IF NOT EXISTS emojis (name TEXT PRIMARY KEY NOT NULL, emoji TEXT, usages INT);";

  // get expected count
  const std::set<std::string>& emojiView = EmojiUtil::GetView();
  const std::map<std::string, std::string>& emojiMap = EmojiUtil::GetMap();
  const int emojiViewCount = emojiListAll ? emojiMap.size() : emojiView.size();

  // populate table if empty
  int rowCount = 0;
  *m_Db << "SELECT COUNT(emoji) FROM emojis;" >> rowCount;

  // update map if needed
  if (rowCount != emojiViewCount)
  {
    LOG_INFO("update emoji db %d to %d", rowCount, emojiViewCount);

    *m_Db << "BEGIN;";
    for (const auto& emoji : emojiMap)
    {
      if (emojiListAll || emojiView.count(emoji.first))
      {
        LOG_TRACE("add emoji %s", emoji.first.c_str());
        *m_Db << "INSERT INTO emojis (name, emoji, usages) "
          "VALUES (?,?,0) ON CONFLICT DO NOTHING;" << emoji.first << emoji.second;
      }
    }
    *m_Db << "COMMIT;";

    // *INDENT-OFF*
    std::vector<std::string> names;
    *m_Db << "SELECT name FROM emojis;" >>
      [&](const std::string& name)
      {
        names.push_back(name);
      };
    // *INDENT-ON*

    *m_Db << "BEGIN;";
    for (const auto& name : names)
    {
      if ((emojiListAll && (emojiMap.count(name) == 0)) ||
          (!emojiListAll && (emojiView.count(name) == 0)))
      {
        LOG_TRACE("remove emoji %s", name.c_str());
        *m_Db << "DELETE FROM emojis WHERE name = ?;" << name;
      }
    }
    *m_Db << "COMMIT;";
  }
}

void EmojiList::Cleanup()
{
  std::unique_lock<std::mutex> lock(m_Mutex);

  if (!m_Db) return;

  m_Db.reset();
}

std::vector<std::pair<std::string, std::string>> EmojiList::Get(const std::string& p_Filter)
{
  std::unique_lock<std::mutex> lock(m_Mutex);

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
  std::unique_lock<std::mutex> lock(m_Mutex);

  if (!m_Db) return;

  *m_Db << "UPDATE emojis SET usages = usages + 1 WHERE name = ?;" << p_Name;
}
