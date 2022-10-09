// clipboard.cpp
//
// Copyright (c) 2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "clipboard.h"

#include "clip.h"

void Clipboard::SetText(const std::string& p_Text)
{
  clip::set_text(p_Text);
}

std::string Clipboard::GetText()
{
  std::string text;
  clip::get_text(text);
  return text;
}
