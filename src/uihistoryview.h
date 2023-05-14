// uihistoryview.h
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include "uiviewbase.h"

class UiHistoryView : public UiViewBase
{
public:
  UiHistoryView(const UiViewParams& p_Params);
  virtual ~UiHistoryView();

  virtual void Draw();
  int GetHistoryShowCount();

private:
  std::string GetTimeString(int64_t p_TimeSent);

private:
  WINDOW* m_PaddedWin = nullptr;
  int m_PaddedH = 0;
  int m_PaddedW = 0;
  int m_HistoryShowCount = 0;
};
