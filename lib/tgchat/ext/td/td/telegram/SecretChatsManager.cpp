//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SecretChatsManager.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DhCache.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/SecretChatEvent.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/SequenceDispatcher.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/telegram_api.hpp"

#include "td/mtproto/DhHandshake.h"

#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/binlog/BinlogInterface.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <memory>

namespace td {

// qts and seq_no
// Each EncryptedMessage (update_message) has qts.
// Such updates must be handled in order of qts
//
// Qts should be handled on level of SecretChatsManager
// 1. Each update can be received by SecretChatsManager multiple times.
// 2. Each update should be sent to SecretChatActor only once. (Though SecretChatActor mustn't rely it)
// 3. Updates must be send in order of qts, without gaps.
// 4. SecretChatActor must notify SecretChatManager when update is processed (saved in database)
// 5. Only after all updates <= qts are processed by SecretChatActor, UpdatesManager should be
// notified about new qts.
//
// seq_no
// 1.
// x_in = 0 if we initiated secret chat.
// x_in = 1 if other client initiated secret chat
// x_out = 1 - x_in
// 2. Send:
// in_seq_no = my_in_seq_no * 2 + x_in
// out_seq_no = my_out_seq_no * 2 + x_out
// my_out_seq_no++;
//
// 3. Receive
// fail_if (in_seq_no % 2 != (1 - x_in)), in_seq_no /= 2.
// fail_if (out_seq_no % 2 != x_out), out_seq_no /= 2.
// drop_if (out_seq_no < my_in_seq_no)
// handle_gap_if(out_seq_no > my_in_seq_no)
// my_in_seq_no++;
//
// fail_if(in_seq_no < his_in_seq_no)
// his_in_seq_no = in_seq_no
// fail_if(my_out_seq_no < his_in_seq_no)
//
// 4. Preventing gaps.
// All messages must be sent in order of out_seq_no
// Messages of older layer have imaginary seq_no = -1
// a. TODO use invokeAfter.
// b. Just don't send next message before server accepted previous one.
//
// 5. Handling gaps.
// TODO
// Just fail chat.

SecretChatsManager::SecretChatsManager(ActorShared<> parent) : parent_(std::move(parent)) {
}

void SecretChatsManager::start_up() {
  if (!G()->parameters().use_secret_chats) {
    dummy_mode_ = true;
    return;
  }
  // TODO: use database wrapper
  auto pmc = G()->td_db()->get_binlog_pmc();
  auto qts_str = pmc->get("updates.qts");
  if (!qts_str.empty()) {
    init_qts(to_integer<int32>(qts_str));
  }

  class StateCallback : public StateManager::Callback {
   public:
    explicit StateCallback(ActorId<SecretChatsManager> parent) : parent_(std::move(parent)) {
    }
    bool on_online(bool online_flag) override {
      send_closure(parent_, &SecretChatsManager::on_online, online_flag);
      return parent_.is_alive();
    }

   private:
    ActorId<SecretChatsManager> parent_;
  };
  send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));
}

void SecretChatsManager::init_qts(int qts) {
  if (dummy_mode_ || close_flag_) {
    return;
  }
  has_qts_ = true;
  qts_manager_.init(qts);
  LOG(INFO) << "Init secret chats qts " << tag("qts", qts);
}

void SecretChatsManager::update_qts(int qts) {
  if (dummy_mode_ || close_flag_ || qts < 0) {
    return;
  }
  LOG(INFO) << "Update qts to " << qts;
  add_qts(qts).set_value(Unit());
  has_qts_ = true;
  LOG(INFO) << "Update secret chats qts " << tag("qts", qts);
}

void SecretChatsManager::create_chat(int32 user_id, int64 user_access_hash, Promise<SecretChatId> promise) {
  int32 random_id;
  ActorId<SecretChatActor> actor;
  do {
    random_id = Random::secure_int32() & 0x7fffffff;
    actor = create_chat_actor(random_id);
  } while (actor.empty());
  send_closure(actor, &SecretChatActor::create_chat, user_id, user_access_hash, random_id, std::move(promise));
}

void SecretChatsManager::cancel_chat(SecretChatId secret_chat_id, Promise<> promise) {
  auto actor = get_chat_actor(secret_chat_id.get());
  auto safe_promise = SafePromise<>(std::move(promise), Unit());
  send_closure(actor, &SecretChatActor::cancel_chat, std::move(safe_promise));
}

void SecretChatsManager::send_message(SecretChatId secret_chat_id, tl_object_ptr<secret_api::decryptedMessage> message,
                                      tl_object_ptr<telegram_api::InputEncryptedFile> file, Promise<> promise) {
  // message->message_ = Random::fast(0, 1) ? string(1, static_cast<char>(0x80)) : "a";
  auto actor = get_chat_actor(secret_chat_id.get());
  auto safe_promise = SafePromise<>(std::move(promise), Status::Error(400, "Can't find secret chat"));
  send_closure(actor, &SecretChatActor::send_message, std::move(message), std::move(file), std::move(safe_promise));
}
void SecretChatsManager::send_message_action(SecretChatId secret_chat_id,
                                             tl_object_ptr<secret_api::SendMessageAction> action) {
  auto actor = get_chat_actor(secret_chat_id.get());
  if (actor.empty()) {
    return;
  }
  send_closure(actor, &SecretChatActor::send_message_action, std::move(action));
}
void SecretChatsManager::send_read_history(SecretChatId secret_chat_id, int32 date, Promise<> promise) {
  auto actor = get_chat_actor(secret_chat_id.get());
  auto safe_promise = SafePromise<>(std::move(promise), Status::Error(400, "Can't find secret chat"));
  send_closure(actor, &SecretChatActor::send_read_history, date, std::move(safe_promise));
}
void SecretChatsManager::send_open_message(SecretChatId secret_chat_id, int64 random_id, Promise<> promise) {
  auto actor = get_chat_actor(secret_chat_id.get());
  auto safe_promise = SafePromise<>(std::move(promise), Status::Error(400, "Can't find secret chat"));
  send_closure(actor, &SecretChatActor::send_open_message, random_id, std::move(safe_promise));
}

void SecretChatsManager::delete_messages(SecretChatId secret_chat_id, vector<int64> random_ids, Promise<> promise) {
  auto actor = get_chat_actor(secret_chat_id.get());
  auto safe_promise = SafePromise<>(std::move(promise), Unit());
  send_closure(actor, &SecretChatActor::delete_messages, std::move(random_ids), std::move(safe_promise));
}

void SecretChatsManager::delete_all_messages(SecretChatId secret_chat_id, Promise<> promise) {
  auto actor = get_chat_actor(secret_chat_id.get());
  auto safe_promise = SafePromise<>(std::move(promise), Unit());
  send_closure(actor, &SecretChatActor::delete_all_messages, std::move(safe_promise));
}

void SecretChatsManager::notify_screenshot_taken(SecretChatId secret_chat_id, Promise<> promise) {
  auto actor = get_chat_actor(secret_chat_id.get());
  auto safe_promise = SafePromise<>(std::move(promise), Status::Error(400, "Can't find secret chat"));
  send_closure(actor, &SecretChatActor::notify_screenshot_taken, std::move(safe_promise));
}

void SecretChatsManager::send_set_ttl_message(SecretChatId secret_chat_id, int32 ttl, int64 random_id,
                                              Promise<> promise) {
  auto actor = get_chat_actor(secret_chat_id.get());
  auto safe_promise = SafePromise<>(std::move(promise), Status::Error(400, "Can't find secret chat"));
  send_closure(actor, &SecretChatActor::send_set_ttl_message, ttl, random_id, std::move(safe_promise));
}

void SecretChatsManager::before_get_difference(int32 qts) {
  if (dummy_mode_ || close_flag_) {
    return;
  }
  last_get_difference_qts_ = qts;
  // We will receive all updates later than qts anyway.
}

void SecretChatsManager::after_get_difference() {
  if (dummy_mode_ || close_flag_) {
    return;
  }
}

void SecretChatsManager::on_update_chat(tl_object_ptr<telegram_api::updateEncryption> update) {
  if (dummy_mode_ || close_flag_) {
    return;
  }
  bool chat_requested = update->chat_->get_id() == telegram_api::encryptedChatRequested::ID;
  pending_chat_updates_.push_back({Timestamp::in(chat_requested ? 1 : 0), std::move(update)});
  flush_pending_chat_updates();
}

void SecretChatsManager::do_update_chat(tl_object_ptr<telegram_api::updateEncryption> update) {
  int32 id = 0;
  downcast_call(*update->chat_, [&](auto &x) { id = x.id_; });

  send_closure(
      update->chat_->get_id() == telegram_api::encryptedChatDiscarded::ID ? get_chat_actor(id) : create_chat_actor(id),
      &SecretChatActor::update_chat, std::move(update->chat_));
}

void SecretChatsManager::on_update_message(tl_object_ptr<telegram_api::updateNewEncryptedMessage> update,
                                           bool force_apply) {
  if (dummy_mode_ || close_flag_) {
    return;
  }
  // UpdatesManager MUST postpone updates during GetDifference
  auto qts = update->qts_;
  if (!force_apply) {
    if (!has_qts_) {
      LOG(INFO) << "Got update, don't know current qts. Force get_difference";
      force_get_difference();
      return;
    }
    if (qts <= last_get_difference_qts_) {
      LOG(WARNING) << "Got updates with " << tag("qts", qts) << " lower or equal than "
                   << tag("last get difference qts", last_get_difference_qts_);
      force_get_difference();
      return;
    }
    auto mem_qts = qts_manager_.mem_pts();
    if (qts <= mem_qts) {
      LOG(WARNING) << "Duplicated update " << tag("qts", qts) << tag("mem_qts", mem_qts);
      return;
    }
    if (qts != mem_qts + 1) {
      LOG(WARNING) << "Got gap in qts " << mem_qts << " ... " << qts;
      force_get_difference();
      // TODO wait 1 second?
      return;
    }
  }

  auto event = make_unique<logevent::InboundSecretMessage>();
  event->qts = qts;
  downcast_call(*update->message_, [&](auto &x) {
    event->chat_id = x.chat_id_;
    event->date = x.date_;
    event->encrypted_message = std::move(x.bytes_);
  });
  if (update->message_->get_id() == telegram_api::encryptedMessage::ID) {
    auto message = move_tl_object_as<telegram_api::encryptedMessage>(update->message_);
    if (message->file_->get_id() == telegram_api::encryptedFile::ID) {
      auto file = move_tl_object_as<telegram_api::encryptedFile>(message->file_);

      event->file.id = file->id_;
      event->file.access_hash = file->access_hash_;
      event->file.size = file->size_;
      event->file.dc_id = file->dc_id_;
      event->file.key_fingerprint = file->key_fingerprint_;

      event->has_encrypted_file = true;
    }
  }
  add_inbound_message(std::move(event));
}

Promise<> SecretChatsManager::add_qts(int32 qts) {
  auto id = qts_manager_.add_pts(qts);
  return PromiseCreator::event(self_closure(this, &SecretChatsManager::on_qts_ack, id));
}

void SecretChatsManager::replay_binlog_event(BinlogEvent &&binlog_event) {
  if (dummy_mode_) {
    binlog_erase(G()->td_db()->get_binlog(), binlog_event.id_);
    return;
  }
  auto r_message = logevent::SecretChatEvent::from_buffer_slice(binlog_event.data_as_buffer_slice());
  LOG_IF(FATAL, r_message.is_error()) << "Failed to deserialize event: " << r_message.error();
  auto message = r_message.move_as_ok();
  message->set_logevent_id(binlog_event.id_);
  LOG(INFO) << "Process binlog event " << *message;
  switch (message->get_type()) {
    case logevent::SecretChatEvent::Type::InboundSecretMessage:
      return replay_inbound_message(
          unique_ptr<logevent::InboundSecretMessage>(static_cast<logevent::InboundSecretMessage *>(message.release())));
    case logevent::SecretChatEvent::Type::OutboundSecretMessage:
      return replay_outbound_message(unique_ptr<logevent::OutboundSecretMessage>(
          static_cast<logevent::OutboundSecretMessage *>(message.release())));
    case logevent::SecretChatEvent::Type::CloseSecretChat:
      return replay_close_chat(
          unique_ptr<logevent::CloseSecretChat>(static_cast<logevent::CloseSecretChat *>(message.release())));
    case logevent::SecretChatEvent::Type::CreateSecretChat:
      return replay_create_chat(
          unique_ptr<logevent::CreateSecretChat>(static_cast<logevent::CreateSecretChat *>(message.release())));
  }
  LOG(FATAL) << "Unknown logevent type " << tag("type", format::as_hex(static_cast<int32>(message->get_type())));
}

void SecretChatsManager::binlog_replay_finish() {
  binlog_replay_finish_flag_ = true;
  for (auto &it : id_to_actor_) {
    send_closure(it.second, &SecretChatActor::binlog_replay_finish);
  }
}

void SecretChatsManager::replay_inbound_message(unique_ptr<logevent::InboundSecretMessage> message) {
  LOG(INFO) << "Replay inbound secret message in chat " << message->chat_id << " with qts " << message->qts;
  auto actor = get_chat_actor(message->chat_id);
  send_closure_later(actor, &SecretChatActor::replay_inbound_message, std::move(message));
}

void SecretChatsManager::add_inbound_message(unique_ptr<logevent::InboundSecretMessage> message) {
  LOG(INFO) << "Process inbound secret message in chat " << message->chat_id << " with qts " << message->qts;
  message->qts_ack = add_qts(message->qts);

  auto actor = get_chat_actor(message->chat_id);
  send_closure(actor, &SecretChatActor::add_inbound_message, std::move(message));
}

void SecretChatsManager::replay_close_chat(unique_ptr<logevent::CloseSecretChat> message) {
  LOG(INFO) << "Replay close secret chat " << message->chat_id;

  auto actor = get_chat_actor(message->chat_id);
  send_closure_later(actor, &SecretChatActor::replay_close_chat, std::move(message));
}

void SecretChatsManager::replay_create_chat(unique_ptr<logevent::CreateSecretChat> message) {
  LOG(INFO) << "Replay create secret chat " << message->random_id;

  auto actor = create_chat_actor(message->random_id);
  send_closure_later(actor, &SecretChatActor::replay_create_chat, std::move(message));
}

void SecretChatsManager::replay_outbound_message(unique_ptr<logevent::OutboundSecretMessage> message) {
  LOG(INFO) << "Replay outbound secret message in chat " << message->chat_id;

  auto actor = get_chat_actor(message->chat_id);
  send_closure_later(actor, &SecretChatActor::replay_outbound_message, std::move(message));
}

void SecretChatsManager::force_get_difference() {
  LOG(INFO) << "Force get difference";
  send_closure(G()->td(), &Td::force_get_difference);
}

ActorId<SecretChatActor> SecretChatsManager::get_chat_actor(int32 id) {
  return create_chat_actor_impl(id, false);
}

ActorId<SecretChatActor> SecretChatsManager::create_chat_actor(int32 id) {
  return create_chat_actor_impl(id, true);
}

unique_ptr<SecretChatActor::Context> SecretChatsManager::make_secret_chat_context(int32 id) {
  class Context : public SecretChatActor::Context {
   public:
    Context(int32 id, ActorShared<SecretChatsManager> parent, unique_ptr<SecretChatDb> secret_chat_db)
        : secret_chat_id_(SecretChatId(id)), parent_(std::move(parent)), secret_chat_db_(std::move(secret_chat_db)) {
      sequence_dispatcher_ = create_actor<SequenceDispatcher>("SecretChat SequenceDispatcher");
    }
    Context(const Context &other) = delete;
    Context &operator=(const Context &other) = delete;
    Context(Context &&other) = delete;
    Context &operator=(Context &&other) = delete;
    ~Context() override {
      send_closure(std::move(sequence_dispatcher_), &SequenceDispatcher::close_silent);
    }

    DhCallback *dh_callback() override {
      return DhCache::instance();
    }
    NetQueryCreator &net_query_creator() override {
      return G()->net_query_creator();
    }
    BinlogInterface *binlog() override {
      return G()->td_db()->get_binlog();
    }
    SecretChatDb *secret_chat_db() override {
      return secret_chat_db_.get();
    }
    std::shared_ptr<DhConfig> dh_config() override {
      return G()->get_dh_config();
    }
    void set_dh_config(std::shared_ptr<DhConfig> dh_config) override {
      G()->set_dh_config(std::move(dh_config));
    }
    void send_net_query(NetQueryPtr query, ActorShared<NetQueryCallback> callback, bool ordered) override {
      if (ordered) {
        send_closure(sequence_dispatcher_, &SequenceDispatcher::send_with_callback, std::move(query),
                     std::move(callback));
      } else {
        G()->net_query_dispatcher().dispatch_with_callback(std::move(query), std::move(callback));
      }
    }

    bool get_config_option_boolean(const string &name) const override {
      return G()->shared_config().get_option_boolean(name);
    }

    int32 unix_time() override {
      return G()->unix_time();
    }

    bool close_flag() override {
      return G()->close_flag();
    }

    void on_update_secret_chat(int64 access_hash, UserId user_id, SecretChatState state, bool is_outbound, int32 ttl,
                               int32 date, string key_hash, int32 layer) override {
      send_closure(G()->contacts_manager(), &ContactsManager::on_update_secret_chat, secret_chat_id_, access_hash,
                   user_id, state, is_outbound, ttl, date, key_hash, layer);
    }

    void on_inbound_message(UserId user_id, MessageId message_id, int32 date,
                            tl_object_ptr<telegram_api::encryptedFile> file,
                            tl_object_ptr<secret_api::decryptedMessage> message, Promise<> promise) override {
      send_closure(G()->messages_manager(), &MessagesManager::on_get_secret_message, secret_chat_id_, user_id,
                   message_id, date, std::move(file), std::move(message), std::move(promise));
    }

    void on_send_message_error(int64 random_id, Status error, Promise<> promise) override {
      send_closure(G()->messages_manager(), &MessagesManager::on_send_secret_message_error, random_id, std::move(error),
                   std::move(promise));
    }

    void on_send_message_ack(int64 random_id) override {
      send_closure(G()->messages_manager(), &MessagesManager::on_send_message_get_quick_ack, random_id);
    }
    void on_send_message_ok(int64 random_id, MessageId message_id, int32 date,
                            tl_object_ptr<telegram_api::EncryptedFile> file, Promise<> promise) override {
      send_closure(G()->messages_manager(), &MessagesManager::on_send_secret_message_success, random_id, message_id,
                   date, std::move(file), std::move(promise));
    }
    void on_delete_messages(std::vector<int64> random_ids, Promise<> promise) override {
      send_closure(G()->messages_manager(), &MessagesManager::delete_secret_messages, secret_chat_id_,
                   std::move(random_ids), std::move(promise));
    }
    void on_flush_history(MessageId message_id, Promise<> promise) override {
      send_closure(G()->messages_manager(), &MessagesManager::delete_secret_chat_history, secret_chat_id_, message_id,
                   std::move(promise));
    }
    void on_read_message(int64 random_id, Promise<> promise) override {
      send_closure(G()->messages_manager(), &MessagesManager::open_secret_message, secret_chat_id_, random_id,
                   std::move(promise));
    }
    void on_screenshot_taken(UserId user_id, MessageId message_id, int32 date, int64 random_id,
                             Promise<> promise) override {
      send_closure(G()->messages_manager(), &MessagesManager::on_secret_chat_screenshot_taken, secret_chat_id_, user_id,
                   message_id, date, random_id, std::move(promise));
    }
    void on_set_ttl(UserId user_id, MessageId message_id, int32 date, int32 ttl, int64 random_id,
                    Promise<> promise) override {
      send_closure(G()->messages_manager(), &MessagesManager::on_secret_chat_ttl_changed, secret_chat_id_, user_id,
                   message_id, date, ttl, random_id, std::move(promise));
    }

   private:
    SecretChatId secret_chat_id_;
    ActorOwn<SequenceDispatcher> sequence_dispatcher_;
    ActorShared<SecretChatsManager> parent_;
    unique_ptr<SecretChatDb> secret_chat_db_;
  };
  return make_unique<Context>(id, actor_shared(this, id),
                              td::make_unique<SecretChatDb>(G()->td_db()->get_binlog_pmc_shared(), id));
}

ActorId<SecretChatActor> SecretChatsManager::create_chat_actor_impl(int32 id, bool can_be_empty) {
  if (id == 0) {
    return Auto();
  }
  auto it_flag = id_to_actor_.emplace(id, ActorOwn<SecretChatActor>());
  if (it_flag.second) {
    LOG(INFO) << "Create SecretChatActor: " << tag("id", id);
    it_flag.first->second =
        create_actor<SecretChatActor>(PSLICE() << "SecretChat " << id, id, make_secret_chat_context(id), can_be_empty);
    if (binlog_replay_finish_flag_) {
      send_closure(it_flag.first->second, &SecretChatActor::binlog_replay_finish);
    }
    return it_flag.first->second.get();
  } else {
    return it_flag.first->second.get();
  }
}

void SecretChatsManager::on_qts_ack(PtsManager::PtsId qts_ack_token) {
  auto old_qts = qts_manager_.db_pts();
  auto new_qts = qts_manager_.finish(qts_ack_token);
  if (old_qts != new_qts) {
    save_qts();
  }
}

void SecretChatsManager::save_qts() {
  LOG(INFO) << "Save " << tag("qts", qts_manager_.db_pts());
  send_closure(G()->td(), &Td::update_qts, qts_manager_.db_pts());
}

void SecretChatsManager::hangup() {
  close_flag_ = true;
  if (dummy_mode_) {
    return stop();
  }
  for (auto &it : id_to_actor_) {
    LOG(INFO) << "Ask close SecretChatActor " << tag("id", it.first);
    it.second.reset();
  }
  if (id_to_actor_.empty()) {
    stop();
  }
}

void SecretChatsManager::hangup_shared() {
  CHECK(!dummy_mode_);
  auto token = get_link_token();
  auto it = id_to_actor_.find(static_cast<int32>(token));
  if (it != id_to_actor_.end()) {
    LOG(INFO) << "Close SecretChatActor " << tag("id", it->first);
    it->second.release();
    id_to_actor_.erase(it);
  } else {
    LOG(FATAL) << "Unknown SecretChatActor hangup " << tag("id", static_cast<int32>(token));
  }
  if (close_flag_ && id_to_actor_.empty()) {
    stop();
  }
}

void SecretChatsManager::timeout_expired() {
  flush_pending_chat_updates();
}

void SecretChatsManager::flush_pending_chat_updates() {
  if (close_flag_ || dummy_mode_) {
    return;
  }
  auto it = pending_chat_updates_.begin();
  while (it != pending_chat_updates_.end() && (it->first.is_in_past() || is_online_)) {
    do_update_chat(std::move(it->second));
    ++it;
  }
  if (it != pending_chat_updates_.end()) {
    set_timeout_at(it->first.at());
  }
  pending_chat_updates_.erase(pending_chat_updates_.begin(), it);
}

void SecretChatsManager::on_online(bool is_online) {
  if (is_online_ == is_online) {
    return;
  }

  is_online_ = is_online;
  flush_pending_chat_updates();
}

}  // namespace td
