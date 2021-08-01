//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileDbId.h"
#include "td/telegram/files/FileEncryptionKey.h"
#include "td/telegram/files/FileGenerateManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileLoadManager.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/files/FileStats.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Location.h"
#include "td/telegram/PhotoSizeSource.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Container.h"
#include "td/utils/Enumerator.h"
#include "td/utils/logging.h"
#include "td/utils/optional.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#include <map>
#include <memory>
#include <set>
#include <unordered_set>
#include <utility>

namespace td {

extern int VERBOSITY_NAME(update_file);

class FileData;
class FileDbInterface;

enum class FileLocationSource : int8 { None, FromUser, FromBinlog, FromDatabase, FromServer };

struct NewRemoteFileLocation {
  NewRemoteFileLocation() = default;
  NewRemoteFileLocation(RemoteFileLocation remote, FileLocationSource source);
  RemoteFileLocation partial_or_empty() const;
  unique_ptr<PartialRemoteFileLocation> partial;

  //TODO: use RemoteId
  // hardest part is to determine wether we should flush this location to db.
  // probably, will need some generation in RemoteInfo
  optional<FullRemoteFileLocation> full;
  bool is_full_alive{false};  // if false, then we may try to upload this file
  FileLocationSource full_source{FileLocationSource::None};
  int64 ready_size = 0;
};

class FileNode {
 public:
  FileNode(LocalFileLocation local, NewRemoteFileLocation remote, unique_ptr<FullGenerateFileLocation> generate,
           int64 size, int64 expected_size, string remote_name, string url, DialogId owner_dialog_id,
           FileEncryptionKey key, FileId main_file_id, int8 main_file_id_priority)
      : local_(std::move(local))
      , remote_(std::move(remote))
      , generate_(std::move(generate))
      , size_(size)
      , expected_size_(expected_size)
      , remote_name_(std::move(remote_name))
      , url_(std::move(url))
      , owner_dialog_id_(owner_dialog_id)
      , encryption_key_(std::move(key))
      , main_file_id_(main_file_id)
      , main_file_id_priority_(main_file_id_priority) {
    init_ready_size();
  }
  void drop_local_location();
  void set_local_location(const LocalFileLocation &local, int64 ready_size, int64 prefix_offset,
                          int64 ready_prefix_size);
  void set_new_remote_location(NewRemoteFileLocation remote);
  void delete_partial_remote_location();
  void set_partial_remote_location(const PartialRemoteFileLocation &remote, int64 ready_size);

  bool delete_file_reference(Slice file_reference);
  void set_generate_location(unique_ptr<FullGenerateFileLocation> &&generate);
  void set_size(int64 size);
  void set_expected_size(int64 expected_size);
  void set_remote_name(string remote_name);
  void set_url(string url);
  void set_owner_dialog_id(DialogId owner_id);
  void set_encryption_key(FileEncryptionKey key);
  void set_upload_pause(FileId file_id);

  void set_download_priority(int8 priority);
  void set_upload_priority(int8 priority);
  void set_generate_priority(int8 download_priority, int8 upload_priority);

  void set_download_offset(int64 download_offset);
  void set_download_limit(int64 download_limit);

  void on_changed();
  void on_info_changed();
  void on_pmc_changed();

  bool need_info_flush() const;
  bool need_pmc_flush() const;

  void on_pmc_flushed();
  void on_info_flushed();

  string suggested_name() const;

 private:
  friend class FileView;
  friend class FileManager;

  LocalFileLocation local_;
  FileLoadManager::QueryId upload_id_ = 0;
  int64 download_offset_ = 0;
  int64 download_limit_ = 0;
  int64 local_ready_size_ = 0;         // PartialLocal only
  int64 local_ready_prefix_size_ = 0;  // PartialLocal only

  NewRemoteFileLocation remote_;

  FileLoadManager::QueryId download_id_ = 0;

  unique_ptr<FullGenerateFileLocation> generate_;
  FileLoadManager::QueryId generate_id_ = 0;

  int64 size_ = 0;
  int64 expected_size_ = 0;
  string remote_name_;
  string url_;
  DialogId owner_dialog_id_;
  FileEncryptionKey encryption_key_;
  FileDbId pmc_id_;
  std::vector<FileId> file_ids_;

  FileId main_file_id_;

  double last_successful_force_reupload_time_ = -1e10;

  FileId upload_pause_;
  int8 upload_priority_ = 0;
  int8 download_priority_ = 0;
  int8 generate_priority_ = 0;

  int8 generate_download_priority_ = 0;
  int8 generate_upload_priority_ = 0;

  int8 main_file_id_priority_ = 0;

  bool is_download_offset_dirty_ = false;
  bool is_download_limit_dirty_ = false;

  bool get_by_hash_{false};
  bool can_search_locally_{true};
  bool need_reload_photo_{false};

  bool is_download_started_ = false;
  bool generate_was_update_ = false;

  bool need_load_from_pmc_ = false;

  bool pmc_changed_flag_{false};
  bool info_changed_flag_{false};

  bool upload_was_update_file_reference_{false};
  bool download_was_update_file_reference_{false};

  void init_ready_size();

  void recalc_ready_prefix_size(int64 prefix_offset, int64 ready_prefix_size);
};

class FileManager;

class FileNodePtr {
 public:
  FileNodePtr() = default;
  FileNodePtr(FileId file_id, FileManager *file_manager) : file_id_(file_id), file_manager_(file_manager) {
  }

  FileNode *operator->() const;
  FileNode &operator*() const;
  FileNode *get() const;
  FullRemoteFileLocation *get_remote() const;
  explicit operator bool() const;

 private:
  FileId file_id_;
  FileManager *file_manager_ = nullptr;
  FileNode *get_unsafe() const;
};

class ConstFileNodePtr {
 public:
  ConstFileNodePtr() = default;
  ConstFileNodePtr(FileNodePtr file_node_ptr) : file_node_ptr_(file_node_ptr) {
  }

  const FileNode *operator->() const {
    return file_node_ptr_.operator->();
  }
  const FileNode &operator*() const {
    return file_node_ptr_.operator*();
  }

  explicit operator bool() const {
    return static_cast<bool>(file_node_ptr_);
  }
  const FullRemoteFileLocation *get_remote() const {
    return file_node_ptr_.get_remote();
  }

 private:
  FileNodePtr file_node_ptr_;
};

class FileView {
 public:
  FileView() = default;
  explicit FileView(ConstFileNodePtr node);

  bool empty() const;

  bool has_local_location() const;
  const FullLocalFileLocation &local_location() const;
  bool has_remote_location() const;
  bool has_alive_remote_location() const;
  bool has_active_upload_remote_location() const;
  bool has_active_download_remote_location() const;
  const FullRemoteFileLocation &remote_location() const;
  const FullRemoteFileLocation &main_remote_location() const;
  bool has_generate_location() const;
  const FullGenerateFileLocation &generate_location() const;

  bool has_url() const;
  const string &url() const;

  const string &remote_name() const;

  string suggested_name() const;

  DialogId owner_dialog_id() const;

  bool get_by_hash() const;

  FileId file_id() const {
    return node_->main_file_id_;
  }

  int64 size() const;
  int64 expected_size(bool may_guess = false) const;
  bool is_downloading() const;
  int64 download_offset() const;
  int64 downloaded_prefix(int64 offset) const;
  int64 local_prefix_size() const;
  int64 local_total_size() const;
  bool is_uploading() const;
  int64 remote_size() const;
  string path() const;

  int64 get_allocated_local_size() const;

  bool can_download_from_server() const;
  bool can_generate() const;
  bool can_delete() const;

  FileType get_type() const {
    if (has_local_location()) {
      return local_location().file_type_;
    }
    if (has_remote_location()) {
      return remote_location().file_type_;
    }
    if (has_generate_location()) {
      return generate_location().file_type_;
    }
    return FileType::Temp;
  }
  bool is_encrypted_secret() const {
    return get_type() == FileType::Encrypted;
  }
  bool is_encrypted_secure() const {
    return get_type() == FileType::Secure;
  }
  bool is_secure() const {
    return get_type() == FileType::Secure || get_type() == FileType::SecureRaw;
  }
  bool is_encrypted_any() const {
    return is_encrypted_secret() || is_encrypted_secure();
  }
  bool is_encrypted() const {
    return is_encrypted_secret() || is_secure();
  }
  const FileEncryptionKey &encryption_key() const {
    return node_->encryption_key_;
  }

  bool may_reload_photo() const {
    if (!has_remote_location()) {
      return false;
    }
    if (!remote_location().is_photo()) {
      return false;
    }
    auto type = remote_location().get_source().get_type();
    return type == PhotoSizeSource::Type::DialogPhotoBig || type == PhotoSizeSource::Type::DialogPhotoSmall ||
           type == PhotoSizeSource::Type::StickerSetThumbnail;
    return false;
  }

 private:
  ConstFileNodePtr node_{};
};

class FileManager : public FileLoadManager::Callback {
 public:
  class DownloadCallback {
   public:
    DownloadCallback() = default;
    DownloadCallback(const DownloadCallback &) = delete;
    DownloadCallback &operator=(const DownloadCallback &) = delete;
    virtual ~DownloadCallback() = default;
    virtual void on_progress(FileId file_id) {
    }

    virtual void on_download_ok(FileId file_id) = 0;
    virtual void on_download_error(FileId file_id, Status error) = 0;
  };

  class UploadCallback {
   public:
    UploadCallback() = default;
    UploadCallback(const UploadCallback &) = delete;
    UploadCallback &operator=(const UploadCallback &) = delete;
    virtual ~UploadCallback() = default;

    virtual void on_progress(FileId file_id) {
    }

    // After on_upload_ok all uploads of this file will be paused till merge, delete_partial_remote_location or
    // explicit upload request with the same file_id.
    // Also upload may be resumed after some other merges.
    virtual void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) = 0;
    virtual void on_upload_encrypted_ok(FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) = 0;
    virtual void on_upload_secure_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) = 0;
    virtual void on_upload_error(FileId file_id, Status error) = 0;
  };

  class Context {
   public:
    virtual void on_new_file(int64 size, int32 cnt) = 0;

    virtual void on_file_updated(FileId size) = 0;

    virtual bool add_file_source(FileId file_id, FileSourceId file_source_id) = 0;

    virtual bool remove_file_source(FileId file_id, FileSourceId file_source_id) = 0;

    virtual void on_merge_files(FileId to_file_id, FileId from_file_id) = 0;

    virtual vector<FileSourceId> get_some_file_sources(FileId file_id) = 0;

    virtual void repair_file_reference(FileId file_id, Promise<Unit> promise) = 0;

    virtual void reload_photo(PhotoSizeSource source, Promise<Unit> promise) = 0;

    virtual ActorShared<> create_reference() = 0;

    Context() = default;
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    virtual ~Context() = default;
  };

  explicit FileManager(unique_ptr<Context> context);
  FileManager(const FileManager &other) = delete;
  FileManager &operator=(const FileManager &other) = delete;
  FileManager(FileManager &&other) = delete;
  FileManager &operator=(FileManager &&other) = delete;
  ~FileManager() override;

  static bool are_modification_times_equal(int64 old_mtime, int64 new_mtime);

  void init_actor();

  FileId dup_file_id(FileId file_id);

  void on_file_unlink(const FullLocalFileLocation &location);

  FileId register_empty(FileType type);
  Result<FileId> register_local(FullLocalFileLocation location, DialogId owner_dialog_id, int64 size,
                                bool get_by_hash = false, bool force = false) TD_WARN_UNUSED_RESULT;
  FileId register_remote(const FullRemoteFileLocation &location, FileLocationSource file_location_source,
                         DialogId owner_dialog_id, int64 size, int64 expected_size, string name) TD_WARN_UNUSED_RESULT;
  Result<FileId> register_generate(FileType file_type, FileLocationSource file_location_source, string original_path,
                                   string conversion, DialogId owner_dialog_id,
                                   int64 expected_size) TD_WARN_UNUSED_RESULT;

  Result<FileId> merge(FileId x_file_id, FileId y_file_id, bool no_sync = false) TD_WARN_UNUSED_RESULT;

  void add_file_source(FileId file_id, FileSourceId file_source_id);

  void remove_file_source(FileId file_id, FileSourceId file_source_id);

  void change_files_source(FileSourceId file_source_id, const vector<FileId> &old_file_ids,
                           const vector<FileId> &new_file_ids);

  bool set_encryption_key(FileId file_id, FileEncryptionKey key);
  bool set_content(FileId file_id, BufferSlice bytes);

  void download(FileId file_id, std::shared_ptr<DownloadCallback> callback, int32 new_priority, int64 offset,
                int64 limit);
  void upload(FileId file_id, std::shared_ptr<UploadCallback> callback, int32 new_priority, uint64 upload_order);
  void resume_upload(FileId file_id, std::vector<int> bad_parts, std::shared_ptr<UploadCallback> callback,
                     int32 new_priority, uint64 upload_order, bool force = false);
  void cancel_upload(FileId file_id);
  bool delete_partial_remote_location(FileId file_id);
  void delete_file_reference(FileId file_id, std::string file_reference);
  void get_content(FileId file_id, Promise<BufferSlice> promise);

  void read_file_part(FileId file_id, int32 offset, int32 count, int left_tries,
                      Promise<td_api::object_ptr<td_api::filePart>> promise);

  void delete_file(FileId file_id, Promise<Unit> promise, const char *source);

  void external_file_generate_write_part(int64 id, int32 offset, string data, Promise<> promise);
  void external_file_generate_progress(int64 id, int32 expected_size, int32 local_prefix_size, Promise<> promise);
  void external_file_generate_finish(int64 id, Status status, Promise<> promise);

  Result<FileId> from_persistent_id(CSlice persistent_id, FileType file_type) TD_WARN_UNUSED_RESULT;
  FileView get_file_view(FileId file_id) const;
  FileView get_sync_file_view(FileId file_id);
  td_api::object_ptr<td_api::file> get_file_object(FileId file_id, bool with_main_file_id = true);
  vector<int32> get_file_ids_object(const vector<FileId> &file_ids, bool with_main_file_id = true);

  Result<FileId> get_input_thumbnail_file_id(const tl_object_ptr<td_api::InputFile> &thumb_input_file,
                                             DialogId owner_dialog_id, bool is_encrypted) TD_WARN_UNUSED_RESULT;
  Result<FileId> get_input_file_id(FileType type, const tl_object_ptr<td_api::InputFile> &file,
                                   DialogId owner_dialog_id, bool allow_zero, bool is_encrypted,
                                   bool get_by_hash = false, bool is_secure = false) TD_WARN_UNUSED_RESULT;

  Result<FileId> get_map_thumbnail_file_id(Location location, int32 zoom, int32 width, int32 height, int32 scale,
                                           DialogId owner_dialog_id) TD_WARN_UNUSED_RESULT;

  vector<tl_object_ptr<telegram_api::InputDocument>> get_input_documents(const vector<FileId> &file_ids);

  static bool extract_was_uploaded(const tl_object_ptr<telegram_api::InputMedia> &input_media);
  static bool extract_was_thumbnail_uploaded(const tl_object_ptr<telegram_api::InputMedia> &input_media);
  static string extract_file_reference(const tl_object_ptr<telegram_api::InputMedia> &input_media);

  static string extract_file_reference(const tl_object_ptr<telegram_api::InputDocument> &input_document);

  static string extract_file_reference(const tl_object_ptr<telegram_api::InputPhoto> &input_photo);

  static bool extract_was_uploaded(const tl_object_ptr<telegram_api::InputChatPhoto> &input_chat_photo);
  static string extract_file_reference(const tl_object_ptr<telegram_api::InputChatPhoto> &input_chat_photo);

  template <class StorerT>
  void store_file(FileId file_id, StorerT &storer, int32 ttl = 5) const;

  template <class ParserT>
  FileId parse_file(ParserT &parser);

 private:
  static constexpr char PERSISTENT_ID_VERSION_OLD = 2;
  static constexpr char PERSISTENT_ID_VERSION_MAP = 3;
  static constexpr char PERSISTENT_ID_VERSION = 4;

  Result<FileId> check_input_file_id(FileType type, Result<FileId> result, bool is_encrypted, bool allow_zero,
                                     bool is_secure) TD_WARN_UNUSED_RESULT;

  FileId register_url(string url, FileType file_type, FileLocationSource file_location_source,
                      DialogId owner_dialog_id);
  Result<FileId> register_file(FileData &&data, FileLocationSource file_location_source, const char *source,
                               bool force);

  static constexpr int8 FROM_BYTES_PRIORITY = 10;

  using FileNodeId = int32;

  using QueryId = FileLoadManager::QueryId;
  class Query {
   public:
    FileId file_id_;
    enum class Type : int32 {
      UploadByHash,
      UploadWaitFileReference,
      Upload,
      DownloadWaitFileReference,
      DownloadReloadDialog,
      Download,
      SetContent,
      Generate
    } type_;
  };

  friend StringBuilder &operator<<(StringBuilder &string_builder, Query::Type type);

  struct FileIdInfo {
    FileNodeId node_id_{0};
    bool send_updates_flag_{false};
    bool pin_flag_{false};
    bool sent_file_id_flag_{false};

    int8 download_priority_{0};
    int8 upload_priority_{0};

    uint64 upload_order_;

    std::shared_ptr<DownloadCallback> download_callback_;
    std::shared_ptr<UploadCallback> upload_callback_;
  };

  class ForceUploadActor;

  ActorShared<> parent_;
  unique_ptr<Context> context_;
  std::shared_ptr<FileDbInterface> file_db_;

  FileIdInfo *get_file_id_info(FileId file_id);

  struct RemoteInfo {
    // mutable is set to to enable changing of access hash
    mutable FullRemoteFileLocation remote_;
    mutable FileLocationSource file_location_source_;
    FileId file_id_;
    bool operator==(const RemoteInfo &other) const {
      return this->remote_ == other.remote_;
    }
    bool operator<(const RemoteInfo &other) const {
      return this->remote_ < other.remote_;
    }
  };
  Enumerator<RemoteInfo> remote_location_info_;

  std::map<FullLocalFileLocation, FileId> local_location_to_file_id_;
  std::map<FullGenerateFileLocation, FileId> generate_location_to_file_id_;
  std::map<FileDbId, int32> pmc_id_to_file_node_id_;

  vector<FileIdInfo> file_id_info_;
  vector<int32> empty_file_ids_;
  vector<unique_ptr<FileNode>> file_nodes_;
  ActorOwn<FileLoadManager> file_load_manager_;
  ActorOwn<FileGenerateManager> file_generate_manager_;

  Container<Query> queries_container_;

  bool is_closed_ = false;

  std::set<std::string> bad_paths_;

  FileId next_file_id();
  FileNodeId next_file_node_id();
  int32 next_pmc_file_id();
  FileId create_file_id(int32 file_node_id, FileNode *file_node);
  void try_forget_file_id(FileId file_id);

  void load_from_pmc(FileId file_id, FullLocalFileLocation full_local);
  void load_from_pmc(FileId file_id, const FullRemoteFileLocation &full_remote);
  void load_from_pmc(FileId file_id, const FullGenerateFileLocation &full_generate);
  template <class LocationT>
  void load_from_pmc_impl(FileId file_id, const LocationT &location);
  void load_from_pmc_result(FileId file_id, Result<FileData> &&result);
  FileId register_pmc_file_data(FileData &&data);

  Status check_local_location(FileNodePtr node);
  bool try_fix_partial_local_location(FileNodePtr node);
  Status check_local_location(FullLocalFileLocation &location, int64 &size);
  void try_flush_node_full(FileNodePtr node, bool new_remote, bool new_local, bool new_generate, FileDbId other_pmc_id);
  void try_flush_node(FileNodePtr node, const char *source);
  void try_flush_node_info(FileNodePtr node, const char *source);
  void try_flush_node_pmc(FileNodePtr node, const char *source);
  void clear_from_pmc(FileNodePtr node);
  void flush_to_pmc(FileNodePtr node, bool new_remote, bool new_local, bool new_generate, const char *source);
  void load_from_pmc(FileNodePtr node, bool new_remote, bool new_local, bool new_generate);

  string get_unique_id(const FullGenerateFileLocation &location);
  string get_unique_id(const FullRemoteFileLocation &location);

  string get_persistent_id(const FullGenerateFileLocation &location);
  string get_persistent_id(const FullRemoteFileLocation &location);

  Result<FileId> from_persistent_id_map(Slice binary, FileType file_type);
  Result<FileId> from_persistent_id_v2(Slice binary, FileType file_type);
  Result<FileId> from_persistent_id_v3(Slice binary, FileType file_type);
  Result<FileId> from_persistent_id_v23(Slice binary, FileType file_type, int32 version);

  string fix_file_extension(Slice file_name, Slice file_type, Slice file_extension);
  string get_file_name(FileType file_type, Slice path);

  ConstFileNodePtr get_file_node(FileId file_id) const {
    return ConstFileNodePtr{FileNodePtr{file_id, const_cast<FileManager *>(this)}};
  }
  FileNodePtr get_file_node(FileId file_id) {
    return FileNodePtr{file_id, this};
  }
  FileNode *get_file_node_raw(FileId file_id, FileNodeId *file_node_id = nullptr);

  FileNodePtr get_sync_file_node(FileId file_id);

  void on_force_reupload_success(FileId file_id);

  // void release_file_node(FileNodeId id);
  void do_cancel_download(FileNodePtr node);
  void do_cancel_upload(FileNodePtr node);
  void do_cancel_generate(FileNodePtr node);
  void run_upload(FileNodePtr node, std::vector<int> bad_parts);
  void run_download(FileNodePtr node);
  void run_generate(FileNodePtr node);

  void on_start_download(QueryId query_id) override;
  void on_partial_download(QueryId query_id, const PartialLocalFileLocation &partial_local, int64 ready_size,
                           int64 size) override;
  void on_hash(QueryId query_id, string hash) override;
  void on_partial_upload(QueryId query_id, const PartialRemoteFileLocation &partial_remote, int64 ready_size) override;
  void on_download_ok(QueryId query_id, const FullLocalFileLocation &local, int64 size, bool is_new) override;
  void on_upload_ok(QueryId query_id, FileType file_type, const PartialRemoteFileLocation &partial_remote,
                    int64 size) override;
  void on_upload_full_ok(QueryId query_id, const FullRemoteFileLocation &remote) override;
  void on_error(QueryId query_id, Status status) override;

  void on_error_impl(FileNodePtr node, Query::Type type, bool was_active, Status status);

  void on_partial_generate(QueryId, const PartialLocalFileLocation &partial_local, int32 expected_size);
  void on_generate_ok(QueryId, const FullLocalFileLocation &local);

  std::pair<Query, bool> finish_query(QueryId query_id);

  FullRemoteFileLocation *get_remote(int32 key);

  std::unordered_set<FileId, FileIdHash> get_main_file_ids(const vector<FileId> &file_ids);

  void hangup() override;
  void tear_down() override;

  friend class FileNodePtr;
};

}  // namespace td
