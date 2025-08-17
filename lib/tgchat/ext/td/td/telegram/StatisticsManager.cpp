//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StatisticsManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/StoryManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TonAmount.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Status.h"

namespace td {

static td_api::object_ptr<td_api::dateRange> convert_date_range(
    const telegram_api::object_ptr<telegram_api::statsDateRangeDays> &obj) {
  return td_api::make_object<td_api::dateRange>(obj->min_date_, obj->max_date_);
}

static td_api::object_ptr<td_api::StatisticalGraph> convert_stats_graph(
    telegram_api::object_ptr<telegram_api::StatsGraph> obj) {
  CHECK(obj != nullptr);

  switch (obj->get_id()) {
    case telegram_api::statsGraphAsync::ID: {
      auto graph = move_tl_object_as<telegram_api::statsGraphAsync>(obj);
      return td_api::make_object<td_api::statisticalGraphAsync>(std::move(graph->token_));
    }
    case telegram_api::statsGraphError::ID: {
      auto graph = move_tl_object_as<telegram_api::statsGraphError>(obj);
      return td_api::make_object<td_api::statisticalGraphError>(std::move(graph->error_));
    }
    case telegram_api::statsGraph::ID: {
      auto graph = move_tl_object_as<telegram_api::statsGraph>(obj);
      return td_api::make_object<td_api::statisticalGraphData>(std::move(graph->json_->data_),
                                                               std::move(graph->zoom_token_));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

static double get_percentage_value(double part, double total, bool is_percentage) {
  if (total < 1e-6 && total > -1e-6) {
    if (part < 1e-6 && part > -1e-6) {
      return 0.0;
    }
    return 100.0;
  }
  if (part > 1e20) {
    return 100.0;
  }
  auto value = part / total * 100;
  if (is_percentage) {
    return clamp(value, 0.0, 100.0);
  } else {
    return max(value, -100.0);
  }
}

static td_api::object_ptr<td_api::statisticalValue> convert_stats_absolute_value(
    const telegram_api::object_ptr<telegram_api::statsAbsValueAndPrev> &obj) {
  return td_api::make_object<td_api::statisticalValue>(
      obj->current_, obj->previous_, get_percentage_value(obj->current_ - obj->previous_, obj->previous_, false));
}

static td_api::object_ptr<td_api::chatStatisticsSupergroup> convert_megagroup_stats(
    Td *td, telegram_api::object_ptr<telegram_api::stats_megagroupStats> obj) {
  CHECK(obj != nullptr);

  td->user_manager_->on_get_users(std::move(obj->users_), "convert_megagroup_stats");

  // just in case
  td::remove_if(obj->top_posters_, [](auto &obj) {
    return !UserId(obj->user_id_).is_valid() || obj->messages_ < 0 || obj->avg_chars_ < 0;
  });
  td::remove_if(obj->top_admins_, [](auto &obj) {
    return !UserId(obj->user_id_).is_valid() || obj->deleted_ < 0 || obj->kicked_ < 0 || obj->banned_ < 0;
  });
  td::remove_if(obj->top_inviters_,
                [](auto &obj) { return !UserId(obj->user_id_).is_valid() || obj->invitations_ < 0; });

  auto top_senders = transform(
      std::move(obj->top_posters_), [td](telegram_api::object_ptr<telegram_api::statsGroupTopPoster> &&top_poster) {
        return td_api::make_object<td_api::chatStatisticsMessageSenderInfo>(
            td->user_manager_->get_user_id_object(UserId(top_poster->user_id_), "get_top_senders"),
            top_poster->messages_, top_poster->avg_chars_);
      });
  auto top_administrators = transform(
      std::move(obj->top_admins_), [td](telegram_api::object_ptr<telegram_api::statsGroupTopAdmin> &&top_admin) {
        return td_api::make_object<td_api::chatStatisticsAdministratorActionsInfo>(
            td->user_manager_->get_user_id_object(UserId(top_admin->user_id_), "get_top_administrators"),
            top_admin->deleted_, top_admin->kicked_, top_admin->banned_);
      });
  auto top_inviters = transform(
      std::move(obj->top_inviters_), [td](telegram_api::object_ptr<telegram_api::statsGroupTopInviter> &&top_inviter) {
        return td_api::make_object<td_api::chatStatisticsInviterInfo>(
            td->user_manager_->get_user_id_object(UserId(top_inviter->user_id_), "get_top_inviters"),
            top_inviter->invitations_);
      });

  return td_api::make_object<td_api::chatStatisticsSupergroup>(
      convert_date_range(obj->period_), convert_stats_absolute_value(obj->members_),
      convert_stats_absolute_value(obj->messages_), convert_stats_absolute_value(obj->viewers_),
      convert_stats_absolute_value(obj->posters_), convert_stats_graph(std::move(obj->growth_graph_)),
      convert_stats_graph(std::move(obj->members_graph_)),
      convert_stats_graph(std::move(obj->new_members_by_source_graph_)),
      convert_stats_graph(std::move(obj->languages_graph_)), convert_stats_graph(std::move(obj->messages_graph_)),
      convert_stats_graph(std::move(obj->actions_graph_)), convert_stats_graph(std::move(obj->top_hours_graph_)),
      convert_stats_graph(std::move(obj->weekdays_graph_)), std::move(top_senders), std::move(top_administrators),
      std::move(top_inviters));
}

static td_api::object_ptr<td_api::chatStatisticsChannel> convert_broadcast_stats(
    telegram_api::object_ptr<telegram_api::stats_broadcastStats> obj) {
  CHECK(obj != nullptr);
  auto recent_interactions = transform(
      std::move(obj->recent_posts_interactions_),
      [](telegram_api::object_ptr<telegram_api::PostInteractionCounters> &&interaction_ptr)
          -> td_api::object_ptr<td_api::chatStatisticsInteractionInfo> {
        switch (interaction_ptr->get_id()) {
          case telegram_api::postInteractionCountersMessage::ID: {
            auto interaction =
                telegram_api::move_object_as<telegram_api::postInteractionCountersMessage>(interaction_ptr);
            return td_api::make_object<td_api::chatStatisticsInteractionInfo>(
                td_api::make_object<td_api::chatStatisticsObjectTypeMessage>(
                    MessageId(ServerMessageId(interaction->msg_id_)).get()),
                interaction->views_, interaction->forwards_, interaction->reactions_);
          }
          case telegram_api::postInteractionCountersStory::ID: {
            auto interaction =
                telegram_api::move_object_as<telegram_api::postInteractionCountersStory>(interaction_ptr);
            return td_api::make_object<td_api::chatStatisticsInteractionInfo>(
                td_api::make_object<td_api::chatStatisticsObjectTypeStory>(StoryId(interaction->story_id_).get()),
                interaction->views_, interaction->forwards_, interaction->reactions_);
          }
          default:
            UNREACHABLE();
            return nullptr;
        }
      });
  return td_api::make_object<td_api::chatStatisticsChannel>(
      convert_date_range(obj->period_), convert_stats_absolute_value(obj->followers_),
      convert_stats_absolute_value(obj->views_per_post_), convert_stats_absolute_value(obj->shares_per_post_),
      convert_stats_absolute_value(obj->reactions_per_post_), convert_stats_absolute_value(obj->views_per_story_),
      convert_stats_absolute_value(obj->shares_per_story_), convert_stats_absolute_value(obj->reactions_per_story_),
      get_percentage_value(obj->enabled_notifications_->part_, obj->enabled_notifications_->total_, true),
      convert_stats_graph(std::move(obj->growth_graph_)), convert_stats_graph(std::move(obj->followers_graph_)),
      convert_stats_graph(std::move(obj->mute_graph_)), convert_stats_graph(std::move(obj->top_hours_graph_)),
      convert_stats_graph(std::move(obj->views_by_source_graph_)),
      convert_stats_graph(std::move(obj->new_followers_by_source_graph_)),
      convert_stats_graph(std::move(obj->languages_graph_)), convert_stats_graph(std::move(obj->interactions_graph_)),
      convert_stats_graph(std::move(obj->reactions_by_emotion_graph_)),
      convert_stats_graph(std::move(obj->story_interactions_graph_)),
      convert_stats_graph(std::move(obj->story_reactions_by_emotion_graph_)),
      convert_stats_graph(std::move(obj->iv_interactions_graph_)), std::move(recent_interactions));
}

class GetMegagroupStatsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::ChatStatistics>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetMegagroupStatsQuery(Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, bool is_dark, DcId dc_id) {
    channel_id_ = channel_id;

    auto input_channel = td_->chat_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::stats_getMegagroupStats(0, is_dark, std::move(input_channel)), {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getMegagroupStats>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(convert_megagroup_stats(td_, result_ptr.move_as_ok()));
  }

  void on_error(Status status) final {
    td_->chat_manager_->on_get_channel_error(channel_id_, status, "GetMegagroupStatsQuery");
    promise_.set_error(std::move(status));
  }
};

class GetBroadcastStatsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::ChatStatistics>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetBroadcastStatsQuery(Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, bool is_dark, DcId dc_id) {
    channel_id_ = channel_id;

    auto input_channel = td_->chat_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::stats_getBroadcastStats(0, is_dark, std::move(input_channel)), {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getBroadcastStats>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = convert_broadcast_stats(result_ptr.move_as_ok());
    for (auto &info : result->recent_interactions_) {
      switch (info->object_type_->get_id()) {
        case td_api::chatStatisticsObjectTypeMessage::ID: {
          MessageId message_id(
              static_cast<const td_api::chatStatisticsObjectTypeMessage *>(info->object_type_.get())->message_id_);
          td_->messages_manager_->on_update_message_interaction_info(
              {DialogId(channel_id_), message_id}, info->view_count_, info->forward_count_, false, nullptr);
          break;
        }
        case td_api::chatStatisticsObjectTypeStory::ID:
          break;
        default:
          UNREACHABLE();
      }
    }
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    td_->chat_manager_->on_get_channel_error(channel_id_, status, "GetBroadcastStatsQuery");
    promise_.set_error(std::move(status));
  }
};

static td_api::object_ptr<td_api::chatRevenueAmount> convert_stars_revenue_status(
    telegram_api::object_ptr<telegram_api::starsRevenueStatus> obj) {
  CHECK(obj != nullptr);
  auto get_amount = [](telegram_api::object_ptr<telegram_api::StarsAmount> &amount) -> int64 {
    CHECK(amount != nullptr);
    if (amount->get_id() != telegram_api::starsTonAmount::ID) {
      LOG(ERROR) << "Receive " << to_string(amount);
      return 0;
    }
    return TonAmount(telegram_api::move_object_as<telegram_api::starsTonAmount>(amount), false).get_ton_amount();
  };

  return td_api::make_object<td_api::chatRevenueAmount>("TON", get_amount(obj->overall_revenue_),
                                                        get_amount(obj->current_balance_),
                                                        get_amount(obj->available_balance_), obj->withdrawal_enabled_);
}

static td_api::object_ptr<td_api::chatRevenueStatistics> convert_ton_revenue_stats(
    telegram_api::object_ptr<telegram_api::payments_starsRevenueStats> obj) {
  CHECK(obj != nullptr);
  return td_api::make_object<td_api::chatRevenueStatistics>(
      convert_stats_graph(std::move(obj->top_hours_graph_)), convert_stats_graph(std::move(obj->revenue_graph_)),
      convert_stars_revenue_status(std::move(obj->status_)),
      obj->usd_rate_ > 0 ? clamp(obj->usd_rate_ * 1e-7, 1e-18, 1e18) : 1.0);
}

class GetTonRevenueStatsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::chatRevenueStatistics>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetTonRevenueStatsQuery(Promise<td_api::object_ptr<td_api::chatRevenueStatistics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, bool is_dark) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::payments_getStarsRevenueStats(0, is_dark, true, std::move(input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getStarsRevenueStats>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetTonRevenueStatsQuery: " << to_string(ptr);
    if (ptr->top_hours_graph_ == nullptr) {
      LOG(ERROR) << "Receive " << to_string(ptr);
      return on_error(Status::Error(500, "Receive invalid response"));
    }
    promise_.set_value(convert_ton_revenue_stats(std::move(ptr)));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetTonRevenueStatsQuery");
    promise_.set_error(std::move(status));
  }
};

class GetTonRevenueWithdrawalUrlQuery final : public Td::ResultHandler {
  Promise<string> promise_;
  DialogId dialog_id_;

 public:
  explicit GetTonRevenueWithdrawalUrlQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> input_check_password) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Have no access to the chat"));
    }

    send_query(G()->net_query_creator().create(telegram_api::payments_getStarsRevenueWithdrawalUrl(
        0, true, std::move(input_peer), 0, std::move(input_check_password))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getStarsRevenueWithdrawalUrl>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(std::move(result_ptr.ok_ref()->url_));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetTonRevenueWithdrawalUrlQuery");
    promise_.set_error(std::move(status));
  }
};

class GetTonRevenueTransactionsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::chatRevenueTransactions>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetTonRevenueTransactionsQuery(Promise<td_api::object_ptr<td_api::chatRevenueTransactions>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const string &offset, int32 limit) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(telegram_api::payments_getStarsTransactions(
        0, false, false, false, true, string(), std::move(input_peer), offset, limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getStarsTransactions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetTonRevenueTransactionsQuery: " << to_string(result);

    td_->user_manager_->on_get_users(std::move(result->users_), "GetTonRevenueTransactionsQuery");
    td_->chat_manager_->on_get_chats(std::move(result->chats_), "GetTonRevenueTransactionsQuery");

    if (result->balance_->get_id() != telegram_api::starsTonAmount::ID) {
      LOG(ERROR) << "Receive " << to_string(result);
      return on_error(Status::Error(500, "Receive invalid response"));
    }

    vector<td_api::object_ptr<td_api::chatRevenueTransaction>> transactions;
    for (auto &transaction : result->history_) {
      if (transaction->amount_->get_id() != telegram_api::starsTonAmount::ID) {
        LOG(ERROR) << "Receive " << to_string(transaction);
        continue;
      }
      auto transaction_amount =
          TonAmount(telegram_api::move_object_as<telegram_api::starsTonAmount>(transaction->amount_), true);
      auto is_refund = transaction->refund_;
      auto is_purchase = transaction_amount.is_positive() == is_refund;
      auto type = [&]() -> td_api::object_ptr<td_api::ChatRevenueTransactionType> {
        switch (transaction->peer_->get_id()) {
          case telegram_api::starsTransactionPeerUnsupported::ID:
          case telegram_api::starsTransactionPeerPremiumBot::ID:
          case telegram_api::starsTransactionPeerAppStore::ID:
          case telegram_api::starsTransactionPeerPlayMarket::ID:
          case telegram_api::starsTransactionPeerAPI::ID:
            return td_api::make_object<td_api::chatRevenueTransactionTypeUnsupported>();
          case telegram_api::starsTransactionPeerFragment::ID: {
            if (is_refund) {
              return td_api::make_object<td_api::chatRevenueTransactionTypeFragmentRefund>(transaction->date_);
            }
            auto state = [&]() -> td_api::object_ptr<td_api::RevenueWithdrawalState> {
              if (transaction->transaction_date_ > 0) {
                SCOPE_EXIT {
                  transaction->transaction_date_ = 0;
                  transaction->transaction_url_.clear();
                };
                return td_api::make_object<td_api::revenueWithdrawalStateSucceeded>(transaction->transaction_date_,
                                                                                    transaction->transaction_url_);
              }
              if (transaction->pending_) {
                transaction->pending_ = false;
                return td_api::make_object<td_api::revenueWithdrawalStatePending>();
              }
              if (transaction->failed_) {
                transaction->failed_ = false;
                return td_api::make_object<td_api::revenueWithdrawalStateFailed>();
              }
              return nullptr;
            }();
            if (state != nullptr) {
              return td_api::make_object<td_api::chatRevenueTransactionTypeFragmentWithdrawal>(transaction->date_,
                                                                                               std::move(state));
            }
            return nullptr;
          }
          case telegram_api::starsTransactionPeerAds::ID:
            if (transaction->ads_proceeds_from_date_ > 0 &&
                transaction->ads_proceeds_from_date_ <= transaction->ads_proceeds_to_date_) {
              SCOPE_EXIT {
                transaction->ads_proceeds_from_date_ = 0;
                transaction->ads_proceeds_to_date_ = 0;
              };
              return td_api::make_object<td_api::chatRevenueTransactionTypeSponsoredMessageEarnings>(
                  transaction->ads_proceeds_from_date_, transaction->ads_proceeds_to_date_);
            }
            return nullptr;
          case telegram_api::starsTransactionPeer::ID: {
            DialogId dialog_id(
                static_cast<const telegram_api::starsTransactionPeer *>(transaction->peer_.get())->peer_);
            if (!dialog_id.is_valid()) {
              return nullptr;
            }
            if (transaction->paid_messages_ && !is_purchase && dialog_id.get_type() == DialogType::User) {
              SCOPE_EXIT {
                transaction->paid_messages_ = 0;
                transaction->title_.clear();
              };
              return td_api::make_object<td_api::chatRevenueTransactionTypeSuggestedPostEarnings>(
                  td_->user_manager_->get_user_id_object(dialog_id.get_user_id(),
                                                         "chatRevenueTransactionTypeSuggestedPostEarnings"));
            }
            return nullptr;
          }
          default:
            UNREACHABLE();
            return nullptr;
        }
      }();
      if (type == nullptr) {
        LOG(ERROR) << "Receive unsupported TON transaction in " << dialog_id_ << ": " << to_string(transaction);
        type = td_api::make_object<td_api::chatRevenueTransactionTypeUnsupported>();
      }
      auto ton_transaction = td_api::make_object<td_api::chatRevenueTransaction>(
          "TON", transaction_amount.get_ton_amount(), std::move(type));
      if (ton_transaction->type_->get_id() != td_api::chatRevenueTransactionTypeUnsupported::ID) {
        /*
        if (product_info != nullptr) {
          LOG(ERROR) << "Receive product info with " << to_string(ton_transaction);
        }
        if (!bot_payload.empty()) {
          LOG(ERROR) << "Receive bot payload with " << to_string(ton_transaction);
        }
        */
        if (transaction->transaction_date_ || !transaction->transaction_url_.empty() || transaction->pending_ ||
            transaction->failed_) {
          LOG(ERROR) << "Receive withdrawal state with " << to_string(ton_transaction);
        }
        if (transaction->msg_id_ != 0) {
          LOG(ERROR) << "Receive message identifier with " << to_string(ton_transaction);
        }
        if (transaction->gift_) {
          LOG(ERROR) << "Receive gift with " << to_string(ton_transaction);
        }
        if (transaction->subscription_period_ != 0) {
          LOG(ERROR) << "Receive subscription period with " << to_string(ton_transaction);
        }
        if (transaction->reaction_) {
          LOG(ERROR) << "Receive reaction with " << to_string(ton_transaction);
        }
        if (!transaction->extended_media_.empty()) {
          LOG(ERROR) << "Receive paid media with " << to_string(ton_transaction);
        }
        if (transaction->giveaway_post_id_ != 0) {
          LOG(ERROR) << "Receive giveaway message with " << to_string(ton_transaction);
        }
        if (transaction->stargift_ != nullptr) {
          LOG(ERROR) << "Receive gift with " << to_string(ton_transaction);
        }
        if (transaction->floodskip_number_ != 0) {
          LOG(ERROR) << "Receive API payment with " << to_string(ton_transaction);
        }
        /*
        if (affiliate != nullptr) {
          LOG(ERROR) << "Receive affiliate with " << to_string(ton_transaction);
        }
        if (commission_per_mille != 0) {
          LOG(ERROR) << "Receive commission with " << to_string(ton_transaction);
        }
        */
        if (transaction->stargift_upgrade_) {
          LOG(ERROR) << "Receive gift upgrade with " << to_string(ton_transaction);
        }
        if (transaction->paid_messages_) {
          LOG(ERROR) << "Receive paid messages with " << to_string(ton_transaction);
        }
        if (transaction->premium_gift_months_) {
          LOG(ERROR) << "Receive Telegram Premium purchase with " << to_string(ton_transaction);
        }
        if (transaction->business_transfer_) {
          LOG(ERROR) << "Receive business bot transfer with " << to_string(ton_transaction);
        }
        if (transaction->stargift_resale_) {
          LOG(ERROR) << "Receive gift resale with " << to_string(ton_transaction);
        }
        if (transaction->ads_proceeds_from_date_ != 0 || transaction->ads_proceeds_to_date_ != 0) {
          LOG(ERROR) << "Receive ads proceeds with " << to_string(ton_transaction);
        }
      }
      transactions.push_back(std::move(ton_transaction));
    }

    auto ton_amount = TonAmount(telegram_api::move_object_as<telegram_api::starsTonAmount>(result->balance_), true);
    promise_.set_value(td_api::make_object<td_api::chatRevenueTransactions>(
        ton_amount.get_ton_amount(), std::move(transactions), result->next_offset_));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetTonRevenueTransactionsQuery");
    promise_.set_error(std::move(status));
  }
};

static td_api::object_ptr<td_api::messageStatistics> convert_message_stats(
    telegram_api::object_ptr<telegram_api::stats_messageStats> obj) {
  return td_api::make_object<td_api::messageStatistics>(
      convert_stats_graph(std::move(obj->views_graph_)),
      convert_stats_graph(std::move(obj->reactions_by_emotion_graph_)));
}

class GetMessageStatsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::messageStatistics>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetMessageStatsQuery(Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, MessageId message_id, bool is_dark, DcId dc_id) {
    channel_id_ = channel_id;

    auto input_channel = td_->chat_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(400, "Supergroup not found");
    }

    send_query(
        G()->net_query_creator().create(telegram_api::stats_getMessageStats(0, is_dark, std::move(input_channel),
                                                                            message_id.get_server_message_id().get()),
                                        {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getMessageStats>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(convert_message_stats(result_ptr.move_as_ok()));
  }

  void on_error(Status status) final {
    td_->chat_manager_->on_get_channel_error(channel_id_, status, "GetMessageStatsQuery");
    promise_.set_error(std::move(status));
  }
};

static td_api::object_ptr<td_api::storyStatistics> convert_story_stats(
    telegram_api::object_ptr<telegram_api::stats_storyStats> obj) {
  return td_api::make_object<td_api::storyStatistics>(convert_stats_graph(std::move(obj->views_graph_)),
                                                      convert_stats_graph(std::move(obj->reactions_by_emotion_graph_)));
}

class GetStoryStatsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::storyStatistics>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetStoryStatsQuery(Promise<td_api::object_ptr<td_api::storyStatistics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, StoryId story_id, bool is_dark, DcId dc_id) {
    channel_id_ = channel_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(DialogId(channel_id), AccessRights::Read);
    if (input_peer == nullptr) {
      return promise_.set_error(400, "Chat not found");
    }

    send_query(G()->net_query_creator().create(
        telegram_api::stats_getStoryStats(0, is_dark, std::move(input_peer), story_id.get()), {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getStoryStats>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(convert_story_stats(result_ptr.move_as_ok()));
  }

  void on_error(Status status) final {
    td_->chat_manager_->on_get_channel_error(channel_id_, status, "GetStoryStatsQuery");
    promise_.set_error(std::move(status));
  }
};

class LoadAsyncGraphQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::StatisticalGraph>> promise_;

 public:
  explicit LoadAsyncGraphQuery(Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &token, int64 x, DcId dc_id) {
    int32 flags = 0;
    if (x != 0) {
      flags |= telegram_api::stats_loadAsyncGraph::X_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::stats_loadAsyncGraph(flags, token, x), {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_loadAsyncGraph>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(convert_stats_graph(std::move(result)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetMessagePublicForwardsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::publicForwards>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetMessagePublicForwardsQuery(Promise<td_api::object_ptr<td_api::publicForwards>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DcId dc_id, MessageFullId message_full_id, const string &offset, int32 limit) {
    dialog_id_ = message_full_id.get_dialog_id();

    auto input_channel = td_->chat_manager_->get_input_channel(dialog_id_.get_channel_id());
    CHECK(input_channel != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::stats_getMessagePublicForwards(
            std::move(input_channel), message_full_id.get_message_id().get_server_message_id().get(), offset, limit),
        {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getMessagePublicForwards>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->statistics_manager_->get_channel_differences_if_needed(result_ptr.move_as_ok(), std::move(promise_),
                                                                "GetMessagePublicForwardsQuery");
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetMessagePublicForwardsQuery");
    promise_.set_error(std::move(status));
  }
};

class GetStoryPublicForwardsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::publicForwards>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetStoryPublicForwardsQuery(Promise<td_api::object_ptr<td_api::publicForwards>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DcId dc_id, StoryFullId story_full_id, const string &offset, int32 limit) {
    dialog_id_ = story_full_id.get_dialog_id();

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't get story statistics"));
    }

    send_query(
        G()->net_query_creator().create(telegram_api::stats_getStoryPublicForwards(
                                            std::move(input_peer), story_full_id.get_story_id().get(), offset, limit),
                                        {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getStoryPublicForwards>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->statistics_manager_->get_channel_differences_if_needed(result_ptr.move_as_ok(), std::move(promise_),
                                                                "GetStoryPublicForwardsQuery");
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetStoryPublicForwardsQuery");
    promise_.set_error(std::move(status));
  }
};

StatisticsManager::StatisticsManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void StatisticsManager::tear_down() {
  parent_.reset();
}

void StatisticsManager::get_channel_statistics(DialogId dialog_id, bool is_dark,
                                               Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise) {
  auto dc_id_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), dialog_id, is_dark, promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
        if (r_dc_id.is_error()) {
          return promise.set_error(r_dc_id.move_as_error());
        }
        send_closure(actor_id, &StatisticsManager::send_get_channel_stats_query, r_dc_id.move_as_ok(),
                     dialog_id.get_channel_id(), is_dark, std::move(promise));
      });
  td_->chat_manager_->get_channel_statistics_dc_id(dialog_id, true, std::move(dc_id_promise));
}

void StatisticsManager::send_get_channel_stats_query(DcId dc_id, ChannelId channel_id, bool is_dark,
                                                     Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  if (td_->chat_manager_->is_megagroup_channel(channel_id)) {
    td_->create_handler<GetMegagroupStatsQuery>(std::move(promise))->send(channel_id, is_dark, dc_id);
  } else {
    td_->create_handler<GetBroadcastStatsQuery>(std::move(promise))->send(channel_id, is_dark, dc_id);
  }
}

void StatisticsManager::get_dialog_revenue_statistics(
    DialogId dialog_id, bool is_dark, Promise<td_api::object_ptr<td_api::chatRevenueStatistics>> &&promise) {
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read,
                                                                        "get_dialog_revenue_statistics"));
  td_->create_handler<GetTonRevenueStatsQuery>(std::move(promise))->send(dialog_id, is_dark);
}

void StatisticsManager::on_update_dialog_revenue_transactions(
    DialogId dialog_id, telegram_api::object_ptr<telegram_api::starsRevenueStatus> &&status) {
  if (!dialog_id.is_valid()) {
    LOG(ERROR) << "Receive updateStarsRevenueStatus in invalid " << dialog_id;
    return;
  }
  if (!td_->messages_manager_->have_dialog(dialog_id)) {
    LOG(INFO) << "Ignore unneeded updateStarsRevenueStatus in " << dialog_id;
    return;
  }
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateChatRevenueAmount>(
                   td_->dialog_manager_->get_chat_id_object(dialog_id, "updateChatRevenueAmount"),
                   convert_stars_revenue_status(std::move(status))));
}

void StatisticsManager::get_dialog_revenue_withdrawal_url(DialogId dialog_id, const string &password,
                                                          Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Write,
                                                                        "get_dialog_revenue_withdrawal_url"));
  if (password.empty()) {
    return promise.set_error(400, "PASSWORD_HASH_INVALID");
  }
  send_closure(
      td_->password_manager_, &PasswordManager::get_input_check_password_srp, password,
      PromiseCreator::lambda([actor_id = actor_id(this), dialog_id, promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP>> result) mutable {
        if (result.is_error()) {
          return promise.set_error(result.move_as_error());
        }
        send_closure(actor_id, &StatisticsManager::send_get_dialog_revenue_withdrawal_url_query, dialog_id,
                     result.move_as_ok(), std::move(promise));
      }));
}

void StatisticsManager::send_get_dialog_revenue_withdrawal_url_query(
    DialogId dialog_id, telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> input_check_password,
    Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<GetTonRevenueWithdrawalUrlQuery>(std::move(promise))
      ->send(dialog_id, std::move(input_check_password));
}

void StatisticsManager::get_dialog_revenue_transactions(
    DialogId dialog_id, const string &offset, int32 limit,
    Promise<td_api::object_ptr<td_api::chatRevenueTransactions>> &&promise) {
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read,
                                                                        "get_dialog_revenue_transactions"));
  td_->create_handler<GetTonRevenueTransactionsQuery>(std::move(promise))->send(dialog_id, offset, limit);
}

void StatisticsManager::get_channel_message_statistics(
    MessageFullId message_full_id, bool is_dark, Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise) {
  auto dc_id_promise = PromiseCreator::lambda([actor_id = actor_id(this), message_full_id, is_dark,
                                               promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
    if (r_dc_id.is_error()) {
      return promise.set_error(r_dc_id.move_as_error());
    }
    send_closure(actor_id, &StatisticsManager::send_get_channel_message_stats_query, r_dc_id.move_as_ok(),
                 message_full_id, is_dark, std::move(promise));
  });
  td_->chat_manager_->get_channel_statistics_dc_id(message_full_id.get_dialog_id(), false, std::move(dc_id_promise));
}

void StatisticsManager::send_get_channel_message_stats_query(
    DcId dc_id, MessageFullId message_full_id, bool is_dark,
    Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  auto dialog_id = message_full_id.get_dialog_id();
  if (!td_->messages_manager_->have_message_force(message_full_id, "send_get_channel_message_stats_query")) {
    return promise.set_error(400, "Message not found");
  }
  if (!td_->messages_manager_->can_get_message_statistics(message_full_id)) {
    return promise.set_error(400, "Message statistics are inaccessible");
  }
  CHECK(dialog_id.get_type() == DialogType::Channel);
  td_->create_handler<GetMessageStatsQuery>(std::move(promise))
      ->send(dialog_id.get_channel_id(), message_full_id.get_message_id(), is_dark, dc_id);
}

void StatisticsManager::get_channel_story_statistics(StoryFullId story_full_id, bool is_dark,
                                                     Promise<td_api::object_ptr<td_api::storyStatistics>> &&promise) {
  auto dc_id_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), story_full_id, is_dark, promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
        if (r_dc_id.is_error()) {
          return promise.set_error(r_dc_id.move_as_error());
        }
        send_closure(actor_id, &StatisticsManager::send_get_channel_story_stats_query, r_dc_id.move_as_ok(),
                     story_full_id, is_dark, std::move(promise));
      });
  td_->chat_manager_->get_channel_statistics_dc_id(story_full_id.get_dialog_id(), false, std::move(dc_id_promise));
}

void StatisticsManager::send_get_channel_story_stats_query(
    DcId dc_id, StoryFullId story_full_id, bool is_dark,
    Promise<td_api::object_ptr<td_api::storyStatistics>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  auto dialog_id = story_full_id.get_dialog_id();
  if (!td_->story_manager_->have_story_force(story_full_id)) {
    return promise.set_error(400, "Story not found");
  }
  if (!td_->story_manager_->can_get_story_statistics(story_full_id)) {
    return promise.set_error(400, "Story statistics are inaccessible");
  }
  CHECK(dialog_id.get_type() == DialogType::Channel);
  td_->create_handler<GetStoryStatsQuery>(std::move(promise))
      ->send(dialog_id.get_channel_id(), story_full_id.get_story_id(), is_dark, dc_id);
}

void StatisticsManager::load_statistics_graph(DialogId dialog_id, string token, int64 x,
                                              Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise) {
  auto dc_id_promise = PromiseCreator::lambda([actor_id = actor_id(this), token = std::move(token), x,
                                               promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
    if (r_dc_id.is_error()) {
      return promise.set_error(r_dc_id.move_as_error());
    }
    send_closure(actor_id, &StatisticsManager::send_load_async_graph_query, r_dc_id.move_as_ok(), std::move(token), x,
                 std::move(promise));
  });
  td_->chat_manager_->get_channel_statistics_dc_id(dialog_id, false, std::move(dc_id_promise));
}

void StatisticsManager::send_load_async_graph_query(DcId dc_id, string token, int64 x,
                                                    Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  td_->create_handler<LoadAsyncGraphQuery>(std::move(promise))->send(token, x, dc_id);
}

void StatisticsManager::get_message_public_forwards(MessageFullId message_full_id, string offset, int32 limit,
                                                    Promise<td_api::object_ptr<td_api::publicForwards>> &&promise) {
  if (limit <= 0) {
    return promise.set_error(400, "Parameter limit must be positive");
  }

  auto dc_id_promise = PromiseCreator::lambda([actor_id = actor_id(this), message_full_id, offset = std::move(offset),
                                               limit, promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
    if (r_dc_id.is_error()) {
      return promise.set_error(r_dc_id.move_as_error());
    }
    send_closure(actor_id, &StatisticsManager::send_get_message_public_forwards_query, r_dc_id.move_as_ok(),
                 message_full_id, std::move(offset), limit, std::move(promise));
  });
  td_->chat_manager_->get_channel_statistics_dc_id(message_full_id.get_dialog_id(), false, std::move(dc_id_promise));
}

void StatisticsManager::send_get_message_public_forwards_query(
    DcId dc_id, MessageFullId message_full_id, string offset, int32 limit,
    Promise<td_api::object_ptr<td_api::publicForwards>> &&promise) {
  if (!td_->messages_manager_->have_message_force(message_full_id, "send_get_message_public_forwards_query")) {
    return promise.set_error(400, "Message not found");
  }
  if (!td_->messages_manager_->can_get_message_statistics(message_full_id)) {
    return promise.set_error(400, "Message forwards are inaccessible");
  }

  static constexpr int32 MAX_MESSAGE_FORWARDS = 100;  // server-side limit
  if (limit > MAX_MESSAGE_FORWARDS) {
    limit = MAX_MESSAGE_FORWARDS;
  }

  td_->create_handler<GetMessagePublicForwardsQuery>(std::move(promise))->send(dc_id, message_full_id, offset, limit);
}

void StatisticsManager::get_story_public_forwards(StoryFullId story_full_id, string offset, int32 limit,
                                                  Promise<td_api::object_ptr<td_api::publicForwards>> &&promise) {
  if (limit <= 0) {
    return promise.set_error(400, "Parameter limit must be positive");
  }
  auto dialog_id = story_full_id.get_dialog_id();
  if (dialog_id.get_type() == DialogType::User) {
    if (dialog_id != td_->dialog_manager_->get_my_dialog_id()) {
      return promise.set_error(400, "Have no access to story statistics");
    }
    return send_get_story_public_forwards_query(DcId::main(), story_full_id, std::move(offset), limit,
                                                std::move(promise));
  }

  auto dc_id_promise = PromiseCreator::lambda([actor_id = actor_id(this), story_full_id, offset = std::move(offset),
                                               limit, promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
    if (r_dc_id.is_error()) {
      return promise.set_error(r_dc_id.move_as_error());
    }
    send_closure(actor_id, &StatisticsManager::send_get_story_public_forwards_query, r_dc_id.move_as_ok(),
                 story_full_id, std::move(offset), limit, std::move(promise));
  });
  td_->chat_manager_->get_channel_statistics_dc_id(dialog_id, false, std::move(dc_id_promise));
}

void StatisticsManager::send_get_story_public_forwards_query(
    DcId dc_id, StoryFullId story_full_id, string offset, int32 limit,
    Promise<td_api::object_ptr<td_api::publicForwards>> &&promise) {
  if (!td_->story_manager_->have_story_force(story_full_id)) {
    return promise.set_error(400, "Story not found");
  }
  if (!td_->story_manager_->can_get_story_statistics(story_full_id) &&
      story_full_id.get_dialog_id() != td_->dialog_manager_->get_my_dialog_id()) {
    return promise.set_error(400, "Story forwards are inaccessible");
  }

  static constexpr int32 MAX_STORY_FORWARDS = 100;  // server-side limit
  if (limit > MAX_STORY_FORWARDS) {
    limit = MAX_STORY_FORWARDS;
  }

  td_->create_handler<GetStoryPublicForwardsQuery>(std::move(promise))->send(dc_id, story_full_id, offset, limit);
}

void StatisticsManager::on_get_public_forwards(
    telegram_api::object_ptr<telegram_api::stats_publicForwards> &&public_forwards,
    Promise<td_api::object_ptr<td_api::publicForwards>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  auto total_count = public_forwards->count_;
  LOG(INFO) << "Receive " << public_forwards->forwards_.size() << " forwarded stories out of "
            << public_forwards->count_;
  vector<td_api::object_ptr<td_api::PublicForward>> result;
  for (auto &forward_ptr : public_forwards->forwards_) {
    switch (forward_ptr->get_id()) {
      case telegram_api::publicForwardMessage::ID: {
        auto forward = telegram_api::move_object_as<telegram_api::publicForwardMessage>(forward_ptr);
        auto dialog_id = DialogId::get_message_dialog_id(forward->message_);
        auto message_full_id = td_->messages_manager_->on_get_message(dialog_id, std::move(forward->message_), false,
                                                                      false, false, "on_get_public_forwards");
        if (message_full_id != MessageFullId()) {
          result.push_back(td_api::make_object<td_api::publicForwardMessage>(
              td_->messages_manager_->get_message_object(message_full_id, "on_get_public_forwards")));
          CHECK(result.back() != nullptr);
        } else {
          total_count--;
        }
        break;
      }
      case telegram_api::publicForwardStory::ID: {
        auto forward = telegram_api::move_object_as<telegram_api::publicForwardStory>(forward_ptr);
        auto dialog_id = DialogId(forward->peer_);
        auto story_id = td_->story_manager_->on_get_story(dialog_id, std::move(forward->story_));
        if (story_id.is_valid() && td_->story_manager_->have_story({dialog_id, story_id})) {
          result.push_back(td_api::make_object<td_api::publicForwardStory>(
              td_->story_manager_->get_story_object({dialog_id, story_id})));
          CHECK(result.back() != nullptr);
        } else {
          total_count--;
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  if (total_count < static_cast<int32>(result.size())) {
    LOG(ERROR) << "Receive " << result.size() << " valid story sorwards out of " << total_count;
    total_count = static_cast<int32>(result.size());
  }
  promise.set_value(
      td_api::make_object<td_api::publicForwards>(total_count, std::move(result), public_forwards->next_offset_));
}

void StatisticsManager::get_channel_differences_if_needed(
    telegram_api::object_ptr<telegram_api::stats_publicForwards> &&public_forwards,
    Promise<td_api::object_ptr<td_api::publicForwards>> promise, const char *source) {
  td_->user_manager_->on_get_users(std::move(public_forwards->users_), "stats_publicForwards");
  td_->chat_manager_->on_get_chats(std::move(public_forwards->chats_), "stats_publicForwards");

  vector<const telegram_api::object_ptr<telegram_api::Message> *> messages;
  for (const auto &forward : public_forwards->forwards_) {
    CHECK(forward != nullptr);
    if (forward->get_id() != telegram_api::publicForwardMessage::ID) {
      continue;
    }
    messages.push_back(&static_cast<const telegram_api::publicForwardMessage *>(forward.get())->message_);
  }
  td_->messages_manager_->get_channel_differences_if_needed(
      messages,
      PromiseCreator::lambda([actor_id = actor_id(this), public_forwards = std::move(public_forwards),
                              promise = std::move(promise)](Result<Unit> &&result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          send_closure(actor_id, &StatisticsManager::on_get_public_forwards, std::move(public_forwards),
                       std::move(promise));
        }
      }),
      source);
}

td_api::object_ptr<td_api::StatisticalGraph> StatisticsManager::convert_stats_graph(
    telegram_api::object_ptr<telegram_api::StatsGraph> obj) {
  return ::td::convert_stats_graph(std::move(obj));
}

}  // namespace td
