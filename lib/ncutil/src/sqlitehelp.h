// sqlitehelp.h
//
// Copyright (c) 2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include <sqlite_modern_cpp.h>

#define HANDLE_SQLITE_EXCEPTION(EX) do { SqliteHelp::HandleSqliteException(__FILENAME__, \
                                                                           __LINE__, \
                                                                           EX); } while (0)

class SqliteHelp
{
public:
  static void HandleSqliteException(const char* p_Filename, int p_LineNo,
                                    const sqlite::sqlite_exception& p_Ex);
};
