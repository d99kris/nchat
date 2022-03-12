// uitopview.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include "uiviewbase.h"

class UiTopView : public UiViewBase
{
public:
  UiTopView(const UiViewParams& p_Params);

  virtual void Draw();
};
