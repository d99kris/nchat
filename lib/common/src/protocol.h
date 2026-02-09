// protocol.h
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

class RequestMessage;
class ServiceMessage;

// Protocol interface
enum ProtocolFeature
{
  FeatureNone = 0,
  FeatureAutoGetChatsOnLogin = (1 << 0),
  FeatureTypingTimeout = (1 << 1),
  FeatureEditMessagesWithinTwoDays = (1 << 2),
  FeatureEditMessagesWithinFifteenMins = (1 << 3),
  FeatureLimitedReactions = (1 << 4),
  FeatureMarkReadEveryView = (1 << 5),
  FeatureAutoGetContactsOnLogin = (1 << 6),
};

class Protocol
{
public:
  Protocol()
  {
  }

  virtual ~Protocol()
  {
  }

  virtual std::string GetProfileId() const = 0;
  virtual std::string GetProfileDisplayName() const = 0;
  virtual bool HasFeature(ProtocolFeature p_ProtocolFeature) const = 0;
  virtual std::string GetSelfId() const = 0;

  virtual bool SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId) = 0;
  virtual bool LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId) = 0;
  virtual bool CloseProfile() = 0;

  virtual bool Login() = 0;
  virtual bool Logout() = 0;

  virtual void SendRequest(std::shared_ptr<RequestMessage> p_Request) = 0;
  virtual void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler) = 0;
};

// Request and notify message types
enum MessageType
{
  UnknownType = 0,
  // Request messages
  RequestMessageType,
  GetContactsRequestType,
  GetChatsRequestType,
  GetStatusRequestType,
  GetMessageRequestType,
  GetMessagesRequestType,
  SendMessageRequestType,
  EditMessageRequestType,
  DeferNotifyRequestType,
  DeferGetChatDetailsRequestType,
  DeferGetUserDetailsRequestType,
  DownloadFileRequestType,
  DeferDownloadFileRequestType,
  MarkMessageReadRequestType,
  DeleteMessageRequestType,
  DeleteChatRequestType,
  SendTypingRequestType,
  SetStatusRequestType,
  CreateChatRequestType,
  SetCurrentChatRequestType,
  DeferGetSponsoredMessagesRequestType,
  GetAvailableReactionsRequestType,
  SendReactionRequestType,
  GetUnreadReactionsRequestType,
  ReinitRequestType,
  FindMessageRequestType,
  // Service messages
  ServiceMessageType,
  NewContactsNotifyType,
  NewChatsNotifyType,
  NewMessagesNotifyType,
  SendMessageNotifyType,
  ConnectNotifyType,
  MarkMessageReadNotifyType,
  DeleteMessageNotifyType,
  SendTypingNotifyType,
  SetStatusNotifyType,
  CreateChatNotifyType,
  ReceiveTypingNotifyType,
  ReceiveStatusNotifyType,
  NewMessageStatusNotifyType,
  NewMessageFileNotifyType,
  DeleteChatNotifyType,
  UpdateMuteNotifyType,
  ProtocolUiControlNotifyType,
  RequestAppExitNotifyType,
  NewMessageReactionsNotifyType,
  AvailableReactionsNotifyType,
  FindMessageNotifyType,
  UpdatePinNotifyType,
};

struct ContactInfo
{
  std::string id;
  std::string name;
  std::string phone;
  bool isSelf = false;
  bool isAlias = false; // only used by wmchat
};

struct ChatInfo
{
  std::string id;
  bool isUnread = false;
  bool isUnreadMention = false; // only required for tgchat
  bool isMuted = false;
  bool isPinned = false;
  int64_t lastMessageTime = -1;
  std::string transcriptionLanguage; // language for audio transcription (e.g., "en", "ru", "auto", or empty for global default)
};

enum FileStatus
{
  FileStatusNone = -1,
  FileStatusNotDownloaded = 0,
  FileStatusDownloaded = 1,
  FileStatusDownloading = 2,
  FileStatusDownloadFailed = 3,
};

struct FileInfo
{
  FileStatus fileStatus = FileStatusNone;
  std::string fileId;
  std::string filePath;
  std::string fileType;
};

// ensure CacheUtil and Serialization are up-to-date after modifying Reactions
static const std::string s_ReactionsSelfId = "You";
struct Reactions
{
  bool needConsolidationWithCache = false; // true = need consolidation with cache before usage
  bool updateCountBasedOnSender = false; // true = need to update emojiCount based on senderEmoji
  bool replaceCount = false; // true = replace emoji counts
  std::map<std::string, std::string> senderEmojis;
  std::map<std::string, size_t> emojiCounts;

  bool operator==(const Reactions& p_Other) const
  {
    if (senderEmojis != p_Other.senderEmojis) return false;

    if (emojiCounts != p_Other.emojiCounts) return false;

    return true;
  }

  bool operator!=(const Reactions& p_Other) const
  {
    return (*this == p_Other);
  }
};

struct ChatMessage
{
  std::string id;
  std::string senderId;
  std::string text;
  std::string quotedId;
  std::string quotedText;
  std::string quotedSender;
  std::string fileInfo;
  std::string link; // only required for tgchat, sponsored msg, not db cached
  Reactions reactions;
  int64_t timeSent = -1;
  bool isOutgoing = true;
  bool isRead = false;
  bool hasMention = false; // only required for tgchat, not db cached
};

enum DownloadFileAction
{
  DownloadFileActionNone = 0,
  DownloadFileActionOpen = 1,
  DownloadFileActionSave = 2,
};

// Request messages
class RequestMessage
{
public:
  virtual ~RequestMessage() { }
  virtual MessageType GetMessageType() const { return RequestMessageType; }
};

class GetContactsRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return GetContactsRequestType; }
};

class GetChatsRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return GetChatsRequestType; }
  std::unordered_set<std::string> chatIds; // optionally fetch only specified chats
};

class GetStatusRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return GetStatusRequestType; }
  std::string userId;
};

class GetMessageRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return GetMessageRequestType; }
  std::string chatId;
  std::string msgId;
  bool cached = true; // try cache before fetch
};

class GetMessagesRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return GetMessagesRequestType; }
  std::string chatId;
  std::string fromMsgId;
  int32_t limit;
};

class SendMessageRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return SendMessageRequestType; }
  std::string chatId;
  ChatMessage chatMessage;
};

class EditMessageRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return EditMessageRequestType; }
  std::string chatId;
  std::string msgId;
  ChatMessage chatMessage;
};

class MarkMessageReadRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return MarkMessageReadRequestType; }
  std::string chatId;
  std::string senderId; // only required for wmchat
  std::string msgId;
  bool readAllReactions = false;
};

class DeleteMessageRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DeleteMessageRequestType; }
  std::string chatId;
  std::string senderId; // only required for wmchat
  std::string msgId;
};

class DeleteChatRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DeleteChatRequestType; }
  std::string chatId;
};

class SendTypingRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return SendTypingRequestType; }
  std::string chatId;
  bool isTyping = false;
};

class SetStatusRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return SetStatusRequestType; }
  bool isOnline = false;
};

class CreateChatRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return CreateChatRequestType; }
  std::string userId;
};

class DeferNotifyRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DeferNotifyRequestType; }
  std::shared_ptr<ServiceMessage> serviceMessage;
};

class DeferGetChatDetailsRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DeferGetChatDetailsRequestType; }
  std::vector<std::string> chatIds;
  bool isGetTypeOnly = false;
};

class DeferGetUserDetailsRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DeferGetUserDetailsRequestType; }
  std::vector<std::string> userIds;
};

class DownloadFileRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DownloadFileRequestType; }
  std::string chatId;
  std::string msgId;
  std::string fileId;
  DownloadFileAction downloadFileAction = DownloadFileActionNone;
};

class DeferDownloadFileRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DeferDownloadFileRequestType; }
  std::string chatId;
  std::string msgId;
  std::string fileId;
  std::string downloadId;
  DownloadFileAction downloadFileAction = DownloadFileActionNone;
};

class SetCurrentChatRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return SetCurrentChatRequestType; }
  std::string chatId;
};

class DeferGetSponsoredMessagesRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DeferGetSponsoredMessagesRequestType; }
  std::string chatId;
};

class GetAvailableReactionsRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return GetAvailableReactionsRequestType; }
  std::string chatId;
  std::string msgId;
};

class SendReactionRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return SendReactionRequestType; }
  std::string chatId;
  std::string senderId; // only required for wmchat
  std::string msgId;
  std::string emoji;
  std::string prevEmoji; // only required for tgchat, to clear reaction
};

class GetUnreadReactionsRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return GetUnreadReactionsRequestType; }
  std::string chatId;
};

class ReinitRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return ReinitRequestType; }
};

class FindMessageRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return FindMessageRequestType; }
  std::string chatId;
  std::string fromMsgId;
  std::string lastMsgId;
  std::string findText;
  std::string findMsgId;
};

// Service messages
class ServiceMessage
{
public:
  explicit ServiceMessage(const std::string& p_ProfileId)
    : profileId(p_ProfileId) { }
  virtual ~ServiceMessage() { }
  virtual MessageType GetMessageType() const { return ServiceMessageType; }
  std::string profileId;
};

class NewContactsNotify : public ServiceMessage
{
public:
  explicit NewContactsNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewContactsNotifyType; }
  bool fullSync = false;
  std::vector<ContactInfo> contactInfos;
};

class NewChatsNotify : public ServiceMessage
{
public:
  explicit NewChatsNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewChatsNotifyType; }
  bool success;
  std::vector<ChatInfo> chatInfos;
};

class NewMessagesNotify : public ServiceMessage
{
public:
  explicit NewMessagesNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewMessagesNotifyType; }
  bool success;
  std::string chatId;
  std::vector<ChatMessage> chatMessages;
  std::string fromMsgId;
  bool cached = false;
  bool sequence = false;
};

class SendMessageNotify : public ServiceMessage
{
public:
  explicit SendMessageNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return SendMessageNotifyType; }
  bool success;
  std::string chatId;
  ChatMessage chatMessage;
};

class ConnectNotify : public ServiceMessage
{
public:
  explicit ConnectNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return ConnectNotifyType; }
  bool success;
};

class MarkMessageReadNotify : public ServiceMessage
{
public:
  explicit MarkMessageReadNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return MarkMessageReadNotifyType; }
  bool success;
  std::string chatId;
  std::string msgId;
};

class DeleteMessageNotify : public ServiceMessage
{
public:
  explicit DeleteMessageNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return DeleteMessageNotifyType; }
  bool success;
  std::string chatId;
  std::string msgId;
};

class SendTypingNotify : public ServiceMessage
{
public:
  explicit SendTypingNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return SendTypingNotifyType; }
  bool success;
  std::string chatId;
  bool isTyping;
};

class SetStatusNotify : public ServiceMessage
{
public:
  explicit SetStatusNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return SetStatusNotifyType; }
  bool success;
  bool isOnline;
};

class CreateChatNotify : public ServiceMessage
{
public:
  explicit CreateChatNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return CreateChatNotifyType; }
  bool success;
  ChatInfo chatInfo;
};

class ReceiveTypingNotify : public ServiceMessage
{
public:
  explicit ReceiveTypingNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return ReceiveTypingNotifyType; }
  std::string chatId;
  std::string userId;
  bool isTyping;
};

enum TimeSeen
{
  TimeSeenNone = -1, // away, offline, seen recently
  TimeSeenReserved = 0, // not used
  TimeSeenLastMonth = 1, // seen last month
  TimeSeenLastWeek = 2, // seen last week
};

class ReceiveStatusNotify : public ServiceMessage
{
public:
  explicit ReceiveStatusNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return ReceiveStatusNotifyType; }
  std::string userId;
  bool isOnline;
  int64_t timeSeen = -1;
};

class NewMessageStatusNotify : public ServiceMessage
{
public:
  explicit NewMessageStatusNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewMessageStatusNotifyType; }
  std::string chatId;
  std::string msgId;
  bool isRead = false;
};

class NewMessageFileNotify : public ServiceMessage
{
public:
  explicit NewMessageFileNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewMessageFileNotifyType; }
  std::string chatId;
  std::string msgId;
  std::string fileInfo;
  DownloadFileAction downloadFileAction;
};

class DeleteChatNotify : public ServiceMessage
{
public:
  explicit DeleteChatNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return DeleteChatNotifyType; }
  bool success;
  std::string chatId;
};

class UpdateMuteNotify : public ServiceMessage
{
public:
  explicit UpdateMuteNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return UpdateMuteNotifyType; }
  bool success;
  std::string chatId;
  bool isMuted;
};

class ProtocolUiControlNotify : public ServiceMessage
{
public:
  explicit ProtocolUiControlNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return ProtocolUiControlNotifyType; }
  bool isTakeControl;
};

class RequestAppExitNotify : public ServiceMessage
{
public:
  explicit RequestAppExitNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return RequestAppExitNotifyType; }
};

class NewMessageReactionsNotify : public ServiceMessage
{
public:
  explicit NewMessageReactionsNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewMessageReactionsNotifyType; }
  std::string chatId;
  std::string msgId;
  Reactions reactions;
};

class AvailableReactionsNotify : public ServiceMessage
{
public:
  explicit AvailableReactionsNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return AvailableReactionsNotifyType; }
  std::string chatId;
  std::string msgId;
  std::set<std::string> emojis;
};

class FindMessageNotify : public ServiceMessage
{
public:
  explicit FindMessageNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return FindMessageNotifyType; }
  bool success;
  std::string chatId;
  std::string msgId;
};

class UpdatePinNotify : public ServiceMessage
{
public:
  explicit UpdatePinNotify(const std::string& p_ProfileId)
    : ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return UpdatePinNotifyType; }
  bool success;
  std::string chatId;
  bool isPinned;
  int64_t timePinned = -1;
};
