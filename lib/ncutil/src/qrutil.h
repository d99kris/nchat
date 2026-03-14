// qrutil.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

class QrUtil
{
public:
  static std::string ToTerminalString(const std::string& p_Text);
  static bool WritePngFile(const std::string& p_Text, const std::string& p_Path);
};
