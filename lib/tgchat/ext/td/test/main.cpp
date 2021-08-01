//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <cstring>

#if TD_EMSCRIPTEN
#include <emscripten.h>
#endif

int main(int argc, char **argv) {
  // TODO port OptionsParser to Windows
  td::TestsRunner &runner = td::TestsRunner::get_default();
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--filter")) {
      CHECK(i + 1 < argc);
      runner.add_substr_filter(argv[++i]);
    }
    if (!std::strcmp(argv[i], "--stress")) {
      runner.set_stress_flag(true);
    }
  }
#if TD_EMSCRIPTEN
  emscripten_set_main_loop(
      [] {
        td::TestsRunner &default_runner = td::TestsRunner::get_default();
        if (!default_runner.run_all_step()) {
          emscripten_cancel_main_loop();
        }
      },
      10, 0);
#else
  runner.run_all();
#endif
  return 0;
}
