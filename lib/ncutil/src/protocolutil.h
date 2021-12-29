// protocolutil.h
//
// Copyright (c) 2021 Kristofer Berggren
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
};
