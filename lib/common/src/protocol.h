// protocol.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class RequestMessage;
class ServiceMessage;

// Protocol interface
enum ProtocolFeature
{
  NoFeature = 0,
  AutoGetChatsOnLogin = (1 << 0),
  TypingTimeout = (1 << 1),
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
  virtual bool HasFeature(ProtocolFeature p_ProtocolFeature) const = 0;

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
  RequestMessageType,
  GetContactsRequestType,
  GetChatsRequestType,
  GetMessagesRequestType,
  SendMessageRequestType,
  DeferNotifyRequestType,
  DeferGetChatDetailsRequestType,
  DeferGetUserDetailsRequestType,
  DeferDownloadFileRequestType,
  MarkMessageReadRequestType,
  DeleteMessageRequestType,
  SendTypingRequestType,
  SetStatusRequestType,
  CreateChatRequestType,
  SetCurrentChatRequestType,
  DeferGetSponsoredMessagesRequestType,

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
};

struct ContactInfo
{
  std::string id;
  std::string name;
  bool isSelf = false;
};

struct ChatInfo
{
  std::string id;
  bool isUnread = false;
  bool isUnreadMention = false; // tgchat only
  bool isMuted = false;
  int64_t lastMessageTime = -1;
};

struct ChatMessage
{
  std::string id;
  std::string senderId;
  std::string text;
  std::string quotedId;
  std::string quotedText;
  std::string quotedSender;
  std::string filePath;
  std::string fileType; // wachat only
  std::string link; // tgchat sponsored msg only, not db cached
  int64_t timeSent = -1;
  bool isOutgoing = true;
  bool isRead = false;
  bool hasMention = false; // tgchat only, not db cached
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
};

class GetMessagesRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return GetMessagesRequestType; }
  std::string chatId;
  std::string fromMsgId;
  int32_t limit;
  bool fromIsOutgoing; // is fromMsgId an outgoing message (needed for wachat only)
};

class SendMessageRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return SendMessageRequestType; }
  std::string chatId;
  ChatMessage chatMessage;
};

class MarkMessageReadRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return MarkMessageReadRequestType; }
  std::string chatId;
  std::string msgId;
};

class DeleteMessageRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DeleteMessageRequestType; }
  std::string chatId;
  std::string msgId;
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

class DeferDownloadFileRequest : public RequestMessage
{
public:
  virtual MessageType GetMessageType() const { return DeferDownloadFileRequestType; }
  std::string chatId;
  std::string msgId;
  std::string fileId;
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

// Service messages
class ServiceMessage
{
public:
  explicit ServiceMessage(const std::string& p_ProfileId) :
    profileId(p_ProfileId) { }
  virtual ~ServiceMessage() { }
  virtual MessageType GetMessageType() const { return ServiceMessageType; }
  std::string profileId;
};

class NewContactsNotify : public ServiceMessage
{
public:
  explicit NewContactsNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewContactsNotifyType; }
  std::vector<ContactInfo> contactInfos;
};

class NewChatsNotify : public ServiceMessage
{
public:
  explicit NewChatsNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewChatsNotifyType; }
  bool success;
  std::vector<ChatInfo> chatInfos;
};

class NewMessagesNotify : public ServiceMessage
{
public:
  explicit NewMessagesNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewMessagesNotifyType; }
  bool success;
  std::string chatId;
  std::vector<ChatMessage> chatMessages;
  std::string fromMsgId;
  bool cached = false;
};

class SendMessageNotify : public ServiceMessage
{
public:
  explicit SendMessageNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return SendMessageNotifyType; }
  bool success;
  std::string chatId;
  ChatMessage chatMessage;
};

class ConnectNotify : public ServiceMessage
{
public:
  explicit ConnectNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return ConnectNotifyType; }
  bool success;
};

class MarkMessageReadNotify : public ServiceMessage
{
public:
  explicit MarkMessageReadNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return MarkMessageReadNotifyType; }
  bool success;
  std::string chatId;
  std::string msgId;
};

class DeleteMessageNotify : public ServiceMessage
{
public:
  explicit DeleteMessageNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return DeleteMessageNotifyType; }
  bool success;
  std::string chatId;
  std::string msgId;
};

class SendTypingNotify : public ServiceMessage
{
public:
  explicit SendTypingNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return SendTypingNotifyType; }
  bool success;
  std::string chatId;
  bool isTyping;
};

class SetStatusNotify : public ServiceMessage
{
public:
  explicit SetStatusNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return SetStatusNotifyType; }
  bool success;
  bool isOnline;
};

class CreateChatNotify : public ServiceMessage
{
public:
  explicit CreateChatNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return CreateChatNotifyType; }
  bool success;
  ChatInfo chatInfo;
};

class ReceiveTypingNotify : public ServiceMessage
{
public:
  explicit ReceiveTypingNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return ReceiveTypingNotifyType; }
  std::string chatId;
  std::string userId;
  bool isTyping;
};

class ReceiveStatusNotify : public ServiceMessage
{
public:
  explicit ReceiveStatusNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return ReceiveStatusNotifyType; }
  std::string userId;
  bool isOnline;
};

class NewMessageStatusNotify : public ServiceMessage
{
public:
  explicit NewMessageStatusNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewMessageStatusNotifyType; }
  std::string chatId;
  std::string msgId;
  bool isRead = false;
};

class NewMessageFileNotify : public ServiceMessage
{
public:
  explicit NewMessageFileNotify(const std::string& p_ProfileId) :
    ServiceMessage(p_ProfileId) { }
  virtual MessageType GetMessageType() const { return NewMessageFileNotifyType; }
  std::string chatId;
  std::string msgId;
  std::string filePath;
};
