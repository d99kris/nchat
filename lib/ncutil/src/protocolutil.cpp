// protocolutil.cpp
//
// Copyright (c) 2021-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "protocolutil.h"

#include "log.h"
#include "protocol.h"
#include "strutil.h"

static std::string JsonEscape(const std::string& p_Str)
{
  std::string result;
  result.reserve(p_Str.size());
  for (char c : p_Str)
  {
    switch (c)
    {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += c; break;
    }
  }
  return result;
}

static std::string JsonUnescape(const std::string& p_Str)
{
  std::string result;
  result.reserve(p_Str.size());
  for (size_t i = 0; i < p_Str.size(); ++i)
  {
    if (p_Str[i] == '\\' && (i + 1) < p_Str.size())
    {
      switch (p_Str[i + 1])
      {
        case '"': result += '"'; ++i; break;
        case '\\': result += '\\'; ++i; break;
        case 'n': result += '\n'; ++i; break;
        case 'r': result += '\r'; ++i; break;
        case 't': result += '\t'; ++i; break;
        default: result += p_Str[i]; break;
      }
    }
    else
    {
      result += p_Str[i];
    }
  }
  return result;
}

static std::string ExtractJsonString(const std::string& p_Json, const std::string& p_Key, size_t p_Start, size_t& p_End)
{
  std::string searchKey = "\"" + p_Key + "\"";
  size_t keyPos = p_Json.find(searchKey, p_Start);
  if (keyPos == std::string::npos) { p_End = std::string::npos; return ""; }

  size_t colonPos = p_Json.find(':', keyPos + searchKey.size());
  if (colonPos == std::string::npos) { p_End = std::string::npos; return ""; }

  size_t quoteStart = p_Json.find('"', colonPos + 1);
  if (quoteStart == std::string::npos) { p_End = std::string::npos; return ""; }

  size_t quoteEnd = quoteStart + 1;
  while (quoteEnd < p_Json.size())
  {
    if (p_Json[quoteEnd] == '\\') { quoteEnd += 2; continue; }
    if (p_Json[quoteEnd] == '"') break;
    ++quoteEnd;
  }

  p_End = quoteEnd + 1;
  return JsonUnescape(p_Json.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
}

std::vector<ContactInfo> ProtocolUtil::ContactInfosFromJson(const std::string& p_Json)
{
  std::vector<ContactInfo> contactInfos;
  size_t pos = 0;
  while (pos < p_Json.size())
  {
    size_t objStart = p_Json.find('{', pos);
    if (objStart == std::string::npos) break;

    size_t objEnd = p_Json.find('}', objStart);
    if (objEnd == std::string::npos) break;

    ContactInfo ci;
    size_t end = 0;
    ci.id = ExtractJsonString(p_Json, "id", objStart, end);
    ci.name = ExtractJsonString(p_Json, "name", objStart, end);
    contactInfos.push_back(ci);

    pos = objEnd + 1;
  }
  return contactInfos;
}

std::string ProtocolUtil::MentionsToJson(const std::map<std::string, std::string>& p_Mentions)
{
  std::string json = "{";
  bool first = true;
  for (const auto& pair : p_Mentions)
  {
    if (!first) json += ",";
    json += "\"" + JsonEscape(pair.first) + "\":\"" + JsonEscape(pair.second) + "\"";
    first = false;
  }
  json += "}";
  return json;
}

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
