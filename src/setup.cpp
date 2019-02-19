// setup.cpp
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "setup.h"

#include <iostream>
#include <vector>

#include "config.h"
#include "protocol.h"

bool Setup::SetupProtocol(Config& p_Config, std::vector<std::shared_ptr<Protocol>> p_Protocols)
{
  std::cout << "Protocols:" << std::endl;
  size_t idx = 0;
  for (auto it = p_Protocols.begin(); it != p_Protocols.end(); ++it, ++idx)
  {
    std::cout << idx << ". " << (*it)->GetName() << std::endl;
  }
  std::cout << idx << ". Exit setup" << std::endl;
  
  size_t selectidx = 0;
  std::cout << "Select protocol (" << selectidx << "): ";
  std::string line;
  std::getline(std::cin, line);

  if (!line.empty())
  {
    selectidx = stoi(line);
  }

  if (selectidx >= p_Protocols.size())
  {
    std::cout << "Setup aborted, exiting." << std::endl;
    return false;
  }

  bool rv = p_Protocols.at(selectidx)->Setup();
  if (rv)
  {
    std::string param(p_Protocols.at(selectidx)->GetName() + std::string("_is_enabled"));
    p_Config.Set(param, "1");
  }

  return rv;
}
