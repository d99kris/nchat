// log.h
//
// Copyright (c) 2020-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstring>
#include <mutex>
#include <string>

#define __FILENAME__ (strrchr("/" __FILE__, '/') + 1)

#define LOG_TRACE(...) Log::Trace(__FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) Log::Debug(__FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) Log::Info(__FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) Log::Warning(__FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Log::Error(__FILENAME__, __LINE__, __VA_ARGS__)

class Log
{
public:
  static const int INFO_LEVEL = 0;
  static const int DEBUG_LEVEL = 1;
  static const int TRACE_LEVEL = 2;

  static void Init(const std::string& p_Path);
  static void SetVerboseLevel(int p_Level);
  static inline int GetVerboseLevel() { return m_VerboseLevel; }
  static inline bool GetDebugEnabled() { return m_VerboseLevel >= DEBUG_LEVEL; }
  static inline bool GetTraceEnabled() { return m_VerboseLevel >= TRACE_LEVEL; }

  static void Trace(const char* p_Filename, int p_LineNo, const char* p_Format, ...);
  static void Debug(const char* p_Filename, int p_LineNo, const char* p_Format, ...);
  static void Info(const char* p_Filename, int p_LineNo, const char* p_Format, ...);
  static void Warning(const char* p_Filename, int p_LineNo, const char* p_Format, ...);
  static void Error(const char* p_Filename, int p_LineNo, const char* p_Format, ...);

  static void Dump(const char* p_Str);

private:
  static void Write(const char* p_Filename, int p_LineNo, const char* p_Level, const char* p_Format, va_list p_VaList);

private:
  static std::string m_Path;
  static int m_VerboseLevel;
  static std::mutex m_Mutex;
};
