// main.cpp
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include <algorithm>
#include <iostream>

#include <path.hpp>

#include "config.h"
#include "log.h"
#include "loghelp.h"
#include "uidefault.h"
#include "uilite.h"
#include "setup.h"
#include "telegram.h"
#include "util.h"

static void ShowVersion();
static void ShowHelp();

int main(int argc, char *argv[])
{
  // Argument handling
  bool isSetup = false;
  bool isVerbose = false;
  const std::vector<std::string> args(argv + 1, argv + argc);
  for (auto& arg : args)
  {
    if ((arg == "-s") || (arg == "--setup"))
    {
      isSetup = true;
    }
    else if ((arg == "-h") || (arg == "--help"))
    {
      ShowHelp();
      return 0;
    }
    else if ((arg == "-e") || (arg == "--verbose"))
    {
      isVerbose = true;
    }
    else if ((arg == "-v") || (arg == "--version"))
    {
      ShowVersion();
      return 0;
    }
    else
    {
      ShowHelp();
      return 1;
    }
  }

  // Ensure application config dir exists
  if (!apathy::Path(Util::GetConfigDir()).exists())
  {
    apathy::Path::makedirs(Util::GetConfigDir());
  }

  // Init logging
  const std::string& logPath = Util::GetConfigDir() + std::string("/main.log");
  Log::SetPath(logPath);
  Util::InitStdErrRedirect(logPath);

  // Init signal handler
  Util::RegisterSignalHandler();

  const std::string version = Util::GetAppVersion();
  LOG_INFO("starting nchat %s", version.c_str());

  const std::string os = Util::GetOs();
  const std::string compiler = Util::GetCompiler();
  LOG_INFO("using %s/%s", os.c_str(), compiler.c_str());

  // Init config
  const std::map<std::string, std::string> defaultConfig =
  {
    {"telegram_is_enabled", "0"},
    {"ui", "uidefault"}
  };
  const std::string configPath(Util::GetConfigDir() + std::string("/main.conf"));
  Config config(configPath, defaultConfig);

  // Init UI
  std::shared_ptr<Ui> ui = nullptr;
  if (!isSetup)
  {
    std::vector<std::shared_ptr<Ui>> allUis =
    {
      std::make_shared<UiDefault>(),
      std::make_shared<UiLite>(),
    };

    for (auto it = allUis.begin(); it != allUis.end(); ++it)
    {
      if (config.Get("ui") == (*it)->GetName())
      {
        ui = *it;
      }
    }

    if (ui.get() != nullptr)
    {
      ui->Init();
    }
    else
    {
      LOG_ERROR("failed loading ui \"%s\"", config.Get("ui").c_str());
      return 1;
    }
  }

  // Construct protocols
  std::vector<std::shared_ptr<Protocol>> allProtocols =
  {
    std::make_shared<Telegram>(ui, isSetup, isVerbose),
  };

  // Handle setup
  if (isSetup)
  {
    bool rv = Setup::SetupProtocol(config, allProtocols);
    if (rv)
    {
      std::cout << "Saving to " << configPath << std::endl;
      config.Save(configPath);
    }
    return rv;
  }

  // Init / start protocols
  for (auto it = allProtocols.begin(); it != allProtocols.end(); ++it)
  {
    std::string param((*it)->GetName() + std::string("_is_enabled"));
    if (config.Get(param) == "1")
    {
      (*it)->Start();
      ui->AddProtocol((*it).get());      
    }
  }

  // Start UI
  ui->Run();

  // Save config
  config.Save(configPath);
  
  if (ui.get() != nullptr)
  {
    ui->Cleanup();
  }
  
  // Stop protocols
  for (auto it = allProtocols.begin(); it != allProtocols.end(); ++it)
  {
    std::string param((*it)->GetName() + std::string("_is_enabled"));
    std::transform(param.begin(), param.end(), param.begin(), ::tolower);
    if (config.Get(param) == "1")
    {
      ui->RemoveProtocol((*it).get());
      (*it)->Stop();
    }
  }

  // Cleanup protocols
  allProtocols.clear();

  return 0;
}

static void ShowHelp()
{
  std::cout <<
    "nchat is a minimalistic console-based chat client with support for\n"
    "telegram.\n"
    "\n"
    "Usage: nchat [OPTION]\n"
    "\n"
    "Command-line Options:\n"
    "   -e, --verbose     enable verbose logging\n"
    "   -h, --help        display this help and exit\n"
    "   -s, --setup       set up chat protocol account\n"
    "   -v, --version     output version information and exit\n"
    "\n"
    "Interactive Commands:\n"
    "   Tab         next chat\n"
    "   Sh-Tab      previous chat\n"
    "   PageDn      next page\n"
    "   PageUp      previous page\n"
    "   Ctrl-e      enable/disable emoji\n"
    "   Ctrl-x      send message\n"
    "   Ctrl-u      next unread chat\n"
    "   Ctrl-q      exit\n"
    "\n"
    "Report bugs at https://github.com/d99kris/nchat\n"
    "\n";
}

static void ShowVersion()
{
  std::cout <<
    "nchat " << Util::GetAppVersion() << "\n"
    "\n"
    "Copyright (c) 2019 Kristofer Berggren\n"
    "\n"
    "nchat is distributed under the MIT license.\n"
    "\n"
    "Written by Kristofer Berggren.\n";
}
