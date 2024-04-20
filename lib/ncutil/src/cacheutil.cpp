// messagecache.cpp
//
// Copyright (c) 2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "cacheutil.h"

#include <string>
#include <sstream>

#include "log.h"

// #define DEBUG_UPDATE_REACTIONS

// Determines whether a Reactions instance needs serialization
bool CacheUtil::IsDefaultReactions(const Reactions& p_Reactions)
{
  return p_Reactions.senderEmojis.empty() && p_Reactions.emojiCounts.empty() &&
         !p_Reactions.updateCountBasedOnSender && !p_Reactions.needConsolidationWithCache && !p_Reactions.replaceCount;
}

// Debug helper
std::string CacheUtil::ReactionsToString(const Reactions& p_Reactions)
{
  std::stringstream sstream;
  sstream << "needConsolidation=" << p_Reactions.needConsolidationWithCache << " ";
  sstream << "updateCount=" << p_Reactions.updateCountBasedOnSender << " ";
  sstream << "replaceCount=" << p_Reactions.replaceCount << " ";
  sstream << "senderEmojis=[ ";
  for (const auto& senderEmoji : p_Reactions.senderEmojis)
  {
    sstream << "(" << senderEmoji.first << ": " << senderEmoji.second << ") ";
  }
  sstream << "] ";

  sstream << "emojiCounts=[ ";
  for (const auto& emojiCount : p_Reactions.emojiCounts)
  {
    sstream << "(" << emojiCount.first << ": " << emojiCount.second << ") ";
  }
  sstream << "] ";

  return sstream.str();
}

// This function takes an original Reactions instance, p_Source, and adds/removes senderEmojis
// based on an "update" Reactions instance, p_Target. Then counting of emoji types is done and
// result is stored in emojiCounts.
void CacheUtil::UpdateReactions(const Reactions& p_Source, Reactions& p_Target)
{
#ifdef DEBUG_UPDATE_REACTIONS
  LOG_INFO("update reactions");
  LOG_INFO("source: %s", ReactionsToString(p_Source).c_str());
  LOG_INFO("target: %s", ReactionsToString(p_Target).c_str());
#endif

  // Update senderEmojis
  std::map<std::string, std::string> combinedSenderEmojis = p_Source.senderEmojis;
  for (const auto& senderEmoji : p_Target.senderEmojis)
  {
    if (senderEmoji.second.empty())
    {
      combinedSenderEmojis.erase(senderEmoji.first);
    }
    else
    {
      combinedSenderEmojis[senderEmoji.first] = senderEmoji.second;
    }
  }

  p_Target.senderEmojis = combinedSenderEmojis;

  // Handle replace count
  if (p_Target.replaceCount)
  {
    // Do nothing, using provided emojiCounts
  }
  else
  {
    p_Target.emojiCounts = p_Source.emojiCounts;
  }

  // Update emojiCounts based on senderEmojis
  if (p_Target.updateCountBasedOnSender)
  {
    p_Target.emojiCounts.clear();
    for (const auto& senderEmoji : p_Target.senderEmojis)
    {
      p_Target.emojiCounts[senderEmoji.second] += 1;
    }
  }

  p_Target.needConsolidationWithCache = false;
  p_Target.updateCountBasedOnSender = false;
  p_Target.replaceCount = false;

#ifdef DEBUG_UPDATE_REACTIONS
  LOG_INFO("result: %s", ReactionsToString(p_Target).c_str());
#endif
}
