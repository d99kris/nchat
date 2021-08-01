//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/BackgroundId.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/PhotoSizeSource.h"
#include "td/telegram/SetWithPosition.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Variant.h"

#include <unordered_map>

namespace td {

class Td;

extern int VERBOSITY_NAME(file_references);

class FileReferenceManager : public Actor {
 public:
  static bool is_file_reference_error(const Status &error);
  static size_t get_file_reference_error_pos(const Status &error);

  FileSourceId create_message_file_source(FullMessageId full_message_id);
  FileSourceId create_user_photo_file_source(UserId user_id, int64 photo_id);
  FileSourceId create_chat_photo_file_source(ChatId chat_id);
  FileSourceId create_channel_photo_file_source(ChannelId channel_id);
  // FileSourceId create_wallpapers_file_source();  old wallpapers can't be repaired
  FileSourceId create_web_page_file_source(string url);
  FileSourceId create_saved_animations_file_source();
  FileSourceId create_recent_stickers_file_source(bool is_attached);
  FileSourceId create_favorite_stickers_file_source();
  FileSourceId create_background_file_source(BackgroundId background_id, int64 access_hash);

  using NodeId = FileId;
  void repair_file_reference(NodeId node_id, Promise<> promise);
  void reload_photo(PhotoSizeSource source, Promise<Unit> promise);

  bool add_file_source(NodeId node_id, FileSourceId file_source_id);

  vector<FileSourceId> get_some_file_sources(NodeId node_id);

  vector<FullMessageId> get_some_message_file_sources(NodeId node_id);

  bool remove_file_source(NodeId node_id, FileSourceId file_source_id);

  void merge(NodeId to_node_id, NodeId from_node_id);

  template <class StorerT>
  void store_file_source(FileSourceId file_source_id, StorerT &storer) const;

  template <class ParserT>
  FileSourceId parse_file_source(Td *td, ParserT &parser);

 private:
  struct Destination {
    bool empty() const {
      return node_id.empty();
    }
    NodeId node_id;
    int64 generation;
  };
  struct Query {
    std::vector<Promise<>> promises;
    int32 active_queries{0};
    Destination proxy;
    int64 generation;
  };

  struct Node {
    SetWithPosition<FileSourceId> file_source_ids;
    unique_ptr<Query> query;
    double last_successful_repair_time = -1e10;
  };

  struct FileSourceMessage {
    FullMessageId full_message_id;
  };
  struct FileSourceUserPhoto {
    int64 photo_id;
    UserId user_id;
  };
  struct FileSourceChatPhoto {
    ChatId chat_id;
  };
  struct FileSourceChannelPhoto {
    ChannelId channel_id;
  };
  struct FileSourceWallpapers {
    // empty
  };
  struct FileSourceWebPage {
    string url;
  };
  struct FileSourceSavedAnimations {
    // empty
  };
  struct FileSourceRecentStickers {
    bool is_attached;
  };
  struct FileSourceFavoriteStickers {
    // empty
  };
  struct FileSourceBackground {
    BackgroundId background_id;
    int64 access_hash;
  };

  // append only
  using FileSource = Variant<FileSourceMessage, FileSourceUserPhoto, FileSourceChatPhoto, FileSourceChannelPhoto,
                             FileSourceWallpapers, FileSourceWebPage, FileSourceSavedAnimations,
                             FileSourceRecentStickers, FileSourceFavoriteStickers, FileSourceBackground>;
  vector<FileSource> file_sources_;

  int64 query_generation_{0};

  std::unordered_map<NodeId, Node, FileIdHash> nodes_;

  void run_node(NodeId node);
  void send_query(Destination dest, FileSourceId file_source_id);
  Destination on_query_result(Destination dest, FileSourceId file_source_id, Status status, int32 sub = 0);

  template <class T>
  FileSourceId add_file_source_id(T source, Slice source_str);

  FileSourceId get_current_file_source_id() const;
};

}  // namespace td
