// ui.h
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

class Protocol;
class ServiceMessage;
class UiController;
class UiModel;

struct screen;

class Ui
{
public:
  Ui();
  virtual ~Ui();

  void Init();

  void Run();
  void AddProtocol(std::shared_ptr<Protocol> p_Protocol);
  std::unordered_map<std::string, std::shared_ptr<Protocol>>& GetProtocols();
  void MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);

  static void RunKeyDump();

private:
  void Cleanup();

private:
  std::shared_ptr<UiModel> m_Model;
  std::shared_ptr<UiController> m_Controller;
  std::string m_TerminalTitle;
  FILE* m_TerminalInFile = nullptr;
  FILE* m_TerminalOutFile = nullptr;
  struct screen* m_Screen = nullptr;
  struct screen* m_OldScreen = nullptr;
};
