// emojiutil.h
//
// Copyright (c) 2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <map>
#include <set>
#include <string>

class EmojiUtil
{
public:
  static std::string Emojize(const std::string& p_Str, bool p_Pad);
  static std::string Textize(const std::string& p_In);
  static const std::map<std::string, std::string>& GetMap();
  static const std::set<std::string>& GetView();
};
