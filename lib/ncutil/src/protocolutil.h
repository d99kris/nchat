// protocolutil.h
//
// Copyright (c) 2021-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include "protocol.h"

class ProtocolUtil
{
public:
  static FileInfo FileInfoFromHex(const std::string& p_Str);
  static std::string FileInfoToHex(const FileInfo& p_FileInfo);
  static std::vector<ContactInfo> ContactInfosFromJson(const std::string& p_Json);
  static std::string MentionsToJson(const std::map<std::string, std::string>& p_Mentions);
};
