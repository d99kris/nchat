// messagecache.cpp
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "messagecache.h"

#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <utility>

#include <sqlite_modern_cpp.h>

#include "appconfig.h"
#include "cacheutil.h"
#include "log.h"
#include "fileutil.h"
#include "protocolutil.h"
#include "serialization.h"
#include "sqlitehelp.h"
#include "strutil.h"
#include "timeutil.h"

std::function<void(std::shared_ptr<ServiceMessage>)> MessageCache::m_MessageHandler;
std::mutex MessageCache::m_DbMutex;
std::map<std::string, std::unique_ptr<sqlite::database>> MessageCache::m_Dbs;
std::unordered_map<std::string, std::unordered_map<std::string, bool>> MessageCache::m_InSync;
std::unordered_map<std::string, bool> MessageCache::m_CheckSync;
bool MessageCache::m_Running = false;
std::thread MessageCache::m_Thread;
std::mutex MessageCache::m_QueueMutex;
std::condition_variable MessageCache::m_CondVar;
std::deque<std::shared_ptr<MessageCache::Request>> MessageCache::m_Queue;
std::string MessageCache::m_HistoryDir;
bool MessageCache::m_CacheEnabled = true;
bool MessageCache::m_CacheReadOnly = false;

static const std::string s_TableContacts = "contacts2";
static const std::string s_TableChats = "chats2";
static const std::string s_TableMessages = "messages";

void MessageCache::Init()
{
  m_CacheEnabled = AppConfig::GetBool("cache_enabled");

  if (!m_CacheEnabled) return;

  static const int dirVersion = 6;
  m_HistoryDir = FileUtil::GetApplicationDir() + "/history";
  FileUtil::InitDirVersion(m_HistoryDir, dirVersion);

  std::unique_lock<std::mutex> lock(m_QueueMutex);
  if (!m_Running)
  {
    m_Running = true;
    m_Thread = std::thread(MessageCache::Process);
  }
}

void MessageCache::Cleanup()
{
  if (!m_CacheEnabled) return;

  if (m_Running)
  {
    {
      std::unique_lock<std::mutex> lock(m_QueueMutex);
      m_Running = false;
      m_CondVar.notify_one();
    }
    m_Thread.join();
  }

  {
    std::unique_lock<std::mutex> lock(m_DbMutex);
    m_MessageHandler = nullptr;
    m_Dbs.clear();
  }
}

void MessageCache::SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler)
{
  if (!m_CacheEnabled) return;

  std::unique_lock<std::mutex> lock(m_DbMutex);
  m_MessageHandler = p_MessageHandler;
}

void MessageCache::AddFromServiceMessage(const std::string& p_ProfileId,
                                         std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  switch (p_ServiceMessage->GetMessageType())
  {
    case NewChatsNotifyType:
      {
        std::shared_ptr<NewChatsNotify> newChatsNotify =
          std::static_pointer_cast<NewChatsNotify>(p_ServiceMessage);
        MessageCache::AddChats(p_ProfileId, newChatsNotify->chatInfos);
      }
      break;

    case NewContactsNotifyType:
      {
        std::shared_ptr<NewContactsNotify> newContactsNotify =
          std::static_pointer_cast<NewContactsNotify>(p_ServiceMessage);
        MessageCache::AddContacts(p_ProfileId, newContactsNotify->fullSync, newContactsNotify->contactInfos);
      }
      break;

    case NewMessagesNotifyType:
      {
        std::shared_ptr<NewMessagesNotify> newMessagesNotify =
          std::static_pointer_cast<NewMessagesNotify>(p_ServiceMessage);
        if (newMessagesNotify->success && !newMessagesNotify->cached &&
            newMessagesNotify->sequence)
        {
          MessageCache::AddMessages(p_ProfileId, newMessagesNotify->chatId,
                                    newMessagesNotify->fromMsgId, newMessagesNotify->chatMessages);
        }
      }
      break;

    case MarkMessageReadNotifyType:
      {
        std::shared_ptr<MarkMessageReadNotify> markMessageReadNotify =
          std::static_pointer_cast<MarkMessageReadNotify>(p_ServiceMessage);
        MessageCache::UpdateMessageIsRead(p_ProfileId, markMessageReadNotify->chatId,
                                          markMessageReadNotify->msgId, true);
      }
      break;

    case DeleteMessageNotifyType:
      {
        std::shared_ptr<DeleteMessageNotify> deleteMessageNotify =
          std::static_pointer_cast<DeleteMessageNotify>(p_ServiceMessage);
        if (deleteMessageNotify->success)
        {
          MessageCache::DeleteOneMessage(p_ProfileId, deleteMessageNotify->chatId, deleteMessageNotify->msgId);
        }
      }
      break;

    case DeleteChatNotifyType:
      {
        std::shared_ptr<DeleteChatNotify> deleteChatNotify =
          std::static_pointer_cast<DeleteChatNotify>(p_ServiceMessage);
        if (deleteChatNotify->success)
        {
          MessageCache::DeleteChat(p_ProfileId, deleteChatNotify->chatId);
        }
      }
      break;

    case NewMessageStatusNotifyType:
      {
        std::shared_ptr<NewMessageStatusNotify> newMessageStatusNotify =
          std::static_pointer_cast<NewMessageStatusNotify>(p_ServiceMessage);
        MessageCache::UpdateMessageIsRead(p_ProfileId, newMessageStatusNotify->chatId,
                                          newMessageStatusNotify->msgId, newMessageStatusNotify->isRead);
      }
      break;

    case NewMessageFileNotifyType:
      {
        std::shared_ptr<NewMessageFileNotify> newMessageFileNotify =
          std::static_pointer_cast<NewMessageFileNotify>(p_ServiceMessage);
        MessageCache::UpdateMessageFileInfo(p_ProfileId, newMessageFileNotify->chatId,
                                            newMessageFileNotify->msgId, newMessageFileNotify->fileInfo);
      }
      break;

    case NewMessageReactionsNotifyType:
      {
        std::shared_ptr<NewMessageReactionsNotify> newMessageReactionsNotify =
          std::static_pointer_cast<NewMessageReactionsNotify>(p_ServiceMessage);
        MessageCache::UpdateMessageReactions(p_ProfileId, newMessageReactionsNotify->chatId,
                                             newMessageReactionsNotify->msgId, newMessageReactionsNotify->reactions);
      }
      break;

    case UpdateMuteNotifyType:
      {
        std::shared_ptr<UpdateMuteNotify> updateMuteNotify = std::static_pointer_cast<UpdateMuteNotify>(
          p_ServiceMessage);
        if (updateMuteNotify->success)
        {
          MessageCache::UpdateMute(p_ProfileId, updateMuteNotify->chatId, updateMuteNotify->isMuted);
        }
      }
      break;

    case UpdatePinNotifyType:
      {
        std::shared_ptr<UpdatePinNotify> updatePinNotify = std::static_pointer_cast<UpdatePinNotify>(
          p_ServiceMessage);
        if (updatePinNotify->success)
        {
          MessageCache::UpdatePin(p_ProfileId, updatePinNotify->chatId, updatePinNotify->isPinned,
                                  updatePinNotify->timePinned);
        }
      }
      break;

    default:
      break;
  }
}

void MessageCache::AddProfile(const std::string& p_ProfileId, bool p_CheckSync, int p_DirVersion, bool p_IsSetup,
                              bool p_AllowReadOnly, bool* p_IsRemoved /*= nullptr*/)
{
  if (!m_CacheEnabled) return;

  std::unique_lock<std::mutex> lock(m_DbMutex);
  if (m_Dbs.count(p_ProfileId) > 0)
  {
    LOG_WARNING("profile %s already added", p_ProfileId.c_str());
    return;
  }

  m_CheckSync[p_ProfileId] = p_CheckSync;

  const std::string& dbDir = m_HistoryDir + "/" + p_ProfileId;
  if (p_IsSetup)
  {
    FileUtil::RmDir(dbDir);
  }

  // @todo: remove MkDir once WmChat::s_CacheDirVersion is bumped from 0, as InitDirVersion will create it
  FileUtil::MkDir(dbDir);
  FileUtil::InitDirVersion(dbDir, p_DirVersion);

  const std::string& dbPath = dbDir + "/db.sqlite";

  // If db file does not exist and we are not performing setup, it means db dir version has been bumped
  // and directory cleared, or user has manually deleted the db dir/file. In this case indicate to
  // protocol, as some need to perform reinit to fetch chats.
  if (p_IsRemoved != nullptr)
  {
    *p_IsRemoved = (!FileUtil::Exists(dbPath) && !p_IsSetup);
  }

  m_CacheReadOnly = p_AllowReadOnly && AppConfig::GetBool("cache_read_only");
  if (m_CacheReadOnly)
  {
    LOG_WARNING("cache read only");
    std::string tmpDbPath = dbPath + ".tmp";
    FileUtil::CopyFile(dbPath, tmpDbPath);
    m_Dbs[p_ProfileId].reset(new sqlite::database(tmpDbPath));
  }
  else
  {
    m_Dbs[p_ProfileId].reset(new sqlite::database(dbPath));
  }

  if (!m_Dbs[p_ProfileId]) return;

  try
  {
    *m_Dbs[p_ProfileId] << "PRAGMA synchronous = FULL";
    *m_Dbs[p_ProfileId] << "PRAGMA journal_mode = DELETE";

    // note: use actual table names instead if variables during schema setup / update

    // fresh database will get version 0
    // existing legacy database will get version 3 (as the three tables existed)
    // existing modern database will have its stored version 4 or newer
    *m_Dbs[p_ProfileId] << "CREATE TABLE IF NOT EXISTS version AS "
      "SELECT COUNT(name) AS schema FROM sqlite_master WHERE TYPE='table' AND "
      "(name='contacts2' OR name='chats2' OR name='messages');";

    // *INDENT-OFF*
    int64_t schemaVersion = 0;
    *m_Dbs[p_ProfileId] << "SELECT schema FROM version;" >>
      [&](const int64_t& schema)
      {
        schemaVersion = schema;
      };
    // *INDENT-ON*

    LOG_DEBUG("detected db schema %d", schemaVersion);

    if (schemaVersion < 3)
    {
      LOG_INFO("create base db schema");

      *m_Dbs[p_ProfileId] << "CREATE TABLE IF NOT EXISTS messages ("
        "chatId TEXT,"
        "id TEXT,"
        "senderId TEXT,"
        "text TEXT,"
        "quotedId TEXT,"
        "quotedText TEXT,"
        "quotedSender TEXT,"
        "fileInfo TEXT,"
        "fileStatus INT,"
        "fileType TEXT,"
        "timeSent INT,"
        "isOutgoing INT,"
        "isRead INT,"
        "UNIQUE(chatId, id) ON CONFLICT REPLACE"
        ");";

      *m_Dbs[p_ProfileId] << "CREATE TABLE IF NOT EXISTS contacts2 ("
        "id TEXT,"
        "name TEXT,"
        "phone TEXT,"
        "isSelf INT,"
        "UNIQUE(id) ON CONFLICT REPLACE"
        ");";

      *m_Dbs[p_ProfileId] << "CREATE TABLE IF NOT EXISTS chats2 ("
        "id TEXT,"
        "isMuted INT,"
        "UNIQUE(id) ON CONFLICT REPLACE"
        ");";

      schemaVersion = 3;
      *m_Dbs[p_ProfileId] << "UPDATE version "
        "SET schema=?;" << schemaVersion;
    }

    if (schemaVersion == 3)
    {
      LOG_INFO("update db schema 3 to 4");

      *m_Dbs[p_ProfileId] << "ALTER TABLE messages ADD COLUMN "
        "reactions BLOB;";

      schemaVersion = 4;
      *m_Dbs[p_ProfileId] << "UPDATE version "
        "SET schema=?;" << schemaVersion;
    }

    if (schemaVersion == 4)
    {
      LOG_INFO("update db schema 4 to 5");

      *m_Dbs[p_ProfileId] << "ALTER TABLE chats2 ADD COLUMN isPinned INT;";
      *m_Dbs[p_ProfileId] << "ALTER TABLE chats2 ADD COLUMN lastMessageTime INT;";

      schemaVersion = 5;
      *m_Dbs[p_ProfileId] << "UPDATE version "
        "SET schema=?;" << schemaVersion;
    }

    static const int64_t s_SchemaVersion = 5;
    if (schemaVersion > s_SchemaVersion)
    {
      LOG_WARNING("cache db schema %d from newer nchat version detected, if cache issues are encountered "
                  "please delete %s or perform a fresh nchat setup", schemaVersion, dbDir.c_str());
    }
    else
    {
      LOG_TRACE("db schema ready");
    }
  }
  catch (const sqlite::sqlite_exception& ex)
  {
    HANDLE_SQLITE_EXCEPTION(ex);
  }
}

void MessageCache::AddMessages(const std::string& p_ProfileId, const std::string& p_ChatId,
                               const std::string& p_FromMsgId,
                               const std::vector<ChatMessage>& p_ChatMessages)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<AddMessagesRequest> addMessagesRequest = std::make_shared<AddMessagesRequest>();
  addMessagesRequest->profileId = p_ProfileId;
  addMessagesRequest->chatId = p_ChatId;
  addMessagesRequest->fromMsgId = p_FromMsgId;
  addMessagesRequest->chatMessages = p_ChatMessages;
  EnqueueRequest(addMessagesRequest);
}

void MessageCache::AddChats(const std::string& p_ProfileId, const std::vector<ChatInfo>& p_ChatInfos)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<AddChatsRequest> addChatsRequest = std::make_shared<AddChatsRequest>();
  addChatsRequest->profileId = p_ProfileId;
  addChatsRequest->chatInfos = p_ChatInfos;
  EnqueueRequest(addChatsRequest);
}

void MessageCache::AddContacts(const std::string& p_ProfileId, bool p_FullSync,
                               const std::vector<ContactInfo>& p_ContactInfos)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<AddContactsRequest> addContactsRequest = std::make_shared<AddContactsRequest>();
  addContactsRequest->profileId = p_ProfileId;
  addContactsRequest->fullSync = p_FullSync;
  addContactsRequest->contactInfos = p_ContactInfos;
  EnqueueRequest(addContactsRequest);
}

bool MessageCache::FetchChats(const std::string& p_ProfileId, const std::unordered_set<std::string>& p_ChatIds)
{
  if (!m_CacheEnabled) return false;

  std::unique_lock<std::mutex> lock(m_DbMutex);
  if (!m_Dbs[p_ProfileId]) return false;

  lock.unlock();

  std::shared_ptr<FetchChatsRequest> fetchChatsRequest = std::make_shared<FetchChatsRequest>();
  fetchChatsRequest->profileId = p_ProfileId;
  fetchChatsRequest->chatIds = p_ChatIds;

  LOG_DEBUG("cache sync fetch chats");
  PerformRequest(fetchChatsRequest);
  return true;
}

bool MessageCache::FetchContacts(const std::string& p_ProfileId)
{
  if (!m_CacheEnabled) return false;

  std::unique_lock<std::mutex> lock(m_DbMutex);
  if (!m_Dbs[p_ProfileId]) return false;

  lock.unlock();

  std::shared_ptr<FetchContactsRequest> fetchContactsRequest =
    std::make_shared<FetchContactsRequest>();
  fetchContactsRequest->profileId = p_ProfileId;

  LOG_DEBUG("cache sync fetch contacts");
  PerformRequest(fetchContactsRequest);
  return true;
}

bool MessageCache::FetchMessagesFrom(const std::string& p_ProfileId, const std::string& p_ChatId,
                                     const std::string& p_FromMsgId, const int p_Limit,
                                     const bool p_Sync)
{
  if (!m_CacheEnabled) return false;

  std::unique_lock<std::mutex> lock(m_DbMutex);
  if (!m_Dbs[p_ProfileId]) return false;

  if (m_CheckSync[p_ProfileId] && !m_InSync[p_ProfileId][p_ChatId]) return false;

  int count = 0;

  try
  {
    int64_t fromMsgIdTimeSent = 0;
    if (!p_FromMsgId.empty())
    {
      // *INDENT-OFF*
      *m_Dbs[p_ProfileId] << "SELECT timeSent FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;"
                          << p_ChatId << p_FromMsgId >>
        [&](const int64_t& timeSent)
        {
          fromMsgIdTimeSent = timeSent;
        };
      // *INDENT-ON*
    }
    else
    {
      fromMsgIdTimeSent = std::numeric_limits<int64_t>::max();
    }

    // *INDENT-OFF*
    *m_Dbs[p_ProfileId] << "SELECT COUNT(*) FROM " + s_TableMessages + " WHERE chatId = ? AND timeSent < ?;"
                        << p_ChatId << fromMsgIdTimeSent >>
      [&](const int& countRes)
      {
        count = countRes;
      };
    // *INDENT-ON*
  }
  catch (const sqlite::sqlite_exception& ex)
  {
    HANDLE_SQLITE_EXCEPTION(ex);
  }

  lock.unlock();

  if (count > 0)
  {
    std::shared_ptr<FetchMessagesFromRequest> fetchFromRequest =
      std::make_shared<FetchMessagesFromRequest>();
    fetchFromRequest->profileId = p_ProfileId;
    fetchFromRequest->chatId = p_ChatId;
    fetchFromRequest->fromMsgId = p_FromMsgId;
    fetchFromRequest->limit = p_Limit;

    if (p_Sync)
    {
      LOG_DEBUG("cache sync fetch %s %s count %d", p_ChatId.c_str(),
                p_FromMsgId.c_str(), count);
      PerformRequest(fetchFromRequest);
    }
    else
    {
      LOG_DEBUG("cache async fetch %s %s count %d", p_ChatId.c_str(),
                p_FromMsgId.c_str(), count);
      EnqueueRequest(fetchFromRequest);
    }

    return true;
  }
  else
  {
    LOG_DEBUG("cache cannot fetch %s %s count %d", p_ChatId.c_str(),
              p_FromMsgId.c_str(), count);
    return false;
  }
}

bool MessageCache::FetchOneMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                                   const std::string& p_MsgId, const bool p_Sync)
{
  if (!m_CacheEnabled) return false;

  std::unique_lock<std::mutex> lock(m_DbMutex);
  if (!m_Dbs[p_ProfileId]) return false;

  bool inSync = (!m_CheckSync[p_ProfileId] || m_InSync[p_ProfileId][p_ChatId]);
  LOG_TRACE("get cached message %d %d in %s", inSync, p_MsgId.c_str(), p_ChatId.c_str());

  int count = 0;
  try
  {
    // *INDENT-OFF*
    *m_Dbs[p_ProfileId] << "SELECT COUNT(*) FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;"
                        << p_ChatId << p_MsgId >>
      [&](const int& countRes)
      {
        count = countRes;
      };
    // *INDENT-ON*
  }
  catch (const sqlite::sqlite_exception& ex)
  {
    HANDLE_SQLITE_EXCEPTION(ex);
  }

  lock.unlock();

  if (count > 0)
  {
    std::shared_ptr<FetchOneMessageRequest> fetchOneRequest =
      std::make_shared<FetchOneMessageRequest>();
    fetchOneRequest->profileId = p_ProfileId;
    fetchOneRequest->chatId = p_ChatId;
    fetchOneRequest->msgId = p_MsgId;

    if (p_Sync)
    {
      LOG_DEBUG("cache sync fetch one %s %s", p_ChatId.c_str(), p_MsgId.c_str());
      PerformRequest(fetchOneRequest);
    }
    else
    {
      LOG_DEBUG("cache async fetch one %s %s", p_ChatId.c_str(), p_MsgId.c_str());
      EnqueueRequest(fetchOneRequest);
    }

    return true;
  }
  else
  {
    return false;
  }
}

bool MessageCache::GetOneMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                                 const std::string& p_MsgId, std::vector<ChatMessage>& p_ChatMessages)
{
  if (!m_CacheEnabled) return false;

  std::unique_lock<std::mutex> lock(m_DbMutex);
  if (!m_Dbs[p_ProfileId]) return false;

  PerformFetchOneMessage(p_ProfileId, p_ChatId, p_MsgId, p_ChatMessages);
  return !p_ChatMessages.empty();
}

void MessageCache::FindMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                               const std::string& p_FromMsgId, const std::string& p_LastMsgId,
                               const std::string& p_FindText, const std::string& p_FindMsgId)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<FindCachedMessageRequest> findCachedMessageRequest =
    std::make_shared<FindCachedMessageRequest>();
  findCachedMessageRequest->profileId = p_ProfileId;
  findCachedMessageRequest->chatId = p_ChatId;
  findCachedMessageRequest->fromMsgId = p_FromMsgId;
  findCachedMessageRequest->lastMsgId = p_LastMsgId;
  findCachedMessageRequest->findText = p_FindText;
  findCachedMessageRequest->findMsgId = p_FindMsgId;
  EnqueueRequest(findCachedMessageRequest);
}

void MessageCache::DeleteOneMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                                    const std::string& p_MsgId)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<DeleteOneMessageRequest> deleteOneMessageRequest =
    std::make_shared<DeleteOneMessageRequest>();
  deleteOneMessageRequest->profileId = p_ProfileId;
  deleteOneMessageRequest->chatId = p_ChatId;
  deleteOneMessageRequest->msgId = p_MsgId;
  EnqueueRequest(deleteOneMessageRequest);
}

void MessageCache::DeleteChat(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<DeleteOneChatRequest> deleteChatRequest =
    std::make_shared<DeleteOneChatRequest>();
  deleteChatRequest->profileId = p_ProfileId;
  deleteChatRequest->chatId = p_ChatId;
  EnqueueRequest(deleteChatRequest);
}

void MessageCache::UpdateMessageIsRead(const std::string& p_ProfileId, const std::string& p_ChatId,
                                       const std::string& p_MsgId, bool p_IsRead)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<UpdateMessageIsReadRequest> updateIsReadRequest = std::make_shared<UpdateMessageIsReadRequest>();
  updateIsReadRequest->profileId = p_ProfileId;
  updateIsReadRequest->chatId = p_ChatId;
  updateIsReadRequest->msgId = p_MsgId;
  updateIsReadRequest->isRead = p_IsRead;
  EnqueueRequest(updateIsReadRequest);
}

void MessageCache::UpdateMessageFileInfo(const std::string& p_ProfileId, const std::string& p_ChatId,
                                         const std::string& p_MsgId, const std::string& p_FileInfo)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<UpdateMessageFileInfoRequest> updateMessageFileInfoRequest =
    std::make_shared<UpdateMessageFileInfoRequest>();
  updateMessageFileInfoRequest->profileId = p_ProfileId;
  updateMessageFileInfoRequest->chatId = p_ChatId;
  updateMessageFileInfoRequest->msgId = p_MsgId;
  updateMessageFileInfoRequest->fileInfo = p_FileInfo;
  EnqueueRequest(updateMessageFileInfoRequest);
}

void MessageCache::UpdateMessageReactions(const std::string& p_ProfileId, const std::string& p_ChatId,
                                          const std::string& p_MsgId, const Reactions& p_Reactions)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<UpdateMessageReactionsRequest> updateMessageReactionsRequest =
    std::make_shared<UpdateMessageReactionsRequest>();
  updateMessageReactionsRequest->profileId = p_ProfileId;
  updateMessageReactionsRequest->chatId = p_ChatId;
  updateMessageReactionsRequest->msgId = p_MsgId;
  updateMessageReactionsRequest->reactions = p_Reactions;
  EnqueueRequest(updateMessageReactionsRequest);
}

void MessageCache::UpdateMute(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_IsMuted)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<UpdateMuteRequest> updateMuteRequest =
    std::make_shared<UpdateMuteRequest>();
  updateMuteRequest->profileId = p_ProfileId;
  updateMuteRequest->chatId = p_ChatId;
  updateMuteRequest->isMuted = p_IsMuted;
  EnqueueRequest(updateMuteRequest);
}

void MessageCache::UpdatePin(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_IsPinned,
                             int64_t p_TimePinned)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<UpdatePinRequest> updatePinRequest =
    std::make_shared<UpdatePinRequest>();
  updatePinRequest->profileId = p_ProfileId;
  updatePinRequest->chatId = p_ChatId;
  updatePinRequest->isPinned = p_IsPinned;
  updatePinRequest->timePinned = p_TimePinned;
  EnqueueRequest(updatePinRequest);
}

void MessageCache::Export(const std::string& p_ExportDir)
{
  if (!m_CacheEnabled)
  {
    std::cout << "Export failed (cache not enabled).\n";
    LOG_ERROR("export failed, cache not enabled.");
    return;
  }

  std::unique_lock<std::mutex> lock(m_DbMutex);

  for (auto& db : m_Dbs)
  {
    const std::string profileId = db.first;
    const std::string dirPath = p_ExportDir + "/" + profileId;
    FileUtil::RmDir(dirPath);
    FileUtil::MkDir(dirPath);

    std::cout << profileId << "\n";

    std::vector<std::string> chatIds;
    std::map<std::string, std::string> contactNames;

    try
    {
      // *INDENT-OFF*
      *m_Dbs[profileId] << "SELECT DISTINCT chatId FROM messages;" >>
        [&](const std::string& chatId)
        {
          chatIds.push_back(chatId);
        };

      const std::string selfName = "You";
      *m_Dbs[profileId] << "SELECT id, name, isSelf FROM " + s_TableContacts + ";" >>
        [&](const std::string& id, const std::string& name, int32_t isSelf)
        {
          contactNames[id] = isSelf ? selfName : name;
        };
      // *INDENT-ON*
    }
    catch (const sqlite::sqlite_exception& ex)
    {
      HANDLE_SQLITE_EXCEPTION(ex);
    }

    const int limit = std::numeric_limits<int>::max();
    const int64_t fromMsgIdTimeSent = std::numeric_limits<int64_t>::max();
    for (const auto& chatId : chatIds)
    {
      std::ofstream outFile;
      std::string lastYear;
      std::string chatName = chatId;
      std::string chatUser = contactNames[chatId];
      if (!chatUser.empty())
      {
        chatUser.erase(remove_if(chatUser.begin(), chatUser.end(), [](char c) { return !isalpha(c); }), chatUser.end());
        chatName += "_" + chatUser;
      }

      std::vector<ChatMessage> chatMessages;
      PerformFetchMessagesFrom(profileId, chatId, fromMsgIdTimeSent, limit, chatMessages);

      std::map<std::string, std::string> messageMap;
      for (auto chatMessage = chatMessages.rbegin(); chatMessage != chatMessages.rend(); ++chatMessage)
      {
        std::string timestr = TimeUtil::GetTimeString(chatMessage->timeSent, true /* p_IsExport */);
        std::string year = TimeUtil::GetYearString(chatMessage->timeSent);;
        if (year != lastYear)
        {
          lastYear = year;
          std::string outPath = dirPath + "/" + chatName + "_" + year + ".txt";
          std::cout << "Writing " << outPath << "\n";
          if (outFile.is_open())
          {
            outFile.close();
          }

          outFile.open(outPath, std::ios::binary);
        }

        std::string sender =
          contactNames[chatMessage->senderId].empty() ? chatMessage->senderId : contactNames[chatMessage->senderId];
        std::string header = sender + " (" + timestr + ")";
        outFile << header << "\n";

        std::string quotedMsg;
        messageMap[chatMessage->id] = chatMessage->text;
        if (!chatMessage->quotedId.empty())
        {
          auto quotedIt = messageMap.find(chatMessage->quotedId);
          if (quotedIt != messageMap.end())
          {
            quotedMsg = "> " + quotedIt->second;
            quotedMsg =
              StrUtil::ToString(StrUtil::Join(StrUtil::WordWrap(StrUtil::ToWString(quotedMsg),
                                                                72, false, false, true, 2), L"\n"));
          }
          else
          {
            quotedMsg = ">";
          }

          outFile << quotedMsg << "\n";
        }

        if (!chatMessage->fileInfo.empty())
        {
          FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(chatMessage->fileInfo);
          std::string fileName = FileUtil::BaseName(fileInfo.filePath);
          outFile << fileName << "\n";
        }

        if (!chatMessage->text.empty())
        {
          outFile << chatMessage->text << "\n";
        }

        outFile << "\n";
      }
    }
  }

  std::cout << "Export completed.\n";
}

void MessageCache::Process()
{
  while (m_Running)
  {
    std::shared_ptr<Request> request;

    {
      std::unique_lock<std::mutex> lock(m_QueueMutex);
      while (m_Queue.empty() && m_Running)
      {
        m_CondVar.wait(lock);
      }

      if (!m_Running)
      {
        if (!m_Queue.empty())
        {
          LOG_WARNING("Exiting with non-empty queue %d", m_Queue.size());
        }
        break;
      }

      request = m_Queue.front();
      m_Queue.pop_front();
    }

    PerformRequest(request);
    TimeUtil::Sleep(0.001); // hack for GCC -O2 to enable context switching for non-empty queue
  }

  if (!m_Queue.empty())
  {
    LOG_WARNING("Exiting with non-empty queue %d", m_Queue.size());
  }
}

void MessageCache::EnqueueRequest(std::shared_ptr<Request> p_Request)
{
  std::unique_lock<std::mutex> lock(m_QueueMutex);
  m_Queue.push_back(p_Request);
  m_CondVar.notify_one();
}

void MessageCache::PerformRequest(std::shared_ptr<Request> p_Request)
{
  switch (p_Request->GetRequestType())
  {
    case AddMessagesRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<AddMessagesRequest> addMessagesRequest =
          std::static_pointer_cast<AddMessagesRequest>(p_Request);
        const std::string& profileId = addMessagesRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = addMessagesRequest->chatId;
        const std::string& fromMsgId = addMessagesRequest->fromMsgId;
        LOG_DEBUG("cache add %s %s %d", chatId.c_str(), fromMsgId.c_str(),
                  addMessagesRequest->chatMessages.size());

        if (m_CheckSync[profileId] && !m_InSync[profileId][chatId])
        {
          if (!addMessagesRequest->chatMessages.empty())
          {
            std::string msgIds;
            for (const auto& msg : addMessagesRequest->chatMessages)
            {
              msgIds += (!msgIds.empty()) ? "," : "";
              msgIds += "'" + msg.id + "'";
            }

            int count = 0;
            try
            {
              // *INDENT-OFF*
              *m_Dbs[profileId] << "SELECT COUNT(*) FROM " + s_TableMessages + " WHERE chatId = ? AND id IN (" +
                msgIds + ");" << chatId >>
                [&](const int& countRes)
                {
                  count = countRes;
                };
              // *INDENT-ON*
            }
            catch (const sqlite::sqlite_exception& ex)
            {
              HANDLE_SQLITE_EXCEPTION(ex);
            }

            if (count > 0)
            {
              m_InSync[profileId][chatId] = true;
              LOG_DEBUG("cache in sync %s list (%s)", chatId.c_str(), msgIds.c_str());
            }
            else
            {
              LOG_DEBUG("cache not in sync %s list (%s)", chatId.c_str(), msgIds.c_str());
            }
          }
        }

        for (const auto& msg : addMessagesRequest->chatMessages)
        {
          // Fetch already cached message reactions
          Reactions oldReactions;
          try
          {
            // *INDENT-OFF*
            *m_Dbs[profileId] << "SELECT reactions FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;" <<
              chatId << msg.id >>
              [&](const std::vector<char>& reactionsBytes)
              {
                if (!reactionsBytes.empty())
                {
                  oldReactions = Serialization::FromBytes<Reactions>(reactionsBytes);
                }
              };
            // *INDENT-ON*
          }
          catch (const sqlite::sqlite_exception& ex)
          {
            HANDLE_SQLITE_EXCEPTION(ex);
          }

          Reactions reactions = msg.reactions;
          if (CacheUtil::IsDefaultReactions(oldReactions))
          {
            // If not previously cached, or cached reactions are default, then overwrite.

            LOG_DEBUG("insert reactions %s", msg.id.c_str());

            std::vector<char> reactionsBytes;
            if (!CacheUtil::IsDefaultReactions(reactions))
            {
              reactionsBytes = Serialization::ToBytes(reactions);
            }

            try
            {
              *m_Dbs[profileId] << "INSERT INTO " + s_TableMessages + " "
                "(chatId, id, senderId, text, quotedId, quotedText, quotedSender, fileInfo, timeSent, isOutgoing, isRead, reactions) VALUES "
                "(?,?,?,?,?,?,?,?,?,?,?,?);" <<
                chatId << msg.id << msg.senderId << msg.text << msg.quotedId << msg.quotedText << msg.quotedSender <<
                msg.fileInfo << msg.timeSent << msg.isOutgoing << msg.isRead << reactionsBytes;
            }
            catch (const sqlite::sqlite_exception& ex)
            {
              HANDLE_SQLITE_EXCEPTION(ex);
            }
          }
          else
          {
            // If message already exists and has non-default reactions, then merge reactions.

            LOG_DEBUG("merge reactions %s", msg.id.c_str());
            CacheUtil::UpdateReactions(oldReactions, reactions);
            std::vector<char> reactionsBytes;
            if (!CacheUtil::IsDefaultReactions(reactions))
            {
              reactionsBytes = Serialization::ToBytes(reactions);
            }

            try
            {
              *m_Dbs[profileId] << "INSERT INTO " + s_TableMessages + " "
                "(chatId, id, senderId, text, quotedId, quotedText, quotedSender, fileInfo, timeSent, isOutgoing, isRead, reactions) VALUES "
                "(?,?,?,?,?,?,?,?,?,?,?,?);" <<
                chatId << msg.id << msg.senderId << msg.text << msg.quotedId << msg.quotedText << msg.quotedSender <<
                msg.fileInfo << msg.timeSent << msg.isOutgoing << msg.isRead << reactionsBytes;
            }
            catch (const sqlite::sqlite_exception& ex)
            {
              HANDLE_SQLITE_EXCEPTION(ex);
            }

            {
              // Send consolidated info to ui
              std::shared_ptr<NewMessageReactionsNotify> newMessageReactionsNotify =
                std::make_shared<NewMessageReactionsNotify>(profileId);
              newMessageReactionsNotify->chatId = chatId;
              newMessageReactionsNotify->msgId = msg.id;
              newMessageReactionsNotify->reactions = reactions;
              CallMessageHandler(newMessageReactionsNotify);
            }
          }
        }
      }
      break;

    case AddChatsRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<AddChatsRequest> addChatsRequest =
          std::static_pointer_cast<AddChatsRequest>(p_Request);
        const std::string& profileId = addChatsRequest->profileId;
        if (!m_Dbs[profileId]) return;

        LOG_DEBUG("cache add chats %d", addChatsRequest->chatInfos.size());

        if (addChatsRequest->chatInfos.empty()) return;

        try
        {
          *m_Dbs[profileId] << "BEGIN;";
          for (const auto& chatInfo : addChatsRequest->chatInfos)
          {
            *m_Dbs[profileId] << "INSERT INTO " + s_TableChats + " "
              "(id, isMuted, isPinned, lastMessageTime) VALUES "
              "(?, ?, ?, ?);" <<
              chatInfo.id << chatInfo.isMuted << chatInfo.isPinned <<
              chatInfo.lastMessageTime;
          }
          *m_Dbs[profileId] << "COMMIT;";
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }
      }
      break;

    case AddContactsRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<AddContactsRequest> addContactsRequest =
          std::static_pointer_cast<AddContactsRequest>(p_Request);
        const bool fullSync = addContactsRequest->fullSync;
        const std::string& profileId = addContactsRequest->profileId;
        if (!m_Dbs[profileId]) return;

        LOG_DEBUG("cache add contacts %d", addContactsRequest->contactInfos.size());

        if (addContactsRequest->contactInfos.empty()) return;

        try
        {
          *m_Dbs[profileId] << "BEGIN;";
          if (fullSync)
          {
            *m_Dbs[profileId] << "DELETE FROM " + s_TableContacts + ";";
          }

          for (const auto& contactInfo : addContactsRequest->contactInfos)
          {
            *m_Dbs[profileId] << "INSERT INTO " + s_TableContacts + " "
              "(id, name, phone, isSelf) VALUES "
              "(?,?,?,?);" <<
              contactInfo.id << contactInfo.name << contactInfo.phone << contactInfo.isSelf;
          }
          *m_Dbs[profileId] << "COMMIT;";
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }
      }
      break;

    case FetchChatsRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<FetchChatsRequest> fetchChatsRequest =
          std::static_pointer_cast<FetchChatsRequest>(p_Request);
        const std::string& profileId = fetchChatsRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const bool noFilter = fetchChatsRequest->chatIds.empty();
        std::vector<ChatInfo> chatInfos;
        try
        {
          // *INDENT-OFF*
          std::map<std::string, int32_t> chatIdMuted;
          std::map<std::string, int32_t> chatIdPinned;
          std::map<std::string, int64_t> chatIdLastMessageTime;
          *m_Dbs[profileId] << "SELECT id, isMuted, isPinned, lastMessageTime FROM " + s_TableChats + ";" >>
            [&](const std::string& chatId, int32_t isMuted, int32_t isPinned, int64_t lastMessageTime)
            {
              chatIdMuted[chatId] = isMuted;
              chatIdPinned[chatId] = isPinned;
              chatIdLastMessageTime[chatId] = lastMessageTime;
            };

          *m_Dbs[profileId] << "SELECT chatId, MAX(timeSent), isOutgoing, isRead FROM " + s_TableMessages + " "
            "GROUP BY chatId;" >>
            [&](const std::string& chatId, int64_t timeSent, int32_t isOutgoing, int32_t isRead)
            {
              if (noFilter || fetchChatsRequest->chatIds.count(chatId))
              {
                ChatInfo chatInfo;
                chatInfo.id = chatId;
                chatInfo.isUnread = !isOutgoing && !isRead;
                chatInfo.isMuted = chatIdMuted[chatId];
                chatInfo.isPinned = chatIdPinned[chatId];
                chatInfo.lastMessageTime = chatInfo.isPinned ? chatIdLastMessageTime[chatId] : timeSent;
                chatInfos.push_back(chatInfo);
              }
            };
          // *INDENT-ON*
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        lock.unlock();
        LOG_DEBUG("cache fetch %d chats", chatInfos.size());

        std::shared_ptr<NewChatsNotify> newChatsNotify =
          std::make_shared<NewChatsNotify>(profileId);
        newChatsNotify->success = true;
        newChatsNotify->chatInfos = chatInfos;
        CallMessageHandler(newChatsNotify);
      }
      break;

    case FetchContactsRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<FetchContactsRequest> fetchContactsRequest =
          std::static_pointer_cast<FetchContactsRequest>(p_Request);
        const std::string& profileId = fetchContactsRequest->profileId;
        if (!m_Dbs[profileId]) return;

        std::vector<ContactInfo> contactInfos;
        try
        {
          // *INDENT-OFF*
          *m_Dbs[profileId] << "SELECT id, name, phone, isSelf FROM " + s_TableContacts + ";" >>
            [&](const std::string& id, const std::string& name, const std::string& phone, int32_t isSelf)
            {
              ContactInfo contactInfo;
              contactInfo.id = id;
              contactInfo.name = name;
              contactInfo.phone = phone;
              contactInfo.isSelf = isSelf;
              contactInfos.push_back(contactInfo);
            };
          // *INDENT-ON*
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        lock.unlock();
        LOG_DEBUG("cache fetch %d contacts", contactInfos.size());

        std::shared_ptr<NewContactsNotify> newContactsNotify =
          std::make_shared<NewContactsNotify>(profileId);
        newContactsNotify->contactInfos = contactInfos;
        CallMessageHandler(newContactsNotify);
      }
      break;

    case FetchMessagesFromRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<FetchMessagesFromRequest> fetchFromRequest =
          std::static_pointer_cast<FetchMessagesFromRequest>(p_Request);
        const std::string& profileId = fetchFromRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = fetchFromRequest->chatId;
        const std::string& fromMsgId = fetchFromRequest->fromMsgId;
        const int limit = fetchFromRequest->limit;

        int64_t fromMsgIdTimeSent = 0;
        if (!fromMsgId.empty())
        {
          try
          {
            // *INDENT-OFF*
            *m_Dbs[profileId] << "SELECT timeSent FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;" <<
              chatId << fromMsgId >>
              [&](const int64_t& timeSent)
              {
                fromMsgIdTimeSent = timeSent;
              };
            // *INDENT-ON*
          }
          catch (const sqlite::sqlite_exception& ex)
          {
            HANDLE_SQLITE_EXCEPTION(ex);
          }
        }
        else
        {
          fromMsgIdTimeSent = std::numeric_limits<int64_t>::max();
        }

        std::vector<ChatMessage> chatMessages;
        PerformFetchMessagesFrom(profileId, chatId, fromMsgIdTimeSent, limit, chatMessages);
        LOG_DEBUG("cache fetch from %s %s %d %d", chatId.c_str(), fromMsgId.c_str(), limit, chatMessages.size());
        lock.unlock();

        std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(profileId);
        newMessagesNotify->success = true;
        newMessagesNotify->chatId = chatId;
        newMessagesNotify->chatMessages = chatMessages;
        newMessagesNotify->fromMsgId = fromMsgId;
        newMessagesNotify->cached = true;
        newMessagesNotify->sequence = true; // in-sequence history request
        CallMessageHandler(newMessagesNotify);
      }
      break;

    case FetchOneMessageRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<FetchOneMessageRequest> fetchOneRequest =
          std::static_pointer_cast<FetchOneMessageRequest>(p_Request);
        const std::string& profileId = fetchOneRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = fetchOneRequest->chatId;
        const std::string& msgId = fetchOneRequest->msgId;

        std::vector<ChatMessage> chatMessages;
        PerformFetchOneMessage(profileId, chatId, msgId, chatMessages);
        LOG_DEBUG("cache fetch one %s %s %d", chatId.c_str(), msgId.c_str(), chatMessages.size());
        lock.unlock();

        if (!chatMessages.empty())
        {
          std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(profileId);
          newMessagesNotify->success = true;
          newMessagesNotify->chatId = chatId;
          newMessagesNotify->chatMessages = chatMessages;
          newMessagesNotify->cached = true;
          newMessagesNotify->sequence = false; // out-of-sequence single message
          CallMessageHandler(newMessagesNotify);
        }
      }
      break;

    case FindCachedMessageRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<FindCachedMessageRequest> findCachedMessageRequest =
          std::static_pointer_cast<FindCachedMessageRequest>(p_Request);
        const std::string& profileId = findCachedMessageRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = findCachedMessageRequest->chatId;
        const std::string& fromMsgId = findCachedMessageRequest->fromMsgId;
        const std::string& findText = findCachedMessageRequest->findText;
        const std::string& findMsgId = findCachedMessageRequest->findMsgId;

        int64_t findFromMsgIdTimeSent = 0;
        if (!fromMsgId.empty())
        {
          try
          {
            // *INDENT-OFF*
            *m_Dbs[profileId] << "SELECT timeSent FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;" <<
              chatId << fromMsgId >>
              [&](const int64_t& timeSent)
              {
                findFromMsgIdTimeSent = timeSent;
              };
            // *INDENT-ON*
          }
          catch (const sqlite::sqlite_exception& ex)
          {
            HANDLE_SQLITE_EXCEPTION(ex);
          }
        }
        else
        {
          findFromMsgIdTimeSent = std::numeric_limits<int64_t>::max();
        }

        std::string foundMsgId;
        int64_t foundMsgIdTimeSent = 0;
        if (!findText.empty())
        {
          try
          {
            // *INDENT-OFF*
            *m_Dbs[profileId] <<
              "SELECT " + s_TableMessages + ".id, timeSent "
              "FROM " + s_TableMessages + " "
              "LEFT JOIN " + s_TableContacts + " "
              "ON " + s_TableMessages + ".senderId = " + s_TableContacts + ".id "
              "WHERE chatId = ? AND timeSent < ? "
              "AND ((instr(lower(text), lower(?)) > 0) OR "
              "     (instr(lower(CASE WHEN isSelf THEN 'You' ELSE name END), lower(?)) > 0)) "
              "ORDER BY timeSent DESC LIMIT 1;"
              << chatId << findFromMsgIdTimeSent << findText << findText >>
              [&](const std::string& id, const int64_t& timeSent)
              {
                foundMsgId = id;
                foundMsgIdTimeSent = timeSent;
              };
            // *INDENT-ON*
          }
          catch (const sqlite::sqlite_exception& ex)
          {
            HANDLE_SQLITE_EXCEPTION(ex);
          }
        }
        else if (!findMsgId.empty())
        {
          try
          {
            // *INDENT-OFF*
            *m_Dbs[profileId] <<
              "SELECT id, timeSent FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;" << chatId << findMsgId >>
              [&](const std::string& id, const int64_t& timeSent)
              {
                foundMsgId = id;
                foundMsgIdTimeSent = timeSent;
              };
            // *INDENT-ON*
          }
          catch (const sqlite::sqlite_exception& ex)
          {
            HANDLE_SQLITE_EXCEPTION(ex);
          }
        }
        else
        {
          LOG_WARNING("neither text nor msg id specified");
        }

        if (!foundMsgId.empty())
        {
          int64_t fetchFromMsgIdTimeSent = 0;
          const std::string& lastMsgId = findCachedMessageRequest->lastMsgId;
          if (!lastMsgId.empty())
          {
            try
            {
              // *INDENT-OFF*
              *m_Dbs[profileId] << "SELECT timeSent FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;" <<
                chatId << lastMsgId >>
                [&](const int64_t& timeSent)
                {
                  fetchFromMsgIdTimeSent = timeSent;
                };
              // *INDENT-ON*
            }
            catch (const sqlite::sqlite_exception& ex)
            {
              HANDLE_SQLITE_EXCEPTION(ex);
            }
          }
          else
          {
            fetchFromMsgIdTimeSent = std::numeric_limits<int64_t>::max();
          }

          int limit = 0;
          if (fetchFromMsgIdTimeSent > foundMsgIdTimeSent)
          {
            // Determine number of messages to fetch
            try
            {
              // *INDENT-OFF*
              *m_Dbs[profileId] << "SELECT COUNT(id) FROM " + s_TableMessages + " WHERE chatId = ? AND timeSent < ? AND timeSent >= ?;" <<
                chatId << fetchFromMsgIdTimeSent << foundMsgIdTimeSent >>
                [&](const int64_t& count)
                {
                  limit = count;
                };
              // *INDENT-ON*
            }
            catch (const sqlite::sqlite_exception& ex)
            {
              HANDLE_SQLITE_EXCEPTION(ex);
            }
          }

          std::vector<ChatMessage> chatMessages;
          if (limit > 0)
          {
            PerformFetchMessagesFrom(profileId, chatId, fetchFromMsgIdTimeSent, limit, chatMessages);
          }

          lock.unlock();

          if (!chatMessages.empty())
          {
            std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(profileId);
            newMessagesNotify->success = true;
            newMessagesNotify->chatId = chatId;
            newMessagesNotify->chatMessages = chatMessages;
            newMessagesNotify->fromMsgId = lastMsgId;
            newMessagesNotify->cached = true;
            newMessagesNotify->sequence = true; // in-sequence history request
            CallMessageHandler(newMessagesNotify);
          }

          std::shared_ptr<FindMessageNotify> findMessageNotify = std::make_shared<FindMessageNotify>(profileId);
          findMessageNotify->success = true;
          findMessageNotify->chatId = chatId;
          findMessageNotify->msgId = foundMsgId;
          CallMessageHandler(findMessageNotify);
        }
        else
        {
          lock.unlock();

          std::shared_ptr<FindMessageNotify> findMessageNotify = std::make_shared<FindMessageNotify>(profileId);
          findMessageNotify->success = false;
          findMessageNotify->chatId = chatId;
          findMessageNotify->msgId = findMsgId;
          CallMessageHandler(findMessageNotify);
        }
      }
      break;

    case DeleteOneMessageRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<DeleteOneMessageRequest> deleteOneMessageRequest =
          std::static_pointer_cast<DeleteOneMessageRequest>(p_Request);
        const std::string& profileId = deleteOneMessageRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = deleteOneMessageRequest->chatId;
        const std::string& msgId = deleteOneMessageRequest->msgId;

        try
        {
          *m_Dbs[profileId] << "DELETE FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;" << chatId << msgId;
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        LOG_DEBUG("cache delete %s %s", chatId.c_str(), msgId.c_str());
      }
      break;

    case DeleteOneChatRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<DeleteOneChatRequest> deleteChatRequest =
          std::static_pointer_cast<DeleteOneChatRequest>(p_Request);
        const std::string& profileId = deleteChatRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = deleteChatRequest->chatId;

        try
        {
          *m_Dbs[profileId] << "DELETE FROM " + s_TableMessages + " WHERE chatId = ?;" << chatId;

          *m_Dbs[profileId] << "DELETE FROM " + s_TableChats + " WHERE id = ?;" << chatId;
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        LOG_DEBUG("cache delete %s", chatId.c_str());
      }
      break;

    case UpdateMessageIsReadRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<UpdateMessageIsReadRequest> updateIsReadRequest =
          std::static_pointer_cast<UpdateMessageIsReadRequest>(p_Request);
        const std::string& profileId = updateIsReadRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = updateIsReadRequest->chatId;
        const std::string& msgId = updateIsReadRequest->msgId;
        bool isRead = updateIsReadRequest->isRead;

        try
        {
          *m_Dbs[profileId] << "UPDATE " + s_TableMessages + " SET isRead = ? WHERE chatId = ? AND id = ?;" <<
            (int)isRead << chatId << msgId;
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        LOG_DEBUG("cache update read %s %s %d", chatId.c_str(), msgId.c_str(), isRead);
      }
      break;

    case UpdateMessageFileInfoRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<UpdateMessageFileInfoRequest> updateMessageFileInfoRequest =
          std::static_pointer_cast<UpdateMessageFileInfoRequest>(p_Request);
        const std::string& profileId = updateMessageFileInfoRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = updateMessageFileInfoRequest->chatId;
        const std::string& msgId = updateMessageFileInfoRequest->msgId;
        const std::string& fileInfo = updateMessageFileInfoRequest->fileInfo;

        try
        {
          *m_Dbs[profileId] << "UPDATE " + s_TableMessages + " SET fileInfo = ? WHERE chatId = ? AND id = ?;"
                            << fileInfo << chatId << msgId;
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        LOG_DEBUG("cache update fileInfo %s %s %s %d", chatId.c_str(), msgId.c_str(), fileInfo.c_str());
      }
      break;

    case UpdateMessageReactionsRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<UpdateMessageReactionsRequest> updateMessageReactionsRequest =
          std::static_pointer_cast<UpdateMessageReactionsRequest>(p_Request);
        const std::string& profileId = updateMessageReactionsRequest->profileId;
        if (!m_Dbs[profileId]) return;

        if (CacheUtil::IsDefaultReactions(updateMessageReactionsRequest->reactions)) return;

        Reactions oldReactions;
        Reactions reactions = updateMessageReactionsRequest->reactions;
        const std::string& chatId = updateMessageReactionsRequest->chatId;
        const std::string& msgId = updateMessageReactionsRequest->msgId;

        try
        {
          // *INDENT-OFF*
          *m_Dbs[profileId] << "SELECT reactions FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;" <<
            chatId << msgId >>
            [&](const std::vector<char>& reactionsBytes)
            {
              if (!reactionsBytes.empty())
              {
                oldReactions = Serialization::FromBytes<Reactions>(reactionsBytes);
              }
            };
          // *INDENT-ON*
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        LOG_DEBUG("update reactions %s", msgId.c_str());
        CacheUtil::UpdateReactions(oldReactions, reactions);

        std::vector<char> reactionsBytes;
        if (!CacheUtil::IsDefaultReactions(reactions))
        {
          reactionsBytes = Serialization::ToBytes(reactions);
        }

        try
        {
          *m_Dbs[profileId] << "UPDATE " + s_TableMessages + " SET reactions = ? WHERE chatId = ? AND id = ?;"
                            << reactionsBytes << chatId << msgId;
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        {
          // Send consolidated info to ui
          std::shared_ptr<NewMessageReactionsNotify> newMessageReactionsNotify =
            std::make_shared<NewMessageReactionsNotify>(profileId);
          newMessageReactionsNotify->chatId = chatId;
          newMessageReactionsNotify->msgId = msgId;
          newMessageReactionsNotify->reactions = reactions;
          CallMessageHandler(newMessageReactionsNotify);
        }

        LOG_DEBUG("cache update reactions %s %s", chatId.c_str(), msgId.c_str());
      }
      break;

    case UpdateMuteRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<UpdateMuteRequest> updateMuteRequest =
          std::static_pointer_cast<UpdateMuteRequest>(p_Request);
        const std::string& profileId = updateMuteRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = updateMuteRequest->chatId;
        bool isMuted = updateMuteRequest->isMuted;

        try
        {
          *m_Dbs[profileId] << "INSERT INTO " + s_TableChats + " "
            "(id, isMuted) VALUES "
            "(?, ?) ON CONFLICT(id) DO UPDATE SET isMuted=?;" <<
            chatId << isMuted << isMuted;
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        LOG_DEBUG("cache update muted %s %d", chatId.c_str(), isMuted);
      }
      break;

    case UpdatePinRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<UpdatePinRequest> updatePinRequest =
          std::static_pointer_cast<UpdatePinRequest>(p_Request);
        const std::string& profileId = updatePinRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = updatePinRequest->chatId;
        bool isPinned = updatePinRequest->isPinned;
        int64_t timePinned = updatePinRequest->timePinned;

        try
        {
          *m_Dbs[profileId] << "INSERT INTO " + s_TableChats + " "
            "(id, isPinned, lastMessageTime) VALUES "
            "(?, ?, ?) ON CONFLICT(id) DO UPDATE SET isPinned=?, lastMessageTime=?;" <<
            chatId << isPinned << timePinned << isPinned << timePinned;
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        LOG_DEBUG("cache update pinned %s %d", chatId.c_str(), isPinned);
      }
      break;

    default:
      {
        LOG_WARNING("cache unknown request type %d", p_Request->GetRequestType());
      }
      break;
  }
}

void MessageCache::PerformFetchMessagesFrom(const std::string& p_ProfileId, const std::string& p_ChatId,
                                            const int64_t p_FromMsgIdTimeSent, const int p_Limit,
                                            std::vector<ChatMessage>& p_ChatMessages)
{
  try
  {
    // *INDENT-OFF*
    *m_Dbs[p_ProfileId] <<
      "SELECT id, senderId, text, quotedId, quotedText, quotedSender, fileInfo, reactions, timeSent, "
      "isOutgoing, isRead FROM " + s_TableMessages + " WHERE chatId = ? AND timeSent < ? "
      "ORDER BY timeSent DESC LIMIT ?;" << p_ChatId << p_FromMsgIdTimeSent << p_Limit >>
      [&](const std::string& id, const std::string& senderId, const std::string& text,
          const std::string& quotedId, const std::string& quotedText,
          const std::string& quotedSender, const std::string& fileInfo,
          std::vector<char> reactionsBytes,
          int64_t timeSent, int32_t isOutgoing, int32_t isRead)
      {
        ChatMessage chatMessage;
        chatMessage.id = id;
        chatMessage.senderId = senderId;
        chatMessage.text = text;
        chatMessage.quotedId = quotedId;
        chatMessage.quotedText = quotedText;
        chatMessage.quotedSender = quotedSender;
        chatMessage.fileInfo = fileInfo;
        chatMessage.timeSent = timeSent;
        chatMessage.isOutgoing = isOutgoing;
        chatMessage.isRead = isRead;

        if (!reactionsBytes.empty())
        {
          chatMessage.reactions = Serialization::FromBytes<Reactions>(reactionsBytes);
        }

        p_ChatMessages.push_back(chatMessage);
      };
    // *INDENT-ON*
  }
  catch (const sqlite::sqlite_exception& ex)
  {
    HANDLE_SQLITE_EXCEPTION(ex);
  }
}

void MessageCache::PerformFetchOneMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                                          const std::string& p_MsgId,
                                          std::vector<ChatMessage>& p_ChatMessages)
{
  try
  {
    // *INDENT-OFF*
    *m_Dbs[p_ProfileId] <<
      "SELECT id, senderId, text, quotedId, quotedText, quotedSender, fileInfo, reactions, timeSent, "
      "isOutgoing, isRead FROM " + s_TableMessages + " WHERE chatId = ? AND id = ?;" << p_ChatId << p_MsgId >>
      [&](const std::string& id, const std::string& senderId, const std::string& text,
          const std::string& quotedId, const std::string& quotedText,
          const std::string& quotedSender, const std::string& fileInfo,
          std::vector<char> reactionsBytes,
          int64_t timeSent, int32_t isOutgoing, int32_t isRead)
      {
        ChatMessage chatMessage;
        chatMessage.id = id;
        chatMessage.senderId = senderId;
        chatMessage.text = text;
        chatMessage.quotedId = quotedId;
        chatMessage.quotedText = quotedText;
        chatMessage.quotedSender = quotedSender;
        chatMessage.fileInfo = fileInfo;
        chatMessage.timeSent = timeSent;
        chatMessage.isOutgoing = isOutgoing;
        chatMessage.isRead = isRead;

        if (!reactionsBytes.empty())
        {
          chatMessage.reactions = Serialization::FromBytes<Reactions>(reactionsBytes);
        }

        p_ChatMessages.push_back(chatMessage);
      };
    // *INDENT-ON*
  }
  catch (const sqlite::sqlite_exception& ex)
  {
    HANDLE_SQLITE_EXCEPTION(ex);
  }
}

void MessageCache::CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  if (m_MessageHandler)
  {
    m_MessageHandler(p_ServiceMessage);
  }
  else
  {
    LOG_WARNING("message handler not set");
  }
}
