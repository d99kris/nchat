//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

#include <functional>
#include <type_traits>

namespace td {

class FolderId {
  int32 id = 0;

 public:
  FolderId() = default;

  explicit FolderId(int32 folder_id) : id(folder_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  FolderId(T folder_id) = delete;

  explicit FolderId(const td_api::object_ptr<td_api::ChatList> &chat_list) {
    if (chat_list != nullptr && chat_list->get_id() == td_api::chatListArchive::ID) {
      id = 1;
    } else {
      CHECK(id == 0);
    }
  }

  int32 get() const {
    return id;
  }

  bool operator==(const FolderId &other) const {
    return id == other.id;
  }

  bool operator!=(const FolderId &other) const {
    return id != other.id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_int(id);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    id = parser.fetch_int();
  }

  static FolderId main() {
    return FolderId();
  }
  static FolderId archive() {
    return FolderId(1);
  }
};

struct FolderIdHash {
  std::size_t operator()(FolderId folder_id) const {
    return std::hash<int32>()(folder_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, FolderId folder_id) {
  return string_builder << "folder " << folder_id.get();
}

}  // namespace td
