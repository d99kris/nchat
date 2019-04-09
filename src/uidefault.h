// uidefault.h
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

class UiDefault : public UiCommon
{
public:
  UiDefault();
  virtual ~UiDefault();

private:
  virtual std::map<std::string, std::string> GetPrivateConfig();
  virtual void PrivateInit();
  virtual void RedrawContactWin();
  virtual void SetupWin();
  virtual void CleanupWin();
  
  WINDOW* m_ListWin = NULL;

  WINDOW* m_InBorderWin = NULL;
  WINDOW* m_OutBorderWin = NULL;
  WINDOW* m_ListBorderWin = NULL;
  
  size_t m_ListWidth = 0;
  size_t m_ListHeight = 0;
};
