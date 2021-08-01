//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/PollManager.h"
#include "td/telegram/Version.h"

#include "td/utils/common.h"
#include "td/utils/misc.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void PollManager::PollOption::store(StorerT &storer) const {
  using ::td::store;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_chosen);
  END_STORE_FLAGS();

  store(text, storer);
  store(data, storer);
  store(voter_count, storer);
}

template <class ParserT>
void PollManager::PollOption::parse(ParserT &parser) {
  using ::td::parse;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_chosen);
  END_PARSE_FLAGS();

  parse(text, parser);
  parse(data, parser);
  parse(voter_count, parser);
}

template <class StorerT>
void PollManager::Poll::store(StorerT &storer) const {
  using ::td::store;
  bool is_public = !is_anonymous;
  bool has_recent_voters = !recent_voter_user_ids.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_closed);
  STORE_FLAG(is_public);
  STORE_FLAG(allow_multiple_answers);
  STORE_FLAG(is_quiz);
  STORE_FLAG(has_recent_voters);
  END_STORE_FLAGS();

  store(question, storer);
  store(options, storer);
  store(total_voter_count, storer);
  if (is_quiz) {
    store(correct_option_id, storer);
  }
  if (has_recent_voters) {
    store(recent_voter_user_ids, storer);
  }
}

template <class ParserT>
void PollManager::Poll::parse(ParserT &parser) {
  using ::td::parse;
  bool is_public;
  bool has_recent_voters;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_closed);
  PARSE_FLAG(is_public);
  PARSE_FLAG(allow_multiple_answers);
  PARSE_FLAG(is_quiz);
  PARSE_FLAG(has_recent_voters);
  END_PARSE_FLAGS();
  is_anonymous = !is_public;

  parse(question, parser);
  parse(options, parser);
  parse(total_voter_count, parser);
  if (is_quiz) {
    parse(correct_option_id, parser);
    if (correct_option_id < -1 || correct_option_id >= static_cast<int32>(options.size())) {
      parser.set_error("Wrong correct_option_id");
    }
  }
  if (has_recent_voters) {
    parse(recent_voter_user_ids, parser);
  }
}

template <class StorerT>
void PollManager::store_poll(PollId poll_id, StorerT &storer) const {
  td::store(poll_id.get(), storer);
  if (is_local_poll_id(poll_id)) {
    auto poll = get_poll(poll_id);
    CHECK(poll != nullptr);
    BEGIN_STORE_FLAGS();
    STORE_FLAG(poll->is_closed);
    STORE_FLAG(poll->is_anonymous);
    STORE_FLAG(poll->allow_multiple_answers);
    STORE_FLAG(poll->is_quiz);
    END_STORE_FLAGS();
    store(poll->question, storer);
    vector<string> options = transform(poll->options, [](const PollOption &option) { return option.text; });
    store(options, storer);
    if (poll->is_quiz) {
      store(poll->correct_option_id, storer);
    }
  }
}

template <class ParserT>
PollId PollManager::parse_poll(ParserT &parser) {
  int64 poll_id_int;
  td::parse(poll_id_int, parser);
  PollId poll_id(poll_id_int);
  if (is_local_poll_id(poll_id)) {
    string question;
    vector<string> options;
    bool is_closed = false;
    bool is_anonymous = true;
    bool allow_multiple_answers = false;
    bool is_quiz = false;
    int32 correct_option_id = -1;

    if (parser.version() >= static_cast<int32>(Version::SupportPolls2_0)) {
      BEGIN_PARSE_FLAGS();
      PARSE_FLAG(is_closed);
      PARSE_FLAG(is_anonymous);
      PARSE_FLAG(allow_multiple_answers);
      PARSE_FLAG(is_quiz);
      END_PARSE_FLAGS();
    }
    parse(question, parser);
    parse(options, parser);
    if (is_quiz) {
      parse(correct_option_id, parser);
      if (correct_option_id < -1 || correct_option_id >= static_cast<int32>(options.size())) {
        parser.set_error("Wrong correct_option_id");
      }
    }
    if (parser.get_error() != nullptr) {
      return PollId();
    }
    return create_poll(std::move(question), std::move(options), is_anonymous, allow_multiple_answers, is_quiz,
                       correct_option_id, is_closed);
  }

  auto poll = get_poll_force(poll_id);
  if (poll == nullptr) {
    return PollId();
  }
  return poll_id;
}

}  // namespace td
