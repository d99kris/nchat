// setup.h
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <memory>
#include <vector>

class Config;
class Protocol;

class Setup
{
public:
  static bool SetupProtocol(Config& p_Config, std::vector<std::shared_ptr<Protocol>> p_Protocols);
};
