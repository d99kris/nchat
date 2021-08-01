//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class HttpUrl {
 public:
  enum class Protocol { HTTP, HTTPS } protocol_ = Protocol::HTTP;
  string userinfo_;
  string host_;
  bool is_ipv6_ = false;
  int specified_port_ = 0;
  int port_ = 0;
  string query_;

  string get_url() const;

  HttpUrl(Protocol protocol, string userinfo, string host, bool is_ipv6, int specified_port, int port, string query)
      : protocol_(protocol)
      , userinfo_(std::move(userinfo))
      , host_(std::move(host))
      , is_ipv6_(is_ipv6)
      , specified_port_(specified_port)
      , port_(port)
      , query_(std::move(query)) {
  }
};

Result<HttpUrl> parse_url(Slice url,
                          HttpUrl::Protocol default_protocol = HttpUrl::Protocol::HTTP) TD_WARN_UNUSED_RESULT;

StringBuilder &operator<<(StringBuilder &sb, const HttpUrl &url);

string get_url_query_file_name(const string &query);

string get_url_file_name(Slice url);

}  // namespace td
