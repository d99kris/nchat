// main.cpp
//
// Copyright (c) 2019-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "appconfig.h"
#include "apputil.h"
#include "debuginfo.h"
#include "fileutil.h"
#include "log.h"
#include "messagecache.h"
#include "profiles.h"
#include "scopeddirlock.h"
#include "status.h"
#include "strutil.h"
#include "sysutil.h"
#include "theme.h"
#include "timeutil.h"
#include "ui.h"

#ifdef HAS_DUMMY
#include "duchat.h"
#endif

#ifdef HAS_TELEGRAM
#include "tgchat.h"
#endif

#ifdef HAS_WHATSAPP
#include "wmchat.h"
#endif

#ifdef HAS_SIGNAL
#include "sgchat.h"
#endif

static std::string GetFeatures();
static void RemoveProfile();
static std::shared_ptr<Protocol> SetupProfile();
static void ShowHelp();
static void ShowVersion();
static int Upgrade(const std::string& p_Version, mode_t p_Umask);


class ProtocolBaseFactory
{
public:
  ProtocolBaseFactory() { }
  virtual ~ProtocolBaseFactory() { }
  virtual std::string GetName() const = 0;
  virtual std::string GetSetupMessage() const = 0;
  virtual std::shared_ptr<Protocol> Create() const = 0;
};

template<typename T>
class ProtocolFactory : public ProtocolBaseFactory
{
public:
  virtual std::string GetName() const
  {
    return T::GetName();
  }

  virtual std::string GetSetupMessage() const
  {
    return T::GetSetupMessage();
  }

  virtual std::shared_ptr<Protocol> Create() const
  {
    std::shared_ptr<T> protocol;
#ifdef HAS_DYNAMICLOAD
    std::string libPath =
      FileUtil::DirName(FileUtil::GetSelfPath()) + "/../" CMAKE_INSTALL_LIBDIR "/" + T::GetLibName() +
      FileUtil::GetLibSuffix();
    std::string createFunc = T::GetCreateFunc();
    void* handle = dlopen(libPath.c_str(), RTLD_LAZY);
    if (handle == nullptr)
    {
      LOG_ERROR("failed dlopen %s", libPath.c_str());
      const char* dlerr = dlerror();
      if (dlerr != nullptr)
      {
        LOG_ERROR("dlerror %s", dlerr);
      }

      std::cout << "Failed to load " << libPath << ", skipping profile.\n";
      return protocol;
    }

    // intentionally leak 'handle' as dlclose may cause exit crash as go routines are not explicitly managed

    T* (* CreateFunc)() = (T * (*)())dlsym(handle, createFunc.c_str());
    if (CreateFunc == nullptr)
    {
      LOG_ERROR("failed dlsym %s", createFunc.c_str());
      return protocol;
    }

    protocol.reset(CreateFunc());
#else
    protocol = std::make_shared<T>();
#endif
    return protocol;
  }
};

static std::vector<std::shared_ptr<ProtocolBaseFactory>> GetProtocolFactorys()
{
  std::vector<std::shared_ptr<ProtocolBaseFactory>> protocolFactorys =
  {
#ifdef HAS_DUMMY
    std::shared_ptr<ProtocolBaseFactory>(new ProtocolFactory<DuChat>()),
#endif
#ifdef HAS_TELEGRAM
    std::shared_ptr<ProtocolBaseFactory>(new ProtocolFactory<TgChat>()),
#endif
#ifdef HAS_WHATSAPP
    std::shared_ptr<ProtocolBaseFactory>(new ProtocolFactory<WmChat>()),
#endif
#ifdef HAS_SIGNAL
    std::shared_ptr<ProtocolBaseFactory>(new ProtocolFactory<SgChat>()),
#endif
  };

  return protocolFactorys;
}

// Returns profile dir names ordered by protocol (as listed in GetProtocolFactorys) and then by profile id
static std::vector<std::string> GetProfileNamesSorted(const std::string& p_ProfilesDir)
{
  const std::vector<std::shared_ptr<ProtocolBaseFactory>> protocolFactorys = GetProtocolFactorys();
  auto getProtocolIndex = [&](const std::string& p_ProfileId)
  {
    const std::string protocolName = p_ProfileId.substr(0, p_ProfileId.find('_'));
    size_t index = 0;
    while ((index < protocolFactorys.size()) && (protocolFactorys[index]->GetName() != protocolName))
    {
      ++index;
    }

    return index;
  };

  std::vector<std::string> profileNames = FileUtil::ListDirNames(p_ProfilesDir);
  std::sort(profileNames.begin(), profileNames.end(), [&](const std::string& p_Lhs, const std::string& p_Rhs)
  {
    return std::make_pair(getProtocolIndex(p_Lhs), p_Lhs) < std::make_pair(getProtocolIndex(p_Rhs), p_Rhs);
  });

  return profileNames;
}


int main(int argc, char* argv[])
{
  // Defaults
  const mode_t origUmask = umask(S_IRWXG | S_IRWXO);
  FileUtil::SetApplicationDir(FileUtil::GetDefaultApplicationDir());
  Log::SetVerboseLevel(Log::INFO_LEVEL);

  // Argument handling
  std::string exportDir;
  bool isKeyDump = false;
  bool isRemove = false;
  bool isSetup = false;
  bool isThemeInstall = false;
  bool isUpgrade = false;
  std::string theme;
  std::string upgradeVersion;
  std::vector<std::string> args(argv + 1, argv + argc);
  for (auto it = args.begin(); it != args.end(); ++it)
  {
    if (((*it == "-d") || (*it == "--confdir")) && (std::distance(it + 1, args.end()) > 0))
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
    else if ((*it == "-k") || (*it == "--keydump"))
    {
      isKeyDump = true;
    }
    else if ((*it == "-m") || (*it == "--devmode"))
    {
      AppUtil::SetDeveloperMode(true);
    }
    else if ((*it == "-mm") || (*it == "--extra-devmode"))
    {
      std::cout << "dev mode starting in 5 sec\n";
      sleep(5);
      AppUtil::SetDeveloperMode(true);
    }
    else if ((*it == "-r") || (*it == "--remove"))
    {
      isRemove = true;
    }
    else if ((*it == "-s") || (*it == "--setup"))
    {
      isSetup = true;
    }
    else if ((*it == "-t") || (*it == "--theme"))
    {
      // optional theme name, prompt for selection if not specified
      isThemeInstall = true;
      if ((std::distance(it + 1, args.end()) > 0) && ((it + 1)->rfind("-", 0) != 0))
      {
        ++it;
        theme = *it;
      }
    }
    else if ((*it == "-u") || (*it == "--upgrade"))
    {
      // optional release version, latest if not specified
      isUpgrade = true;
      if ((std::distance(it + 1, args.end()) > 0) && ((it + 1)->rfind("-", 0) != 0))
      {
        ++it;
        upgradeVersion = *it;
      }
    }
    else if ((*it == "-v") || (*it == "--version"))
    {
      ShowVersion();
      return 0;
    }
    else if (((*it == "-x") || (*it == "--export")) && (std::distance(it + 1, args.end()) > 0))
    {
      ++it;
      exportDir = *it;
    }
    else
    {
      ShowHelp();
      return 1;
    }
  }

  // Upgrade - done before config dir init, as it does not use it (and may run as root)
  if (isUpgrade)
  {
    return Upgrade(upgradeVersion, origUmask);
  }

  bool isDirInited = false;
  static const int dirVersion = 1;
  if (!FileUtil::Exists(FileUtil::GetApplicationDir()))
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
        std::cerr << "error: invalid config dir content, exiting. use -s to setup nchat.\n";
        return 1;
      }
    }
  }

  // Install theme - done while holding the dir lock, as a running nchat would
  // otherwise overwrite color.conf on exit
  if (isThemeInstall)
  {
    return Theme::Install(theme) ? 0 : 1;
  }

  // Remove profile
  if (isRemove)
  {
    RemoveProfile();
    return 0;
  }

  // Init profiles dir
  Profiles::Init();

  // Init logging
  const std::string& logPath = FileUtil::GetApplicationDir() + std::string("/log.txt");
  Log::Init(logPath);
  std::string appNameVersion = AppUtil::GetAppName(true /*p_WithVersion*/, true /*p_WithBranch*/);
  LOG_INFO("%s", appNameVersion.c_str());
  std::string osArch = SysUtil::GetOsArch(false /*p_Verbose*/);
  LOG_INFO("%s", osArch.c_str());
  std::string buildInfo = SysUtil::GetBuildInfo();
  LOG_INFO("%s", buildInfo.c_str());
  std::string compiler = SysUtil::GetCompiler(false /*p_Verbose*/);
  LOG_INFO("%s", compiler.c_str());
  std::string go = SysUtil::GetGo(GO_VERSION);
  LOG_INFO("%s", go.c_str());
  std::string features = GetFeatures();
  LOG_INFO("%s", features.c_str());

  // Init signal handler
  AppUtil::InitSignalHandler();

  // Run keydump if required
  if (isKeyDump)
  {
    Ui::RunKeyDump();
    return 0;
  }

  // Init app config
  AppConfig::Init();
  FileUtil::SetDownloadsDir(AppConfig::GetStr("downloads_dir"));
  static const bool isLogdumpEnabled = AppConfig::GetBool("logdump_enabled");

  // Init debug info, log last version
  DebugInfo::Init();
  const std::string versionUsed = DebugInfo::GetStr("version_used");
  if (!versionUsed.empty() && (versionUsed != AppUtil::GetAppVersion()))
  {
    LOG_INFO("last version %s", versionUsed.c_str());
  }

  // Init core dump
  static const bool isCoredumpEnabled = AppConfig::GetBool("coredump_enabled");
  if (isCoredumpEnabled)
  {
#ifndef HAS_COREDUMP
    LOG_WARNING("core dump not supported");
#else
    AppUtil::InitCoredump();
#endif
  }

  // Init message cache
  MessageCache::Init();

  // Run setup if required
  std::shared_ptr<Protocol> setupProtocol;
  if (isSetup)
  {
    setupProtocol = SetupProfile();
    if (!setupProtocol)
    {
      MessageCache::Cleanup();
      DebugInfo::Cleanup();
      AppConfig::Cleanup();
      return 1;
    }
  }

  // Init temp
  FileUtil::InitTempDir();

  // Init ui
  std::shared_ptr<Ui> ui = std::make_shared<Ui>();

  // Set message cache message handler
  std::function<void(std::shared_ptr<ServiceMessage>)> messageHandler =
    std::bind(&Ui::MessageHandler, std::ref(*ui), std::placeholders::_1);
  MessageCache::SetMessageHandler(messageHandler);

  // Load profile(s)
  std::string profilesDir = FileUtil::GetApplicationDir() + "/profiles";
  const std::vector<std::string> profileNames = GetProfileNamesSorted(profilesDir);
  std::vector<std::unique_ptr<ScopedDirLock>> profileDirLocks;
  for (auto& profileId : profileNames)
  {
    if (profileId == "version") continue;

    std::stringstream ss(profileId);
    std::string protocolName;
    if ((profileId.find("_") == std::string::npos) || !std::getline(ss, protocolName, '_'))
    {
      LOG_WARNING("invalid profile name, skipping %s", profileId.c_str());
      continue;
    }

#ifndef HAS_MULTIPROTOCOL
    if (!ui->GetProtocols().empty())
    {
      LOG_WARNING("multiple profile support not enabled, skipping %s", profileId.c_str());
      continue;
    }
#endif

    std::string profileDir = profilesDir + "/" + profileId;
    profileDirLocks.push_back(std::make_unique<ScopedDirLock>(profileDir));
    if (!profileDirLocks.back()->IsLocked())
    {
      LOG_WARNING("unable to acquire lock for %s, skipping", profileDir.c_str());
      std::cerr << "warning: unable to acquire lock for " << profileDir << ", skipping.\n";
      profileDirLocks.pop_back();
      continue;
    }

    if (setupProtocol && (setupProtocol->GetProfileId() == profileId))
    {
      LOG_DEBUG("adding new profile %s", profileId.c_str());
      ui->AddProtocol(setupProtocol);
      setupProtocol.reset();
    }
    else
    {
      bool found = false;
      std::vector<std::shared_ptr<ProtocolBaseFactory>> allProtocolFactorys = GetProtocolFactorys();
      for (auto& protocolFactory : allProtocolFactorys)
      {
        if (protocolFactory->GetName() == protocolName)
        {
          LOG_DEBUG("loading existing profile %s", profileId.c_str());
          std::shared_ptr<Protocol> protocol = protocolFactory->Create();
          if (protocol)
          {
            protocol->LoadProfile(profilesDir, profileId);
            ui->AddProtocol(protocol);
            found = true;
          }
        }
      }

      if (!found)
      {
        std::string msg = "Protocol " + protocolName + " not supported";
        if (protocolName == "WhatsAppMd")
        {
          // Special logging for WhatsApp which may be auto-disabled from build if go dependency not met
          msg += " (requires Go >= " + std::string(GO_VERSION_MIN) + ")";
        }

        LOG_WARNING("%s", msg.c_str());
        std::cout << msg << "\n";
      }
    }
  }

  // Start protocol(s) and ui
  ui->Init();
  std::unordered_map<std::string, std::shared_ptr<Protocol>> protocols = ui->GetProtocols();
  bool hasProtocols = !protocols.empty();
  if (hasProtocols && exportDir.empty())
  {
    // Sort protocols, using same order as at load
    std::vector<std::pair<std::string, std::shared_ptr<Protocol>>> protocolsSorted;
    for (auto& profileId : profileNames)
    {
      auto it = protocols.find(profileId);
      if (it != protocols.end())
      {
        protocolsSorted.push_back(*it);
      }
    }

    // Connecting status
    for (auto& protocol : protocolsSorted)
    {
      Status::Set(protocol.first, Status::FlagConnecting);
    }

    // Signal handler for terminate signals
    AppUtil::InitTerminateSignalHandler();

    // Run a protocol function for all protocols in parallel started in protocol order
    auto runParallel = [&](const std::function<void(const std::shared_ptr<Protocol>&)>& p_Func)
    {
      std::vector<std::thread> threads;
      for (auto& protocol : protocolsSorted)
      {
        if (!threads.empty())
        {
          TimeUtil::Sleep(0.001); // Small delay for consistent order
        }

        threads.emplace_back(p_Func, protocol.second);
      }

      for (auto& thread : threads)
      {
        thread.join();
      }
    };

    // Login
    std::thread loginThread([&]
    {
      runParallel([&](const std::shared_ptr<Protocol>& p_Protocol)
      {
        p_Protocol->SetMessageHandler(messageHandler);
        p_Protocol->Login();
      });
    });

    // Ui main loop
    ui->Run();

    // Cleanup login thread
    if (loginThread.joinable())
    {
      loginThread.join();
    }

    // Logout
    runParallel([&](const std::shared_ptr<Protocol>& p_Protocol)
    {
      p_Protocol->Logout();
    });

    // Close profiles
    for (auto& protocol : protocolsSorted)
    {
      protocol.second->CloseProfile();
    }
  }

  // Clear cache message handler
  MessageCache::SetMessageHandler(nullptr);

  // Cleanup ui
  ui->Cleanup();
  ui.reset();

  // Perform export if requested
  if (!exportDir.empty())
  {
    MessageCache::Export(exportDir);
  }

  // Save last version
  DebugInfo::SetStr("version_used", AppUtil::GetAppVersion());

  // Cleanup
  FileUtil::CleanupTempDir();
  MessageCache::Cleanup();
  DebugInfo::Cleanup();
  AppConfig::Cleanup();
  Profiles::Cleanup();

  // Exit code
  int rv = 0;
  if (!hasProtocols)
  {
    std::cout << "No profiles setup, exiting.\n";
    rv = 1;
  }

  LOG_INFO("exit");

  Log::Cleanup(isLogdumpEnabled);

  if (AppUtil::IsTerminateRequested())
  {
    // Re-raise so the parent sees termination by signal (shell message, exit status 128+N)
    const int sig = AppUtil::GetTerminateSignal();
    signal(sig, SIG_DFL);
    kill(getpid(), sig);
  }

  return rv;
}

std::string GetFeatures()
{
  std::vector<std::string> features;
#if defined(HAS_TELEGRAM)
  features.push_back("telegram");
#endif

#if defined(HAS_WHATSAPP)
  features.push_back("whatsapp");
#endif

#if defined(HAS_SIGNAL)
  features.push_back("signal");
#endif

  return StrUtil::Join(features, ", ");
}

void RemoveProfile()
{
  // Show profiles
  std::string profilesDir = FileUtil::GetApplicationDir() + "/profiles";
  const std::vector<std::string> profileNames = GetProfileNamesSorted(profilesDir);
  int id = 0;
  std::map<int, std::string> idPath;
  std::cout << "Remove profile:\n";
  for (auto& profileId : profileNames)
  {
    if (profileId == "version") continue;

    std::stringstream ss(profileId);
    std::string protocolName;
    if ((profileId.find("_") == std::string::npos) || !std::getline(ss, protocolName, '_'))
    {
      LOG_WARNING("invalid profile name, skipping %s", profileId.c_str());
      continue;
    }

    std::cout << id << ". " << profileId << "\n";
    idPath[id++] = profilesDir + "/" + profileId;
  }

  std::cout << id << ". Cancel removal\n";

  size_t selectid = id;
  std::cout << "Remove profile (" << selectid << "): ";
  std::string line;
  std::getline(std::cin, line);

  if (!line.empty())
  {
    try
    {
      selectid = stoi(line);
    }
    catch (...)
    {
    }
  }

  if (!idPath.count(selectid))
  {
    std::cout << "Removal aborted, exiting." << std::endl;
    return;
  }

  std::string profilePath = idPath.at(selectid);
  LOG_TRACE("deleting %s", profilePath.c_str());
  FileUtil::RmDir(profilePath);
}

std::shared_ptr<Protocol> SetupProfile()
{
  std::shared_ptr<Protocol> rv;
  std::vector<std::shared_ptr<ProtocolBaseFactory>> protocolFactorys = GetProtocolFactorys();

  std::cout << "Protocols:" << std::endl;
  size_t idx = 0;
  for (auto it = protocolFactorys.begin(); it != protocolFactorys.end(); ++it, ++idx)
  {
    std::cout << idx << ". " << (*it)->GetName() << std::endl;
  }
  std::cout << idx << ". Exit setup" << std::endl;

  size_t selectidx = idx;
  std::cout << "Select protocol (" << selectidx << "): ";
  std::string line;
  std::getline(std::cin, line);

  if (!line.empty())
  {
    try
    {
      selectidx = stoi(line);
    }
    catch (...)
    {
    }
  }

  if (selectidx >= protocolFactorys.size())
  {
    std::cout << "Setup aborted, exiting." << std::endl;
    return rv;
  }

  std::string profileId;
  std::string profilesDir = FileUtil::GetApplicationDir() + std::string("/profiles");

#ifndef HAS_MULTIPROTOCOL
  FileUtil::RmDir(profilesDir);
  FileUtil::MkDir(profilesDir);
  Profiles::Init();
#endif

  std::string setupMessage = protocolFactorys.at(selectidx)->GetSetupMessage();
  if (!setupMessage.empty())
  {
    LOG_WARNING("%s", setupMessage.c_str());
    std::cout << setupMessage;
    sleep(3);
  }

  std::shared_ptr<Protocol> protocol = protocolFactorys.at(selectidx)->Create();
  bool setupResult = protocol && protocol->SetupProfile(profilesDir, profileId);
  if (setupResult)
  {
    std::cout << "Succesfully set up profile " << profileId << "\n";
    rv = protocol;
  }
  else
  {
    std::cout << "Setup failed\n";
    if (protocol)
    {
      protocol->Logout();
      protocol->CloseProfile();
    }
  }

  return rv;
}

void ShowHelp()
{
  std::cout <<
    "nchat is a terminal-based messaging client with Telegram, WhatsApp and Signal support.\n"
    "\n"
    "Usage: nchat [OPTION]\n"
    "\n"
    "Command-line Options:\n"
    "    -d, --confdir <DIR>    use a different directory than ~/.config/nchat\n"
    "    -e, --verbose          enable verbose logging\n"
    "    -ee, --extra-verbose   enable extra verbose logging\n"
    "    -h, --help             display this help and exit\n"
    "    -k, --keydump          key code dump mode\n"
    "    -m, --devmode          developer mode\n"
    "    -r, --remove           remove chat protocol account\n"
    "    -s, --setup            set up chat protocol account\n"
    "    -t, --theme [THEME]    install color theme, or select from list of\n"
    "                           available themes if not specified\n"
    "    -u, --upgrade [VER]    upgrade release binary to latest or given version\n"
    "    -v, --version          output version information and exit\n"
    "    -x, --export <DIR>     export message cache to specified dir\n"
    "\n"
    "Interactive Commands:\n"
    "    PageDn      history next page\n"
    "    PageUp      history previous page\n"
    "    Tab         next chat\n"
    "    Sh-Tab      previous chat\n"
    "    Ctrl-f      jump to unread chat\n"
    "    Ctrl-g      toggle show help bar\n"
    "    Ctrl-l      toggle show contact list\n"
    "    Ctrl-n      goto chat\n"
    "    Ctrl-p      toggle show top bar\n"
    "    Ctrl-q      quit\n"
    "    Ctrl-s      insert emoji\n"
    "    Ctrl-t      send file\n"
    "    Ctrl-x      send message\n"
    "    Ctrl-y      toggle show emojis\n"
    "    KeyUp       select message\n"
    "    Alt-@       insert mention\n"
    "    Alt-a       archive current chat\n"
    "    Alt-d       delete/leave current chat\n"
    "    Alt-e       external editor compose\n"
    "    Alt-i       auto-compose reply\n"
    "    Alt-n       search contacts\n"
    "    Alt-p       pin/unpin current chat\n"
    "    Alt-t       external telephone call\n"
    "    Alt-/       find in chat\n"
    "    Alt-?       find next in chat\n"
    "    Alt-$       external spell check\n"
    "    Alt-,       decrease contact list width\n"
    "    Alt-.       increase contact list width\n"
    "\n"
    "Interactive Commands for Selected Message:\n"
    "    Ctrl-d      delete selected message\n"
    "    Ctrl-r      download attached file\n"
    "    Ctrl-v      open/view attached file\n"
    "    Ctrl-w      open link\n"
    "    Ctrl-x      send reply to selected message\n"
    "    Ctrl-z      edit selected message\n"
    "    Alt-c       copy selected message to clipboard\n"
    "    Alt-q       jump to quoted/replied message\n"
    "    Alt-r       forward selected message\n"
    "    Alt-s       add/remove reaction on selected message\n"
    "    Alt-w       external message viewer\n"
    "\n"
    "Interactive Commands for Text Input:\n"
    "    Ctrl-a      move cursor to start of line\n"
    "    Ctrl-c      clear input buffer\n"
    "    Ctrl-e      move cursor to end of line\n"
    "    Ctrl-k      delete from cursor to end of line\n"
    "    Ctrl-u      delete from cursor to start of line\n"
    "    Alt-Left    move cursor backward one word\n"
    "    Alt-Right   move cursor forward one word\n"
    "    Alt-Backsp  delete previous word\n"
    "    Alt-Delete  delete next word\n"
    "    Alt-c       copy input buffer to clipboard (if no message selected)\n"
    "    Alt-v       paste into input buffer from clipboard\n"
    "    Alt-x       cut input buffer to clipboard\n"
    "\n"
    "Report bugs at https://github.com/d99kris/nchat\n"
    "\n";
}

void ShowVersion()
{
  // *INDENT-OFF*
  std::cout <<
    AppUtil::GetAppName(true /*p_WithVersion*/) << "\n"
    "\n"
    "Copyright (c) 2019-2026 Kristofer Berggren\n"
    "\n"
#if defined(HAS_SIGNAL)
    "Combined distribution subject to GNU AGPL v3 license.\n"
#elif defined(HAS_WHATSAPP)
    "Combined distribution subject to GNU GPL v3 license.\n"
#else
    "Combined distribution subject to MIT license.\n"
#endif
    "\n"
    "Written by Kristofer Berggren.\n";
  // *INDENT-ON*
}

int Upgrade(const std::string& p_Version, mode_t p_Umask)
{
  // exit status 0 upgraded, 3 up to date (from install.sh), 5 failed
  static const int failed = 5;
  if (!AppUtil::IsUpgradeSupported())
  {
    std::cerr <<
      "error: upgrade is only supported for pre-built release binaries, upgrade\n"
      "       using the package manager or method nchat was installed with.\n";
    return failed;
  }

  // installed by install.sh as <prefix>/bin/nchat
  char realPath[PATH_MAX];
  const std::string selfPath = FileUtil::GetSelfPath();
  if (selfPath.empty() || (realpath(selfPath.c_str(), realPath) == nullptr))
  {
    std::cerr << "error: cannot determine nchat binary path\n";
    return failed;
  }

  const std::string binPath = realPath;
  const std::string binDir = binPath.substr(0, binPath.rfind('/'));
  const size_t binDirPos = binDir.rfind('/');
  if ((binDirPos == std::string::npos) || (binDir.substr(binDirPos + 1) != "bin"))
  {
    std::cerr <<
      "error: nchat binary " << binPath << " is not in a bin dir,\n"
      "       re-install using install.sh\n";
    return failed;
  }

  if (access(binDir.c_str(), W_OK) != 0)
  {
    std::cerr <<
      "error: install dir " << binDir << " is not writable by the current user,\n"
      "       re-run with sudo, e.g.: sudo nchat -u\n";
    return failed;
  }

  const std::string prefix = (binDirPos == 0) ? "/" : binDir.substr(0, binDirPos);

  // NCHAT_INSTALL_URL allows testing a local install.sh, e.g. file://$PWD/utils/install.sh
  const char* envUrl = getenv("NCHAT_INSTALL_URL");
  const std::string url = ((envUrl != nullptr) && (*envUrl != '\0')) ? envUrl :
    "https://raw.githubusercontent.com/d99kris/nchat/master/utils/install.sh";

  // values passed as positional args to avoid shell quoting
  static const std::string script =
    "if command -v curl >/dev/null 2>&1; then s=\"$(curl -fsSL \"$1\")\"\n"
    "elif command -v wget >/dev/null 2>&1; then s=\"$(wget -qO- \"$1\")\"\n"
    "else echo \"error: need curl or wget to download\" >&2; exit 5; fi ||\n"
    "  { echo \"error: failed to download $1\" >&2; exit 5; }\n"
    "NCHAT_PREFIX=\"$2\" NCHAT_VERSION=\"$3\" NCHAT_CURRENT_VERSION=\"$4\" \\\n"
    "  exec bash -c \"$s\" install.sh\n";

  // exec so the running binary is not held during install, with user umask restored
  const std::string curVersion = AppUtil::GetAppVersion();
  umask(p_Umask);
  execlp("bash", "bash", "-c", script.c_str(), "nchat-upgrade", url.c_str(), prefix.c_str(),
         p_Version.c_str(), curVersion.c_str(), (char*)nullptr);
  std::cerr << "error: failed to run bash: " << strerror(errno) << "\n";
  return failed;
}
