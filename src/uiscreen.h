// uiscreen.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

class UiScreen
{
public:
  UiScreen();
  int W();
  int H();

private:
  int m_W = 0;
  int m_H = 0;
};
