//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DeviceTokenManager.h"
#include "td/telegram/Document.h"
#include "td/telegram/Document.hpp"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Photo.hpp"
#include "td/telegram/SecretChatId.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/mtproto_api.h"
#include "td/mtproto/PacketInfo.h"
#include "td/mtproto/Transport.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"

#include "td/actor/SleepActor.h"

#include "td/utils/as.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/Gzip.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/utf8.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace td {

int VERBOSITY_NAME(notifications) = VERBOSITY_NAME(INFO);

class SetContactSignUpNotificationQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetContactSignUpNotificationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool is_disabled) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::account_setContactSignUpNotification(is_disabled))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_setContactSignUpNotification>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->close_flag()) {
      LOG(ERROR) << "Receive error for set contact sign up notification: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

class GetContactSignUpNotificationQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetContactSignUpNotificationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_getContactSignUpNotification())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_getContactSignUpNotification>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->notification_manager_->on_get_disable_contact_registered_notifications(result_ptr.ok());
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->close_flag() || 1) {
      LOG(ERROR) << "Receive error for get contact sign up notification: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

NotificationManager::NotificationManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  flush_pending_notifications_timeout_.set_callback(on_flush_pending_notifications_timeout_callback);
  flush_pending_notifications_timeout_.set_callback_data(static_cast<void *>(this));

  flush_pending_updates_timeout_.set_callback(on_flush_pending_updates_timeout_callback);
  flush_pending_updates_timeout_.set_callback_data(static_cast<void *>(this));
}

void NotificationManager::on_flush_pending_notifications_timeout_callback(void *notification_manager_ptr,
                                                                          int64 group_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto notification_manager = static_cast<NotificationManager *>(notification_manager_ptr);
  VLOG(notifications) << "Ready to flush pending notifications for notification group " << group_id_int;
  if (group_id_int > 0) {
    send_closure_later(notification_manager->actor_id(notification_manager),
                       &NotificationManager::flush_pending_notifications,
                       NotificationGroupId(narrow_cast<int32>(group_id_int)));
  } else if (group_id_int == 0) {
    send_closure_later(notification_manager->actor_id(notification_manager),
                       &NotificationManager::after_get_difference_impl);
  } else {
    send_closure_later(notification_manager->actor_id(notification_manager),
                       &NotificationManager::after_get_chat_difference_impl,
                       NotificationGroupId(narrow_cast<int32>(-group_id_int)));
  }
}

void NotificationManager::on_flush_pending_updates_timeout_callback(void *notification_manager_ptr,
                                                                    int64 group_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto notification_manager = static_cast<NotificationManager *>(notification_manager_ptr);
  send_closure_later(notification_manager->actor_id(notification_manager), &NotificationManager::flush_pending_updates,
                     narrow_cast<int32>(group_id_int), "timeout");
}

bool NotificationManager::is_disabled() const {
  return !td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot() || G()->close_flag();
}

StringBuilder &operator<<(StringBuilder &string_builder, const NotificationManager::ActiveNotificationsUpdate &update) {
  if (update.update == nullptr) {
    return string_builder << "null";
  }
  string_builder << "update[\n";
  for (auto &group : update.update->groups_) {
    vector<int32> added_notification_ids;
    for (auto &notification : group->notifications_) {
      added_notification_ids.push_back(notification->id_);
    }

    string_builder << "    [" << NotificationGroupId(group->id_) << " of type "
                   << get_notification_group_type(group->type_) << " from " << DialogId(group->chat_id_)
                   << "; total_count = " << group->total_count_ << ", restore " << added_notification_ids << "]\n";
  }
  return string_builder << ']';
}

NotificationManager::ActiveNotificationsUpdate NotificationManager::as_active_notifications_update(
    const td_api::updateActiveNotifications *update) {
  return ActiveNotificationsUpdate{update};
}

string NotificationManager::get_is_contact_registered_notifications_synchronized_key() {
  return "notifications_contact_registered_sync_state";
}

void NotificationManager::start_up() {
  init();
}

void NotificationManager::init() {
  if (is_disabled()) {
    return;
  }

  disable_contact_registered_notifications_ =
      G()->shared_config().get_option_boolean("disable_contact_registered_notifications");
  auto sync_state = G()->td_db()->get_binlog_pmc()->get(get_is_contact_registered_notifications_synchronized_key());
  if (sync_state.empty()) {
    sync_state = "00";
  }
  contact_registered_notifications_sync_state_ = static_cast<SyncState>(sync_state[0] - '0');
  VLOG(notifications) << "Loaded disable_contact_registered_notifications = "
                      << disable_contact_registered_notifications_ << " in state " << sync_state;
  if (contact_registered_notifications_sync_state_ != SyncState::Completed ||
      sync_state[1] != static_cast<int32>(disable_contact_registered_notifications_) + '0') {
    run_contact_registered_notifications_sync();
  }

  current_notification_id_ =
      NotificationId(to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("notification_id_current")));
  current_notification_group_id_ =
      NotificationGroupId(to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("notification_group_id_current")));

  VLOG(notifications) << "Loaded current " << current_notification_id_ << " and " << current_notification_group_id_;

  on_notification_group_count_max_changed(false);
  on_notification_group_size_max_changed();

  on_online_cloud_timeout_changed();
  on_notification_cloud_delay_changed();
  on_notification_default_delay_changed();

  last_loaded_notification_group_key_.last_notification_date = std::numeric_limits<int32>::max();
  if (max_notification_group_count_ != 0) {
    int32 loaded_groups = 0;
    int32 needed_groups = static_cast<int32>(max_notification_group_count_);
    do {
      loaded_groups += load_message_notification_groups_from_database(needed_groups, false);
    } while (loaded_groups < needed_groups && last_loaded_notification_group_key_.last_notification_date != 0);
  }

  auto call_notification_group_ids_string = G()->td_db()->get_binlog_pmc()->get("notification_call_group_ids");
  if (!call_notification_group_ids_string.empty()) {
    auto call_notification_group_ids = transform(full_split(call_notification_group_ids_string, ','), [](Slice str) {
      return NotificationGroupId{to_integer_safe<int32>(str).ok()};
    });
    VLOG(notifications) << "Load call_notification_group_ids = " << call_notification_group_ids;
    for (auto &group_id : call_notification_group_ids) {
      if (group_id.get() > current_notification_group_id_.get()) {
        LOG(ERROR) << "Fix current notification group id from " << current_notification_group_id_ << " to " << group_id;
        current_notification_group_id_ = group_id;
        G()->td_db()->get_binlog_pmc()->set("notification_group_id_current",
                                            to_string(current_notification_group_id_.get()));
      }
      auto it = get_group_force(group_id);
      if (it != groups_.end()) {
        LOG(ERROR) << "Have " << it->first << " " << it->second << " as a call notification group";
      } else {
        call_notification_group_ids_.push_back(group_id);
        available_call_notification_group_ids_.insert(group_id);
      }
    }
  }

  auto notification_announcement_ids_string = G()->td_db()->get_binlog_pmc()->get("notification_announcement_ids");
  if (!notification_announcement_ids_string.empty()) {
    VLOG(notifications) << "Load announcement ids = " << notification_announcement_ids_string;
    auto ids = transform(full_split(notification_announcement_ids_string, ','),
                         [](Slice str) { return to_integer_safe<int32>(str).ok(); });
    CHECK(ids.size() % 2 == 0);
    bool is_changed = false;
    auto min_date = G()->unix_time() - ANNOUNCEMENT_ID_CACHE_TIME;
    for (size_t i = 0; i < ids.size(); i += 2) {
      auto id = ids[i];
      auto date = ids[i + 1];
      if (date < min_date) {
        is_changed = true;
        continue;
      }
      announcement_id_date_.emplace(id, date);
    }
    if (is_changed) {
      save_announcement_ids();
    }
  }

  class StateCallback : public StateManager::Callback {
   public:
    explicit StateCallback(ActorId<NotificationManager> parent) : parent_(std::move(parent)) {
    }
    bool on_online(bool is_online) override {
      if (is_online) {
        send_closure(parent_, &NotificationManager::flush_all_pending_notifications);
      }
      return parent_.is_alive();
    }

   private:
    ActorId<NotificationManager> parent_;
  };
  send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));

  is_inited_ = true;
  try_send_update_active_notifications();
}

void NotificationManager::save_announcement_ids() {
  auto min_date = G()->unix_time() - ANNOUNCEMENT_ID_CACHE_TIME;
  vector<int32> ids;
  for (auto &it : announcement_id_date_) {
    auto id = it.first;
    auto date = it.second;
    if (date < min_date) {
      continue;
    }
    ids.push_back(id);
    ids.push_back(date);
  }

  VLOG(notifications) << "Save announcement ids " << ids;
  if (ids.empty()) {
    G()->td_db()->get_binlog_pmc()->erase("notification_announcement_ids");
    return;
  }

  auto notification_announcement_ids_string = implode(transform(ids, [](int32 id) { return to_string(id); }), ',');
  G()->td_db()->get_binlog_pmc()->set("notification_announcement_ids", notification_announcement_ids_string);
}

td_api::object_ptr<td_api::updateActiveNotifications> NotificationManager::get_update_active_notifications() const {
  auto needed_groups = max_notification_group_count_;
  vector<td_api::object_ptr<td_api::notificationGroup>> groups;
  for (auto &group : groups_) {
    if (needed_groups == 0 || group.first.last_notification_date == 0) {
      break;
    }
    needed_groups--;

    vector<td_api::object_ptr<td_api::notification>> notifications;
    for (auto &notification : reversed(group.second.notifications)) {
      auto notification_object = get_notification_object(group.first.dialog_id, notification);
      if (notification_object->type_ != nullptr) {
        notifications.push_back(std::move(notification_object));
      }
      if (notifications.size() == max_notification_group_size_) {
        break;
      }
    }
    if (!notifications.empty()) {
      std::reverse(notifications.begin(), notifications.end());
      groups.push_back(td_api::make_object<td_api::notificationGroup>(
          group.first.group_id.get(), get_notification_group_type_object(group.second.type),
          group.first.dialog_id.get(), group.second.total_count, std::move(notifications)));
    }
  }

  return td_api::make_object<td_api::updateActiveNotifications>(std::move(groups));
}

void NotificationManager::tear_down() {
  parent_.reset();
}

NotificationManager::NotificationGroups::iterator NotificationManager::add_group(NotificationGroupKey &&group_key,
                                                                                 NotificationGroup &&group,
                                                                                 const char *source) {
  if (group.notifications.empty()) {
    LOG_CHECK(group_key.last_notification_date == 0) << "Trying to add empty " << group_key << " from " << source;
  }
  bool is_inserted = group_keys_.emplace(group_key.group_id, group_key).second;
  CHECK(is_inserted);
  return groups_.emplace(std::move(group_key), std::move(group)).first;
}

NotificationManager::NotificationGroups::iterator NotificationManager::get_group(NotificationGroupId group_id) {
  auto group_keys_it = group_keys_.find(group_id);
  if (group_keys_it != group_keys_.end()) {
    return groups_.find(group_keys_it->second);
  }
  return groups_.end();
}

void NotificationManager::load_group_force(NotificationGroupId group_id) {
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  auto group_it = get_group_force(group_id, true);
  CHECK(group_it != groups_.end());
}

NotificationManager::NotificationGroups::iterator NotificationManager::get_group_force(NotificationGroupId group_id,
                                                                                       bool send_update) {
  auto group_it = get_group(group_id);
  if (group_it != groups_.end()) {
    return group_it;
  }

  if (td::contains(call_notification_group_ids_, group_id)) {
    return groups_.end();
  }

  auto message_group = td_->messages_manager_->get_message_notification_group_force(group_id);
  if (!message_group.dialog_id.is_valid()) {
    return groups_.end();
  }

  NotificationGroupKey group_key(group_id, message_group.dialog_id, 0);
  for (auto &notification : message_group.notifications) {
    if (notification.date > group_key.last_notification_date) {
      group_key.last_notification_date = notification.date;
    }
    if (notification.notification_id.get() > current_notification_id_.get()) {
      LOG(ERROR) << "Fix current notification id from " << current_notification_id_ << " to "
                 << notification.notification_id;
      current_notification_id_ = notification.notification_id;
      G()->td_db()->get_binlog_pmc()->set("notification_id_current", to_string(current_notification_id_.get()));
    }
  }
  if (group_id.get() > current_notification_group_id_.get()) {
    LOG(ERROR) << "Fix current notification group id from " << current_notification_group_id_ << " to " << group_id;
    current_notification_group_id_ = group_id;
    G()->td_db()->get_binlog_pmc()->set("notification_group_id_current",
                                        to_string(current_notification_group_id_.get()));
  }

  NotificationGroup group;
  group.type = message_group.type;
  group.total_count = message_group.total_count;
  group.notifications = std::move(message_group.notifications);

  VLOG(notifications) << "Finish to load " << group_id << " of type " << message_group.type << " with total_count "
                      << message_group.total_count << " and notifications " << group.notifications;

  if (send_update && group_key.last_notification_date != 0) {
    auto last_group_key = get_last_updated_group_key();
    if (group_key < last_group_key) {
      if (last_group_key.last_notification_date != 0) {
        send_remove_group_update(last_group_key, groups_[last_group_key], vector<int32>());
      }
      send_add_group_update(group_key, group);
    }
  }
  return add_group(std::move(group_key), std::move(group), "get_group_force");
}

void NotificationManager::delete_group(NotificationGroups::iterator &&group_it) {
  auto erased_count = group_keys_.erase(group_it->first.group_id);
  CHECK(erased_count > 0);
  groups_.erase(group_it);
}

int32 NotificationManager::load_message_notification_groups_from_database(int32 limit, bool send_update) {
  CHECK(limit > 0);
  if (last_loaded_notification_group_key_.last_notification_date == 0) {
    // everything was already loaded
    return 0;
  }

  vector<NotificationGroupKey> group_keys = td_->messages_manager_->get_message_notification_group_keys_from_database(
      last_loaded_notification_group_key_, limit);
  last_loaded_notification_group_key_ =
      group_keys.size() == static_cast<size_t>(limit) ? group_keys.back() : NotificationGroupKey();

  int32 result = 0;
  for (auto &group_key : group_keys) {
    auto group_it = get_group_force(group_key.group_id, send_update);
    LOG_CHECK(group_it != groups_.end()) << call_notification_group_ids_ << " " << group_keys << " "
                                         << current_notification_group_id_ << " " << limit;
    CHECK(group_it->first.dialog_id.is_valid());
    if (!(last_loaded_notification_group_key_ < group_it->first)) {
      result++;
    }
  }
  return result;
}

NotificationId NotificationManager::get_first_notification_id(const NotificationGroup &group) {
  if (!group.notifications.empty()) {
    return group.notifications[0].notification_id;
  }
  if (!group.pending_notifications.empty()) {
    return group.pending_notifications[0].notification_id;
  }
  return NotificationId();
}

NotificationId NotificationManager::get_last_notification_id(const NotificationGroup &group) {
  if (!group.pending_notifications.empty()) {
    return group.pending_notifications.back().notification_id;
  }
  if (!group.notifications.empty()) {
    return group.notifications.back().notification_id;
  }
  return NotificationId();
}

MessageId NotificationManager::get_first_message_id(const NotificationGroup &group) {
  // it's fine to return MessageId() if first notification has no message_id, because
  // non-message notification can't be mixed with message notifications
  if (!group.notifications.empty()) {
    return group.notifications[0].type->get_message_id();
  }
  if (!group.pending_notifications.empty()) {
    return group.pending_notifications[0].type->get_message_id();
  }
  return MessageId();
}

MessageId NotificationManager::get_last_message_id(const NotificationGroup &group) {
  // it's fine to return MessageId() if last notification has no message_id, because
  // non-message notification can't be mixed with message notifications
  if (!group.pending_notifications.empty()) {
    return group.pending_notifications.back().type->get_message_id();
  }
  if (!group.notifications.empty()) {
    return group.notifications.back().type->get_message_id();
  }
  return MessageId();
}

MessageId NotificationManager::get_last_message_id_by_notification_id(const NotificationGroup &group,
                                                                      NotificationId max_notification_id) {
  for (auto &notification : reversed(group.pending_notifications)) {
    if (notification.notification_id.get() <= max_notification_id.get()) {
      auto message_id = notification.type->get_message_id();
      if (message_id.is_valid()) {
        return message_id;
      }
    }
  }
  for (auto &notification : reversed(group.notifications)) {
    if (notification.notification_id.get() <= max_notification_id.get()) {
      auto message_id = notification.type->get_message_id();
      if (message_id.is_valid()) {
        return message_id;
      }
    }
  }
  return MessageId();
}

void NotificationManager::load_message_notifications_from_database(const NotificationGroupKey &group_key,
                                                                   NotificationGroup &group, size_t desired_size) {
  if (!G()->parameters().use_message_db) {
    return;
  }
  if (group.is_loaded_from_database || group.is_being_loaded_from_database ||
      group.type == NotificationGroupType::Calls) {
    return;
  }
  if (group.total_count == 0) {
    return;
  }

  VLOG(notifications) << "Trying to load up to " << desired_size << " notifications in " << group_key.group_id
                      << " with " << group.notifications.size() << " current notifications";

  group.is_being_loaded_from_database = true;

  CHECK(desired_size > group.notifications.size());
  size_t limit = desired_size - group.notifications.size();
  auto first_notification_id = get_first_notification_id(group);
  auto from_notification_id = first_notification_id.is_valid() ? first_notification_id : NotificationId::max();
  auto first_message_id = get_first_message_id(group);
  auto from_message_id = first_message_id.is_valid() ? first_message_id : MessageId::max();
  send_closure(G()->messages_manager(), &MessagesManager::get_message_notifications_from_database, group_key.dialog_id,
               group_key.group_id, from_notification_id, from_message_id, static_cast<int32>(limit),
               PromiseCreator::lambda([actor_id = actor_id(this), group_id = group_key.group_id,
                                       limit](Result<vector<Notification>> r_notifications) {
                 send_closure_later(actor_id, &NotificationManager::on_get_message_notifications_from_database,
                                    group_id, limit, std::move(r_notifications));
               }));
}

void NotificationManager::on_get_message_notifications_from_database(NotificationGroupId group_id, size_t limit,
                                                                     Result<vector<Notification>> r_notifications) {
  auto group_it = get_group(group_id);
  CHECK(group_it != groups_.end());
  auto &group = group_it->second;
  CHECK(group.is_being_loaded_from_database == true);
  group.is_being_loaded_from_database = false;

  if (r_notifications.is_error()) {
    group.is_loaded_from_database = true;  // do not try again to load it
    return;
  }
  auto notifications = r_notifications.move_as_ok();

  CHECK(limit > 0);
  if (notifications.empty()) {
    group.is_loaded_from_database = true;
  }

  auto first_notification_id = get_first_notification_id(group);
  if (first_notification_id.is_valid()) {
    while (!notifications.empty() && notifications.back().notification_id.get() >= first_notification_id.get()) {
      // possible if notifications was added after the database request was sent
      notifications.pop_back();
    }
  }
  auto first_message_id = get_first_message_id(group);
  if (first_message_id.is_valid()) {
    while (!notifications.empty() && notifications.back().type->get_message_id() >= first_message_id) {
      // possible if notifications was added after the database request was sent
      notifications.pop_back();
    }
  }

  add_notifications_to_group_begin(std::move(group_it), std::move(notifications));

  group_it = get_group(group_id);
  CHECK(group_it != groups_.end());
  if (max_notification_group_size_ > group_it->second.notifications.size()) {
    load_message_notifications_from_database(group_it->first, group_it->second, keep_notification_group_size_);
  }
}

void NotificationManager::add_notifications_to_group_begin(NotificationGroups::iterator group_it,
                                                           vector<Notification> notifications) {
  CHECK(group_it != groups_.end());

  td::remove_if(notifications, [dialog_id = group_it->first.dialog_id](const Notification &notification) {
    return notification.type->get_notification_type_object(dialog_id) == nullptr;
  });

  if (notifications.empty()) {
    return;
  }
  VLOG(notifications) << "Add to beginning of " << group_it->first << " of size "
                      << group_it->second.notifications.size() << ' ' << notifications;

  auto group_key = group_it->first;
  auto final_group_key = group_key;
  for (auto &notification : notifications) {
    if (notification.date > final_group_key.last_notification_date) {
      final_group_key.last_notification_date = notification.date;
    }
  }
  CHECK(final_group_key.last_notification_date != 0);

  bool is_position_changed = final_group_key.last_notification_date != group_key.last_notification_date;

  NotificationGroup group = std::move(group_it->second);
  if (is_position_changed) {
    VLOG(notifications) << "Position of notification group is changed from " << group_key << " to " << final_group_key;
    delete_group(std::move(group_it));
  }

  auto last_group_key = get_last_updated_group_key();
  bool was_updated = false;
  bool is_updated = false;
  if (is_position_changed) {
    was_updated = group_key.last_notification_date != 0 && group_key < last_group_key;
    is_updated = final_group_key.last_notification_date != 0 && final_group_key < last_group_key;
  } else {
    CHECK(group_key.last_notification_date != 0);
    was_updated = is_updated = !(last_group_key < group_key);
  }

  if (!is_updated) {
    CHECK(!was_updated);
    VLOG(notifications) << "There is no need to send updateNotificationGroup in " << group_key
                        << ", because of newer notification groups";
    group.notifications.insert(group.notifications.begin(), std::make_move_iterator(notifications.begin()),
                               std::make_move_iterator(notifications.end()));
  } else {
    if (!was_updated) {
      if (last_group_key.last_notification_date != 0) {
        // need to remove last notification group to not exceed max_notification_group_count_
        send_remove_group_update(last_group_key, groups_[last_group_key], vector<int32>());
      }
      send_add_group_update(group_key, group);
    }

    vector<Notification> new_notifications;
    vector<td_api::object_ptr<td_api::notification>> added_notifications;
    new_notifications.reserve(notifications.size());
    added_notifications.reserve(notifications.size());
    for (auto &notification : notifications) {
      added_notifications.push_back(get_notification_object(group_key.dialog_id, notification));
      CHECK(added_notifications.back()->type_ != nullptr);
      new_notifications.push_back(std::move(notification));
    }
    notifications = std::move(new_notifications);

    size_t old_notification_count = group.notifications.size();
    auto updated_notification_count = old_notification_count < max_notification_group_size_
                                          ? max_notification_group_size_ - old_notification_count
                                          : 0;
    if (added_notifications.size() > updated_notification_count) {
      added_notifications.erase(added_notifications.begin(), added_notifications.end() - updated_notification_count);
    }
    auto new_notification_count = old_notification_count < keep_notification_group_size_
                                      ? keep_notification_group_size_ - old_notification_count
                                      : 0;
    if (new_notification_count > notifications.size()) {
      new_notification_count = notifications.size();
    }
    if (new_notification_count != 0) {
      VLOG(notifications) << "Add " << new_notification_count << " notifications to " << group_key.group_id
                          << " with current size " << group.notifications.size();
      group.notifications.insert(group.notifications.begin(),
                                 std::make_move_iterator(notifications.end() - new_notification_count),
                                 std::make_move_iterator(notifications.end()));
    }

    if (!added_notifications.empty()) {
      add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
          group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(), 0, true,
          group.total_count, std::move(added_notifications), vector<int32>()));
    }
  }

  if (is_position_changed) {
    add_group(std::move(final_group_key), std::move(group), "add_notifications_to_group_begin");
  } else {
    CHECK(group_it->first.last_notification_date == 0 || !group.notifications.empty());
    group_it->second = std::move(group);
  }
}

size_t NotificationManager::get_max_notification_group_size() const {
  return max_notification_group_size_;
}

NotificationId NotificationManager::get_max_notification_id() const {
  return current_notification_id_;
}

NotificationId NotificationManager::get_next_notification_id() {
  if (is_disabled()) {
    return NotificationId();
  }
  if (current_notification_id_.get() == std::numeric_limits<int32>::max()) {
    LOG(ERROR) << "Notification id overflowed";
    return NotificationId();
  }

  current_notification_id_ = NotificationId(current_notification_id_.get() + 1);
  G()->td_db()->get_binlog_pmc()->set("notification_id_current", to_string(current_notification_id_.get()));
  return current_notification_id_;
}

NotificationGroupId NotificationManager::get_next_notification_group_id() {
  if (is_disabled()) {
    return NotificationGroupId();
  }
  if (current_notification_group_id_.get() == std::numeric_limits<int32>::max()) {
    LOG(ERROR) << "Notification group id overflowed";
    return NotificationGroupId();
  }

  current_notification_group_id_ = NotificationGroupId(current_notification_group_id_.get() + 1);
  G()->td_db()->get_binlog_pmc()->set("notification_group_id_current", to_string(current_notification_group_id_.get()));
  return current_notification_group_id_;
}

void NotificationManager::try_reuse_notification_group_id(NotificationGroupId group_id) {
  if (is_disabled()) {
    return;
  }
  if (!group_id.is_valid()) {
    return;
  }

  VLOG(notifications) << "Trying to reuse " << group_id;
  if (group_id != current_notification_group_id_) {
    // may be implemented in the future
    return;
  }

  auto group_it = get_group(group_id);
  if (group_it != groups_.end()) {
    LOG_CHECK(group_it->first.last_notification_date == 0 && group_it->second.total_count == 0)
        << running_get_difference_ << " " << delayed_notification_update_count_ << " "
        << unreceived_notification_update_count_ << " " << pending_updates_[group_id.get()].size() << " "
        << group_it->first << " " << group_it->second;
    CHECK(group_it->second.notifications.empty());
    CHECK(group_it->second.pending_notifications.empty());
    CHECK(!group_it->second.is_being_loaded_from_database);
    delete_group(std::move(group_it));

    CHECK(running_get_chat_difference_.count(group_id.get()) == 0);

    flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
    flush_pending_updates_timeout_.cancel_timeout(group_id.get());
    if (pending_updates_.erase(group_id.get()) == 1) {
      on_delayed_notification_update_count_changed(-1, group_id.get(), "try_reuse_notification_group_id");
    }
  }

  current_notification_group_id_ = NotificationGroupId(current_notification_group_id_.get() - 1);
  G()->td_db()->get_binlog_pmc()->set("notification_group_id_current", to_string(current_notification_group_id_.get()));
}

NotificationGroupKey NotificationManager::get_last_updated_group_key() const {
  size_t left = max_notification_group_count_;
  auto it = groups_.begin();
  while (it != groups_.end() && left > 1) {
    ++it;
    left--;
  }
  if (it == groups_.end()) {
    return NotificationGroupKey();
  }
  return it->first;
}

int32 NotificationManager::get_notification_delay_ms(DialogId dialog_id, const PendingNotification &notification,
                                                     int32 min_delay_ms) const {
  if (dialog_id.get_type() == DialogType::SecretChat) {
    return MIN_NOTIFICATION_DELAY_MS;  // there is no reason to delay notifications in secret chats
  }
  if (!notification.type->can_be_delayed()) {
    return MIN_NOTIFICATION_DELAY_MS;
  }

  auto delay_ms = [&]() {
    auto online_info = td_->contacts_manager_->get_my_online_status();
    if (!online_info.is_online_local && online_info.is_online_remote) {
      // If we are offline, but online from some other client, then delay notification
      // for 'notification_cloud_delay' seconds.
      return notification_cloud_delay_ms_;
    }

    if (!online_info.is_online_local &&
        online_info.was_online_remote > max(static_cast<double>(online_info.was_online_local),
                                            G()->server_time_cached() - online_cloud_timeout_ms_ * 1e-3)) {
      // If we are offline, but was online from some other client in last 'online_cloud_timeout' seconds
      // after we had gone offline, then delay notification for 'notification_cloud_delay' seconds.
      return notification_cloud_delay_ms_;
    }

    if (online_info.is_online_remote) {
      // If some other client is online, then delay notification for 'notification_default_delay' seconds.
      return notification_default_delay_ms_;
    }

    // otherwise send update without additional delay
    return 0;
  }();

  auto passed_time_ms = max(0, static_cast<int32>((G()->server_time_cached() - notification.date - 1) * 1000));
  return max(max(min_delay_ms, delay_ms) - passed_time_ms, MIN_NOTIFICATION_DELAY_MS);
}

void NotificationManager::add_notification(NotificationGroupId group_id, NotificationGroupType group_type,
                                           DialogId dialog_id, int32 date, DialogId notification_settings_dialog_id,
                                           bool initial_is_silent, bool is_silent, int32 min_delay_ms,
                                           NotificationId notification_id, unique_ptr<NotificationType> type,
                                           const char *source) {
  if (is_disabled() || max_notification_group_count_ == 0) {
    on_notification_removed(notification_id);
    return;
  }

  CHECK(group_id.is_valid());
  CHECK(dialog_id.is_valid());
  CHECK(notification_settings_dialog_id.is_valid());
  LOG_CHECK(notification_id.is_valid()) << notification_id << " " << source;
  CHECK(type != nullptr);
  VLOG(notifications) << "Add " << notification_id << " to " << group_id << " of type " << group_type << " in "
                      << dialog_id << " with settings from " << notification_settings_dialog_id
                      << (is_silent ? "   silently" : " with sound") << ": " << *type;

  if (!type->is_temporary()) {
    remove_temporary_notifications(group_id, "add_notification");
  }

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    group_it = add_group(NotificationGroupKey(group_id, dialog_id, 0), NotificationGroup(), "add_notification");
  }
  if (group_it->second.notifications.empty() && group_it->second.pending_notifications.empty()) {
    group_it->second.type = group_type;
  }
  CHECK(group_it->second.type == group_type);

  NotificationGroup &group = group_it->second;
  if (notification_id.get() <= get_last_notification_id(group).get()) {
    LOG(ERROR) << "Failed to add " << notification_id << " to " << group_id << " of type " << group_type << " in "
               << dialog_id << ", because have already added " << get_last_notification_id(group);
    on_notification_removed(notification_id);
    return;
  }
  auto message_id = type->get_message_id();
  if (message_id.is_valid() && message_id <= get_last_message_id(group)) {
    LOG(ERROR) << "Failed to add " << notification_id << " of type " << *type << " to " << group_id << " of type "
               << group_type << " in " << dialog_id << ", because have already added notification about "
               << get_last_message_id(group);
    on_notification_removed(notification_id);
    return;
  }

  PendingNotification notification;
  notification.date = date;
  notification.settings_dialog_id = notification_settings_dialog_id;
  notification.initial_is_silent = initial_is_silent;
  notification.is_silent = is_silent;
  notification.notification_id = notification_id;
  notification.type = std::move(type);

  auto delay_ms = get_notification_delay_ms(dialog_id, notification, min_delay_ms);
  VLOG(notifications) << "Delay " << notification_id << " for " << delay_ms << " milliseconds";
  auto flush_time = delay_ms * 0.001 + Time::now();

  if (group.pending_notifications_flush_time == 0 || flush_time < group.pending_notifications_flush_time) {
    group.pending_notifications_flush_time = flush_time;
    flush_pending_notifications_timeout_.set_timeout_at(group_id.get(), group.pending_notifications_flush_time);
  }
  if (group.pending_notifications.empty()) {
    on_delayed_notification_update_count_changed(1, group_id.get(), source);
  }
  group.pending_notifications.push_back(std::move(notification));
}

StringBuilder &operator<<(StringBuilder &string_builder, const NotificationManager::NotificationUpdate &update) {
  if (update.update == nullptr) {
    return string_builder << "null";
  }
  switch (update.update->get_id()) {
    case td_api::updateNotification::ID: {
      auto p = static_cast<const td_api::updateNotification *>(update.update);
      return string_builder << "update[" << NotificationId(p->notification_->id_) << " from "
                            << NotificationGroupId(p->notification_group_id_) << ']';
    }
    case td_api::updateNotificationGroup::ID: {
      auto p = static_cast<const td_api::updateNotificationGroup *>(update.update);
      vector<int32> added_notification_ids;
      for (auto &notification : p->added_notifications_) {
        added_notification_ids.push_back(notification->id_);
      }

      return string_builder << "update[" << NotificationGroupId(p->notification_group_id_) << " of type "
                            << get_notification_group_type(p->type_) << " from " << DialogId(p->chat_id_)
                            << " with settings from " << DialogId(p->notification_settings_chat_id_)
                            << (p->is_silent_ ? "   silently" : " with sound") << "; total_count = " << p->total_count_
                            << ", add " << added_notification_ids << ", remove " << p->removed_notification_ids_;
    }
    default:
      UNREACHABLE();
      return string_builder << "unknown";
  }
}

NotificationManager::NotificationUpdate NotificationManager::as_notification_update(const td_api::Update *update) {
  return NotificationUpdate{update};
}

void NotificationManager::add_update(int32 group_id, td_api::object_ptr<td_api::Update> update) {
  if (!is_binlog_processed_ || !is_inited_) {
    return;
  }
  VLOG(notifications) << "Add " << as_notification_update(update.get());
  auto &updates = pending_updates_[group_id];
  if (updates.empty()) {
    on_delayed_notification_update_count_changed(1, group_id, "add_update");
  }
  updates.push_back(std::move(update));
  if (!running_get_difference_ && running_get_chat_difference_.count(group_id) == 0) {
    flush_pending_updates_timeout_.add_timeout_in(group_id, MIN_UPDATE_DELAY_MS * 1e-3);
  } else {
    flush_pending_updates_timeout_.set_timeout_in(group_id, MAX_UPDATE_DELAY_MS * 1e-3);
  }
}

void NotificationManager::add_update_notification_group(td_api::object_ptr<td_api::updateNotificationGroup> update) {
  auto group_id = update->notification_group_id_;
  if (update->notification_settings_chat_id_ == 0) {
    update->notification_settings_chat_id_ = update->chat_id_;
  }
  add_update(group_id, std::move(update));
}

void NotificationManager::add_update_notification(NotificationGroupId notification_group_id, DialogId dialog_id,
                                                  const Notification &notification) {
  auto notification_object = get_notification_object(dialog_id, notification);
  if (notification_object->type_ == nullptr) {
    return;
  }

  add_update(notification_group_id.get(), td_api::make_object<td_api::updateNotification>(
                                              notification_group_id.get(), std::move(notification_object)));
  if (!notification.type->can_be_delayed()) {
    force_flush_pending_updates(notification_group_id, "add_update_notification");
  }
}

void NotificationManager::flush_pending_updates(int32 group_id, const char *source) {
  auto it = pending_updates_.find(group_id);
  if (it == pending_updates_.end()) {
    return;
  }

  auto updates = std::move(it->second);
  pending_updates_.erase(it);

  if (is_destroyed_) {
    return;
  }

  VLOG(notifications) << "Send " << updates.size() << " pending updates in " << NotificationGroupId(group_id)
                      << " from " << source;
  for (auto &update : updates) {
    VLOG(notifications) << "Have " << as_notification_update(update.get());
  }

  td::remove_if(updates, [](auto &update) { return update == nullptr; });

  // if a notification was added, then deleted and then re-added we need to keep
  // first addition, because it can be with sound,
  // deletion, because number of notification should never exceed max_notification_group_size_,
  // and second addition, because we has kept the deletion

  // calculate last state of all notifications
  std::unordered_set<int32> added_notification_ids;
  std::unordered_set<int32> edited_notification_ids;
  std::unordered_set<int32> removed_notification_ids;
  for (auto &update : updates) {
    CHECK(update != nullptr);
    if (update->get_id() == td_api::updateNotificationGroup::ID) {
      auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());
      for (auto &notification : update_ptr->added_notifications_) {
        auto notification_id = notification->id_;
        bool is_inserted = added_notification_ids.insert(notification_id).second;
        CHECK(is_inserted);                                          // there must be no additions after addition
        CHECK(edited_notification_ids.count(notification_id) == 0);  // there must be no additions after edit
        removed_notification_ids.erase(notification_id);
      }
      for (auto &notification_id : update_ptr->removed_notification_ids_) {
        added_notification_ids.erase(notification_id);
        edited_notification_ids.erase(notification_id);
        if (!removed_notification_ids.insert(notification_id).second) {
          // sometimes there can be deletion of notification without previous addition, because the notification
          // has already been deleted at the time of addition and get_notification_object_type was nullptr
          VLOG(notifications) << "Remove duplicated deletion of " << notification_id;
          notification_id = 0;
        }
      }
      td::remove_if(update_ptr->removed_notification_ids_, [](auto &notification_id) { return notification_id == 0; });
    } else {
      CHECK(update->get_id() == td_api::updateNotification::ID);
      auto update_ptr = static_cast<td_api::updateNotification *>(update.get());
      auto notification_id = update_ptr->notification_->id_;
      CHECK(removed_notification_ids.count(notification_id) == 0);  // there must be no edits of deleted notifications
      added_notification_ids.erase(notification_id);
      edited_notification_ids.insert(notification_id);
    }
  }

  // we need to keep only additions of notifications from added_notification_ids/edited_notification_ids and
  // all edits of notifications from edited_notification_ids
  // deletions of a notification can be removed, only if the addition of the notification has already been deleted
  // deletions of all unkept notifications can be moved to the first updateNotificationGroup
  // after that at every moment there is no more active notifications than in the last moment,
  // so left deletions after add/edit can be safely removed and following additions can be treated as edits
  // we still need to keep deletions coming first, because we can't have 2 consequent additions
  // from all additions of the same notification, we need to preserve the first, because it can be with sound,
  // all other additions and edits can be merged to the first addition/edit
  // i.e. in edit+delete+add chain we want to remove deletion and merge addition to the edit

  auto group_key = group_keys_[NotificationGroupId(group_id)];
  bool is_hidden = group_key.last_notification_date == 0 || get_last_updated_group_key() < group_key;
  bool is_changed = true;
  while (is_changed) {
    is_changed = false;

    size_t cur_pos = 0;
    std::unordered_map<int32, size_t> first_add_notification_pos;
    std::unordered_map<int32, size_t> first_edit_notification_pos;
    std::unordered_set<int32> can_be_deleted_notification_ids;
    std::vector<int32> moved_deleted_notification_ids;
    size_t first_notification_group_pos = 0;

    for (auto &update : updates) {
      cur_pos++;

      CHECK(update != nullptr);
      if (update->get_id() == td_api::updateNotificationGroup::ID) {
        auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());

        for (auto &notification : update_ptr->added_notifications_) {
          auto notification_id = notification->id_;
          bool is_needed =
              added_notification_ids.count(notification_id) != 0 || edited_notification_ids.count(notification_id) != 0;
          if (!is_needed) {
            VLOG(notifications) << "Remove unneeded addition of " << notification_id << " in update " << cur_pos;
            can_be_deleted_notification_ids.insert(notification_id);
            notification = nullptr;
            is_changed = true;
            continue;
          }

          auto edit_it = first_edit_notification_pos.find(notification_id);
          if (edit_it != first_edit_notification_pos.end()) {
            VLOG(notifications) << "Move addition of " << notification_id << " in update " << cur_pos
                                << " to edit in update " << edit_it->second;
            CHECK(edit_it->second < cur_pos);
            auto previous_update_ptr = static_cast<td_api::updateNotification *>(updates[edit_it->second - 1].get());
            CHECK(previous_update_ptr->notification_->id_ == notification_id);
            previous_update_ptr->notification_->type_ = std::move(notification->type_);
            is_changed = true;
            notification = nullptr;
            continue;
          }
          auto add_it = first_add_notification_pos.find(notification_id);
          if (add_it != first_add_notification_pos.end()) {
            VLOG(notifications) << "Move addition of " << notification_id << " in update " << cur_pos << " to update "
                                << add_it->second;
            CHECK(add_it->second < cur_pos);
            auto previous_update_ptr =
                static_cast<td_api::updateNotificationGroup *>(updates[add_it->second - 1].get());
            bool is_found = false;
            for (auto &prev_notification : previous_update_ptr->added_notifications_) {
              if (prev_notification->id_ == notification_id) {
                prev_notification->type_ = std::move(notification->type_);
                is_found = true;
                break;
              }
            }
            CHECK(is_found);
            is_changed = true;
            notification = nullptr;
            continue;
          }

          // it is a first addition/edit of needed notification
          first_add_notification_pos[notification_id] = cur_pos;
        }
        td::remove_if(update_ptr->added_notifications_, [](auto &notification) { return notification == nullptr; });
        if (update_ptr->added_notifications_.empty() && !update_ptr->is_silent_) {
          update_ptr->is_silent_ = true;
          is_changed = true;
        }

        for (auto &notification_id : update_ptr->removed_notification_ids_) {
          bool is_needed =
              added_notification_ids.count(notification_id) != 0 || edited_notification_ids.count(notification_id) != 0;
          if (can_be_deleted_notification_ids.count(notification_id) == 1) {
            CHECK(!is_needed);
            VLOG(notifications) << "Remove unneeded deletion of " << notification_id << " in update " << cur_pos;
            notification_id = 0;
            is_changed = true;
            continue;
          }
          if (!is_needed) {
            if (first_notification_group_pos != 0) {
              VLOG(notifications) << "Need to keep deletion of " << notification_id << " in update " << cur_pos
                                  << ", but can move it to the first updateNotificationGroup at pos "
                                  << first_notification_group_pos;
              moved_deleted_notification_ids.push_back(notification_id);
              notification_id = 0;
              is_changed = true;
            }
            continue;
          }

          if (first_add_notification_pos.count(notification_id) != 0 ||
              first_edit_notification_pos.count(notification_id) != 0) {
            // the notification will be re-added, and we will be able to merge the addition with previous update, so we can just remove the deletion
            VLOG(notifications) << "Remove unneeded deletion in update " << cur_pos;
            notification_id = 0;
            is_changed = true;
            continue;
          }

          // we need to keep the deletion, because otherwise we will have 2 consequent additions
        }
        td::remove_if(update_ptr->removed_notification_ids_,
                      [](auto &notification_id) { return notification_id == 0; });

        if (update_ptr->removed_notification_ids_.empty() && update_ptr->added_notifications_.empty()) {
          for (size_t i = cur_pos - 1; i > 0; i--) {
            if (updates[i - 1] != nullptr && updates[i - 1]->get_id() == td_api::updateNotificationGroup::ID) {
              VLOG(notifications) << "Move total_count from empty update " << cur_pos << " to update " << i;
              auto previous_update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[i - 1].get());
              previous_update_ptr->type_ = std::move(update_ptr->type_);
              previous_update_ptr->total_count_ = update_ptr->total_count_;
              is_changed = true;
              update = nullptr;
              break;
            }
          }
          if (update != nullptr && cur_pos == 1) {
            bool is_empty_group =
                added_notification_ids.empty() && edited_notification_ids.empty() && update_ptr->total_count_ == 0;
            if (updates.size() > 1 || (is_hidden && !is_empty_group)) {
              VLOG(notifications) << "Remove empty update " << cur_pos;
              CHECK(moved_deleted_notification_ids.empty());
              is_changed = true;
              update = nullptr;
            }
          }
        }

        if (first_notification_group_pos == 0 && update != nullptr) {
          first_notification_group_pos = cur_pos;
        }
      } else {
        CHECK(update->get_id() == td_api::updateNotification::ID);
        auto update_ptr = static_cast<td_api::updateNotification *>(update.get());
        auto notification_id = update_ptr->notification_->id_;
        bool is_needed =
            added_notification_ids.count(notification_id) != 0 || edited_notification_ids.count(notification_id) != 0;
        if (!is_needed) {
          VLOG(notifications) << "Remove unneeded update " << cur_pos;
          is_changed = true;
          update = nullptr;
          continue;
        }
        auto edit_it = first_edit_notification_pos.find(notification_id);
        if (edit_it != first_edit_notification_pos.end()) {
          VLOG(notifications) << "Move edit of " << notification_id << " in update " << cur_pos << " to update "
                              << edit_it->second;
          CHECK(edit_it->second < cur_pos);
          auto previous_update_ptr = static_cast<td_api::updateNotification *>(updates[edit_it->second - 1].get());
          CHECK(previous_update_ptr->notification_->id_ == notification_id);
          previous_update_ptr->notification_->type_ = std::move(update_ptr->notification_->type_);
          is_changed = true;
          update = nullptr;
          continue;
        }
        auto add_it = first_add_notification_pos.find(notification_id);
        if (add_it != first_add_notification_pos.end()) {
          VLOG(notifications) << "Move edit of " << notification_id << " in update " << cur_pos << " to update "
                              << add_it->second;
          CHECK(add_it->second < cur_pos);
          auto previous_update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[add_it->second - 1].get());
          bool is_found = false;
          for (auto &notification : previous_update_ptr->added_notifications_) {
            if (notification->id_ == notification_id) {
              notification->type_ = std::move(update_ptr->notification_->type_);
              is_found = true;
              break;
            }
          }
          CHECK(is_found);
          is_changed = true;
          update = nullptr;
          continue;
        }

        // it is a first addition/edit of needed notification
        first_edit_notification_pos[notification_id] = cur_pos;
      }
    }
    if (!moved_deleted_notification_ids.empty()) {
      CHECK(first_notification_group_pos != 0);
      auto &update = updates[first_notification_group_pos - 1];
      CHECK(update->get_id() == td_api::updateNotificationGroup::ID);
      auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());
      append(update_ptr->removed_notification_ids_, std::move(moved_deleted_notification_ids));
      auto old_size = update_ptr->removed_notification_ids_.size();
      std::sort(update_ptr->removed_notification_ids_.begin(), update_ptr->removed_notification_ids_.end());
      update_ptr->removed_notification_ids_.erase(
          std::unique(update_ptr->removed_notification_ids_.begin(), update_ptr->removed_notification_ids_.end()),
          update_ptr->removed_notification_ids_.end());
      CHECK(old_size == update_ptr->removed_notification_ids_.size());
    }

    td::remove_if(updates, [](auto &update) { return update == nullptr; });
    if (updates.empty()) {
      VLOG(notifications) << "There are no updates to send in " << NotificationGroupId(group_id);
      break;
    }

    auto has_common_notifications = [](const vector<td_api::object_ptr<td_api::notification>> &notifications,
                                       const vector<int32> &notification_ids) {
      for (auto &notification : notifications) {
        if (td::contains(notification_ids, notification->id_)) {
          return true;
        }
      }
      return false;
    };

    size_t last_update_pos = 0;
    for (size_t i = 1; i < updates.size(); i++) {
      if (updates[last_update_pos]->get_id() == td_api::updateNotificationGroup::ID &&
          updates[i]->get_id() == td_api::updateNotificationGroup::ID) {
        auto last_update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[last_update_pos].get());
        auto update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[i].get());
        if ((last_update_ptr->notification_settings_chat_id_ == update_ptr->notification_settings_chat_id_ ||
             last_update_ptr->added_notifications_.empty()) &&
            !has_common_notifications(last_update_ptr->added_notifications_, update_ptr->removed_notification_ids_) &&
            !has_common_notifications(update_ptr->added_notifications_, last_update_ptr->removed_notification_ids_)) {
          // combine updates
          VLOG(notifications) << "Combine " << as_notification_update(last_update_ptr) << " and "
                              << as_notification_update(update_ptr);
          CHECK(last_update_ptr->notification_group_id_ == update_ptr->notification_group_id_);
          CHECK(last_update_ptr->chat_id_ == update_ptr->chat_id_);
          if (last_update_ptr->is_silent_ && !update_ptr->is_silent_) {
            last_update_ptr->is_silent_ = false;
          }
          last_update_ptr->notification_settings_chat_id_ = update_ptr->notification_settings_chat_id_;
          last_update_ptr->type_ = std::move(update_ptr->type_);
          last_update_ptr->total_count_ = update_ptr->total_count_;
          append(last_update_ptr->added_notifications_, std::move(update_ptr->added_notifications_));
          append(last_update_ptr->removed_notification_ids_, std::move(update_ptr->removed_notification_ids_));
          updates[i] = nullptr;
          is_changed = true;
          continue;
        }
      }
      last_update_pos++;
      if (last_update_pos != i) {
        updates[last_update_pos] = std::move(updates[i]);
      }
    }
    updates.resize(last_update_pos + 1);
  }

  for (auto &update : updates) {
    CHECK(update != nullptr);
    if (update->get_id() == td_api::updateNotificationGroup::ID) {
      auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());
      std::sort(update_ptr->added_notifications_.begin(), update_ptr->added_notifications_.end(),
                [](const auto &lhs, const auto &rhs) { return lhs->id_ < rhs->id_; });
      std::sort(update_ptr->removed_notification_ids_.begin(), update_ptr->removed_notification_ids_.end());
    }
    VLOG(notifications) << "Send " << as_notification_update(update.get());
    send_closure(G()->td(), &Td::send_update, std::move(update));
  }
  on_delayed_notification_update_count_changed(-1, group_id, "flush_pending_updates");

  auto group_it = get_group_force(NotificationGroupId(group_id));
  CHECK(group_it != groups_.end());
  NotificationGroup &group = group_it->second;
  for (auto &notification : group.notifications) {
    on_notification_processed(notification.notification_id);
  }
}

void NotificationManager::force_flush_pending_updates(NotificationGroupId group_id, const char *source) {
  flush_pending_updates_timeout_.cancel_timeout(group_id.get());
  flush_pending_updates(group_id.get(), source);
}

void NotificationManager::flush_all_pending_updates(bool include_delayed_chats, const char *source) {
  VLOG(notifications) << "Flush all pending notification updates "
                      << (include_delayed_chats ? "with delayed chats " : "") << "from " << source;
  if (!include_delayed_chats && running_get_difference_) {
    return;
  }

  vector<NotificationGroupKey> ready_group_keys;
  for (const auto &it : pending_updates_) {
    if (include_delayed_chats || running_get_chat_difference_.count(it.first) == 0) {
      auto group_it = get_group(NotificationGroupId(it.first));
      CHECK(group_it != groups_.end());
      ready_group_keys.push_back(group_it->first);
    }
  }

  // flush groups in reverse order to not exceed max_notification_group_count_
  VLOG(notifications) << "Flush pending updates in " << ready_group_keys.size() << " notification groups";
  std::sort(ready_group_keys.begin(), ready_group_keys.end());
  for (auto group_key : reversed(ready_group_keys)) {
    force_flush_pending_updates(group_key.group_id, "flush_all_pending_updates");
  }
  if (include_delayed_chats) {
    CHECK(pending_updates_.empty());
  }
}

bool NotificationManager::do_flush_pending_notifications(NotificationGroupKey &group_key, NotificationGroup &group,
                                                         vector<PendingNotification> &pending_notifications) {
  if (pending_notifications.empty()) {
    return false;
  }

  VLOG(notifications) << "Do flush " << pending_notifications.size() << " pending notifications in " << group_key
                      << " with known " << group.notifications.size() << " from total of " << group.total_count
                      << " notifications";

  size_t old_notification_count = group.notifications.size();
  size_t shown_notification_count = min(old_notification_count, max_notification_group_size_);

  bool force_update = false;
  vector<td_api::object_ptr<td_api::notification>> added_notifications;
  added_notifications.reserve(pending_notifications.size());
  for (auto &pending_notification : pending_notifications) {
    Notification notification(pending_notification.notification_id, pending_notification.date,
                              pending_notification.initial_is_silent, std::move(pending_notification.type));
    added_notifications.push_back(get_notification_object(group_key.dialog_id, notification));
    CHECK(added_notifications.back()->type_ != nullptr);

    if (!notification.type->can_be_delayed()) {
      force_update = true;
    }
    group.notifications.push_back(std::move(notification));
  }
  group.total_count += narrow_cast<int32>(added_notifications.size());
  if (added_notifications.size() > max_notification_group_size_) {
    added_notifications.erase(added_notifications.begin(), added_notifications.end() - max_notification_group_size_);
  }

  vector<int32> removed_notification_ids;
  if (shown_notification_count + added_notifications.size() > max_notification_group_size_) {
    auto removed_notification_count =
        shown_notification_count + added_notifications.size() - max_notification_group_size_;
    removed_notification_ids.reserve(removed_notification_count);
    for (size_t i = 0; i < removed_notification_count; i++) {
      removed_notification_ids.push_back(
          group.notifications[old_notification_count - shown_notification_count + i].notification_id.get());
    }
  }

  if (!added_notifications.empty()) {
    add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
        group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(),
        pending_notifications[0].settings_dialog_id.get(), pending_notifications[0].is_silent, group.total_count,
        std::move(added_notifications), std::move(removed_notification_ids)));
  } else {
    CHECK(removed_notification_ids.empty());
  }
  pending_notifications.clear();
  return force_update;
}

td_api::object_ptr<td_api::updateNotificationGroup> NotificationManager::get_remove_group_update(
    const NotificationGroupKey &group_key, const NotificationGroup &group,
    vector<int32> &&removed_notification_ids) const {
  auto total_size = group.notifications.size();
  CHECK(removed_notification_ids.size() <= max_notification_group_size_);
  auto removed_size = min(total_size, max_notification_group_size_ - removed_notification_ids.size());
  removed_notification_ids.reserve(removed_size + removed_notification_ids.size());
  for (size_t i = total_size - removed_size; i < total_size; i++) {
    removed_notification_ids.push_back(group.notifications[i].notification_id.get());
  }

  if (removed_notification_ids.empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::updateNotificationGroup>(
      group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(),
      group_key.dialog_id.get(), true, group.total_count, vector<td_api::object_ptr<td_api::notification>>(),
      std::move(removed_notification_ids));
}

void NotificationManager::send_remove_group_update(const NotificationGroupKey &group_key,
                                                   const NotificationGroup &group,
                                                   vector<int32> &&removed_notification_ids) {
  VLOG(notifications) << "Remove " << group_key.group_id;
  auto update = get_remove_group_update(group_key, group, std::move(removed_notification_ids));
  if (update != nullptr) {
    add_update_notification_group(std::move(update));
  }
}

void NotificationManager::send_add_group_update(const NotificationGroupKey &group_key, const NotificationGroup &group) {
  VLOG(notifications) << "Add " << group_key.group_id;
  auto total_size = group.notifications.size();
  auto added_size = min(total_size, max_notification_group_size_);
  vector<td_api::object_ptr<td_api::notification>> added_notifications;
  added_notifications.reserve(added_size);
  for (size_t i = total_size - added_size; i < total_size; i++) {
    added_notifications.push_back(get_notification_object(group_key.dialog_id, group.notifications[i]));
    if (added_notifications.back()->type_ == nullptr) {
      added_notifications.pop_back();
    }
  }

  if (!added_notifications.empty()) {
    add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
        group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(), 0, true,
        group.total_count, std::move(added_notifications), vector<int32>()));
  }
}

void NotificationManager::flush_pending_notifications(NotificationGroupId group_id) {
  auto group_it = get_group(group_id);
  if (group_it == groups_.end()) {
    return;
  }

  td::remove_if(group_it->second.pending_notifications,
                [dialog_id = group_it->first.dialog_id](const PendingNotification &pending_notification) {
                  return pending_notification.type->get_notification_type_object(dialog_id) == nullptr;
                });

  if (group_it->second.pending_notifications.empty()) {
    return;
  }

  auto group_key = group_it->first;
  auto group = std::move(group_it->second);

  delete_group(std::move(group_it));

  auto final_group_key = group_key;
  for (auto &pending_notification : group.pending_notifications) {
    if (pending_notification.date >= final_group_key.last_notification_date) {
      final_group_key.last_notification_date = pending_notification.date;
    }
  }
  CHECK(final_group_key.last_notification_date != 0);

  VLOG(notifications) << "Flush pending notifications in " << group_key << " up to "
                      << final_group_key.last_notification_date;

  auto last_group_key = get_last_updated_group_key();
  bool was_updated = group_key.last_notification_date != 0 && group_key < last_group_key;
  bool is_updated = final_group_key < last_group_key;
  bool force_update = false;

  NotificationGroupId removed_group_id;
  if (!is_updated) {
    CHECK(!was_updated);
    VLOG(notifications) << "There is no need to send updateNotificationGroup in " << group_key
                        << ", because of newer notification groups";
    group.total_count += narrow_cast<int32>(group.pending_notifications.size());
    for (auto &pending_notification : group.pending_notifications) {
      group.notifications.emplace_back(pending_notification.notification_id, pending_notification.date,
                                       pending_notification.initial_is_silent, std::move(pending_notification.type));
    }
  } else {
    if (!was_updated) {
      if (last_group_key.last_notification_date != 0) {
        // need to remove last notification group to not exceed max_notification_group_count_
        removed_group_id = last_group_key.group_id;
        send_remove_group_update(last_group_key, groups_[last_group_key], vector<int32>());
      }
      send_add_group_update(group_key, group);
    }

    DialogId notification_settings_dialog_id;
    bool is_silent = false;

    // split notifications by groups with common settings
    vector<PendingNotification> grouped_notifications;
    for (auto &pending_notification : group.pending_notifications) {
      if (notification_settings_dialog_id != pending_notification.settings_dialog_id ||
          is_silent != pending_notification.is_silent) {
        if (do_flush_pending_notifications(group_key, group, grouped_notifications)) {
          force_update = true;
        }
        notification_settings_dialog_id = pending_notification.settings_dialog_id;
        is_silent = pending_notification.is_silent;
      }
      grouped_notifications.push_back(std::move(pending_notification));
    }
    if (do_flush_pending_notifications(group_key, group, grouped_notifications)) {
      force_update = true;
    }
  }

  group.pending_notifications_flush_time = 0;
  group.pending_notifications.clear();
  on_delayed_notification_update_count_changed(-1, group_id.get(), "flush_pending_notifications");
  // if we can delete a lot of notifications simultaneously
  if (group.notifications.size() > keep_notification_group_size_ + EXTRA_GROUP_SIZE &&
      group.type != NotificationGroupType::Calls) {
    // keep only keep_notification_group_size_ last notifications in memory
    for (auto it = group.notifications.begin(); it != group.notifications.end() - keep_notification_group_size_; ++it) {
      on_notification_removed(it->notification_id);
    }
    group.notifications.erase(group.notifications.begin(), group.notifications.end() - keep_notification_group_size_);
    group.is_loaded_from_database = false;
  }

  add_group(std::move(final_group_key), std::move(group), "flush_pending_notifications");

  if (force_update) {
    if (removed_group_id.is_valid()) {
      force_flush_pending_updates(removed_group_id, "flush_pending_notifications 1");
    }
    force_flush_pending_updates(group_key.group_id, "flush_pending_notifications 2");
  }
}

void NotificationManager::flush_all_pending_notifications() {
  std::multimap<int32, NotificationGroupId> group_ids;
  for (auto &group_it : groups_) {
    if (!group_it.second.pending_notifications.empty()) {
      group_ids.emplace(group_it.second.pending_notifications.back().date, group_it.first.group_id);
    }
  }

  // flush groups in order of last notification date
  VLOG(notifications) << "Flush pending notifications in " << group_ids.size() << " notification groups";
  for (auto &it : group_ids) {
    flush_pending_notifications_timeout_.cancel_timeout(it.second.get());
    flush_pending_notifications(it.second);
  }
}

void NotificationManager::edit_notification(NotificationGroupId group_id, NotificationId notification_id,
                                            unique_ptr<NotificationType> type) {
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }
  if (!group_id.is_valid()) {
    return;
  }

  CHECK(notification_id.is_valid());
  CHECK(type != nullptr);
  VLOG(notifications) << "Edit " << notification_id << ": " << *type;

  auto group_it = get_group(group_id);
  if (group_it == groups_.end()) {
    return;
  }
  auto &group = group_it->second;
  for (size_t i = 0; i < group.notifications.size(); i++) {
    auto &notification = group.notifications[i];
    if (notification.notification_id == notification_id) {
      if (notification.type->get_message_id() != type->get_message_id() ||
          notification.type->is_temporary() != type->is_temporary()) {
        LOG(ERROR) << "Ignore edit of " << notification_id << " with " << *type << ", because previous type is "
                   << *notification.type;
        return;
      }

      notification.type = std::move(type);
      if (i + max_notification_group_size_ >= group.notifications.size() &&
          !(get_last_updated_group_key() < group_it->first)) {
        CHECK(group_it->first.last_notification_date != 0);
        add_update_notification(group_it->first.group_id, group_it->first.dialog_id, notification);
      }
      return;
    }
  }
  for (auto &notification : group.pending_notifications) {
    if (notification.notification_id == notification_id) {
      if (notification.type->get_message_id() != type->get_message_id() ||
          notification.type->is_temporary() != type->is_temporary()) {
        LOG(ERROR) << "Ignore edit of " << notification_id << " with " << *type << ", because previous type is "
                   << *notification.type;
        return;
      }

      notification.type = std::move(type);
      return;
    }
  }
}

void NotificationManager::on_notification_processed(NotificationId notification_id) {
  auto promise_it = push_notification_promises_.find(notification_id);
  if (promise_it != push_notification_promises_.end()) {
    auto promises = std::move(promise_it->second);
    push_notification_promises_.erase(promise_it);

    for (auto &promise : promises) {
      promise.set_value(Unit());
    }
  }
}

void NotificationManager::on_notification_removed(NotificationId notification_id) {
  VLOG(notifications) << "In on_notification_removed with " << notification_id;

  auto add_it = temporary_notification_logevent_ids_.find(notification_id);
  if (add_it == temporary_notification_logevent_ids_.end()) {
    return;
  }

  auto edit_it = temporary_edit_notification_logevent_ids_.find(notification_id);
  if (edit_it != temporary_edit_notification_logevent_ids_.end()) {
    VLOG(notifications) << "Remove from binlog edit of " << notification_id << " with logevent " << edit_it->second;
    if (!is_being_destroyed_) {
      binlog_erase(G()->td_db()->get_binlog(), edit_it->second);
    }
    temporary_edit_notification_logevent_ids_.erase(edit_it);
  }

  VLOG(notifications) << "Remove from binlog " << notification_id << " with logevent " << add_it->second;
  if (!is_being_destroyed_) {
    binlog_erase(G()->td_db()->get_binlog(), add_it->second);
  }
  temporary_notification_logevent_ids_.erase(add_it);

  auto erased_notification_count = temporary_notifications_.erase(temporary_notification_message_ids_[notification_id]);
  auto erased_message_id_count = temporary_notification_message_ids_.erase(notification_id);
  CHECK(erased_notification_count > 0);
  CHECK(erased_message_id_count > 0);

  on_notification_processed(notification_id);
}

void NotificationManager::on_notifications_removed(
    NotificationGroups::iterator &&group_it, vector<td_api::object_ptr<td_api::notification>> &&added_notifications,
    vector<int32> &&removed_notification_ids, bool force_update) {
  VLOG(notifications) << "In on_notifications_removed for " << group_it->first.group_id << " with "
                      << added_notifications.size() << " added notifications and " << removed_notification_ids.size()
                      << " removed notifications, new total_count = " << group_it->second.total_count;
  auto group_key = group_it->first;
  auto final_group_key = group_key;
  final_group_key.last_notification_date = 0;
  for (auto &notification : group_it->second.notifications) {
    if (notification.date > final_group_key.last_notification_date) {
      final_group_key.last_notification_date = notification.date;
    }
  }

  bool is_position_changed = final_group_key.last_notification_date != group_key.last_notification_date;

  NotificationGroup group = std::move(group_it->second);
  if (is_position_changed) {
    VLOG(notifications) << "Position of notification group is changed from " << group_key << " to " << final_group_key;
    delete_group(std::move(group_it));
  }

  auto last_group_key = get_last_updated_group_key();
  bool was_updated = false;
  bool is_updated = false;
  if (is_position_changed) {
    was_updated = group_key.last_notification_date != 0 && group_key < last_group_key;
    is_updated = final_group_key.last_notification_date != 0 && final_group_key < last_group_key;
  } else {
    was_updated = is_updated = group_key.last_notification_date != 0 && !(last_group_key < group_key);
  }

  if (!was_updated) {
    CHECK(!is_updated);
    if (final_group_key.last_notification_date == 0 && group.total_count == 0) {
      // send update about empty invisible group anyway
      add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
          group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(), 0, true,
          0, vector<td_api::object_ptr<td_api::notification>>(), vector<int32>()));
    } else {
      VLOG(notifications) << "There is no need to send updateNotificationGroup about " << group_key.group_id;
    }
  } else {
    if (is_updated) {
      // group is still visible
      add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
          group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(), 0, true,
          group.total_count, std::move(added_notifications), std::move(removed_notification_ids)));
    } else {
      // group needs to be removed
      send_remove_group_update(group_key, group, std::move(removed_notification_ids));
      if (last_group_key.last_notification_date != 0) {
        // need to add new last notification group
        send_add_group_update(last_group_key, groups_[last_group_key]);
      }
    }
  }

  if (is_position_changed) {
    add_group(std::move(final_group_key), std::move(group), "on_notifications_removed");

    last_group_key = get_last_updated_group_key();
  } else {
    CHECK(group_it->first.last_notification_date == 0 || !group.notifications.empty());
    group_it->second = std::move(group);
  }

  if (force_update) {
    force_flush_pending_updates(group_key.group_id, "on_notifications_removed");
  }

  if (last_loaded_notification_group_key_ < last_group_key) {
    load_message_notification_groups_from_database(td::max(static_cast<int32>(max_notification_group_count_), 10) / 2,
                                                   true);
  }
}

void NotificationManager::remove_added_notifications_from_pending_updates(
    NotificationGroupId group_id,
    std::function<bool(const td_api::object_ptr<td_api::notification> &notification)> is_removed) {
  auto it = pending_updates_.find(group_id.get());
  if (it == pending_updates_.end()) {
    return;
  }

  std::unordered_set<int32> removed_notification_ids;
  for (auto &update : it->second) {
    if (update == nullptr) {
      continue;
    }
    if (update->get_id() == td_api::updateNotificationGroup::ID) {
      auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());
      if (!removed_notification_ids.empty() && !update_ptr->removed_notification_ids_.empty()) {
        td::remove_if(update_ptr->removed_notification_ids_, [&removed_notification_ids](auto &notification_id) {
          return removed_notification_ids.count(notification_id) == 1;
        });
      }
      for (auto &notification : update_ptr->added_notifications_) {
        if (is_removed(notification)) {
          removed_notification_ids.insert(notification->id_);
          VLOG(notifications) << "Remove " << NotificationId(notification->id_) << " in " << group_id;
          notification = nullptr;
        }
      }
      td::remove_if(update_ptr->added_notifications_, [](auto &notification) { return notification == nullptr; });
    } else {
      CHECK(update->get_id() == td_api::updateNotification::ID);
      auto update_ptr = static_cast<td_api::updateNotification *>(update.get());
      if (is_removed(update_ptr->notification_)) {
        removed_notification_ids.insert(update_ptr->notification_->id_);
        VLOG(notifications) << "Remove " << NotificationId(update_ptr->notification_->id_) << " in " << group_id;
        update = nullptr;
      }
    }
  }
}

void NotificationManager::remove_notification(NotificationGroupId group_id, NotificationId notification_id,
                                              bool is_permanent, bool force_update, Promise<Unit> &&promise,
                                              const char *source) {
  if (!group_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification group identifier is invalid"));
  }
  if (!notification_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }

  if (is_disabled() || max_notification_group_count_ == 0) {
    return promise.set_value(Unit());
  }

  VLOG(notifications) << "Remove " << notification_id << " from " << group_id << " with is_permanent = " << is_permanent
                      << ", force_update = " << force_update << " from " << source;

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    return promise.set_value(Unit());
  }

  if (!is_permanent && group_it->second.type != NotificationGroupType::Calls) {
    td_->messages_manager_->remove_message_notification(group_it->first.dialog_id, group_id, notification_id);
  }

  for (auto it = group_it->second.pending_notifications.begin(); it != group_it->second.pending_notifications.end();
       ++it) {
    if (it->notification_id == notification_id) {
      // notification is still pending, just delete it
      on_notification_removed(notification_id);
      group_it->second.pending_notifications.erase(it);
      if (group_it->second.pending_notifications.empty()) {
        group_it->second.pending_notifications_flush_time = 0;
        flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
        on_delayed_notification_update_count_changed(-1, group_id.get(), "remove_notification");
      }
      return promise.set_value(Unit());
    }
  }

  bool is_found = false;
  auto old_group_size = group_it->second.notifications.size();
  size_t notification_pos = old_group_size;
  for (size_t pos = 0; pos < notification_pos; pos++) {
    if (group_it->second.notifications[pos].notification_id == notification_id) {
      on_notification_removed(notification_id);
      notification_pos = pos;
      is_found = true;
    }
  }

  bool have_all_notifications = group_it->second.type == NotificationGroupType::Calls ||
                                group_it->second.type == NotificationGroupType::SecretChat;
  bool is_total_count_changed = false;
  if ((!have_all_notifications && is_permanent) || (have_all_notifications && is_found)) {
    if (group_it->second.total_count == 0) {
      LOG(ERROR) << "Total notification count became negative in " << group_id << " after removing " << notification_id;
    } else {
      group_it->second.total_count--;
      is_total_count_changed = true;
    }
  }
  if (is_found) {
    group_it->second.notifications.erase(group_it->second.notifications.begin() + notification_pos);
  }

  vector<td_api::object_ptr<td_api::notification>> added_notifications;
  vector<int32> removed_notification_ids;
  CHECK(max_notification_group_size_ > 0);
  if (is_found && notification_pos + max_notification_group_size_ >= old_group_size) {
    removed_notification_ids.push_back(notification_id.get());
    if (old_group_size >= max_notification_group_size_ + 1) {
      added_notifications.push_back(
          get_notification_object(group_it->first.dialog_id,
                                  group_it->second.notifications[old_group_size - max_notification_group_size_ - 1]));
      if (added_notifications.back()->type_ == nullptr) {
        added_notifications.pop_back();
      }
    }
    if (added_notifications.empty() && max_notification_group_size_ > group_it->second.notifications.size()) {
      load_message_notifications_from_database(group_it->first, group_it->second, keep_notification_group_size_);
    }
  }

  if (is_total_count_changed || !removed_notification_ids.empty()) {
    on_notifications_removed(std::move(group_it), std::move(added_notifications), std::move(removed_notification_ids),
                             force_update);
  }

  remove_added_notifications_from_pending_updates(
      group_id, [notification_id](const td_api::object_ptr<td_api::notification> &notification) {
        return notification->id_ == notification_id.get();
      });

  promise.set_value(Unit());
}

void NotificationManager::remove_temporary_notification_by_message_id(NotificationGroupId group_id,
                                                                      MessageId message_id, bool force_update,
                                                                      const char *source) {
  if (!group_id.is_valid()) {
    return;
  }

  VLOG(notifications) << "Remove notification for " << message_id << " in " << group_id << " from " << source;
  CHECK(message_id.is_valid());

  auto group_it = get_group(group_id);
  if (group_it == groups_.end()) {
    return;
  }

  auto remove_notification_by_message_id = [&](auto &notifications) {
    for (auto &notification : notifications) {
      if (notification.type->get_message_id() == message_id) {
        for (auto file_id : notification.type->get_file_ids(td_)) {
          this->td_->file_manager_->delete_file(file_id, Promise<>(), "remove_temporary_notification_by_message_id");
        }
        return this->remove_notification(group_id, notification.notification_id, true, force_update, Auto(),
                                         "remove_temporary_notification_by_message_id");
      }
    }
  };

  remove_notification_by_message_id(group_it->second.pending_notifications);
  remove_notification_by_message_id(group_it->second.notifications);
}

void NotificationManager::remove_notification_group(NotificationGroupId group_id, NotificationId max_notification_id,
                                                    MessageId max_message_id, int32 new_total_count, bool force_update,
                                                    Promise<Unit> &&promise) {
  if (!group_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Group identifier is invalid"));
  }
  if (!max_notification_id.is_valid() && !max_message_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }

  if (is_disabled() || max_notification_group_count_ == 0) {
    return promise.set_value(Unit());
  }

  if (new_total_count == 0) {
    remove_temporary_notifications(group_id, "remove_notification_group");
  }

  VLOG(notifications) << "Remove " << group_id << " up to " << max_notification_id << " or " << max_message_id
                      << " with new_total_count = " << new_total_count << " and force_update = " << force_update;

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    VLOG(notifications) << "Can't find " << group_id;
    return promise.set_value(Unit());
  }

  if (max_notification_id.is_valid()) {
    if (max_notification_id.get() > current_notification_id_.get()) {
      max_notification_id = current_notification_id_;
    }
    if (group_it->second.type != NotificationGroupType::Calls) {
      td_->messages_manager_->remove_message_notifications(
          group_it->first.dialog_id, group_id, max_notification_id,
          get_last_message_id_by_notification_id(group_it->second, max_notification_id));
    }
  }

  auto pending_delete_end = group_it->second.pending_notifications.begin();
  for (auto it = group_it->second.pending_notifications.begin(); it != group_it->second.pending_notifications.end();
       ++it) {
    if (it->notification_id.get() <= max_notification_id.get() ||
        (max_message_id.is_valid() && it->type->get_message_id() <= max_message_id)) {
      pending_delete_end = it + 1;
      on_notification_removed(it->notification_id);
    }
  }
  if (pending_delete_end != group_it->second.pending_notifications.begin()) {
    group_it->second.pending_notifications.erase(group_it->second.pending_notifications.begin(), pending_delete_end);
    if (group_it->second.pending_notifications.empty()) {
      group_it->second.pending_notifications_flush_time = 0;
      flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
      on_delayed_notification_update_count_changed(-1, group_id.get(), "remove_notification_group");
    }
  }
  if (new_total_count != -1) {
    new_total_count += get_temporary_notification_total_count(group_it->second);
    new_total_count -= static_cast<int32>(group_it->second.pending_notifications.size());
    if (new_total_count < 0) {
      LOG(ERROR) << "Have wrong new_total_count " << new_total_count << " + "
                 << group_it->second.pending_notifications.size();
    }
  }

  auto old_group_size = group_it->second.notifications.size();
  auto notification_delete_end = old_group_size;
  for (size_t pos = 0; pos < notification_delete_end; pos++) {
    auto &notification = group_it->second.notifications[pos];
    if (notification.notification_id.get() > max_notification_id.get() &&
        (!max_message_id.is_valid() || notification.type->get_message_id() > max_message_id)) {
      notification_delete_end = pos;
    } else {
      on_notification_removed(notification.notification_id);
    }
  }

  bool is_found = notification_delete_end != 0;

  vector<int32> removed_notification_ids;
  if (is_found && notification_delete_end + max_notification_group_size_ > old_group_size) {
    for (size_t i = old_group_size >= max_notification_group_size_ ? old_group_size - max_notification_group_size_ : 0;
         i < notification_delete_end; i++) {
      removed_notification_ids.push_back(group_it->second.notifications[i].notification_id.get());
    }
  }

  VLOG(notifications) << "Need to delete " << notification_delete_end << " from "
                      << group_it->second.notifications.size() << " notifications";
  if (is_found) {
    group_it->second.notifications.erase(group_it->second.notifications.begin(),
                                         group_it->second.notifications.begin() + notification_delete_end);
  }
  if (group_it->second.type == NotificationGroupType::Calls ||
      group_it->second.type == NotificationGroupType::SecretChat) {
    new_total_count = static_cast<int32>(group_it->second.notifications.size());
  }
  if (group_it->second.total_count == new_total_count) {
    new_total_count = -1;
  }
  if (new_total_count != -1) {
    group_it->second.total_count = new_total_count;
  }

  if (new_total_count != -1 || !removed_notification_ids.empty()) {
    on_notifications_removed(std::move(group_it), vector<td_api::object_ptr<td_api::notification>>(),
                             std::move(removed_notification_ids), force_update);
  } else {
    VLOG(notifications) << "Have new_total_count = " << new_total_count << ", " << removed_notification_ids.size()
                        << " removed notifications and force_update = " << force_update;
    if (force_update) {
      force_flush_pending_updates(group_id, "remove_notification_group");
    }
  }

  if (max_notification_id.is_valid()) {
    remove_added_notifications_from_pending_updates(
        group_id, [max_notification_id](const td_api::object_ptr<td_api::notification> &notification) {
          return notification->id_ <= max_notification_id.get();
        });
  } else {
    remove_added_notifications_from_pending_updates(
        group_id, [max_message_id](const td_api::object_ptr<td_api::notification> &notification) {
          return notification->type_->get_id() == td_api::notificationTypeNewMessage::ID &&
                 static_cast<const td_api::notificationTypeNewMessage *>(notification->type_.get())->message_->id_ <=
                     max_message_id.get();
        });
  }

  promise.set_value(Unit());
}

void NotificationManager::remove_temporary_notifications(NotificationGroupId group_id, const char *source) {
  CHECK(group_id.is_valid());

  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  auto group_it = get_group(group_id);
  if (group_it == groups_.end()) {
    return;
  }

  if (get_temporary_notification_total_count(group_it->second) == 0) {
    return;
  }

  VLOG(notifications) << "Remove temporary notifications in " << group_id << " from " << source;

  auto &group = group_it->second;
  while (!group.pending_notifications.empty() && group.pending_notifications.back().type->is_temporary()) {
    VLOG(notifications) << "Remove temporary " << group.pending_notifications.back() << " from " << group_id;
    // notification is still pending, just delete it
    on_notification_removed(group.pending_notifications.back().notification_id);
    group.pending_notifications.pop_back();
    if (group.pending_notifications.empty()) {
      group.pending_notifications_flush_time = 0;
      flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
      on_delayed_notification_update_count_changed(-1, group_id.get(), "remove_temporary_notifications");
    }
  }

  auto old_group_size = group.notifications.size();
  size_t notification_pos = old_group_size;
  for (size_t pos = 0; pos < notification_pos; pos++) {
    if (group.notifications[pos].type->is_temporary()) {
      notification_pos = pos;
    }
  }
  auto removed_notification_count = narrow_cast<int32>(old_group_size - notification_pos);
  if (removed_notification_count == 0) {
    CHECK(get_temporary_notification_total_count(group_it->second) == 0);
    return;
  }

  if (group.total_count < removed_notification_count) {
    LOG(ERROR) << "Total notification count became negative in " << group_id << " after removing "
               << removed_notification_count << " temporary notificaitions";
    group.total_count = 0;
  } else {
    group.total_count -= removed_notification_count;
  }

  vector<int32> removed_notification_ids;
  for (auto i = notification_pos; i < old_group_size; i++) {
    LOG_CHECK(group.notifications[i].type->is_temporary())
        << notification_pos << ' ' << i << ' ' << old_group_size << ' ' << removed_notification_count << ' '
        << group.notifications[i] << ' ' << group << ' ' << group_it->first;
    VLOG(notifications) << "Remove temporary " << group.notifications[i] << " from " << group_id;
    auto notification_id = group.notifications[i].notification_id;
    on_notification_removed(notification_id);
    if (i + max_notification_group_size_ >= old_group_size) {
      removed_notification_ids.push_back(notification_id.get());
    }
  }
  group.notifications.erase(group.notifications.begin() + notification_pos, group.notifications.end());
  CHECK(!removed_notification_ids.empty());

  vector<td_api::object_ptr<td_api::notification>> added_notifications;
  if (old_group_size >= max_notification_group_size_) {
    size_t added_notification_count = 0;
    for (size_t i = min(old_group_size - max_notification_group_size_, notification_pos);
         i-- > 0 && added_notification_count++ < removed_notification_ids.size();) {
      added_notifications.push_back(get_notification_object(group_it->first.dialog_id, group.notifications[i]));
      if (added_notifications.back()->type_ == nullptr) {
        added_notifications.pop_back();
      }
    }
    if (added_notification_count < removed_notification_ids.size() &&
        max_notification_group_size_ > group.notifications.size()) {
      load_message_notifications_from_database(group_it->first, group, keep_notification_group_size_);
    }
    std::reverse(added_notifications.begin(), added_notifications.end());
  }
  CHECK(get_temporary_notification_total_count(group_it->second) == 0);

  on_notifications_removed(std::move(group_it), std::move(added_notifications), std::move(removed_notification_ids),
                           false);

  remove_added_notifications_from_pending_updates(
      group_id, [](const td_api::object_ptr<td_api::notification> &notification) {
        return notification->get_id() == td_api::notificationTypeNewPushMessage::ID;
      });
}

int32 NotificationManager::get_temporary_notification_total_count(const NotificationGroup &group) {
  int32 result = 0;
  for (auto &notification : reversed(group.notifications)) {
    if (!notification.type->is_temporary()) {
      break;
    }
    result++;
  }
  for (auto &pending_notification : reversed(group.pending_notifications)) {
    if (!pending_notification.type->is_temporary()) {
      break;
    }
    result++;
  }
  return result;
}

void NotificationManager::set_notification_total_count(NotificationGroupId group_id, int32 new_total_count) {
  if (!group_id.is_valid()) {
    return;
  }
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    VLOG(notifications) << "Can't find " << group_id;
    return;
  }

  new_total_count += get_temporary_notification_total_count(group_it->second);
  new_total_count -= static_cast<int32>(group_it->second.pending_notifications.size());
  if (new_total_count < 0) {
    LOG(ERROR) << "Have wrong new_total_count " << new_total_count << " after removing "
               << group_it->second.pending_notifications.size() << " pending notifications";
    return;
  }
  if (new_total_count < static_cast<int32>(group_it->second.notifications.size())) {
    LOG(ERROR) << "Have wrong new_total_count " << new_total_count << " less than number of known notifications "
               << group_it->second.notifications.size();
    return;
  }

  CHECK(group_it->second.type != NotificationGroupType::Calls);
  if (group_it->second.total_count == new_total_count) {
    return;
  }

  VLOG(notifications) << "Set total_count in " << group_id << " to " << new_total_count;
  group_it->second.total_count = new_total_count;

  on_notifications_removed(std::move(group_it), vector<td_api::object_ptr<td_api::notification>>(), vector<int32>(),
                           false);
}

vector<MessageId> NotificationManager::get_notification_group_message_ids(NotificationGroupId group_id) {
  CHECK(group_id.is_valid());
  if (is_disabled() || max_notification_group_count_ == 0) {
    return {};
  }

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    return {};
  }

  vector<MessageId> message_ids;
  for (auto &notification : group_it->second.notifications) {
    auto message_id = notification.type->get_message_id();
    if (message_id.is_valid()) {
      message_ids.push_back(message_id);
    }
  }
  for (auto &notification : group_it->second.pending_notifications) {
    auto message_id = notification.type->get_message_id();
    if (message_id.is_valid()) {
      message_ids.push_back(message_id);
    }
  }

  return message_ids;
}

NotificationGroupId NotificationManager::get_call_notification_group_id(DialogId dialog_id) {
  auto it = dialog_id_to_call_notification_group_id_.find(dialog_id);
  if (it != dialog_id_to_call_notification_group_id_.end()) {
    return it->second;
  }

  if (available_call_notification_group_ids_.empty()) {
    // need to reserve new group_id for calls
    if (call_notification_group_ids_.size() >= MAX_CALL_NOTIFICATION_GROUPS) {
      return {};
    }
    NotificationGroupId last_group_id;
    if (!call_notification_group_ids_.empty()) {
      last_group_id = call_notification_group_ids_.back();
    }
    NotificationGroupId next_notification_group_id;
    do {
      next_notification_group_id = get_next_notification_group_id();
      if (!next_notification_group_id.is_valid()) {
        return {};
      }
    } while (last_group_id.get() >= next_notification_group_id.get());  // just in case
    VLOG(notifications) << "Add call " << next_notification_group_id;

    call_notification_group_ids_.push_back(next_notification_group_id);
    auto call_notification_group_ids_string = implode(
        transform(call_notification_group_ids_, [](NotificationGroupId group_id) { return to_string(group_id.get()); }),
        ',');
    G()->td_db()->get_binlog_pmc()->set("notification_call_group_ids", call_notification_group_ids_string);
    available_call_notification_group_ids_.insert(next_notification_group_id);
  }

  auto available_it = available_call_notification_group_ids_.begin();
  auto group_id = *available_it;
  available_call_notification_group_ids_.erase(available_it);
  dialog_id_to_call_notification_group_id_[dialog_id] = group_id;
  return group_id;
}

void NotificationManager::add_call_notification(DialogId dialog_id, CallId call_id) {
  CHECK(dialog_id.is_valid());
  CHECK(call_id.is_valid());
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  auto group_id = get_call_notification_group_id(dialog_id);
  if (!group_id.is_valid()) {
    VLOG(notifications) << "Ignore notification about " << call_id << " in " << dialog_id;
    return;
  }

  G()->td().get_actor_unsafe()->messages_manager_->force_create_dialog(dialog_id, "add_call_notification");

  auto &active_notifications = active_call_notifications_[dialog_id];
  if (active_notifications.size() >= MAX_CALL_NOTIFICATIONS) {
    VLOG(notifications) << "Ignore notification about " << call_id << " in " << dialog_id << " and " << group_id;
    return;
  }

  auto notification_id = get_next_notification_id();
  if (!notification_id.is_valid()) {
    return;
  }
  active_notifications.push_back(ActiveCallNotification{call_id, notification_id});

  add_notification(group_id, NotificationGroupType::Calls, dialog_id, G()->unix_time() + 120, dialog_id, false, false,
                   0, notification_id, create_new_call_notification(call_id), "add_call_notification");
}

void NotificationManager::remove_call_notification(DialogId dialog_id, CallId call_id) {
  CHECK(dialog_id.is_valid());
  CHECK(call_id.is_valid());
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  auto group_id_it = dialog_id_to_call_notification_group_id_.find(dialog_id);
  if (group_id_it == dialog_id_to_call_notification_group_id_.end()) {
    VLOG(notifications) << "Ignore removing notification about " << call_id << " in " << dialog_id;
    return;
  }
  auto group_id = group_id_it->second;
  CHECK(group_id.is_valid());

  auto &active_notifications = active_call_notifications_[dialog_id];
  for (auto it = active_notifications.begin(); it != active_notifications.end(); ++it) {
    if (it->call_id == call_id) {
      remove_notification(group_id, it->notification_id, true, true, Promise<Unit>(), "remove_call_notification");
      active_notifications.erase(it);
      if (active_notifications.empty()) {
        VLOG(notifications) << "Reuse call " << group_id;
        active_call_notifications_.erase(dialog_id);
        available_call_notification_group_ids_.insert(group_id);
        dialog_id_to_call_notification_group_id_.erase(dialog_id);

        flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
        flush_pending_notifications(group_id);
        force_flush_pending_updates(group_id, "reuse call group_id");

        auto group_it = get_group(group_id);
        LOG_CHECK(group_it->first.dialog_id == dialog_id)
            << group_id << ' ' << dialog_id << ' ' << group_it->first << ' ' << group_it->second;
        CHECK(group_it->first.last_notification_date == 0);
        CHECK(group_it->second.total_count == 0);
        CHECK(group_it->second.notifications.empty());
        CHECK(group_it->second.pending_notifications.empty());
        CHECK(group_it->second.type == NotificationGroupType::Calls);
        CHECK(!group_it->second.is_being_loaded_from_database);
        CHECK(pending_updates_.count(group_id.get()) == 0);
        delete_group(std::move(group_it));
      }
      return;
    }
  }

  VLOG(notifications) << "Failed to find " << call_id << " in " << dialog_id << " and " << group_id;
}

void NotificationManager::on_notification_group_count_max_changed(bool send_updates) {
  if (is_disabled()) {
    return;
  }

  auto new_max_notification_group_count =
      G()->shared_config().get_option_integer("notification_group_count_max", DEFAULT_GROUP_COUNT_MAX);
  CHECK(MIN_NOTIFICATION_GROUP_COUNT_MAX <= new_max_notification_group_count &&
        new_max_notification_group_count <= MAX_NOTIFICATION_GROUP_COUNT_MAX);

  auto new_max_notification_group_count_size_t = static_cast<size_t>(new_max_notification_group_count);
  if (new_max_notification_group_count_size_t == max_notification_group_count_) {
    return;
  }

  VLOG(notifications) << "Change max notification group count from " << max_notification_group_count_ << " to "
                      << new_max_notification_group_count;

  bool is_increased = new_max_notification_group_count_size_t > max_notification_group_count_;
  if (send_updates) {
    flush_all_notifications();

    size_t cur_pos = 0;
    size_t min_group_count = min(new_max_notification_group_count_size_t, max_notification_group_count_);
    size_t max_group_count = max(new_max_notification_group_count_size_t, max_notification_group_count_);
    for (auto it = groups_.begin(); it != groups_.end() && cur_pos < max_group_count; ++it, cur_pos++) {
      if (cur_pos < min_group_count) {
        continue;
      }

      auto &group_key = it->first;
      auto &group = it->second;
      CHECK(group.pending_notifications.empty());
      CHECK(pending_updates_.count(group_key.group_id.get()) == 0);

      if (group_key.last_notification_date == 0) {
        break;
      }

      if (is_increased) {
        send_add_group_update(group_key, group);
      } else {
        send_remove_group_update(group_key, group, vector<int32>());
      }
    }

    flush_all_pending_updates(true, "on_notification_group_size_max_changed end");

    if (new_max_notification_group_count == 0) {
      last_loaded_notification_group_key_ = NotificationGroupKey();
      last_loaded_notification_group_key_.last_notification_date = std::numeric_limits<int32>::max();
      CHECK(pending_updates_.empty());
      groups_.clear();
      group_keys_.clear();
    }
  }

  max_notification_group_count_ = new_max_notification_group_count_size_t;
  if (is_increased && last_loaded_notification_group_key_ < get_last_updated_group_key()) {
    load_message_notification_groups_from_database(td::max(new_max_notification_group_count, 5), true);
  }
}

void NotificationManager::on_notification_group_size_max_changed() {
  if (is_disabled()) {
    return;
  }

  auto new_max_notification_group_size =
      G()->shared_config().get_option_integer("notification_group_size_max", DEFAULT_GROUP_SIZE_MAX);
  CHECK(MIN_NOTIFICATION_GROUP_SIZE_MAX <= new_max_notification_group_size &&
        new_max_notification_group_size <= MAX_NOTIFICATION_GROUP_SIZE_MAX);

  auto new_max_notification_group_size_size_t = static_cast<size_t>(new_max_notification_group_size);
  if (new_max_notification_group_size_size_t == max_notification_group_size_) {
    return;
  }

  auto new_keep_notification_group_size =
      new_max_notification_group_size_size_t +
      clamp(new_max_notification_group_size_size_t, EXTRA_GROUP_SIZE / 2, EXTRA_GROUP_SIZE);

  VLOG(notifications) << "Change max notification group size from " << max_notification_group_size_ << " to "
                      << new_max_notification_group_size;

  if (max_notification_group_size_ != 0) {
    flush_all_notifications();

    size_t left = max_notification_group_count_;
    for (auto it = groups_.begin(); it != groups_.end() && left > 0; ++it, left--) {
      auto &group_key = it->first;
      auto &group = it->second;
      CHECK(group.pending_notifications.empty());
      CHECK(pending_updates_.count(group_key.group_id.get()) == 0);

      if (group_key.last_notification_date == 0) {
        break;
      }

      vector<td_api::object_ptr<td_api::notification>> added_notifications;
      vector<int32> removed_notification_ids;
      auto notification_count = group.notifications.size();
      if (new_max_notification_group_size_size_t < max_notification_group_size_) {
        if (notification_count <= new_max_notification_group_size_size_t) {
          VLOG(notifications) << "There is no need to update " << group_key.group_id;
          continue;
        }
        for (size_t i = notification_count - min(notification_count, max_notification_group_size_);
             i < notification_count - new_max_notification_group_size_size_t; i++) {
          removed_notification_ids.push_back(group.notifications[i].notification_id.get());
        }
        CHECK(!removed_notification_ids.empty());
      } else {
        if (new_max_notification_group_size_size_t > notification_count) {
          load_message_notifications_from_database(group_key, group, new_keep_notification_group_size);
        }
        if (notification_count <= max_notification_group_size_) {
          VLOG(notifications) << "There is no need to update " << group_key.group_id;
          continue;
        }
        for (size_t i = notification_count - min(notification_count, new_max_notification_group_size_size_t);
             i < notification_count - max_notification_group_size_; i++) {
          added_notifications.push_back(get_notification_object(group_key.dialog_id, group.notifications[i]));
          if (added_notifications.back()->type_ == nullptr) {
            added_notifications.pop_back();
          }
        }
        if (added_notifications.empty()) {
          continue;
        }
      }
      if (!is_destroyed_) {
        auto update = td_api::make_object<td_api::updateNotificationGroup>(
            group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(),
            group_key.dialog_id.get(), true, group.total_count, std::move(added_notifications),
            std::move(removed_notification_ids));
        VLOG(notifications) << "Send " << as_notification_update(update.get());
        send_closure(G()->td(), &Td::send_update, std::move(update));
      }
    }
  }

  max_notification_group_size_ = new_max_notification_group_size_size_t;
  keep_notification_group_size_ = new_keep_notification_group_size;
}

void NotificationManager::on_online_cloud_timeout_changed() {
  if (is_disabled()) {
    return;
  }

  online_cloud_timeout_ms_ =
      G()->shared_config().get_option_integer("online_cloud_timeout_ms", DEFAULT_ONLINE_CLOUD_TIMEOUT_MS);
  VLOG(notifications) << "Set online_cloud_timeout_ms to " << online_cloud_timeout_ms_;
}

void NotificationManager::on_notification_cloud_delay_changed() {
  if (is_disabled()) {
    return;
  }

  notification_cloud_delay_ms_ =
      G()->shared_config().get_option_integer("notification_cloud_delay_ms", DEFAULT_ONLINE_CLOUD_DELAY_MS);
  VLOG(notifications) << "Set notification_cloud_delay_ms to " << notification_cloud_delay_ms_;
}

void NotificationManager::on_notification_default_delay_changed() {
  if (is_disabled()) {
    return;
  }

  notification_default_delay_ms_ =
      G()->shared_config().get_option_integer("notification_default_delay_ms", DEFAULT_DEFAULT_DELAY_MS);
  VLOG(notifications) << "Set notification_default_delay_ms to " << notification_default_delay_ms_;
}

void NotificationManager::on_disable_contact_registered_notifications_changed() {
  if (is_disabled()) {
    return;
  }

  auto is_disabled = G()->shared_config().get_option_boolean("disable_contact_registered_notifications");

  if (is_disabled == disable_contact_registered_notifications_) {
    return;
  }

  disable_contact_registered_notifications_ = is_disabled;
  if (contact_registered_notifications_sync_state_ == SyncState::Completed) {
    run_contact_registered_notifications_sync();
  }
}

void NotificationManager::on_get_disable_contact_registered_notifications(bool is_disabled) {
  if (disable_contact_registered_notifications_ == is_disabled) {
    return;
  }
  disable_contact_registered_notifications_ = is_disabled;

  if (is_disabled) {
    G()->shared_config().set_option_boolean("disable_contact_registered_notifications", is_disabled);
  } else {
    G()->shared_config().set_option_empty("disable_contact_registered_notifications");
  }
}

void NotificationManager::set_contact_registered_notifications_sync_state(SyncState new_state) {
  if (is_disabled()) {
    return;
  }

  contact_registered_notifications_sync_state_ = new_state;
  string value;
  value += static_cast<char>(static_cast<int32>(new_state) + '0');
  value += static_cast<char>(static_cast<int32>(disable_contact_registered_notifications_) + '0');
  G()->td_db()->get_binlog_pmc()->set(get_is_contact_registered_notifications_synchronized_key(), value);
}

void NotificationManager::run_contact_registered_notifications_sync() {
  if (is_disabled()) {
    return;
  }

  auto is_disabled = disable_contact_registered_notifications_;
  if (contact_registered_notifications_sync_state_ == SyncState::NotSynced && !is_disabled) {
    set_contact_registered_notifications_sync_state(SyncState::Completed);
    return;
  }
  if (contact_registered_notifications_sync_state_ != SyncState::Pending) {
    set_contact_registered_notifications_sync_state(SyncState::Pending);
  }

  VLOG(notifications) << "Send SetContactSignUpNotificationQuery with " << is_disabled;
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), is_disabled](Result<Unit> result) {
    send_closure(actor_id, &NotificationManager::on_contact_registered_notifications_sync, is_disabled,
                 std::move(result));
  });
  td_->create_handler<SetContactSignUpNotificationQuery>(std::move(promise))->send(is_disabled);
}

void NotificationManager::on_contact_registered_notifications_sync(bool is_disabled, Result<Unit> result) {
  CHECK(contact_registered_notifications_sync_state_ == SyncState::Pending);
  if (is_disabled != disable_contact_registered_notifications_) {
    return run_contact_registered_notifications_sync();
  }
  if (result.is_ok()) {
    // everything is synchronized
    set_contact_registered_notifications_sync_state(SyncState::Completed);
  } else {
    // let's resend the query forever
    run_contact_registered_notifications_sync();
  }
}

void NotificationManager::get_disable_contact_registered_notifications(Promise<Unit> &&promise) {
  if (is_disabled()) {
    promise.set_value(Unit());
    return;
  }

  td_->create_handler<GetContactSignUpNotificationQuery>(std::move(promise))->send();
}

void NotificationManager::process_push_notification(string payload, Promise<Unit> &&user_promise) {
  auto promise = PromiseCreator::lambda([user_promise = std::move(user_promise)](Result<Unit> &&result) mutable {
    if (result.is_error()) {
      if (result.error().code() == 200) {
        user_promise.set_value(Unit());
      } else {
        user_promise.set_error(result.move_as_error());
      }
    } else {
      create_actor<SleepActor>("FinishProcessPushNotificationActor", 0.01, std::move(user_promise)).release();
    }
  });

  if (is_disabled() || payload == "{}") {
    return promise.set_error(Status::Error(200, "Immediate success"));
  }

  auto r_receiver_id = get_push_receiver_id(payload);
  if (r_receiver_id.is_error()) {
    VLOG(notifications) << "Failed to get push notification receiver from \"" << format::escaped(payload)
                        << "\":" << r_receiver_id.is_error();
    return promise.set_error(r_receiver_id.move_as_error());
  }

  auto receiver_id = r_receiver_id.move_as_ok();
  auto encryption_keys = td_->device_token_manager_->get_actor_unsafe()->get_encryption_keys();
  VLOG(notifications) << "Process push notification \"" << format::escaped(payload)
                      << "\" with receiver_id = " << receiver_id << " and " << encryption_keys.size()
                      << " encryption keys";
  bool was_encrypted = false;
  for (auto &key : encryption_keys) {
    VLOG(notifications) << "Have key " << key.first;
    // VLOG(notifications) << "Have key " << key.first << ": \"" << format::escaped(key.second) << '"';
    if (key.first == receiver_id) {
      if (!key.second.empty()) {
        auto r_payload = decrypt_push(key.first, key.second.str(), std::move(payload));
        if (r_payload.is_error()) {
          LOG(ERROR) << "Failed to decrypt push: " << r_payload.error();
          return promise.set_error(Status::Error(400, "Failed to decrypt push payload"));
        }
        payload = r_payload.move_as_ok();
        was_encrypted = true;
      }
      receiver_id = 0;
      break;
    }
  }

  if (!td_->is_online()) {
    // reset online flag to false to immediately check all connections aliveness
    send_closure(G()->state_manager(), &StateManager::on_online, false);
  }

  if (receiver_id == 0 || receiver_id == G()->get_my_id()) {
    auto status = process_push_notification_payload(payload, was_encrypted, promise);
    if (status.is_error()) {
      if (status.code() == 406 || status.code() == 200) {
        return promise.set_error(std::move(status));
      }

      LOG(ERROR) << "Receive error " << status << ", while parsing push payload " << payload;
      return promise.set_error(Status::Error(400, status.message()));
    }
    // promise will be set after updateNotificationGroup is sent to the client
    return;
  }

  VLOG(notifications) << "Failed to process push notification";
  promise.set_error(Status::Error(200, "Immediate success"));
}

string NotificationManager::convert_loc_key(const string &loc_key) {
  if (loc_key.size() <= 8) {
    if (loc_key == "MESSAGES" || loc_key == "ALBUM") {
      return "MESSAGES";
    }
    return string();
  }
  switch (loc_key[8]) {
    case 'A':
      if (loc_key == "PINNED_GAME") {
        return "PINNED_MESSAGE_GAME";
      }
      if (loc_key == "PINNED_GAME_SCORE") {
        return "PINNED_MESSAGE_GAME_SCORE";
      }
      if (loc_key == "CHAT_CREATED") {
        return "MESSAGE_BASIC_GROUP_CHAT_CREATE";
      }
      if (loc_key == "MESSAGE_AUDIO") {
        return "MESSAGE_VOICE_NOTE";
      }
      break;
    case 'C':
      if (loc_key == "MESSAGE_CONTACT") {
        return "MESSAGE_CONTACT";
      }
      break;
    case 'D':
      if (loc_key == "MESSAGE_DOC") {
        return "MESSAGE_DOCUMENT";
      }
      if (loc_key == "ENCRYPTED_MESSAGE") {
        return "MESSAGE";
      }
      break;
    case 'E':
      if (loc_key == "PINNED_GEO") {
        return "PINNED_MESSAGE_LOCATION";
      }
      if (loc_key == "PINNED_GEOLIVE") {
        return "PINNED_MESSAGE_LIVE_LOCATION";
      }
      if (loc_key == "CHAT_DELETE_MEMBER") {
        return "MESSAGE_CHAT_DELETE_MEMBER";
      }
      if (loc_key == "CHAT_DELETE_YOU") {
        return "MESSAGE_CHAT_DELETE_MEMBER_YOU";
      }
      if (loc_key == "PINNED_TEXT") {
        return "PINNED_MESSAGE_TEXT";
      }
      break;
    case 'F':
      if (loc_key == "MESSAGE_FWDS") {
        return "MESSAGE_FORWARDS";
      }
      break;
    case 'G':
      if (loc_key == "MESSAGE_GAME") {
        return "MESSAGE_GAME";
      }
      if (loc_key == "MESSAGE_GAME_SCORE") {
        return "MESSAGE_GAME_SCORE";
      }
      if (loc_key == "MESSAGE_GEO") {
        return "MESSAGE_LOCATION";
      }
      if (loc_key == "MESSAGE_GEOLIVE") {
        return "MESSAGE_LIVE_LOCATION";
      }
      if (loc_key == "MESSAGE_GIF") {
        return "MESSAGE_ANIMATION";
      }
      break;
    case 'H':
      if (loc_key == "PINNED_PHOTO") {
        return "PINNED_MESSAGE_PHOTO";
      }
      break;
    case 'I':
      if (loc_key == "PINNED_VIDEO") {
        return "PINNED_MESSAGE_VIDEO";
      }
      if (loc_key == "PINNED_GIF") {
        return "PINNED_MESSAGE_ANIMATION";
      }
      if (loc_key == "MESSAGE_INVOICE") {
        return "MESSAGE_INVOICE";
      }
      break;
    case 'J':
      if (loc_key == "CONTACT_JOINED") {
        return "MESSAGE_CONTACT_REGISTERED";
      }
      break;
    case 'L':
      if (loc_key == "CHAT_TITLE_EDITED") {
        return "MESSAGE_CHAT_CHANGE_TITLE";
      }
      break;
    case 'N':
      if (loc_key == "CHAT_JOINED") {
        return "MESSAGE_CHAT_JOIN_BY_LINK";
      }
      if (loc_key == "MESSAGE_NOTEXT") {
        return "MESSAGE";
      }
      if (loc_key == "PINNED_INVOICE") {
        return "PINNED_MESSAGE_INVOICE";
      }
      break;
    case 'O':
      if (loc_key == "PINNED_DOC") {
        return "PINNED_MESSAGE_DOCUMENT";
      }
      if (loc_key == "PINNED_POLL") {
        return "PINNED_MESSAGE_POLL";
      }
      if (loc_key == "PINNED_CONTACT") {
        return "PINNED_MESSAGE_CONTACT";
      }
      if (loc_key == "PINNED_NOTEXT") {
        return "PINNED_MESSAGE";
      }
      if (loc_key == "PINNED_ROUND") {
        return "PINNED_MESSAGE_VIDEO_NOTE";
      }
      break;
    case 'P':
      if (loc_key == "MESSAGE_PHOTO") {
        return "MESSAGE_PHOTO";
      }
      if (loc_key == "MESSAGE_PHOTOS") {
        return "MESSAGE_PHOTOS";
      }
      if (loc_key == "MESSAGE_PHOTO_SECRET") {
        return "MESSAGE_SECRET_PHOTO";
      }
      if (loc_key == "MESSAGE_POLL") {
        return "MESSAGE_POLL";
      }
      break;
    case 'Q':
      if (loc_key == "MESSAGE_QUIZ") {
        return "MESSAGE_QUIZ";
      }
      break;
    case 'R':
      if (loc_key == "MESSAGE_ROUND") {
        return "MESSAGE_VIDEO_NOTE";
      }
      break;
    case 'S':
      if (loc_key == "MESSAGE_SCREENSHOT") {
        return "MESSAGE_SCREENSHOT_TAKEN";
      }
      if (loc_key == "MESSAGE_STICKER") {
        return "MESSAGE_STICKER";
      }
      break;
    case 'T':
      if (loc_key == "CHAT_LEFT") {
        return "MESSAGE_CHAT_DELETE_MEMBER_LEFT";
      }
      if (loc_key == "MESSAGE_TEXT") {
        return "MESSAGE_TEXT";
      }
      if (loc_key == "PINNED_STICKER") {
        return "PINNED_MESSAGE_STICKER";
      }
      if (loc_key == "CHAT_PHOTO_EDITED") {
        return "MESSAGE_CHAT_CHANGE_PHOTO";
      }
      break;
    case 'U':
      if (loc_key == "PINNED_AUDIO") {
        return "PINNED_MESSAGE_VOICE_NOTE";
      }
      if (loc_key == "PINNED_QUIZ") {
        return "PINNED_MESSAGE_QUIZ";
      }
      if (loc_key == "CHAT_RETURNED") {
        return "MESSAGE_CHAT_ADD_MEMBERS_RETURNED";
      }
      break;
    case 'V':
      if (loc_key == "MESSAGE_VIDEO") {
        return "MESSAGE_VIDEO";
      }
      if (loc_key == "MESSAGE_VIDEOS") {
        return "MESSAGE_VIDEOS";
      }
      if (loc_key == "MESSAGE_VIDEO_SECRET") {
        return "MESSAGE_SECRET_VIDEO";
      }
      break;
    case '_':
      if (loc_key == "CHAT_ADD_MEMBER") {
        return "MESSAGE_CHAT_ADD_MEMBERS";
      }
      if (loc_key == "CHAT_ADD_YOU") {
        return "MESSAGE_CHAT_ADD_MEMBERS_YOU";
      }
      break;
  }
  return string();
}

Status NotificationManager::process_push_notification_payload(string payload, bool was_encrypted,
                                                              Promise<Unit> &promise) {
  VLOG(notifications) << "Process push notification payload " << payload;
  auto r_json_value = json_decode(payload);
  if (r_json_value.is_error()) {
    return Status::Error("Failed to parse payload as JSON object");
  }

  auto json_value = r_json_value.move_as_ok();
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected a JSON object as push payload");
  }

  auto data = std::move(json_value.get_object());
  int32 sent_date = G()->unix_time();
  if (has_json_object_field(data, "data")) {
    TRY_RESULT(date, get_json_object_int_field(data, "date", true, sent_date));
    if (sent_date - 28 * 86400 <= date && date <= sent_date + 5) {
      sent_date = date;
    }
    TRY_RESULT(data_data, get_json_object_field(data, "data", JsonValue::Type::Object, false));
    data = std::move(data_data.get_object());
  }

  string loc_key;
  JsonObject custom;
  string announcement_message_text;
  vector<string> loc_args;
  string sender_name;
  for (auto &field_value : data) {
    if (field_value.first == "loc_key") {
      if (field_value.second.type() != JsonValue::Type::String) {
        return Status::Error("Expected loc_key as a String");
      }
      loc_key = field_value.second.get_string().str();
    } else if (field_value.first == "loc_args") {
      if (field_value.second.type() != JsonValue::Type::Array) {
        return Status::Error("Expected loc_args as an Array");
      }
      loc_args.reserve(field_value.second.get_array().size());
      for (auto &arg : field_value.second.get_array()) {
        if (arg.type() != JsonValue::Type::String) {
          return Status::Error("Expected loc_arg as a String");
        }
        loc_args.push_back(arg.get_string().str());
      }
    } else if (field_value.first == "custom") {
      if (field_value.second.type() != JsonValue::Type::Object) {
        return Status::Error("Expected custom as an Object");
      }
      custom = std::move(field_value.second.get_object());
    } else if (field_value.first == "message") {
      if (field_value.second.type() != JsonValue::Type::String) {
        return Status::Error("Expected announcement message text as a String");
      }
      announcement_message_text = field_value.second.get_string().str();
    } else if (field_value.first == "google.sent_time") {
      TRY_RESULT(google_sent_time, get_json_object_long_field(data, "google.sent_time"));
      google_sent_time /= 1000;
      if (sent_date - 28 * 86400 <= google_sent_time && google_sent_time <= sent_date + 5) {
        sent_date = narrow_cast<int32>(google_sent_time);
      }
    }
  }

  if (!clean_input_string(loc_key)) {
    return Status::Error(PSLICE() << "Receive invalid loc_key " << format::escaped(loc_key));
  }
  if (loc_key.empty()) {
    return Status::Error("Receive empty loc_key");
  }
  for (auto &loc_arg : loc_args) {
    if (!clean_input_string(loc_arg)) {
      return Status::Error(PSLICE() << "Receive invalid loc_arg " << format::escaped(loc_arg));
    }
  }

  if (loc_key == "MESSAGE_ANNOUNCEMENT") {
    if (announcement_message_text.empty()) {
      return Status::Error("Have empty announcement message text");
    }
    TRY_RESULT(announcement_id, get_json_object_int_field(custom, "announcement"));
    auto &date = announcement_id_date_[announcement_id];
    auto now = G()->unix_time();
    if (date >= now - ANNOUNCEMENT_ID_CACHE_TIME) {
      VLOG(notifications) << "Ignore duplicate announcement " << announcement_id;
      return Status::Error(200, "Immediate success");
    }
    date = now;

    auto update = telegram_api::make_object<telegram_api::updateServiceNotification>(
        telegram_api::updateServiceNotification::INBOX_DATE_MASK, false, G()->unix_time(), string(),
        announcement_message_text, nullptr, vector<telegram_api::object_ptr<telegram_api::MessageEntity>>());
    send_closure(G()->messages_manager(), &MessagesManager::on_update_service_notification, std::move(update), false,
                 std::move(promise));
    save_announcement_ids();
    return Status::OK();
  }
  if (!announcement_message_text.empty()) {
    LOG(ERROR) << "Have non-empty announcement message text with loc_key = " << loc_key;
  }

  if (loc_key == "DC_UPDATE") {
    TRY_RESULT(dc_id, get_json_object_int_field(custom, "dc", false));
    TRY_RESULT(addr, get_json_object_string_field(custom, "addr", false));
    if (!DcId::is_valid(dc_id)) {
      return Status::Error("Invalid datacenter ID");
    }
    if (!clean_input_string(addr)) {
      return Status::Error(PSLICE() << "Receive invalid addr " << format::escaped(addr));
    }
    send_closure(G()->connection_creator(), &ConnectionCreator::on_dc_update, DcId::internal(dc_id), std::move(addr),
                 std::move(promise));
    return Status::OK();
  }

  if (loc_key == "SESSION_REVOKE") {
    if (was_encrypted) {
      send_closure(td_->auth_manager_actor_, &AuthManager::on_authorization_lost);
    } else {
      LOG(ERROR) << "Receive unencrypted SESSION_REVOKE push notification";
    }
    promise.set_value(Unit());
    return Status::OK();
  }

  if (loc_key == "LOCKED_MESSAGE") {
    return Status::Error(200, "Immediate success");
  }

  if (loc_key == "GEO_LIVE_PENDING") {
    td_->messages_manager_->on_update_some_live_location_viewed(std::move(promise));
    return Status::OK();
  }

  if (loc_key == "AUTH_REGION" || loc_key == "AUTH_UNKNOWN") {
    // TODO
    return Status::Error(200, "Immediate success");
  }

  DialogId dialog_id;
  if (has_json_object_field(custom, "from_id")) {
    TRY_RESULT(user_id_int, get_json_object_int_field(custom, "from_id"));
    UserId user_id(user_id_int);
    if (!user_id.is_valid()) {
      return Status::Error("Receive invalid user_id");
    }
    dialog_id = DialogId(user_id);
  }
  if (has_json_object_field(custom, "chat_id")) {
    TRY_RESULT(chat_id_int, get_json_object_int_field(custom, "chat_id"));
    ChatId chat_id(chat_id_int);
    if (!chat_id.is_valid()) {
      return Status::Error("Receive invalid chat_id");
    }
    dialog_id = DialogId(chat_id);
  }
  if (has_json_object_field(custom, "channel_id")) {
    TRY_RESULT(channel_id_int, get_json_object_int_field(custom, "channel_id"));
    ChannelId channel_id(channel_id_int);
    if (!channel_id.is_valid()) {
      return Status::Error("Receive invalid channel_id");
    }
    dialog_id = DialogId(channel_id);
  }
  if (has_json_object_field(custom, "encryption_id")) {
    TRY_RESULT(secret_chat_id_int, get_json_object_int_field(custom, "encryption_id"));
    SecretChatId secret_chat_id(secret_chat_id_int);
    if (!secret_chat_id.is_valid()) {
      return Status::Error("Receive invalid secret_chat_id");
    }
    dialog_id = DialogId(secret_chat_id);
  }
  if (!dialog_id.is_valid()) {
    // TODO if (loc_key == "ENCRYPTED_MESSAGE") ?
    return Status::Error("Can't find dialog_id");
  }

  if (loc_key == "READ_HISTORY") {
    if (dialog_id.get_type() == DialogType::SecretChat) {
      return Status::Error("Receive read history in a secret chat");
    }

    TRY_RESULT(max_id, get_json_object_int_field(custom, "max_id"));
    ServerMessageId max_server_message_id(max_id);
    if (!max_server_message_id.is_valid()) {
      return Status::Error("Receive invalid max_id");
    }

    td_->messages_manager_->read_history_inbox(dialog_id, MessageId(max_server_message_id), -1,
                                               "process_push_notification_payload");
    promise.set_value(Unit());
    return Status::OK();
  }

  if (loc_key == "MESSAGE_DELETED") {
    if (dialog_id.get_type() == DialogType::SecretChat) {
      return Status::Error("Receive MESSAGE_DELETED in a secret chat");
    }
    TRY_RESULT(server_message_ids_str, get_json_object_string_field(custom, "messages", false));
    auto server_message_ids = full_split(server_message_ids_str, ',');
    vector<MessageId> message_ids;
    for (const auto &server_message_id_str : server_message_ids) {
      TRY_RESULT(server_message_id_int, to_integer_safe<int32>(server_message_id_str));
      ServerMessageId server_message_id(server_message_id_int);
      if (!server_message_id.is_valid()) {
        return Status::Error("Receive invalid message_id");
      }
      message_ids.push_back(MessageId(server_message_id));
    }
    td_->messages_manager_->remove_message_notifications_by_message_ids(dialog_id, message_ids);
    promise.set_value(Unit());
    return Status::OK();
  }

  if (loc_key == "MESSAGE_MUTED") {
    return Status::Error(406, "Notifications about muted messages force loading data from the server");
  }

  TRY_RESULT(msg_id, get_json_object_int_field(custom, "msg_id"));
  ServerMessageId server_message_id(msg_id);
  if (server_message_id != ServerMessageId() && !server_message_id.is_valid()) {
    return Status::Error("Receive invalid msg_id");
  }

  TRY_RESULT(random_id, get_json_object_long_field(custom, "random_id"));

  UserId sender_user_id;
  if (has_json_object_field(custom, "chat_from_id")) {
    TRY_RESULT(sender_user_id_int, get_json_object_int_field(custom, "chat_from_id"));
    sender_user_id = UserId(sender_user_id_int);
    if (!sender_user_id.is_valid()) {
      return Status::Error("Receive invalid chat_from_id");
    }
  } else if (dialog_id.get_type() == DialogType::User) {
    sender_user_id = dialog_id.get_user_id();
  }

  TRY_RESULT(contains_mention_int, get_json_object_int_field(custom, "mention"));
  bool contains_mention = contains_mention_int != 0;

  if (begins_with(loc_key, "CHANNEL_MESSAGE") || loc_key == "CHANNEL_ALBUM") {
    if (dialog_id.get_type() != DialogType::Channel) {
      return Status::Error("Receive wrong chat type");
    }
    loc_key = loc_key.substr(8);
  }
  if (begins_with(loc_key, "CHAT_")) {
    auto dialog_type = dialog_id.get_type();
    if (dialog_type != DialogType::Chat && dialog_type != DialogType::Channel) {
      return Status::Error("Receive wrong chat type");
    }

    if (begins_with(loc_key, "CHAT_MESSAGE") || loc_key == "CHAT_ALBUM") {
      loc_key = loc_key.substr(5);
    }
    if (loc_args.empty()) {
      return Status::Error("Expect sender name as first argument");
    }
    sender_name = std::move(loc_args[0]);
    loc_args.erase(loc_args.begin());
  }
  if (begins_with(loc_key, "MESSAGE") && !server_message_id.is_valid()) {
    return Status::Error("Receive no message ID");
  }
  if (begins_with(loc_key, "ENCRYPT") || random_id != 0) {
    if (dialog_id.get_type() != DialogType::SecretChat) {
      return Status::Error("Receive wrong chat type");
    }
  }
  if (server_message_id.is_valid() && dialog_id.get_type() == DialogType::SecretChat) {
    return Status::Error("Receive message ID in secret chat push");
  }

  if (begins_with(loc_key, "ENCRYPTION_")) {
    // TODO ENCRYPTION_REQUEST/ENCRYPTION_ACCEPT notifications
    return Status::Error(406, "New secret chat notification is not supported");
  }

  if (begins_with(loc_key, "PHONE_CALL_")) {
    // TODO PHONE_CALL_REQUEST/PHONE_CALL_DECLINE/PHONE_CALL_MISSED notification
    return Status::Error(406, "Phone call notification is not supported");
  }

  loc_key = convert_loc_key(loc_key);
  if (loc_key.empty()) {
    return Status::Error("Push type is unknown");
  }

  if (loc_args.empty()) {
    return Status::Error("Expected chat name as next argument");
  }
  if (dialog_id.get_type() == DialogType::User) {
    sender_name = std::move(loc_args[0]);
  } else if (sender_user_id.is_valid() && begins_with(loc_key, "PINNED_")) {
    if (loc_args.size() < 2) {
      return Status::Error("Expected chat title as the last argument");
    }
    loc_args.pop_back();
  }
  // chat title for CHAT_*, CHANNEL_* and ENCRYPTED_MESSAGE, sender name for MESSAGE_* and CONTACT_JOINED
  // chat title or sender name for PINNED_*
  loc_args.erase(loc_args.begin());

  string arg;
  if (loc_key == "MESSAGE_GAME_SCORE") {
    if (loc_args.size() != 2) {
      return Status::Error("Expected 2 arguments for MESSAGE_GAME_SCORE");
    }
    TRY_RESULT(score, to_integer_safe<int32>(loc_args[1]));
    if (score < 0) {
      return Status::Error("Expected score to be non-negative");
    }
    arg = PSTRING() << loc_args[1] << ' ' << loc_args[0];
    loc_args.clear();
  }
  if (loc_args.size() > 1) {
    return Status::Error("Receive too much arguments");
  }

  if (loc_args.size() == 1) {
    arg = std::move(loc_args[0]);
  }

  if (sender_user_id.is_valid() && !td_->contacts_manager_->have_user_force(sender_user_id)) {
    int64 sender_access_hash = -1;
    telegram_api::object_ptr<telegram_api::UserProfilePhoto> sender_photo;
    TRY_RESULT(mtpeer, get_json_object_field(custom, "mtpeer", JsonValue::Type::Object));
    if (mtpeer.type() != JsonValue::Type::Null) {
      TRY_RESULT(ah, get_json_object_string_field(mtpeer.get_object(), "ah"));
      if (!ah.empty()) {
        TRY_RESULT_ASSIGN(sender_access_hash, to_integer_safe<int64>(ah));
      }
      TRY_RESULT(ph, get_json_object_field(mtpeer.get_object(), "ph", JsonValue::Type::Object));
      if (ph.type() != JsonValue::Type::Null) {
        // TODO parse sender photo
      }
    }

    int32 flags = telegram_api::user::FIRST_NAME_MASK | telegram_api::user::MIN_MASK;
    if (sender_access_hash != -1) {
      // set phone number flag to show that this is a full access hash
      flags |= telegram_api::user::ACCESS_HASH_MASK | telegram_api::user::PHONE_MASK;
    }
    if (sender_photo != nullptr) {
      flags |= telegram_api::user::PHOTO_MASK;
    }
    auto user = telegram_api::make_object<telegram_api::user>(
        flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
        false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
        false /*ignored*/, false /*ignored*/, false /*ignored*/, sender_user_id.get(), sender_access_hash, sender_name,
        string(), string(), string(), std::move(sender_photo), nullptr, 0, Auto(), string(), string());
    td_->contacts_manager_->on_get_user(std::move(user), "process_push_notification_payload");
  }

  Photo attached_photo;
  attached_photo.id = -2;
  Document attached_document;
  if (has_json_object_field(custom, "attachb64")) {
    TRY_RESULT(attachb64, get_json_object_string_field(custom, "attachb64", false));
    TRY_RESULT(attach, base64url_decode(attachb64));

    TlParser gzip_parser(attach);
    int32 id = gzip_parser.fetch_int();
    if (gzip_parser.get_error()) {
      return Status::Error(PSLICE() << "Failed to parse attach: " << gzip_parser.get_error());
    }
    BufferSlice buffer;
    if (id == mtproto_api::gzip_packed::ID) {
      mtproto_api::gzip_packed gzip(gzip_parser);
      gzip_parser.fetch_end();
      if (gzip_parser.get_error()) {
        return Status::Error(PSLICE() << "Failed to parse mtproto_api::gzip_packed in attach: "
                                      << gzip_parser.get_error());
      }
      buffer = gzdecode(gzip.packed_data_);
      if (buffer.empty()) {
        return Status::Error("Failed to uncompress attach");
      }
    } else {
      buffer = BufferSlice(attach);
    }

    TlBufferParser parser(&buffer);
    auto result = telegram_api::Object::fetch(parser);
    parser.fetch_end();
    const char *error = parser.get_error();
    if (error != nullptr) {
      LOG(ERROR) << "Can't parse attach: " << Slice(error) << " at " << parser.get_error_pos() << ": "
                 << format::as_hex_dump<4>(Slice(attach));
    } else {
      switch (result->get_id()) {
        case telegram_api::photo::ID:
          if (ends_with(loc_key, "MESSAGE_PHOTO") || ends_with(loc_key, "MESSAGE_TEXT")) {
            VLOG(notifications) << "Have attached photo";
            loc_key.resize(loc_key.rfind('_') + 1);
            loc_key += "PHOTO";
            attached_photo = get_photo(td_->file_manager_.get(),
                                       telegram_api::move_object_as<telegram_api::photo>(result), dialog_id);
          } else {
            LOG(ERROR) << "Receive attached photo for " << loc_key;
          }
          break;
        case telegram_api::document::ID: {
          if (ends_with(loc_key, "MESSAGE_ANIMATION") || ends_with(loc_key, "MESSAGE_AUDIO") ||
              ends_with(loc_key, "MESSAGE_DOCUMENT") || ends_with(loc_key, "MESSAGE_STICKER") ||
              ends_with(loc_key, "MESSAGE_VIDEO") || ends_with(loc_key, "MESSAGE_VIDEO_NOTE") ||
              ends_with(loc_key, "MESSAGE_VOICE_NOTE") || ends_with(loc_key, "MESSAGE_TEXT")) {
            VLOG(notifications) << "Have attached document";
            attached_document = td_->documents_manager_->on_get_document(
                telegram_api::move_object_as<telegram_api::document>(result), dialog_id);
            if (!attached_document.empty()) {
              if (ends_with(loc_key, "_NOTE")) {
                loc_key.resize(loc_key.rfind('_'));
              }
              loc_key.resize(loc_key.rfind('_') + 1);

              auto type = [attached_document] {
                switch (attached_document.type) {
                  case Document::Type::Animation:
                    return "ANIMATION";
                  case Document::Type::Audio:
                    return "AUDIO";
                  case Document::Type::General:
                    return "DOCUMENT";
                  case Document::Type::Sticker:
                    return "STICKER";
                  case Document::Type::Video:
                    return "VIDEO";
                  case Document::Type::VideoNote:
                    return "VIDEO_NOTE";
                  case Document::Type::VoiceNote:
                    return "VOICE_NOTE";
                  case Document::Type::Unknown:
                  default:
                    UNREACHABLE();
                    return "UNREACHABLE";
                }
              }();

              loc_key += type;
            }
          } else {
            LOG(ERROR) << "Receive attached document for " << loc_key;
          }
          break;
        }
        default:
          LOG(ERROR) << "Receive unexpected attached " << to_string(result);
      }
    }
  }
  if (!arg.empty()) {
    uint32 emoji = [&] {
      if (ends_with(loc_key, "PHOTO")) {
        return 0x1F5BC;
      }
      if (ends_with(loc_key, "ANIMATION")) {
        return 0x1F3AC;
      }
      if (ends_with(loc_key, "DOCUMENT")) {
        return 0x1F4CE;
      }
      if (ends_with(loc_key, "VIDEO")) {
        return 0x1F4F9;
      }
      return 0;
    }();
    if (emoji != 0) {
      string prefix;
      append_utf8_character(prefix, emoji);
      prefix += ' ';
      if (begins_with(arg, prefix)) {
        arg = arg.substr(prefix.size());
      }
    }
  }

  if (has_json_object_field(custom, "edit_date")) {
    if (random_id != 0) {
      return Status::Error("Receive edit of secret message");
    }
    TRY_RESULT(edit_date, get_json_object_int_field(custom, "edit_date"));
    if (edit_date <= 0) {
      return Status::Error("Receive wrong edit date");
    }
    edit_message_push_notification(dialog_id, MessageId(server_message_id), edit_date, std::move(loc_key),
                                   std::move(arg), std::move(attached_photo), std::move(attached_document), 0,
                                   std::move(promise));
  } else {
    bool is_from_scheduled = has_json_object_field(custom, "schedule");
    bool is_silent = has_json_object_field(custom, "silent");
    add_message_push_notification(dialog_id, MessageId(server_message_id), random_id, sender_user_id,
                                  std::move(sender_name), sent_date, is_from_scheduled, contains_mention, is_silent,
                                  is_silent, std::move(loc_key), std::move(arg), std::move(attached_photo),
                                  std::move(attached_document), NotificationId(), 0, std::move(promise));
  }
  return Status::OK();
}

class NotificationManager::AddMessagePushNotificationLogEvent {
 public:
  DialogId dialog_id_;
  MessageId message_id_;
  int64 random_id_;
  UserId sender_user_id_;
  string sender_name_;
  int32 date_;
  bool is_from_scheduled_;
  bool contains_mention_;
  bool is_silent_;
  string loc_key_;
  string arg_;
  Photo photo_;
  Document document_;
  NotificationId notification_id_;

  template <class StorerT>
  void store(StorerT &storer) const {
    bool has_message_id = message_id_.is_valid();
    bool has_random_id = random_id_ != 0;
    bool has_sender = sender_user_id_.is_valid();
    bool has_sender_name = !sender_name_.empty();
    bool has_arg = !arg_.empty();
    bool has_photo = photo_.id != -2;
    bool has_document = !document_.empty();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(contains_mention_);
    STORE_FLAG(is_silent_);
    STORE_FLAG(has_message_id);
    STORE_FLAG(has_random_id);
    STORE_FLAG(has_sender);
    STORE_FLAG(has_sender_name);
    STORE_FLAG(has_arg);
    STORE_FLAG(has_photo);
    STORE_FLAG(has_document);
    STORE_FLAG(is_from_scheduled_);
    END_STORE_FLAGS();
    td::store(dialog_id_, storer);
    if (has_message_id) {
      td::store(message_id_, storer);
    }
    if (has_random_id) {
      td::store(random_id_, storer);
    }
    if (has_sender) {
      td::store(sender_user_id_, storer);
    }
    if (has_sender_name) {
      td::store(sender_name_, storer);
    }
    td::store(date_, storer);
    td::store(loc_key_, storer);
    if (has_arg) {
      td::store(arg_, storer);
    }
    if (has_photo) {
      td::store(photo_, storer);
    }
    if (has_document) {
      td::store(document_, storer);
    }
    td::store(notification_id_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    bool has_message_id;
    bool has_random_id;
    bool has_sender;
    bool has_sender_name;
    bool has_arg;
    bool has_photo;
    bool has_document;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(contains_mention_);
    PARSE_FLAG(is_silent_);
    PARSE_FLAG(has_message_id);
    PARSE_FLAG(has_random_id);
    PARSE_FLAG(has_sender);
    PARSE_FLAG(has_sender_name);
    PARSE_FLAG(has_arg);
    PARSE_FLAG(has_photo);
    PARSE_FLAG(has_document);
    PARSE_FLAG(is_from_scheduled_);
    END_PARSE_FLAGS();
    td::parse(dialog_id_, parser);
    if (has_message_id) {
      td::parse(message_id_, parser);
    }
    if (has_random_id) {
      td::parse(random_id_, parser);
    } else {
      random_id_ = 0;
    }
    if (has_sender) {
      td::parse(sender_user_id_, parser);
    }
    if (has_sender_name) {
      td::parse(sender_name_, parser);
    }
    td::parse(date_, parser);
    td::parse(loc_key_, parser);
    if (has_arg) {
      td::parse(arg_, parser);
    }
    if (has_photo) {
      td::parse(photo_, parser);
    } else {
      photo_.id = -2;
    }
    if (has_document) {
      td::parse(document_, parser);
    }
    td::parse(notification_id_, parser);
  }
};

void NotificationManager::add_message_push_notification(
    DialogId dialog_id, MessageId message_id, int64 random_id, UserId sender_user_id, string sender_name, int32 date,
    bool is_from_scheduled, bool contains_mention, bool initial_is_silent, bool is_silent, string loc_key, string arg,
    Photo photo, Document document, NotificationId notification_id, uint64 logevent_id, Promise<Unit> promise) {
  auto is_pinned = begins_with(loc_key, "PINNED_");
  auto r_info = td_->messages_manager_->get_message_push_notification_info(
      dialog_id, message_id, random_id, sender_user_id, date, is_from_scheduled, contains_mention, is_pinned,
      logevent_id != 0);
  if (r_info.is_error()) {
    VLOG(notifications) << "Don't need message push notification for " << message_id << "/" << random_id << " from "
                        << dialog_id << " sent by " << sender_user_id << " at " << date << ": " << r_info.error();
    if (logevent_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), logevent_id);
    }
    if (r_info.error().code() == 406) {
      promise.set_error(r_info.move_as_error());
    } else {
      promise.set_error(Status::Error(200, "Immediate success"));
    }
    return;
  }

  auto info = r_info.move_as_ok();
  CHECK(info.group_id.is_valid());

  if (dialog_id.get_type() == DialogType::SecretChat) {
    VLOG(notifications) << "Skip notification in secret " << dialog_id;
    // TODO support secret chat notifications
    // main problem: there is no message_id yet
    // also don't forget to delete newSecretChat notification
    CHECK(logevent_id == 0);
    return promise.set_error(Status::Error(406, "Secret chat push notifications are unsupported"));
  }
  CHECK(random_id == 0);

  if (is_disabled() || max_notification_group_count_ == 0) {
    CHECK(logevent_id == 0);
    return promise.set_error(Status::Error(200, "Immediate success"));
  }

  if (!notification_id.is_valid()) {
    CHECK(logevent_id == 0);
    notification_id = get_next_notification_id();
    if (!notification_id.is_valid()) {
      return promise.set_value(Unit());
    }
  } else {
    CHECK(logevent_id != 0);
  }

  if (sender_user_id.is_valid() && !td_->contacts_manager_->have_user_force(sender_user_id)) {
    int32 flags = telegram_api::user::FIRST_NAME_MASK | telegram_api::user::MIN_MASK;
    auto user = telegram_api::make_object<telegram_api::user>(
        flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
        false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
        false /*ignored*/, false /*ignored*/, false /*ignored*/, sender_user_id.get(), 0, sender_name, string(),
        string(), string(), nullptr, nullptr, 0, Auto(), string(), string());
    td_->contacts_manager_->on_get_user(std::move(user), "add_message_push_notification");
  }

  if (logevent_id == 0 && G()->parameters().use_message_db) {
    AddMessagePushNotificationLogEvent logevent{
        dialog_id,         message_id,       random_id,         sender_user_id, sender_name, date,
        is_from_scheduled, contains_mention, initial_is_silent, loc_key,        arg,         photo,
        document,          notification_id};
    auto storer = LogEventStorerImpl<AddMessagePushNotificationLogEvent>(logevent);
    logevent_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::AddMessagePushNotification, storer);
  }

  auto group_id = info.group_id;
  CHECK(group_id.is_valid());

  if (logevent_id != 0) {
    VLOG(notifications) << "Register temporary " << notification_id << " with logevent " << logevent_id;
    temporary_notification_logevent_ids_[notification_id] = logevent_id;
    temporary_notifications_[FullMessageId(dialog_id, message_id)] = {group_id, notification_id, sender_user_id};
    temporary_notification_message_ids_[notification_id] = FullMessageId(dialog_id, message_id);
  }
  push_notification_promises_[notification_id].push_back(std::move(promise));

  auto group_type = info.group_type;
  auto settings_dialog_id = info.settings_dialog_id;
  VLOG(notifications) << "Add message push " << notification_id << " of type " << loc_key << " for " << message_id
                      << "/" << random_id << " in " << dialog_id << ", sent by " << sender_user_id << " at " << date
                      << " with arg " << arg << ", photo " << photo << " and document " << document << " to "
                      << group_id << " of type " << group_type << " with settings from " << settings_dialog_id;

  add_notification(group_id, group_type, dialog_id, date, settings_dialog_id, initial_is_silent, is_silent, 0,
                   notification_id,
                   create_new_push_message_notification(sender_user_id, message_id, std::move(loc_key), std::move(arg),
                                                        std::move(photo), std::move(document)),
                   "add_message_push_notification");
}

class NotificationManager::EditMessagePushNotificationLogEvent {
 public:
  DialogId dialog_id_;
  MessageId message_id_;
  int32 edit_date_;
  string loc_key_;
  string arg_;
  Photo photo_;
  Document document_;

  template <class StorerT>
  void store(StorerT &storer) const {
    bool has_message_id = message_id_.is_valid();
    bool has_arg = !arg_.empty();
    bool has_photo = photo_.id != -2;
    bool has_document = !document_.empty();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_message_id);
    STORE_FLAG(has_arg);
    STORE_FLAG(has_photo);
    STORE_FLAG(has_document);
    END_STORE_FLAGS();
    td::store(dialog_id_, storer);
    if (has_message_id) {
      td::store(message_id_, storer);
    }
    td::store(edit_date_, storer);
    td::store(loc_key_, storer);
    if (has_arg) {
      td::store(arg_, storer);
    }
    if (has_photo) {
      td::store(photo_, storer);
    }
    if (has_document) {
      td::store(document_, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    bool has_message_id;
    bool has_arg;
    bool has_photo;
    bool has_document;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_message_id);
    PARSE_FLAG(has_arg);
    PARSE_FLAG(has_photo);
    PARSE_FLAG(has_document);
    END_PARSE_FLAGS();
    td::parse(dialog_id_, parser);
    if (has_message_id) {
      td::parse(message_id_, parser);
    }
    td::parse(edit_date_, parser);
    td::parse(loc_key_, parser);
    if (has_arg) {
      td::parse(arg_, parser);
    }
    if (has_photo) {
      td::parse(photo_, parser);
    } else {
      photo_.id = -2;
    }
    if (has_document) {
      td::parse(document_, parser);
    }
  }
};

void NotificationManager::edit_message_push_notification(DialogId dialog_id, MessageId message_id, int32 edit_date,
                                                         string loc_key, string arg, Photo photo, Document document,
                                                         uint64 logevent_id, Promise<Unit> promise) {
  if (is_disabled() || max_notification_group_count_ == 0) {
    CHECK(logevent_id == 0);
    return promise.set_error(Status::Error(200, "Immediate success"));
  }

  auto it = temporary_notifications_.find(FullMessageId(dialog_id, message_id));
  if (it == temporary_notifications_.end()) {
    VLOG(notifications) << "Ignore edit of message push notification for " << message_id << " in " << dialog_id
                        << " edited at " << edit_date;
    return promise.set_error(Status::Error(200, "Immediate success"));
  }

  auto group_id = it->second.group_id;
  auto notification_id = it->second.notification_id;
  auto sender_user_id = it->second.sender_user_id;
  CHECK(group_id.is_valid());
  CHECK(notification_id.is_valid());

  if (logevent_id == 0 && G()->parameters().use_message_db) {
    EditMessagePushNotificationLogEvent logevent{dialog_id, message_id, edit_date, loc_key, arg, photo, document};
    auto storer = LogEventStorerImpl<EditMessagePushNotificationLogEvent>(logevent);
    auto &cur_logevent_id = temporary_edit_notification_logevent_ids_[notification_id];
    if (cur_logevent_id == 0) {
      logevent_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::EditMessagePushNotification, storer);
      cur_logevent_id = logevent_id;
      VLOG(notifications) << "Add edit message push notification logevent " << logevent_id;
    } else {
      auto new_logevent_id = binlog_rewrite(G()->td_db()->get_binlog(), cur_logevent_id,
                                            LogEvent::HandlerType::EditMessagePushNotification, storer);
      VLOG(notifications) << "Rewrite edit message push notification logevent " << cur_logevent_id << " with "
                          << new_logevent_id;
    }
  } else if (logevent_id != 0) {
    VLOG(notifications) << "Register edit of temporary " << notification_id << " with logevent " << logevent_id;
    temporary_edit_notification_logevent_ids_[notification_id] = logevent_id;
  }

  push_notification_promises_[notification_id].push_back(std::move(promise));

  edit_notification(group_id, notification_id,
                    create_new_push_message_notification(sender_user_id, message_id, std::move(loc_key), std::move(arg),
                                                         std::move(photo), std::move(document)));
}

Result<int64> NotificationManager::get_push_receiver_id(string payload) {
  if (payload == "{}") {
    return static_cast<int64>(0);
  }

  auto r_json_value = json_decode(payload);
  if (r_json_value.is_error()) {
    return Status::Error(400, "Failed to parse payload as JSON object");
  }

  auto json_value = r_json_value.move_as_ok();
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error(400, "Expected JSON object");
  }

  auto data = std::move(json_value.get_object());
  if (has_json_object_field(data, "data")) {
    auto r_data_data = get_json_object_field(data, "data", JsonValue::Type::Object, false);
    if (r_data_data.is_error()) {
      return Status::Error(400, r_data_data.error().message());
    }
    auto data_data = r_data_data.move_as_ok();
    data = std::move(data_data.get_object());
  }

  for (auto &field_value : data) {
    if (field_value.first == "p") {
      auto encrypted_payload = std::move(field_value.second);
      if (encrypted_payload.type() != JsonValue::Type::String) {
        return Status::Error(400, "Expected encrypted payload as a String");
      }
      Slice encrypted_data = encrypted_payload.get_string();
      if (encrypted_data.size() < 12) {
        return Status::Error(400, "Encrypted payload is too small");
      }
      auto r_decoded = base64url_decode(encrypted_data.substr(0, 12));
      if (r_decoded.is_error()) {
        return Status::Error(400, "Failed to base64url-decode payload");
      }
      CHECK(r_decoded.ok().size() == 9);
      return as<int64>(r_decoded.ok().c_str());
    }
    if (field_value.first == "user_id") {
      auto user_id = std::move(field_value.second);
      if (user_id.type() != JsonValue::Type::String && user_id.type() != JsonValue::Type::Number) {
        return Status::Error(400, "Expected user_id as a String or a Number");
      }
      Slice user_id_str = user_id.type() == JsonValue::Type::String ? user_id.get_string() : user_id.get_number();
      auto r_user_id = to_integer_safe<int32>(user_id_str);
      if (r_user_id.is_error()) {
        return Status::Error(400, PSLICE() << "Failed to get user_id from " << user_id_str);
      }
      if (r_user_id.ok() <= 0) {
        return Status::Error(400, PSLICE() << "Receive wrong user_id " << user_id_str);
      }
      return static_cast<int64>(r_user_id.ok());
    }
  }

  return static_cast<int64>(0);
}

Result<string> NotificationManager::decrypt_push(int64 encryption_key_id, string encryption_key, string push) {
  auto r_json_value = json_decode(push);
  if (r_json_value.is_error()) {
    return Status::Error(400, "Failed to parse payload as JSON object");
  }

  auto json_value = r_json_value.move_as_ok();
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error(400, "Expected JSON object");
  }

  for (auto &field_value : json_value.get_object()) {
    if (field_value.first == "p") {
      auto encrypted_payload = std::move(field_value.second);
      if (encrypted_payload.type() != JsonValue::Type::String) {
        return Status::Error(400, "Expected encrypted payload as a String");
      }
      Slice encrypted_data = encrypted_payload.get_string();
      if (encrypted_data.size() < 12) {
        return Status::Error(400, "Encrypted payload is too small");
      }
      auto r_decoded = base64url_decode(encrypted_data);
      if (r_decoded.is_error()) {
        return Status::Error(400, "Failed to base64url-decode payload");
      }
      return decrypt_push_payload(encryption_key_id, std::move(encryption_key), r_decoded.move_as_ok());
    }
  }
  return Status::Error(400, "No 'p'(payload) field found in push");
}

Result<string> NotificationManager::decrypt_push_payload(int64 encryption_key_id, string encryption_key,
                                                         string payload) {
  mtproto::AuthKey auth_key(encryption_key_id, std::move(encryption_key));
  mtproto::PacketInfo packet_info;
  packet_info.version = 2;
  packet_info.type = mtproto::PacketInfo::EndToEnd;
  packet_info.is_creator = true;
  packet_info.check_mod4 = false;

  TRY_RESULT(result, mtproto::Transport::read(payload, auth_key, &packet_info));
  if (result.type() != mtproto::Transport::ReadResult::Packet) {
    return Status::Error(400, "Wrong packet type");
  }
  if (result.packet().size() < 4) {
    return Status::Error(400, "Packet is too small");
  }
  return result.packet().substr(4).str();
}

void NotificationManager::before_get_difference() {
  if (is_disabled()) {
    return;
  }
  if (running_get_difference_) {
    return;
  }

  running_get_difference_ = true;
  on_unreceived_notification_update_count_changed(1, 0, "before_get_difference");
}

void NotificationManager::after_get_difference() {
  if (is_disabled()) {
    return;
  }

  CHECK(running_get_difference_);
  running_get_difference_ = false;
  on_unreceived_notification_update_count_changed(-1, 0, "after_get_difference");
  flush_pending_notifications_timeout_.set_timeout_in(0, MIN_NOTIFICATION_DELAY_MS * 1e-3);
}

void NotificationManager::after_get_difference_impl() {
  if (running_get_difference_) {
    return;
  }

  VLOG(notifications) << "After get difference";

  vector<NotificationGroupId> to_remove_temporary_notifications_group_ids;
  for (auto &group_it : groups_) {
    const auto &group_key = group_it.first;
    const auto &group = group_it.second;
    if (running_get_chat_difference_.count(group_key.group_id.get()) == 0 &&
        get_temporary_notification_total_count(group) > 0) {
      to_remove_temporary_notifications_group_ids.push_back(group_key.group_id);
    }
  }
  for (auto group_id : reversed(to_remove_temporary_notifications_group_ids)) {
    remove_temporary_notifications(group_id, "after_get_difference");
  }

  flush_all_pending_updates(false, "after_get_difference");
}

void NotificationManager::before_get_chat_difference(NotificationGroupId group_id) {
  if (is_disabled()) {
    return;
  }

  VLOG(notifications) << "Before get chat difference in " << group_id;
  CHECK(group_id.is_valid());
  running_get_chat_difference_.insert(group_id.get());
  on_unreceived_notification_update_count_changed(1, group_id.get(), "before_get_chat_difference");
}

void NotificationManager::after_get_chat_difference(NotificationGroupId group_id) {
  if (is_disabled()) {
    return;
  }

  VLOG(notifications) << "After get chat difference in " << group_id;
  CHECK(group_id.is_valid());
  auto erased_count = running_get_chat_difference_.erase(group_id.get());
  if (erased_count == 1) {
    flush_pending_notifications_timeout_.set_timeout_in(-group_id.get(), MIN_NOTIFICATION_DELAY_MS * 1e-3);
    on_unreceived_notification_update_count_changed(-1, group_id.get(), "after_get_chat_difference");
  }
}

void NotificationManager::after_get_chat_difference_impl(NotificationGroupId group_id) {
  if (running_get_chat_difference_.count(group_id.get()) == 1) {
    return;
  }

  VLOG(notifications) << "Flush updates after get chat difference in " << group_id;
  CHECK(group_id.is_valid());
  if (!running_get_difference_ && pending_updates_.count(group_id.get()) == 1) {
    remove_temporary_notifications(group_id, "after_get_chat_difference");
    force_flush_pending_updates(group_id, "after_get_chat_difference");
  }
}

void NotificationManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (is_disabled() || max_notification_group_count_ == 0 || is_destroyed_) {
    return;
  }

  updates.push_back(get_update_active_notifications());
  updates.push_back(get_update_have_pending_notifications());
}

void NotificationManager::flush_all_notifications() {
  flush_all_pending_notifications();
  flush_all_pending_updates(true, "flush_all_notifications");
}

void NotificationManager::destroy_all_notifications() {
  if (is_destroyed_) {
    return;
  }
  is_being_destroyed_ = true;

  size_t cur_pos = 0;
  for (auto it = groups_.begin(); it != groups_.end() && cur_pos < max_notification_group_count_; ++it, cur_pos++) {
    auto &group_key = it->first;
    auto &group = it->second;

    if (group_key.last_notification_date == 0) {
      break;
    }

    VLOG(notifications) << "Destroy " << group_key.group_id;
    send_remove_group_update(group_key, group, vector<int32>());
  }

  flush_all_pending_updates(true, "destroy_all_notifications");
  if (delayed_notification_update_count_ != 0) {
    on_delayed_notification_update_count_changed(-delayed_notification_update_count_, 0, "destroy_all_notifications");
  }
  if (unreceived_notification_update_count_ != 0) {
    on_unreceived_notification_update_count_changed(-unreceived_notification_update_count_, 0,
                                                    "destroy_all_notifications");
  }

  while (!push_notification_promises_.empty()) {
    on_notification_processed(push_notification_promises_.begin()->first);
  }

  is_destroyed_ = true;
}

td_api::object_ptr<td_api::updateHavePendingNotifications> NotificationManager::get_update_have_pending_notifications()
    const {
  return td_api::make_object<td_api::updateHavePendingNotifications>(delayed_notification_update_count_ != 0,
                                                                     unreceived_notification_update_count_ != 0);
}

void NotificationManager::send_update_have_pending_notifications() const {
  if (is_destroyed_ || !is_inited_ || !is_binlog_processed_) {
    return;
  }

  auto update = get_update_have_pending_notifications();
  VLOG(notifications) << "Send " << oneline(to_string(update));
  send_closure(G()->td(), &Td::send_update, std::move(update));
}

void NotificationManager::on_delayed_notification_update_count_changed(int32 diff, int32 notification_group_id,
                                                                       const char *source) {
  bool had_delayed = delayed_notification_update_count_ != 0;
  delayed_notification_update_count_ += diff;
  CHECK(delayed_notification_update_count_ >= 0);
  VLOG(notifications) << "Update delayed notification count with diff " << diff << " to "
                      << delayed_notification_update_count_ << " from group " << notification_group_id << " and "
                      << source;
  bool have_delayed = delayed_notification_update_count_ != 0;
  if (had_delayed != have_delayed) {
    send_update_have_pending_notifications();
  }
}

void NotificationManager::on_unreceived_notification_update_count_changed(int32 diff, int32 notification_group_id,
                                                                          const char *source) {
  bool had_unreceived = unreceived_notification_update_count_ != 0;
  unreceived_notification_update_count_ += diff;
  CHECK(unreceived_notification_update_count_ >= 0);
  VLOG(notifications) << "Update unreceived notification count with diff " << diff << " to "
                      << unreceived_notification_update_count_ << " from group " << notification_group_id << " and "
                      << source;
  bool have_unreceived = unreceived_notification_update_count_ != 0;
  if (had_unreceived != have_unreceived) {
    send_update_have_pending_notifications();
  }
}

void NotificationManager::try_send_update_active_notifications() {
  if (max_notification_group_count_ == 0) {
    return;
  }
  if (!is_binlog_processed_ || !is_inited_) {
    return;
  }

  auto update = get_update_active_notifications();
  VLOG(notifications) << "Send " << as_active_notifications_update(update.get());
  send_closure(G()->td(), &Td::send_update, std::move(update));

  while (!push_notification_promises_.empty()) {
    on_notification_processed(push_notification_promises_.begin()->first);
  }
}

void NotificationManager::on_binlog_events(vector<BinlogEvent> &&events) {
  VLOG(notifications) << "Begin to process " << events.size() << " binlog events";
  for (auto &event : events) {
    if (!G()->parameters().use_message_db || is_disabled() || max_notification_group_count_ == 0) {
      binlog_erase(G()->td_db()->get_binlog(), event.id_);
      break;
    }

    switch (event.type_) {
      case LogEvent::HandlerType::AddMessagePushNotification: {
        CHECK(is_inited_);
        AddMessagePushNotificationLogEvent log_event;
        log_event_parse(log_event, event.data_).ensure();

        add_message_push_notification(
            log_event.dialog_id_, log_event.message_id_, log_event.random_id_, log_event.sender_user_id_,
            log_event.sender_name_, log_event.date_, log_event.is_from_scheduled_, log_event.contains_mention_,
            log_event.is_silent_, true, log_event.loc_key_, log_event.arg_, log_event.photo_, log_event.document_,
            log_event.notification_id_, event.id_, PromiseCreator::lambda([](Result<Unit> result) {
              if (result.is_error() && result.error().code() != 200 && result.error().code() != 406) {
                LOG(ERROR) << "Receive error " << result.error() << ", while processing message push notification";
              }
            }));
        break;
      }
      case LogEvent::HandlerType::EditMessagePushNotification: {
        CHECK(is_inited_);
        EditMessagePushNotificationLogEvent log_event;
        log_event_parse(log_event, event.data_).ensure();

        edit_message_push_notification(
            log_event.dialog_id_, log_event.message_id_, log_event.edit_date_, log_event.loc_key_, log_event.arg_,
            log_event.photo_, log_event.document_, event.id_, PromiseCreator::lambda([](Result<Unit> result) {
              if (result.is_error() && result.error().code() != 200 && result.error().code() != 406) {
                LOG(ERROR) << "Receive error " << result.error() << ", while processing edit message push notification";
              }
            }));
        break;
      }
      default:
        LOG(FATAL) << "Unsupported logevent type " << event.type_;
    }
  }
  if (is_inited_) {
    flush_all_pending_notifications();
  }
  is_binlog_processed_ = true;
  try_send_update_active_notifications();
  VLOG(notifications) << "Finish processing binlog events";
}

}  // namespace td
