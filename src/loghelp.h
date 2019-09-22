// loghelp.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstring>
#include <sstream>

#include "log.h"

#define __FILENAME__ (strrchr("/" __FILE__, '/') + 1)

#define LOG_DEBUG(EXPR, ...) Log::Debug(EXPR "  (%s:%d)", ##__VA_ARGS__, __FILENAME__, __LINE__)
#define LOG_INFO(EXPR, ...) Log::Info(EXPR "  (%s:%d)", ##__VA_ARGS__, __FILENAME__, __LINE__)
#define LOG_WARNING(EXPR, ...) Log::Warning(EXPR "  (%s:%d)", ##__VA_ARGS__, __FILENAME__, __LINE__)
#define LOG_ERROR(EXPR, ...) Log::Error(EXPR "  (%s:%d)", ##__VA_ARGS__, __FILENAME__, __LINE__)
#define LOG_DUMP(STR) Log::Dump(STR)
