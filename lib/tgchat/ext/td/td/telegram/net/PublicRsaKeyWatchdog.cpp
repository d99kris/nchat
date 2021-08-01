//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/PublicRsaKeyWatchdog.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/TdDb.h"

#include "td/telegram/telegram_api.h"

#include "td/mtproto/crypto.h"

#include "td/utils/logging.h"
#include "td/utils/Time.h"

namespace td {

PublicRsaKeyWatchdog::PublicRsaKeyWatchdog(ActorShared<> parent) : parent_(std::move(parent)) {
}

void PublicRsaKeyWatchdog::add_public_rsa_key(std::shared_ptr<PublicRsaKeyShared> key) {
  class Listener : public PublicRsaKeyShared::Listener {
   public:
    explicit Listener(ActorId<PublicRsaKeyWatchdog> parent) : parent_(std::move(parent)) {
    }
    bool notify() override {
      send_event(parent_, Event::yield());
      return parent_.is_alive();
    }

   private:
    ActorId<PublicRsaKeyWatchdog> parent_;
  };

  key->add_listener(make_unique<Listener>(actor_id(this)));
  sync_key(key);
  keys_.push_back(std::move(key));
  loop();
}

void PublicRsaKeyWatchdog::start_up() {
  flood_control_.add_limit(1, 1);
  flood_control_.add_limit(2, 60);
  flood_control_.add_limit(3, 2 * 60);

  sync(BufferSlice(G()->td_db()->get_binlog_pmc()->get("cdn_config")));
}

void PublicRsaKeyWatchdog::loop() {
  if (has_query_) {
    return;
  }
  if (Time::now_cached() < flood_control_.get_wakeup_at()) {
    return;
  }
  bool ok = true;
  for (auto &key : keys_) {
    if (!key->has_keys()) {
      ok = false;
    }
  }
  if (ok) {
    return;
  }
  flood_control_.add_event(static_cast<int32>(Time::now_cached()));
  has_query_ = true;
  G()->net_query_dispatcher().dispatch_with_callback(
      G()->net_query_creator().create(create_storer(telegram_api::help_getCdnConfig()), DcId::main(),
                                      NetQuery::Type::Common, NetQuery::AuthFlag::On, NetQuery::GzipFlag::On,
                                      60 * 60 * 24),
      actor_shared(this));
}

void PublicRsaKeyWatchdog::on_result(NetQueryPtr net_query) {
  has_query_ = false;
  yield();
  if (net_query->is_error()) {
    LOG(ERROR) << "Receive error for getCdnConfig: " << net_query->move_as_error();
    return;
  }

  auto buf = net_query->move_as_ok();
  G()->td_db()->get_binlog_pmc()->set("cdn_config", buf.as_slice().str());
  sync(std::move(buf));
}

void PublicRsaKeyWatchdog::sync(BufferSlice cdn_config_serialized) {
  if (cdn_config_serialized.empty()) {
    return;
  }
  auto r_keys = fetch_result<telegram_api::help_getCdnConfig>(cdn_config_serialized);
  if (r_keys.is_error()) {
    LOG(WARNING) << "Failed to deserialize help_getCdnConfig (probably not a problem) " << r_keys.error();
    return;
  }
  cdn_config_ = r_keys.move_as_ok();
  LOG(INFO) << "Receive " << to_string(cdn_config_);
  for (auto &key : keys_) {
    sync_key(key);
  }
}

void PublicRsaKeyWatchdog::sync_key(std::shared_ptr<PublicRsaKeyShared> &key) {
  if (!cdn_config_) {
    return;
  }
  for (auto &config_key : cdn_config_->public_keys_) {
    if (key->dc_id().get_raw_id() == config_key->dc_id_) {
      auto r_rsa = RSA::from_pem(config_key->public_key_);
      if (r_rsa.is_error()) {
        LOG(ERROR) << r_rsa.error();
        continue;
      }
      LOG(INFO) << "Add CDN " << key->dc_id() << " key with fingerprint " << r_rsa.ok().get_fingerprint();
      key->add_rsa(r_rsa.move_as_ok());
    }
  }
}

}  // namespace td
