//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/net/HttpQuery.h"
#include "td/net/Wget.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <string>

int main(int argc, char *argv[]) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  td::VERBOSITY_NAME(fd) = VERBOSITY_NAME(INFO);

  std::string url = (argc > 1 ? argv[1] : "https://telegram.org");
  auto timeout = 10;
  auto ttl = 3;
  auto prefer_ipv6 = (argc > 2 && std::string(argv[2]) == "-6");
  auto scheduler = td::make_unique<td::ConcurrentScheduler>();
  scheduler->init(0);
  scheduler
      ->create_actor_unsafe<td::Wget>(0, "Client",
                                      td::PromiseCreator::lambda([](td::Result<td::unique_ptr<td::HttpQuery>> res) {
                                        LOG(ERROR) << *res.ok();
                                        td::Scheduler::instance()->finish();
                                      }),
                                      url, td::Auto(), timeout, ttl, prefer_ipv6)
      .release();
  scheduler->start();
  while (scheduler->run_main(10)) {
    // empty
  }
  scheduler->finish();
  return 0;
}
