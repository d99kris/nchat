// main.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>

#include <path.hpp>

#include "apputil.h"
#include "fileutil.h"
#include "log.h"
#include "messagecache.h"
#include "profiles.h"
#include "scopeddirlock.h"
#include "tgchat.h"
#include "ui.h"

#ifdef HAS_DUMMY
#include "duchat.h"
#endif

#ifdef HAS_WHATSAPP
#include "wachat.h"
#endif

static bool SetupProfile();
static void ShowHelp();
static void ShowVersion();

static std::vector<std::shared_ptr<Protocol>> GetProtocols()
{
  std::vector<std::shared_ptr<Protocol>> protocols =
  {
#ifdef HAS_DUMMY
    std::make_shared<DuChat>(),
#endif
    std::make_shared<TgChat>(),
#ifdef HAS_WHATSAPP
    std::make_shared<WaChat>(),
#endif
  };

  return protocols;
}

int main(int argc, char* argv[])
{
  // Defaults
  umask(S_IRWXG | S_IRWXO);
  FileUtil::SetApplicationDir(std::string(getenv("HOME")) + std::string("/.nchat"));
  Log::SetVerboseLevel(Log::INFO_LEVEL);

  // Argument handling
  bool isSetup = false;
  std::vector<std::string> args(argv + 1, argv + argc);
  for (auto it = args.begin(); it != args.end(); ++it)
  {
    if (((*it == "-d") || (*it == "--configdir")) && (std::distance(it + 1, args.end()) > 0))
    {
      ++it;
      FileUtil::SetApplicationDir(*it);
    }
    else if ((*it == "-e") || (*it == "--verbose"))
    {
      Log::SetVerboseLevel(Log::DEBUG_LEVEL);
    }
    else if ((*it == "-ee") || (*it == "--extra-verbose"))
    {
      Log::SetVerboseLevel(Log::TRACE_LEVEL);
    }
    else if ((*it == "-h") || (*it == "--help"))
    {
      ShowHelp();
      return 0;
    }
    else if ((*it == "-s") || (*it == "--setup"))
    {
      isSetup = true;
    }
    else if ((*it == "-v") || (*it == "--version"))
    {
      ShowVersion();
      return 0;
    }
    else if (*it == "-x")
    {
      AppUtil::SetDeveloperMode(true);
    }
    else
    {
      ShowHelp();
      return 1;
    }
  }

  bool isDirInited = false;
  static const int dirVersion = 1;
  if (!apathy::Path(FileUtil::GetApplicationDir()).exists())
  {
    FileUtil::InitDirVersion(FileUtil::GetApplicationDir(), dirVersion);
    isDirInited = true;
  }

  ScopedDirLock dirLock(FileUtil::GetApplicationDir());
  if (!dirLock.IsLocked())
  {
    std::cerr <<
      "error: unable to acquire lock for " << FileUtil::GetApplicationDir() << "\n" <<
      "       only one nchat session per account/confdir is supported.\n";
    return 1;
  }

  if (!isDirInited)
  {
    int storedVersion = FileUtil::GetDirVersion(FileUtil::GetApplicationDir());
    if (storedVersion != dirVersion)
    {
      if (isSetup)
      {
        FileUtil::InitDirVersion(FileUtil::GetApplicationDir(), dirVersion);
      }
      else
      {
        std::cout << "Config dir " << FileUtil::GetApplicationDir() << " is incompatible with this version of nchat\n";
        if ((storedVersion == -1) && FileUtil::Exists(FileUtil::GetApplicationDir() + "/tdlib"))
        {
          std::cout << "Attempt to migrate config dir to new version (y/n)? ";
          std::string migrateYesNo;
          std::getline(std::cin, migrateYesNo);
          if (migrateYesNo != "y")
          {
            std::cout << "Migration cancelled, exiting.\n";
            return 1;
          }

          std::cout << "Enter phone number (optional, ex. +6511111111): ";
          std::string phoneNumber = "";
          std::getline(std::cin, phoneNumber);

          std::string profileName = "Telegram_" + phoneNumber;
          std::string tmpDir = "/tmp/" + profileName;
          FileUtil::RmDir(tmpDir);
          FileUtil::MkDir(tmpDir);
          FileUtil::Move(FileUtil::GetApplicationDir() + "/tdlib", tmpDir + "/tdlib");
          FileUtil::Move(FileUtil::GetApplicationDir() + "/telegram.conf", tmpDir + "/telegram.conf");

          FileUtil::InitDirVersion(FileUtil::GetApplicationDir(), dirVersion);
          Profiles::Init();

          FileUtil::Move(tmpDir, FileUtil::GetApplicationDir() + "/profiles/" + profileName);
        }
        else
        {
          std::cerr << "error: invalid config dir content, exiting.\n";
          return 1;
        }
      }
    }
  }

  // Init profiles dir
  Profiles::Init();

  // Init logging
  const std::string& logPath = FileUtil::GetApplicationDir() + std::string("/log.txt");
  Log::SetPath(logPath);
  std::string appNameVersion = AppUtil::GetAppNameVersion();
  LOG_INFO("starting %s", appNameVersion.c_str());

  // Run setup if required
  if (isSetup)
  {
    bool rv = SetupProfile();
    return rv ? 0 : 1;
  }

  // Init ui
  std::shared_ptr<Ui> ui = std::make_shared<Ui>();

  // Init message cache
  std::function<void(std::shared_ptr<ServiceMessage>)> messageHandler =
    std::bind(&Ui::MessageHandler, std::ref(*ui), std::placeholders::_1);
  MessageCache::Init(messageHandler);

  // Load profile(s)
  std::string profilesDir = FileUtil::GetApplicationDir() + "/profiles";
  const std::vector<apathy::Path>& profilePaths = apathy::Path::listdir(profilesDir);
  for (auto& profilePath : profilePaths)
  {
    std::stringstream ss(profilePath.filename());
    std::string protocolName;
    if (!std::getline(ss, protocolName, '_'))
    {
      LOG_WARNING("invalid profile name, skipping.");
      continue;
    }

    std::vector<std::shared_ptr<Protocol>> allProtocols = GetProtocols();
    for (auto& protocol : allProtocols)
    {
      if (protocol->GetProfileId() == protocolName)
      {
        protocol->LoadProfile(profilesDir, profilePath.filename());
        ui->AddProtocol(protocol);
        MessageCache::AddProfile(profilePath.filename());
      }
    }

#ifndef HAS_MULTIPROTOCOL
    if (!ui->GetProtocols().empty())
    {
      break;
    }
#endif
  }

  // Start protocol(s) and ui
  std::unordered_map<std::string, std::shared_ptr<Protocol>>& protocols = ui->GetProtocols();
  bool hasProtocols = !protocols.empty();
  if (hasProtocols)
  {
    // Login
    for (auto& protocol : protocols)
    {
      protocol.second->SetMessageHandler(messageHandler);
      protocol.second->Login();
    }

    // Ui main loop
    ui->Run();

    // Logout
    for (auto& protocol : protocols)
    {
      protocol.second->Logout();
      protocol.second->CloseProfile();
    }
  }

  // Cleanup
  MessageCache::Cleanup();
  ui.reset();
  Profiles::Cleanup();

  // Exit code
  int rv = 0;
  if (!hasProtocols)
  {
    std::cout << "no profiles setup, exiting.\n";
    rv = 1;
  }

  return rv;
}

bool SetupProfile()
{
  std::vector<std::shared_ptr<Protocol>> p_Protocols = GetProtocols();

  std::cout << "Protocols:" << std::endl;
  size_t idx = 0;
  for (auto it = p_Protocols.begin(); it != p_Protocols.end(); ++it, ++idx)
  {
    std::cout << idx << ". " << (*it)->GetProfileId() << std::endl;
  }
  std::cout << idx << ". Exit setup" << std::endl;

  size_t selectidx = idx;
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

  std::string profileId;
  std::string profilesDir = FileUtil::GetApplicationDir() + std::string("/profiles");

#ifndef HAS_MULTIPROTOCOL
  FileUtil::RmDir(profilesDir);
  FileUtil::MkDir(profilesDir);
  Profiles::Init();
#endif

  bool rv = p_Protocols.at(selectidx)->SetupProfile(profilesDir, profileId);
  if (rv)
  {
    std::cout << "Succesfully set up profile " << profileId << "\n";
  }

  return rv;
}

void ShowHelp()
{
  std::cout <<
    "nchat is a minimalistic console-based chat client with support for\n"
    "telegram.\n"
    "\n"
    "Usage: nchat [OPTION]\n"
    "\n"
    "Command-line Options:\n"
    "    -d, --confdir <DIR>    use a different directory than ~/.nchat\n"
    "    -e, --verbose          enable verbose logging\n"
    "    -ee, --extra-verbose   enable extra verbose logging\n"
    "    -h, --help             display this help and exit\n"
    "    -s, --setup            set up chat protocol account\n"
    "    -v, --version          output version information and exit\n"
    "\n"
    "Interactive Commands:\n"
    "    PageDn      history next page\n"
    "    PageUp      history previous page\n"
    "    Tab         next chat\n"
    "    Sh-Tab      previous chat\n"
    "    Ctrl-e      insert emoji\n"
    "    Ctrl-g      toggle show help bar\n"
    "    Ctrl-l      toggle show contact list\n"
    "    Ctrl-p      toggle show top bar\n"
    "    Ctrl-q      quit\n"
    "    Ctrl-s      search contacts\n"
    "    Ctrl-t      send file\n"
    "    Ctrl-u      jump to unread chat\n"
    "    Ctrl-x      send message\n"
    "    Ctrl-y      toggle show emojis\n"
    "    KeyUp       select message\n"
    "\n"
    "Interactive Commands for Selected Message:\n"
    "    Ctrl-d      delete selected message\n"
    "    Ctrl-r      download attached file\n"
    "    Ctrl-v      open/view attached file\n"
    "    Ctrl-x      reply to selected message\n"
    "\n"
    "Report bugs at https://github.com/d99kris/nchat\n"
    "\n";
}

void ShowVersion()
{
  std::cout <<
    "nchat v" << AppUtil::GetAppVersion() << "\n"
    "\n"
    "Copyright (c) 2019-2021 Kristofer Berggren\n"
    "\n"
    "nchat is distributed under the MIT license.\n"
    "\n"
    "Written by Kristofer Berggren.\n";
}
