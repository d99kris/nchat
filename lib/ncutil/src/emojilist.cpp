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

  // get expected content
  const std::set<std::string>& emojiView = EmojiUtil::GetView();
  const std::map<std::string, std::string>& emojiMap = EmojiUtil::GetMap();
  std::map<std::string, std::string> expected;
  for (const auto& emoji : emojiMap)
  {
    if (emojiListAll || emojiView.count(emoji.first))
    {
      expected.insert(emoji);
    }
  }

  // get current content
  std::map<std::string, std::pair<std::string, int>> rows; // name -> emoji, usages
  // *INDENT-OFF*
  *m_Db << "SELECT name, emoji, usages FROM emojis;" >>
    [&](const std::string& name, const std::string& emoji, int usages)
    {
      rows[name] = std::make_pair(emoji, usages);
    };
  // *INDENT-ON*

  // update table if needed
  bool inSync = (rows.size() == expected.size());
  for (auto it = expected.begin(); inSync && (it != expected.end()); ++it)
  {
    auto rit = rows.find(it->first);
    inSync = (rit != rows.end()) && (rit->second.first == it->second);
  }

  if (inSync) return;

  LOG_INFO("update emoji db %d to %d", static_cast<int>(rows.size()), static_cast<int>(expected.size()));

  // remove obsolete names, keeping their usages for a remaining name of same emoji
  std::map<std::string, int> removedUsages; // emoji without vs16 -> usages
  *m_Db << "BEGIN;";
  for (const auto& row : rows)
  {
    if (expected.count(row.first)) continue;

    LOG_TRACE("remove emoji %s", row.first.c_str());
    *m_Db << "DELETE FROM emojis WHERE name = ?;" << row.first;
    removedUsages[EmojiUtil::StripVs16(row.second.first)] += row.second.second;
  }

  // add new names and update emoji of existing names
  for (const auto& emoji : expected)
  {
    int usages = 0;
    auto uit = removedUsages.find(EmojiUtil::StripVs16(emoji.second));
    if (uit != removedUsages.end())
    {
      usages = uit->second;
      removedUsages.erase(uit);
    }

    LOG_TRACE("add emoji %s", emoji.first.c_str());
    *m_Db << "INSERT INTO emojis (name, emoji, usages) VALUES (?,?,?) "
      "ON CONFLICT(name) DO UPDATE SET emoji = excluded.emoji, usages = usages + excluded.usages;" <<
      emoji.first << emoji.second << usages;
  }
  *m_Db << "COMMIT;";
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
