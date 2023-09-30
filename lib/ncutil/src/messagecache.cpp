// messagecache.cpp
//
// Copyright (c) 2020-2023 Kristofer Berggren
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
#include "log.h"
#include "fileutil.h"
#include "protocolutil.h"
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

// @note: minor db schema updates can simply update table name to avoid losing other tables data
static const std::string s_TableContacts = "contacts2";

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
        MessageCache::AddContacts(p_ProfileId, newContactsNotify->contactInfos);
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

    default:
      break;
  }
}

void MessageCache::AddProfile(const std::string& p_ProfileId, bool p_CheckSync, int p_DirVersion, bool p_IsSetup)
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
  m_Dbs[p_ProfileId].reset(new sqlite::database(dbPath));
  if (!m_Dbs[p_ProfileId]) return;

  try
  {
    *m_Dbs[p_ProfileId] << "PRAGMA synchronous = OFF";
    *m_Dbs[p_ProfileId] << "PRAGMA journal_mode = MEMORY";

    // create table if not exists
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

    *m_Dbs[p_ProfileId] << "CREATE TABLE IF NOT EXISTS " + s_TableContacts + " ("
      "id TEXT,"
      "name TEXT,"
      "phone TEXT,"
      "isSelf INT,"
      "UNIQUE(id) ON CONFLICT REPLACE"
      ");";

    *m_Dbs[p_ProfileId] << "CREATE TABLE IF NOT EXISTS chats ("
      "id TEXT,"
      "UNIQUE(id) ON CONFLICT REPLACE"
      ");";

    // @todo: create index (id, timeSent, chatId)
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

void MessageCache::AddContacts(const std::string& p_ProfileId,
                               const std::vector<ContactInfo>& p_ContactInfos)
{
  if (!m_CacheEnabled) return;

  std::shared_ptr<AddContactsRequest> addContactsRequest = std::make_shared<AddContactsRequest>();
  addContactsRequest->profileId = p_ProfileId;
  addContactsRequest->contactInfos = p_ContactInfos;
  EnqueueRequest(addContactsRequest);
}

bool MessageCache::FetchChats(const std::string& p_ProfileId)
{
  if (!m_CacheEnabled) return false;

  std::unique_lock<std::mutex> lock(m_DbMutex);
  if (!m_Dbs[p_ProfileId]) return false;

  lock.unlock();

  std::shared_ptr<FetchChatsRequest> fetchChatsRequest = std::make_shared<FetchChatsRequest>();
  fetchChatsRequest->profileId = p_ProfileId;

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
      *m_Dbs[p_ProfileId] << "SELECT timeSent FROM messages WHERE chatId = ? AND id = ?;"
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
    *m_Dbs[p_ProfileId] << "SELECT COUNT(*) FROM messages WHERE chatId = ? AND timeSent < ?;"
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
    *m_Dbs[p_ProfileId] << "SELECT COUNT(*) FROM messages WHERE chatId = ? AND id = ?;"
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
        std::string timestr = TimeUtil::GetTimeString(chatMessage->timeSent, false /* p_Short */);
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
              *m_Dbs[profileId] << "SELECT COUNT(*) FROM messages WHERE chatId = ? AND id IN (" +
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

        try
        {
          *m_Dbs[profileId] << "BEGIN;";
          for (const auto& msg : addMessagesRequest->chatMessages)
          {
            *m_Dbs[profileId] << "INSERT INTO messages "
              "(chatId, id, senderId, text, quotedId, quotedText, quotedSender, fileInfo, timeSent, isOutgoing, isRead) VALUES "
              "(?,?,?,?,?,?,?,?,?,?,?);" <<
              chatId << msg.id << msg.senderId << msg.text << msg.quotedId << msg.quotedText << msg.quotedSender <<
              msg.fileInfo << msg.timeSent <<
              msg.isOutgoing << msg.isRead;
          }
          *m_Dbs[profileId] << "COMMIT;";
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
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
            *m_Dbs[profileId] << "INSERT INTO chats "
              "(id) VALUES "
              "(?);" <<
              chatInfo.id;
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
        const std::string& profileId = addContactsRequest->profileId;
        if (!m_Dbs[profileId]) return;

        LOG_DEBUG("cache add contacts %d", addContactsRequest->contactInfos.size());

        if (addContactsRequest->contactInfos.empty()) return;

        try
        {
          *m_Dbs[profileId] << "BEGIN;";
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

        std::vector<ChatInfo> chatInfos;
        try
        {
          // *INDENT-OFF*
          *m_Dbs[profileId] << "SELECT chatId, MAX(timeSent), isOutgoing, isRead FROM messages "
            "GROUP BY chatId;" >>
            [&](const std::string& chatId, int64_t timeSent, int32_t isOutgoing, int32_t isRead)
            {
              ChatInfo chatInfo;
              chatInfo.id = chatId;
              chatInfo.isUnread = !isOutgoing && !isRead;
              chatInfo.lastMessageTime = timeSent;
              chatInfos.push_back(chatInfo);
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
            *m_Dbs[profileId] << "SELECT timeSent FROM messages WHERE chatId = ? AND id = ?;" <<
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
          *m_Dbs[profileId] << "DELETE FROM messages WHERE chatId = ? AND id = ?;" << chatId << msgId;
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        LOG_DEBUG("cache delete %s %s", chatId.c_str(), msgId.c_str());
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
          *m_Dbs[profileId] << "UPDATE messages SET isRead = ? WHERE chatId = ? AND id = ?;" << (int)isRead << chatId <<
            msgId;
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
          *m_Dbs[profileId] << "UPDATE messages SET fileInfo = ? WHERE chatId = ? AND id = ?;"
                            << fileInfo << chatId << msgId;
        }
        catch (const sqlite::sqlite_exception& ex)
        {
          HANDLE_SQLITE_EXCEPTION(ex);
        }

        LOG_DEBUG("cache update fileInfo %s %s %s %d", chatId.c_str(), msgId.c_str(), fileInfo.c_str());
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
      "SELECT id, senderId, text, quotedId, quotedText, quotedSender, fileInfo, timeSent, "
      "isOutgoing, isRead FROM messages WHERE chatId = ? AND timeSent < ? "
      "ORDER BY timeSent DESC LIMIT ?;" << p_ChatId << p_FromMsgIdTimeSent << p_Limit >>
      [&](const std::string& id, const std::string& senderId, const std::string& text,
          const std::string& quotedId, const std::string& quotedText,
          const std::string& quotedSender, const std::string& fileInfo,
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
      "SELECT id, senderId, text, quotedId, quotedText, quotedSender, fileInfo, timeSent, "
      "isOutgoing, isRead FROM messages WHERE chatId = ? AND id = ?;" << p_ChatId << p_MsgId >>
      [&](const std::string& id, const std::string& senderId, const std::string& text,
          const std::string& quotedId, const std::string& quotedText,
          const std::string& quotedSender, const std::string& fileInfo,
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
