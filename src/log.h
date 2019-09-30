// log.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstring>
#include <mutex>
#include <string>

#define __FILENAME__ (strrchr("/" __FILE__, '/') + 1)

#define LOG_DEBUG(...) Log::Debug(__FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) Log::Info(__FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) Log::Warning(__FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Log::Error(__FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_DUMP(STR) Log::Dump(STR)

class Log
{
public:
  static void SetPath(const std::string& p_Path);
  static void SetDebugEnabled(bool p_DebugEnabled);

  static void Debug(const char* p_Filename, int p_LineNo, const char* p_Format, ...);
  static void Info(const char* p_Filename, int p_LineNo, const char* p_Format, ...);
  static void Warning(const char* p_Filename, int p_LineNo, const char* p_Format, ...);
  static void Error(const char* p_Filename, int p_LineNo, const char* p_Format, ...);

  static void Dump(const char *p_Str);

private:
  static void Write(const char* p_Filename, int p_LineNo, const char* p_Level, const char* p_Format, va_list p_VaList);
  
private:
  static std::string m_Path;
  static bool m_DebugEnabled;
  static std::mutex m_Mutex;
};
