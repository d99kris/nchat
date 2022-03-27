// uicontroller.cpp
//
// Copyright (c) 2019-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uicontroller.h"

#include <unistd.h>
#include <sys/select.h>

UiController::UiController()
{
  define_key("\033[I", KEY_FOCUS_IN);
  define_key("\033[O", KEY_FOCUS_OUT);
}

UiController::~UiController()
{
}

wint_t UiController::GetKey(int p_TimeOutMs)
{
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  int maxfd = STDIN_FILENO;
  struct timeval tv = {(p_TimeOutMs / 1000), (p_TimeOutMs % 1000) * 1000};
  wint_t key = 0;
  select(maxfd + 1, &fds, NULL, NULL, &tv); // ignore select() rv to get resize events
  if (FD_ISSET(STDIN_FILENO, &fds))
  {
    get_wch(&key);
  }

  return key;
}
