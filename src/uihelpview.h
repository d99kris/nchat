// uihelpview.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>

#include "uiviewbase.h"

class UiHelpView : public UiViewBase
{
public:
  UiHelpView(const UiViewParams& p_Params);

  virtual void Draw();

private:
  static std::vector<std::wstring> GetHelpViews(const int p_MaxW, const std::vector<std::wstring>& p_HelpItems,
                                                const std::wstring& p_OtherHelpItem);
  static void AppendHelpItem(const int p_Key, const std::string& p_Desc, std::vector<std::wstring>& p_HelpItems);
  static std::string GetKeyDisplay(int p_Key);
};
