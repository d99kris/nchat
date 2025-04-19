// uiviewbase.h
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <mutex>

#include <ncurses.h>

class UiModel;

struct UiViewParams
{
  UiViewParams(int p_X, int p_Y, int p_W, int p_H, bool p_Enabled, UiModel* p_Model)
    : x(p_X)
    , y(p_Y)
    , w(p_W)
    , h(p_H)
    , enabled(p_Enabled)
    , model(p_Model)
  {
  }

  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  bool enabled = false;
  UiModel* model = nullptr;
};

class UiViewBase
{
public:
  UiViewBase(const UiViewParams& p_Params);
  virtual ~UiViewBase();

  virtual void Draw() = 0;

  int W();
  int H();
  int X();
  int Y();
  void SetDirty(bool p_Dirty);

protected:
  int m_X = 0;
  int m_Y = 0;
  int m_W = 0;
  int m_H = 0;
  bool m_Enabled = false;
  UiModel* m_Model = nullptr;
  bool m_Dirty = true;
  WINDOW* m_Win = nullptr;
};
