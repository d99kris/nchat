//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/SqliteConnectionSafe.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/port/Stat.h"

namespace td {

SqliteConnectionSafe::SqliteConnectionSafe(string path, DbKey key)
    : path_(std::move(path)), lsls_connection_([path = path_, key = std::move(key)] {
      auto r_db = SqliteDb::open_with_key(path, key);
      if (r_db.is_error()) {
        auto r_stat = stat(path);
        if (r_stat.is_error()) {
          LOG(FATAL) << "Can't open database " << path << " (" << r_stat.error() << "): " << r_db.error();
        } else {
          LOG(FATAL) << "Can't open database " << path << " of size " << r_stat.ok().size_ << ": " << r_db.error();
        }
      }
      auto db = r_db.move_as_ok();
      db.exec("PRAGMA synchronous=NORMAL").ensure();
      db.exec("PRAGMA temp_store=MEMORY").ensure();
      db.exec("PRAGMA secure_delete=1").ensure();
      db.exec("PRAGMA recursive_triggers=1").ensure();
      return db;
    }) {
}

SqliteDb &SqliteConnectionSafe::get() {
  return lsls_connection_.get();
}

void SqliteConnectionSafe::close() {
  LOG(INFO) << "Close SQLite database " << tag("path", path_);
  lsls_connection_.clear_values();
}

void SqliteConnectionSafe::close_and_destroy() {
  close();
  LOG(INFO) << "Destroy SQLite database " << tag("path", path_);
  SqliteDb::destroy(path_).ignore();
}

}  // namespace td
