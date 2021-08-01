//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StorageManager.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileGcWorker.h"
#include "td/telegram/files/FileStatsWorker.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/TdDb.h"

#include "td/db/SqliteDb.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/port/Stat.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"

namespace td {

tl_object_ptr<td_api::databaseStatistics> DatabaseStats::as_td_api() const {
  return make_tl_object<td_api::databaseStatistics>(debug);
}

StorageManager::StorageManager(ActorShared<> parent, int32 scheduler_id)
    : parent_(std::move(parent)), scheduler_id_(scheduler_id) {
}

void StorageManager::start_up() {
  load_last_gc_timestamp();
  schedule_next_gc();

  load_fast_stat();
}

void StorageManager::on_new_file(int64 size, int32 cnt) {
  LOG(INFO) << "Add " << cnt << " file of size " << size << " to fast storage statistics";
  fast_stat_.cnt += cnt;
  fast_stat_.size += size;

  if (fast_stat_.cnt < 0 || fast_stat_.size < 0) {
    LOG(ERROR) << "Wrong fast stat after adding size " << size << " and cnt " << cnt;
    fast_stat_ = FileTypeStat();
  }
  save_fast_stat();
}

void StorageManager::get_storage_stats(bool need_all_files, int32 dialog_limit, Promise<FileStats> promise) {
  if (is_closed_) {
    promise.set_error(Status::Error(500, "Request aborted"));
    return;
  }
  if (pending_storage_stats_.size() != 0) {
    if (stats_dialog_limit_ == dialog_limit && need_all_files == stats_need_all_files_) {
      pending_storage_stats_.emplace_back(std::move(promise));
      return;
    }
    //TODO group same queries
    close_stats_worker();
  }
  if (!pending_run_gc_.empty()) {
    close_gc_worker();
  }
  stats_dialog_limit_ = dialog_limit;
  stats_need_all_files_ = need_all_files;
  pending_storage_stats_.emplace_back(std::move(promise));

  create_stats_worker();
  send_closure(stats_worker_, &FileStatsWorker::get_stats, need_all_files, stats_dialog_limit_ != 0,
               PromiseCreator::lambda(
                   [actor_id = actor_id(this), stats_generation = stats_generation_](Result<FileStats> file_stats) {
                     send_closure(actor_id, &StorageManager::on_file_stats, std::move(file_stats), stats_generation);
                   }));
}

void StorageManager::get_storage_stats_fast(Promise<FileStatsFast> promise) {
  promise.set_value(FileStatsFast(fast_stat_.size, fast_stat_.cnt, get_database_size(),
                                  get_language_pack_database_size(), get_log_size()));
}

void StorageManager::get_database_stats(Promise<DatabaseStats> promise) {
  //TODO: use another thread
  auto r_stats = G()->td_db()->get_stats();
  if (r_stats.is_error()) {
    promise.set_error(r_stats.move_as_error());
  } else {
    promise.set_value(DatabaseStats(r_stats.move_as_ok()));
  }
}

void StorageManager::update_use_storage_optimizer() {
  schedule_next_gc();
}

void StorageManager::run_gc(FileGcParameters parameters, Promise<FileStats> promise) {
  if (is_closed_) {
    promise.set_error(Status::Error(500, "Request aborted"));
    return;
  }
  if (!pending_run_gc_.empty()) {
    close_gc_worker();
  }

  bool split_by_owner_dialog_id = !parameters.owner_dialog_ids.empty() ||
                                  !parameters.exclude_owner_dialog_ids.empty() || parameters.dialog_limit != 0;
  get_storage_stats(true /*need_all_files*/, split_by_owner_dialog_id,
                    PromiseCreator::lambda(
                        [actor_id = actor_id(this), parameters = std::move(parameters)](Result<FileStats> file_stats) {
                          send_closure(actor_id, &StorageManager::on_all_files, std::move(parameters),
                                       std::move(file_stats), false);
                        }));

  //NB: get_storage_stats will cancel all gc queries, so promise needs to be added after the call
  pending_run_gc_.emplace_back(std::move(promise));
}

void StorageManager::on_file_stats(Result<FileStats> r_file_stats, uint32 generation) {
  if (generation != stats_generation_) {
    return;
  }
  if (r_file_stats.is_error()) {
    auto promises = std::move(pending_storage_stats_);
    for (auto &promise : promises) {
      promise.set_error(r_file_stats.error().clone());
    }
    return;
  }

  send_stats(r_file_stats.move_as_ok(), stats_dialog_limit_, std::move(pending_storage_stats_));
}

void StorageManager::create_stats_worker() {
  CHECK(!is_closed_);
  if (stats_worker_.empty()) {
    stats_worker_ =
        create_actor_on_scheduler<FileStatsWorker>("FileStatsWorker", scheduler_id_, create_reference(),
                                                   stats_cancellation_token_source_.get_cancellation_token());
  }
}

void StorageManager::on_all_files(FileGcParameters gc_parameters, Result<FileStats> r_file_stats, bool dummy) {
  int32 dialog_limit = gc_parameters.dialog_limit;
  if (is_closed_ && r_file_stats.is_ok()) {
    r_file_stats = Status::Error(500, "Request aborted");
  }
  if (r_file_stats.is_error()) {
    return on_gc_finished(dialog_limit, std::move(r_file_stats), false);
  }

  create_gc_worker();

  send_closure(gc_worker_, &FileGcWorker::run_gc, std::move(gc_parameters), r_file_stats.move_as_ok().all_files,
               PromiseCreator::lambda([actor_id = actor_id(this), dialog_limit](Result<FileStats> r_file_stats) {
                 send_closure(actor_id, &StorageManager::on_gc_finished, dialog_limit, std::move(r_file_stats), false);
               }));
}

int64 StorageManager::get_file_size(CSlice path) {
  auto r_info = stat(path);
  if (r_info.is_error()) {
    return 0;
  }

  auto size = r_info.ok().real_size_;
  LOG(DEBUG) << "Add file \"" << path << "\" of size " << size << " to fast storage statistics";
  return size;
}

int64 StorageManager::get_database_size() {
  int64 size = 0;
  G()->td_db()->with_db_path([&size](CSlice path) { size += get_file_size(path); });
  return size;
}

int64 StorageManager::get_language_pack_database_size() {
  int64 size = 0;
  auto path = G()->shared_config().get_option_string("language_pack_database_path");
  if (!path.empty()) {
    SqliteDb::with_db_path(path, [&size](CSlice path) { size += get_file_size(path); });
  }
  return size;
}

int64 StorageManager::get_log_size() {
  int64 size = 0;
  for (auto &log_path : log_interface->get_file_paths()) {
    size += get_file_size(log_path);
  }
  return size;
}

void StorageManager::create_gc_worker() {
  CHECK(!is_closed_);
  if (gc_worker_.empty()) {
    gc_worker_ = create_actor_on_scheduler<FileGcWorker>("FileGcWorker", scheduler_id_, create_reference(),
                                                         gc_cancellation_token_source_.get_cancellation_token());
  }
}

void StorageManager::on_gc_finished(int32 dialog_limit, Result<FileStats> r_file_stats, bool dummy) {
  if (r_file_stats.is_error()) {
    if (r_file_stats.error().code() != 500) {
      LOG(ERROR) << "GC failed: " << r_file_stats.error();
    }
    auto promises = std::move(pending_run_gc_);
    for (auto &promise : promises) {
      promise.set_error(r_file_stats.error().clone());
    }
    return;
  }

  send_stats(r_file_stats.move_as_ok(), dialog_limit, std::move(pending_run_gc_));
}

void StorageManager::save_fast_stat() {
  G()->td_db()->get_binlog_pmc()->set("fast_file_stat", log_event_store(fast_stat_).as_slice().str());
}

void StorageManager::load_fast_stat() {
  auto status = log_event_parse(fast_stat_, G()->td_db()->get_binlog_pmc()->get("fast_file_stat"));
  if (status.is_error()) {
    fast_stat_ = FileTypeStat();
  }
  LOG(INFO) << "Loaded fast storage statistics with " << fast_stat_.cnt << " files of total size " << fast_stat_.size;
}

void StorageManager::send_stats(FileStats &&stats, int32 dialog_limit, std::vector<Promise<FileStats>> promises) {
  fast_stat_ = stats.get_total_nontemp_stat();
  LOG(INFO) << "Recalculate fast storage statistics to " << fast_stat_.cnt << " files of total size "
            << fast_stat_.size;
  save_fast_stat();

  stats.apply_dialog_limit(dialog_limit);
  std::vector<DialogId> dialog_ids = stats.get_dialog_ids();

  auto promise =
      PromiseCreator::lambda([promises = std::move(promises), stats = std::move(stats)](Result<Unit>) mutable {
        for (auto &promise : promises) {
          promise.set_value(FileStats(stats));
        }
      });

  send_closure(G()->messages_manager(), &MessagesManager::load_dialogs, std::move(dialog_ids), std::move(promise));
}

ActorShared<> StorageManager::create_reference() {
  ref_cnt_++;
  return actor_shared(this, 1);
}

void StorageManager::hangup_shared() {
  ref_cnt_--;
  if (ref_cnt_ == 0) {
    stop();
  }
}

void StorageManager::close_stats_worker() {
  auto promises = std::move(pending_storage_stats_);
  pending_storage_stats_.clear();
  for (auto &promise : promises) {
    promise.set_error(Status::Error(500, "Request aborted"));
  }
  stats_generation_++;
  stats_worker_.reset();
  stats_cancellation_token_source_.cancel();
}

void StorageManager::close_gc_worker() {
  auto promises = std::move(pending_run_gc_);
  pending_run_gc_.clear();
  for (auto &promise : promises) {
    promise.set_error(Status::Error(500, "Request aborted"));
  }
  gc_worker_.reset();
  gc_cancellation_token_source_.cancel();
}

void StorageManager::hangup() {
  is_closed_ = true;
  close_stats_worker();
  close_gc_worker();
  hangup_shared();
}

uint32 StorageManager::load_last_gc_timestamp() {
  last_gc_timestamp_ = to_integer<uint32>(G()->td_db()->get_binlog_pmc()->get("files_gc_ts"));
  return last_gc_timestamp_;
}

void StorageManager::save_last_gc_timestamp() {
  last_gc_timestamp_ = static_cast<uint32>(Clocks::system());
  G()->td_db()->get_binlog_pmc()->set("files_gc_ts", to_string(last_gc_timestamp_));
}

void StorageManager::schedule_next_gc() {
  if (!G()->shared_config().get_option_boolean("use_storage_optimizer") &&
      !G()->parameters().enable_storage_optimizer) {
    next_gc_at_ = 0;
    cancel_timeout();
    LOG(INFO) << "No next file gc is scheduled";
    return;
  }
  auto sys_time = static_cast<uint32>(Clocks::system());

  auto next_gc_at = last_gc_timestamp_ + GC_EACH;
  if (next_gc_at < sys_time) {
    next_gc_at = sys_time;
  }
  if (next_gc_at > sys_time + GC_EACH) {
    next_gc_at = sys_time + GC_EACH;
  }
  next_gc_at += Random::fast(GC_DELAY, GC_DELAY + GC_RAND_DELAY);
  CHECK(next_gc_at >= sys_time);
  auto next_gc_in = next_gc_at - sys_time;

  LOG(INFO) << "Schedule next file gc in " << next_gc_in;
  next_gc_at_ = Time::now() + next_gc_in;
  set_timeout_at(next_gc_at_);
}

void StorageManager::timeout_expired() {
  if (next_gc_at_ == 0) {
    return;
  }
  if (!pending_run_gc_.empty() || !pending_storage_stats_.empty()) {
    set_timeout_in(60);
    return;
  }
  next_gc_at_ = 0;
  run_gc({}, PromiseCreator::lambda([actor_id = actor_id(this)](Result<FileStats> r_stats) {
           if (!r_stats.is_error() || r_stats.error().code() != 500) {
             // do not save gc timestamp is request was cancelled
             send_closure(actor_id, &StorageManager::save_last_gc_timestamp);
           }
           send_closure(actor_id, &StorageManager::schedule_next_gc);
         }));
}

}  // namespace td
