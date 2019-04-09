// uilite.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <map>
#include <mutex>
#include <stack>
#include <string>
#include <vector>

#include <ncursesw/ncurses.h>

#include "config.h"
#include "uicommon.h"

class Config;
class Contact;
struct Message;
class Protocol;

class UiLite : public UiCommon
{
public:
  UiLite();
  virtual ~UiLite();

private:
  virtual std::map<std::string, std::string> GetPrivateConfig();
  virtual void PrivateInit();
  virtual void RedrawContactWin();
  virtual void SetupWin();
  virtual void CleanupWin();

  WINDOW* m_StatusWin = NULL;
};
