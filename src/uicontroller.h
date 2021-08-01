// uicontroller.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <ncursesw/ncurses.h>

class UiController
{
public:
  static wint_t GetKey(int p_TimeOutMs);

private:
};
