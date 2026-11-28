// uikeyinput.cpp
//
// Copyright (c) 2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uikeyinput.h"

#include <cstdio>

#include <ncurses.h>

#include "uikeyconfig.h"

int UiKeyInput::GetWch(wint_t* p_Wch)
{
  int rv = get_wch(p_Wch);
  if (rv == KEY_CODE_YES)
  {
    *p_Wch = UiKeyConfig::GetOffsettedKeyCode(*p_Wch, true);
  }

  return rv;
}

void UiKeyInput::EnableMetaSendsEsc()
{
  // save xterm meta/alt sends escape modes, then enable them, so that alt-key
  // combinations are sent esc-prefixed (ignored by terminals not supporting it)
  printf("\033[?1036;1039s\033[?1036;1039h");
}

void UiKeyInput::RestoreMetaSendsEsc()
{
  // restore xterm meta/alt sends escape modes
  printf("\033[?1036;1039r");
}
