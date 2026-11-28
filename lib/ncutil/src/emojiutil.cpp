// emojiutil.cpp
//
// Copyright (c) 2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "emojiutil.h"

#include <cstring>

#include "emojiutil_map.h"
#include "emojiutil_view.h"
#include "strutil.h"

std::string EmojiUtil::Emojize(const std::string& p_Str, bool p_Pad)
{
  std::string str = p_Str;
  std::size_t position = 0;
  std::size_t firstColon = std::string::npos;
  while (firstColon = str.find(":", position), firstColon != std::string::npos)
  {
    std::size_t secondColon = str.find(":", firstColon + 1);
    if (secondColon != std::string::npos)
    {
      std::string colonStr = str.substr(firstColon, secondColon - firstColon + 1);
      std::map<std::string, std::string>::iterator it = s_Map.find(colonStr);
      if (it != s_Map.end())
      {
        std::string emoji = it->second + (p_Pad ? std::string(1, EMOJI_PAD) : "");
        str.replace(firstColon, secondColon - firstColon + 1, emoji);
      }
    }

    position = firstColon + 1;
  }

  return str;
}

std::string EmojiUtil::Textize(const std::string& p_In)
{
  static const std::map<std::string, std::string> emojiToText = []()
  {
    // map both fully-qualified and bare (no VS16) forms, as protocols send
    // either, and prefer the name listed in the emoji picker over aliases
    std::map<std::string, std::string> emToText;
    for (const bool viewPass : { false, true })
    {
      for (auto& emojiPair : s_Map)
      {
        if (viewPass && !s_View.count(emojiPair.first)) continue;

        emToText[emojiPair.second] = emojiPair.first;
        emToText[StripVs16(emojiPair.second)] = emojiPair.first;
      }
    }

    return emToText;
  }();

  const bool enableDoubleMultiByteLookup = true;
  std::string out;
  const char* cstr = p_In.c_str();
  size_t charlen = 0;
  mbstate_t mbs;
  std::string mbprev;

  memset(&mbs, 0, sizeof(mbs));
  while ((charlen = mbrlen(cstr, MB_CUR_MAX, &mbs)) != 0 &&
         charlen != (size_t)-1 && charlen != (size_t)-2)
  {
    std::string mbcur = std::string(cstr, charlen);

    if (enableDoubleMultiByteLookup)
    {
      if (!mbprev.empty())
      {
        std::string mbcomb = mbprev + mbcur;
        if (emojiToText.find(mbcomb) != emojiToText.end())
        {
          out += emojiToText.at(mbcomb);
          mbcur.clear();
          mbprev.clear();
        }
        else
        {
          if (emojiToText.find(mbprev) != emojiToText.end())
          {
            out += emojiToText.at(mbprev);
          }
          else
          {
            out += mbprev;
          }

          mbprev.clear();
        }
      }

      if (!mbcur.empty())
      {
        mbprev = mbcur;
      }
    }
    else
    {
      if (emojiToText.find(mbcur) != emojiToText.end())
      {
        out += emojiToText.at(mbcur);
      }
      else
      {
        out += mbcur;
      }
    }

    cstr += charlen;
  }

  if (enableDoubleMultiByteLookup && !mbprev.empty())
  {
    if (emojiToText.find(mbprev) != emojiToText.end())
    {
      out += emojiToText.at(mbprev);
    }
    else
    {
      out += mbprev;
    }
  }

  return out;
}

const std::map<std::string, std::string>& EmojiUtil::GetMap()
{
  return s_Map;
}

const std::set<std::string>& EmojiUtil::GetView()
{
  return s_View;
}

std::string EmojiUtil::StripVs16(const std::string& p_Str)
{
  static const std::string vs16 = "\xef\xb8\x8f"; // U+FE0F
  std::string str = p_Str;
  std::size_t pos = 0;
  while ((pos = str.find(vs16, pos)) != std::string::npos)
  {
    str.erase(pos, vs16.size());
  }

  return str;
}
