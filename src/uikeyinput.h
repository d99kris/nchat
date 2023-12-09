// uikeyinput.h
//
// Copyright (c) 2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <ncurses.h>

class UiKeyInput
{
public:
  static int GetWch(wint_t* p_Wch);
};
