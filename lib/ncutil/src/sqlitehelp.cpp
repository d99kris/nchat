// sqlitehelp.cpp
//
// Copyright (c) 2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "sqlitehelp.h"

#include "log.h"

void SqliteHelp::HandleSqliteException(const char* p_Filename, int p_LineNo,
                                       const sqlite::sqlite_exception& p_Ex)
{
  const int code = p_Ex.get_code();
  const char* what = p_Ex.what();
  const std::string& sql = p_Ex.get_sql();
  Log::Error(p_Filename, p_LineNo, "sqlite exception %d: \"%s\" in \"%s\"",
             code, what, sql.c_str());
  throw;
}
