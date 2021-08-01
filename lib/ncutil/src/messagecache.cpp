// messagecache.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "messagecache.h"

#include <map>
#include <utility>

#include <sqlite_modern_cpp.h>

#include "log.h"
#include "fileutil.h"

std::function<void(std::shared_ptr<ServiceMessage>)> MessageCache::m_MessageHandler;
std::mutex MessageCache::m_DbMutex;
std::map<std::string, std::unique_ptr<sqlite::database>> MessageCache::m_Dbs;
bool MessageCache::m_Running = false;
std::thread MessageCache::m_Thread;
std::mutex MessageCache::m_QueueMutex;
std::condition_variable MessageCache::m_CondVar;
std::deque<std::shared_ptr<MessageCache::Request>> MessageCache::m_Queue;
std::string MessageCache::m_HistoryDir;

void MessageCache::Init(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler)
{
  static const int dirVersion = 1;
  m_HistoryDir = FileUtil::GetApplicationDir() + "/history";
  FileUtil::InitDirVersion(m_HistoryDir, dirVersion);

  {
    std::lock_guard<std::mutex> lock(m_DbMutex);
    m_MessageHandler = p_MessageHandler;
  }

  std::lock_guard<std::mutex> lock(m_QueueMutex);
  if (!m_Running)
  {
    m_Running = true;
    m_Thread = std::thread(MessageCache::Process);
  }
}

void MessageCache::Cleanup()
{
  if (m_Running)
  {
    {
      std::lock_guard<std::mutex> lock(m_QueueMutex);
      m_Running = false;
      m_CondVar.notify_one();
    }
    m_Thread.join();
  }

  {
    std::lock_guard<std::mutex> lock(m_DbMutex);
    m_MessageHandler = nullptr;
    m_Dbs.clear();
  }
}

void MessageCache::AddProfile(const std::string& p_ProfileId)
{
  std::lock_guard<std::mutex> lock(m_DbMutex);
  const std::string& dbDir = m_HistoryDir + "/" + p_ProfileId;
  FileUtil::MkDir(dbDir);

  const std::string& dbPath = dbDir + "/db.sqlite";
  m_Dbs[p_ProfileId].reset(new sqlite::database(dbPath));
  if (!m_Dbs[p_ProfileId]) return;

  // create table if not exists
  *m_Dbs[p_ProfileId] << "CREATE TABLE IF NOT EXISTS messages ("
    "chatId TEXT,"
    "id TEXT,"
    "senderId TEXT,"
    "text TEXT,"
    "quotedId TEXT,"
    "quotedText TEXT,"
    "quotedSender TEXT,"
    "filePath TEXT,"
    "fileType TEXT,"
    "timeSent INT,"
    "isOutgoing INT,"
    "isRead INT,"
    "isLast INT DEFAULT 0,"
    "UNIQUE(chatId, id) ON CONFLICT REPLACE"
    ");";

  // @todo: create index (id, timeSent, chatId)
}

void MessageCache::Add(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_FromMsgId,
                       const std::vector<ChatMessage>& p_ChatMessages)
{
  std::shared_ptr<AddRequest> addRequest = std::make_shared<AddRequest>();
  addRequest->profileId = p_ProfileId;
  addRequest->chatId = p_ChatId;
  addRequest->fromMsgId = p_FromMsgId;
  addRequest->chatMessages = p_ChatMessages;
  EnqueueRequest(addRequest);
}

bool MessageCache::Fetch(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_FromMsgId,
                         int p_Limit)
{
  std::lock_guard<std::mutex> lock(m_DbMutex);
  if (!m_Dbs[p_ProfileId]) return false;

  if (p_FromMsgId.empty())
  {
    LOG_DEBUG("cache cannot fetch %s frommsg empty", p_ChatId.c_str());
    return false;
  }

  int64_t fromMsgIdTimeSent = 0;
  bool fromMsgIsLast = false;
  *m_Dbs[p_ProfileId] << "SELECT timeSent,isLast FROM messages WHERE chatId = ? AND id = ?;" << p_ChatId <<
    p_FromMsgId >>
    [&](const int64_t& timeSent, const int32_t& isLast)
      {
      fromMsgIdTimeSent = timeSent;
      fromMsgIsLast = isLast;
    };

  int countOlder = 0;
  *m_Dbs[p_ProfileId] << "SELECT COUNT(*) FROM messages WHERE chatId = ? AND timeSent < ?;" << p_ChatId <<
    fromMsgIdTimeSent >>
    [&](const int& count)
      {
      countOlder = count;
    };

  if (fromMsgIsLast || (countOlder > 0))
  {
    std::shared_ptr<FetchRequest> fetchRequest = std::make_shared<FetchRequest>();
    fetchRequest->profileId = p_ProfileId;
    fetchRequest->chatId = p_ChatId;
    fetchRequest->fromMsgId = p_FromMsgId;
    fetchRequest->limit = p_Limit;
    EnqueueRequest(fetchRequest);

    LOG_DEBUG("cache will fetch %s %s last %d older %d", p_ChatId.c_str(),
              p_FromMsgId.c_str(), fromMsgIsLast, countOlder);
    return true;
  }
  else
  {
    LOG_DEBUG("cache cannot fetch %s %s last %d older %d", p_ChatId.c_str(),
              p_FromMsgId.c_str(), fromMsgIsLast, countOlder);
    return false;
  }
}

void MessageCache::Delete(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId)
{
  std::shared_ptr<DeleteRequest> deleteRequest = std::make_shared<DeleteRequest>();
  deleteRequest->profileId = p_ProfileId;
  deleteRequest->chatId = p_ChatId;
  deleteRequest->msgId = p_MsgId;
  EnqueueRequest(deleteRequest);
}

void MessageCache::UpdateIsRead(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId, bool p_IsRead)
{
  std::shared_ptr<UpdateIsReadRequest> updateIsReadRequest = std::make_shared<UpdateIsReadRequest>();
  updateIsReadRequest->profileId = p_ProfileId;
  updateIsReadRequest->chatId = p_ChatId;
  updateIsReadRequest->msgId = p_MsgId;
  updateIsReadRequest->isRead = p_IsRead;
  EnqueueRequest(updateIsReadRequest);
}

void MessageCache::UpdateFilePath(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId, const std::string& p_FilePath)
{
  std::shared_ptr<UpdateFilePathRequest> updateFilePathRequest = std::make_shared<UpdateFilePathRequest>();
  updateFilePathRequest->profileId = p_ProfileId;
  updateFilePathRequest->chatId = p_ChatId;
  updateFilePathRequest->msgId = p_MsgId;
  updateFilePathRequest->filePath = p_FilePath;
  EnqueueRequest(updateFilePathRequest);
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
        break;
      }

      request = m_Queue.front();
      m_Queue.pop_front();
    }

    PerformRequest(request);
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
    case AddRequestType:
      {
        std::lock_guard<std::mutex> lock(m_DbMutex);
        std::shared_ptr<AddRequest> addRequest = std::static_pointer_cast<AddRequest>(p_Request);
        const std::string& profileId = addRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = addRequest->chatId;
        const std::string& fromMsgId = addRequest->fromMsgId;
        LOG_DEBUG("cache add %s %s %d", chatId.c_str(), fromMsgId.c_str(), addRequest->chatMessages.size());

        *m_Dbs[profileId] << "BEGIN;";
        for (const auto& msg : addRequest->chatMessages)
        {
          *m_Dbs[profileId] << "INSERT INTO messages "
            "(chatId, id, senderId, text, quotedId, quotedText, quotedSender, filePath, fileType, timeSent, isOutgoing, isRead) VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?,?);" <<
            chatId << msg.id << msg.senderId << msg.text << msg.quotedId << msg.quotedText << msg.quotedSender <<
            msg.filePath << msg.fileType << msg.timeSent <<
            msg.isOutgoing << msg.isRead;
        }
        *m_Dbs[profileId] << "COMMIT;";

        if (addRequest->chatMessages.empty() && !fromMsgId.empty())
        {
          bool isLast = true;
          *m_Dbs[profileId] << "UPDATE messages SET isLast = ? WHERE chatId = ? AND id = ?;" << (int)isLast << chatId <<
            fromMsgId;
          LOG_DEBUG("cache set last %s %s", chatId.c_str(), fromMsgId.c_str());
        }
      }
      break;

    case FetchRequestType:
      {
        std::unique_lock<std::mutex> lock(m_DbMutex);
        std::shared_ptr<FetchRequest> fetchRequest = std::static_pointer_cast<FetchRequest>(p_Request);
        const std::string& profileId = fetchRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = fetchRequest->chatId;
        const std::string& fromMsgId = fetchRequest->fromMsgId;
        const int limit = fetchRequest->limit;

        int64_t fromMsgIdTimeSent = 0;
        *m_Dbs[profileId] << "SELECT timeSent FROM messages WHERE chatId = ? AND id = ?;" << chatId << fromMsgId >>
          [&](const int64_t& timeSent)
            {
            fromMsgIdTimeSent = timeSent;
          };

        std::vector<ChatMessage> chatMessages;
        *m_Dbs[profileId] <<
          "SELECT id, senderId, text, quotedId, quotedText, quotedSender, filePath, fileType, timeSent, isOutgoing, isRead FROM "
          "messages WHERE chatId = ? AND timeSent < ? ORDER BY timeSent DESC LIMIT ?;" << chatId << fromMsgIdTimeSent <<
          limit >>
          [&](const std::string& id, const std::string& senderId, const std::string& text, const std::string& quotedId,
              const std::string& quotedText,
              const std::string& quotedSender, const std::string& filePath, const std::string& fileType,
              int64_t timeSent,
              int32_t isOutgoing, int32_t isRead)
            {
            ChatMessage chatMessage;
            chatMessage.id = id;
            chatMessage.senderId = senderId;
            chatMessage.text = text;
            chatMessage.quotedId = quotedId;
            chatMessage.quotedText = quotedText;
            chatMessage.quotedSender = quotedSender;
            chatMessage.filePath = filePath;
            chatMessage.fileType = fileType;
            chatMessage.timeSent = timeSent;
            chatMessage.isOutgoing = isOutgoing;
            chatMessage.isRead = isRead;

            chatMessages.push_back(chatMessage);
          };

        LOG_DEBUG("cache fetch %s %s %d %d", chatId.c_str(), fromMsgId.c_str(), limit, chatMessages.size());

        lock.unlock();

        std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::make_shared<NewMessagesNotify>(profileId);
        newMessagesNotify->success = true;
        newMessagesNotify->chatId = chatId;
        newMessagesNotify->chatMessages = chatMessages;
        newMessagesNotify->fromMsgId = fromMsgId;
        newMessagesNotify->cached = true;
        CallMessageHandler(newMessagesNotify);
      }
      break;

    case DeleteRequestType:
      {
        std::lock_guard<std::mutex> lock(m_DbMutex);
        std::shared_ptr<DeleteRequest> deleteRequest = std::static_pointer_cast<DeleteRequest>(p_Request);
        const std::string& profileId = deleteRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = deleteRequest->chatId;
        const std::string& msgId = deleteRequest->msgId;

        *m_Dbs[profileId] << "DELETE FROM messages WHERE chatId = ? AND id = ?;" << chatId << msgId;
        LOG_DEBUG("cache delete %s %s", chatId.c_str(), msgId.c_str());
      }
      break;

    case UpdateIsReadRequestType:
      {
        std::lock_guard<std::mutex> lock(m_DbMutex);
        std::shared_ptr<UpdateIsReadRequest> updateIsReadRequest = std::static_pointer_cast<UpdateIsReadRequest>(
          p_Request);
        const std::string& profileId = updateIsReadRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = updateIsReadRequest->chatId;
        const std::string& msgId = updateIsReadRequest->msgId;
        bool isRead = updateIsReadRequest->isRead;

        *m_Dbs[profileId] << "UPDATE messages SET isRead = ? WHERE chatId = ? AND id = ?;" << (int)isRead << chatId <<
          msgId;
        LOG_DEBUG("cache update read %s %s %d", chatId.c_str(), msgId.c_str(), isRead);
      }
      break;

    case UpdateFilePathRequestType:
      {
        std::lock_guard<std::mutex> lock(m_DbMutex);
        std::shared_ptr<UpdateFilePathRequest> updateFilePathRequest = std::static_pointer_cast<UpdateFilePathRequest>(
          p_Request);
        const std::string& profileId = updateFilePathRequest->profileId;
        if (!m_Dbs[profileId]) return;

        const std::string& chatId = updateFilePathRequest->chatId;
        const std::string& msgId = updateFilePathRequest->msgId;
        const std::string& filePath = updateFilePathRequest->filePath;;
        std::string text = "";

        *m_Dbs[profileId] << "UPDATE messages SET filePath = ?, text = ? WHERE chatId = ? AND id = ?;" << filePath <<
          text << chatId << msgId;
        LOG_DEBUG("cache update filePath %s %s %s", chatId.c_str(), msgId.c_str(), filePath.c_str());
      }
      break;

    default:
      {
        LOG_WARNING("cache unknown request type %d", p_Request->GetRequestType());
      }
      break;
  }
}

void MessageCache::CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  if (m_MessageHandler)
  {
    m_MessageHandler(p_ServiceMessage);
  }
}
