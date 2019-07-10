// log.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <mutex>
#include <string>

class Log
{
public:
  static void SetPath(const std::string& p_Path);
  static void SetDebugEnabled(bool p_DebugEnabled);

  static void Debug(const char* p_Format, ...);
  static void Info(const char* p_Format, ...);
  static void Warning(const char* p_Format, ...);
  static void Error(const char* p_Format, ...);

  static void Dump(const char *p_Str);

private:
  static void Write(const char* p_Level, const char* p_Format, va_list p_VaList);
  
private:
  static std::string m_Path;
  static bool m_DebugEnabled;
  static std::mutex m_Mutex;
};
