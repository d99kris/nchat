// uientryview.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include "uiviewbase.h"

class UiEntryView : public UiViewBase
{
public:
  UiEntryView(const UiViewParams& p_Params);

  virtual void Draw();

private:
  int m_CursX = 0;
  int m_CursY = 0;
};
