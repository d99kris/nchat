// uikeyinput.cpp
//
// Copyright (c) 2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uikeyinput.h"

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
