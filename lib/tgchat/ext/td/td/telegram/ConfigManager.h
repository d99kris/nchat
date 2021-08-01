//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/NetQuery.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

namespace td {

extern int VERBOSITY_NAME(config_recoverer);

class ConfigShared;

using SimpleConfig = tl_object_ptr<telegram_api::help_configSimple>;
struct SimpleConfigResult {
  Result<SimpleConfig> r_config;
  Result<int32> r_http_date;
};

Result<SimpleConfig> decode_config(Slice input);

ActorOwn<> get_simple_config_azure(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config, bool is_test,
                                   int32 scheduler_id);

ActorOwn<> get_simple_config_google_dns(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config,
                                        bool is_test, int32 scheduler_id);

ActorOwn<> get_simple_config_mozilla_dns(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config,
                                         bool is_test, int32 scheduler_id);

ActorOwn<> get_simple_config_firebase_remote_config(Promise<SimpleConfigResult> promise,
                                                    const ConfigShared *shared_config, bool is_test,
                                                    int32 scheduler_id);

ActorOwn<> get_simple_config_firebase_realtime(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config,
                                               bool is_test, int32 scheduler_id);

ActorOwn<> get_simple_config_firebase_firestore(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config,
                                                bool is_test, int32 scheduler_id);

class HttpDate {
  static bool is_leap(int32 year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  }
  static int32 days_in_month(int32 year, int32 month) {
    static int cnt[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return cnt[month - 1] + (month == 2 && is_leap(year));
  }
  static int32 seconds_in_day() {
    return 24 * 60 * 60;
  }

 public:
  static Result<int32> to_unix_time(int32 year, int32 month, int32 day, int32 hour, int32 minute, int32 second);
  static Result<int32> parse_http_date(std::string slice);
};

using FullConfig = tl_object_ptr<telegram_api::config>;

ActorOwn<> get_full_config(DcId dc_id, IPAddress ip_address, Promise<FullConfig> promise);

class ConfigRecoverer;
class ConfigManager : public NetQueryCallback {
 public:
  explicit ConfigManager(ActorShared<> parent);

  void request_config();

  void get_app_config(Promise<td_api::object_ptr<td_api::JsonValue>> &&promise);

  void get_content_settings(Promise<Unit> &&promise);

  void set_content_settings(bool ignore_sensitive_content_restrictions, Promise<Unit> &&promise);

  void on_dc_options_update(DcOptions dc_options);

 private:
  ActorShared<> parent_;
  int32 config_sent_cnt_{0};
  ActorOwn<ConfigRecoverer> config_recoverer_;
  int ref_cnt_{1};
  Timestamp expire_time_;

  vector<Promise<td_api::object_ptr<td_api::JsonValue>>> get_app_config_queries_;
  vector<Promise<Unit>> get_content_settings_queries_;
  vector<Promise<Unit>> set_content_settings_queries_[2];
  bool is_set_content_settings_request_sent_ = false;
  bool last_set_content_settings_ = false;

  void start_up() override;
  void hangup_shared() override;
  void hangup() override;
  void loop() override;
  void try_stop();

  void on_result(NetQueryPtr res) override;

  void request_config_from_dc_impl(DcId dc_id);
  void process_config(tl_object_ptr<telegram_api::config> config);
  void process_app_config(tl_object_ptr<telegram_api::JSONValue> &config);
  void set_ignore_sensitive_content_restrictions(bool ignore_sensitive_content_restrictions);

  Timestamp load_config_expire_time();
  void save_config_expire(Timestamp timestamp);
  void save_dc_options_update(DcOptions dc_options);
  DcOptions load_dc_options_update();
};

}  // namespace td
