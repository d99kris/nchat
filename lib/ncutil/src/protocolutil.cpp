// protocolutil.cpp
//
// Copyright (c) 2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "protocolutil.h"

#include "log.h"
#include "protocol.h"
#include "strutil.h"

FileInfo ProtocolUtil::FileInfoFromHex(const std::string& p_Str)
{
  FileInfo fileInfo;
  std::istringstream ss(p_Str);
  std::string tmp;

  if (!std::getline(ss, tmp, ','))
  {
    LOG_WARNING("deserialization error %s", p_Str.c_str());
    return FileInfo();
  }

  fileInfo.fileStatus = (FileStatus)StrUtil::NumFromHex<int>(tmp);

  if (!std::getline(ss, tmp, ','))
  {
    LOG_WARNING("deserialization error %s", p_Str.c_str());
    return FileInfo();
  }

  fileInfo.fileId = StrUtil::StrFromHex(tmp);

  if (!std::getline(ss, tmp, ','))
  {
    LOG_WARNING("deserialization error %s", p_Str.c_str());
    return FileInfo();
  }

  fileInfo.filePath = StrUtil::StrFromHex(tmp);

  if (!std::getline(ss, tmp))
  {
    LOG_WARNING("deserialization error %s", p_Str.c_str());
    return FileInfo();
  }

  fileInfo.fileType = StrUtil::StrFromHex(tmp);

  return fileInfo;
}

std::string ProtocolUtil::FileInfoToHex(const FileInfo& p_FileInfo)
{
  const std::string hexStr =
    StrUtil::NumToHex<int>(p_FileInfo.fileStatus) + "," +
    StrUtil::StrToHex(p_FileInfo.fileId) + "," +
    StrUtil::StrToHex(p_FileInfo.filePath) + "," +
    StrUtil::StrToHex(p_FileInfo.fileType) + "\n";
  return hexStr;
}
