//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogParticipant.h"

#include "td/telegram/Global.h"
#include "td/telegram/misc.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <limits>

namespace td {

DialogParticipantStatus::DialogParticipantStatus(Type type, uint32 flags, int32 until_date, string rank)
    : type_(type), flags_(flags), until_date_(until_date), rank_(strip_empty_characters(std::move(rank), 16)) {
}

int32 DialogParticipantStatus::fix_until_date(int32 date) {
  if (date == std::numeric_limits<int32>::max() || date < 0) {
    return 0;
  }
  return date;
}

DialogParticipantStatus DialogParticipantStatus::Creator(bool is_member, string rank) {
  return DialogParticipantStatus(Type::Creator,
                                 ALL_ADMINISTRATOR_RIGHTS | ALL_PERMISSION_RIGHTS | (is_member ? IS_MEMBER : 0), 0,
                                 std::move(rank));
}

DialogParticipantStatus DialogParticipantStatus::Administrator(string rank, bool can_be_edited, bool can_change_info,
                                                               bool can_post_messages, bool can_edit_messages,
                                                               bool can_delete_messages, bool can_invite_users,
                                                               bool can_restrict_members, bool can_pin_messages,
                                                               bool can_promote_members) {
  uint32 flags = (static_cast<uint32>(can_be_edited) * CAN_BE_EDITED) |
                 (static_cast<uint32>(can_change_info) * CAN_CHANGE_INFO_AND_SETTINGS_ADMIN) |
                 (static_cast<uint32>(can_post_messages) * CAN_POST_MESSAGES) |
                 (static_cast<uint32>(can_edit_messages) * CAN_EDIT_MESSAGES) |
                 (static_cast<uint32>(can_delete_messages) * CAN_DELETE_MESSAGES) |
                 (static_cast<uint32>(can_invite_users) * CAN_INVITE_USERS_ADMIN) |
                 (static_cast<uint32>(can_restrict_members) * CAN_RESTRICT_MEMBERS) |
                 (static_cast<uint32>(can_pin_messages) * CAN_PIN_MESSAGES_ADMIN) |
                 (static_cast<uint32>(can_promote_members) * CAN_PROMOTE_MEMBERS);
  if (flags == 0 || flags == CAN_BE_EDITED) {
    return Member();
  }
  return DialogParticipantStatus(Type::Administrator, IS_MEMBER | ALL_RESTRICTED_RIGHTS | flags, 0, std::move(rank));
}

DialogParticipantStatus DialogParticipantStatus::Member() {
  return DialogParticipantStatus(Type::Member, IS_MEMBER | ALL_PERMISSION_RIGHTS, 0, string());
}

DialogParticipantStatus DialogParticipantStatus::Restricted(
    bool is_member, int32 restricted_until_date, bool can_send_messages, bool can_send_media, bool can_send_stickers,
    bool can_send_animations, bool can_send_games, bool can_use_inline_bots, bool can_add_web_page_previews,
    bool can_send_polls, bool can_change_info_and_settings, bool can_invite_users, bool can_pin_messages) {
  uint32 flags = (static_cast<uint32>(can_send_messages) * CAN_SEND_MESSAGES) |
                 (static_cast<uint32>(can_send_media) * CAN_SEND_MEDIA) |
                 (static_cast<uint32>(can_send_stickers) * CAN_SEND_STICKERS) |
                 (static_cast<uint32>(can_send_animations) * CAN_SEND_ANIMATIONS) |
                 (static_cast<uint32>(can_send_games) * CAN_SEND_GAMES) |
                 (static_cast<uint32>(can_use_inline_bots) * CAN_USE_INLINE_BOTS) |
                 (static_cast<uint32>(can_add_web_page_previews) * CAN_ADD_WEB_PAGE_PREVIEWS) |
                 (static_cast<uint32>(can_send_polls) * CAN_SEND_POLLS) |
                 (static_cast<uint32>(can_change_info_and_settings) * CAN_CHANGE_INFO_AND_SETTINGS_BANNED) |
                 (static_cast<uint32>(can_invite_users) * CAN_INVITE_USERS_BANNED) |
                 (static_cast<uint32>(can_pin_messages) * CAN_PIN_MESSAGES_BANNED) |
                 (static_cast<uint32>(is_member) * IS_MEMBER);
  if (flags == (IS_MEMBER | ALL_PERMISSION_RIGHTS)) {
    return Member();
  }
  return DialogParticipantStatus(Type::Restricted, flags, fix_until_date(restricted_until_date), string());
}

DialogParticipantStatus DialogParticipantStatus::Left() {
  return DialogParticipantStatus(Type::Left, ALL_PERMISSION_RIGHTS, 0, string());
}

DialogParticipantStatus DialogParticipantStatus::Banned(int32 banned_until_date) {
  return DialogParticipantStatus(Type::Banned, 0, fix_until_date(banned_until_date), string());
}

DialogParticipantStatus DialogParticipantStatus::GroupAdministrator(bool is_creator) {
  return Administrator(string(), is_creator, true, false, false, true, true, true, true, false);
}

DialogParticipantStatus DialogParticipantStatus::ChannelAdministrator(bool is_creator, bool is_megagroup) {
  if (is_megagroup) {
    return Administrator(string(), is_creator, true, false, false, true, true, true, true, false);
  } else {
    return Administrator(string(), is_creator, false, true, true, true, false, true, false, false);
  }
}

RestrictedRights DialogParticipantStatus::get_restricted_rights() const {
  return RestrictedRights(can_send_messages(), can_send_media(), can_send_stickers(), can_send_animations(),
                          can_send_games(), can_use_inline_bots(), can_add_web_page_previews(), can_send_polls(),
                          can_change_info_and_settings(), can_invite_users(), can_pin_messages());
}

tl_object_ptr<td_api::ChatMemberStatus> DialogParticipantStatus::get_chat_member_status_object() const {
  switch (type_) {
    case Type::Creator:
      return td_api::make_object<td_api::chatMemberStatusCreator>(rank_, is_member());
    case Type::Administrator:
      return td_api::make_object<td_api::chatMemberStatusAdministrator>(
          rank_, can_be_edited(), can_change_info_and_settings(), can_post_messages(), can_edit_messages(),
          can_delete_messages(), can_invite_users(), can_restrict_members(), can_pin_messages(), can_promote_members());
    case Type::Member:
      return td_api::make_object<td_api::chatMemberStatusMember>();
    case Type::Restricted:
      return td_api::make_object<td_api::chatMemberStatusRestricted>(
          is_member(), until_date_, get_restricted_rights().get_chat_permissions_object());
    case Type::Left:
      return td_api::make_object<td_api::chatMemberStatusLeft>();
    case Type::Banned:
      return td_api::make_object<td_api::chatMemberStatusBanned>(until_date_);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<telegram_api::chatAdminRights> DialogParticipantStatus::get_chat_admin_rights() const {
  int32 flags = 0;
  if ((flags_ & CAN_CHANGE_INFO_AND_SETTINGS_ADMIN) != 0) {
    flags |= telegram_api::chatAdminRights::CHANGE_INFO_MASK;
  }
  if (can_post_messages()) {
    flags |= telegram_api::chatAdminRights::POST_MESSAGES_MASK;
  }
  if (can_edit_messages()) {
    flags |= telegram_api::chatAdminRights::EDIT_MESSAGES_MASK;
  }
  if (can_delete_messages()) {
    flags |= telegram_api::chatAdminRights::DELETE_MESSAGES_MASK;
  }
  if ((flags_ & CAN_INVITE_USERS_ADMIN) != 0) {
    flags |= telegram_api::chatAdminRights::INVITE_USERS_MASK;
  }
  if (can_restrict_members()) {
    flags |= telegram_api::chatAdminRights::BAN_USERS_MASK;
  }
  if ((flags_ & CAN_PIN_MESSAGES_ADMIN) != 0) {
    flags |= telegram_api::chatAdminRights::PIN_MESSAGES_MASK;
  }
  if (can_promote_members()) {
    flags |= telegram_api::chatAdminRights::ADD_ADMINS_MASK;
  }

  LOG(INFO) << "Create chat admin rights " << flags;
  return make_tl_object<telegram_api::chatAdminRights>(flags, false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                                       false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                                       false /*ignored*/, false /*ignored*/);
}

tl_object_ptr<telegram_api::chatBannedRights> DialogParticipantStatus::get_chat_banned_rights() const {
  int32 flags = 0;
  if (type_ == Type::Banned) {
    flags |= telegram_api::chatBannedRights::VIEW_MESSAGES_MASK;
  }
  if (!can_send_messages()) {
    flags |= telegram_api::chatBannedRights::SEND_MESSAGES_MASK;
  }
  if (!can_send_media()) {
    flags |= telegram_api::chatBannedRights::SEND_MEDIA_MASK;
  }
  if (!can_send_stickers()) {
    flags |= telegram_api::chatBannedRights::SEND_STICKERS_MASK;
  }
  if (!can_send_animations()) {
    flags |= telegram_api::chatBannedRights::SEND_GIFS_MASK;
  }
  if (!can_send_games()) {
    flags |= telegram_api::chatBannedRights::SEND_GAMES_MASK;
  }
  if (!can_use_inline_bots()) {
    flags |= telegram_api::chatBannedRights::SEND_INLINE_MASK;
  }
  if (!can_add_web_page_previews()) {
    flags |= telegram_api::chatBannedRights::EMBED_LINKS_MASK;
  }
  if (!can_send_polls()) {
    flags |= telegram_api::chatBannedRights::SEND_POLLS_MASK;
  }
  if ((flags_ & CAN_CHANGE_INFO_AND_SETTINGS_BANNED) == 0) {
    flags |= telegram_api::chatBannedRights::CHANGE_INFO_MASK;
  }
  if ((flags_ & CAN_INVITE_USERS_BANNED) == 0) {
    flags |= telegram_api::chatBannedRights::INVITE_USERS_MASK;
  }
  if ((flags_ & CAN_PIN_MESSAGES_BANNED) == 0) {
    flags |= telegram_api::chatBannedRights::PIN_MESSAGES_MASK;
  }

  LOG(INFO) << "Create chat banned rights " << flags << " until " << until_date_;
  return make_tl_object<telegram_api::chatBannedRights>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, until_date_);
}

DialogParticipantStatus DialogParticipantStatus::apply_restrictions(RestrictedRights default_restrictions,
                                                                    bool is_bot) const {
  auto flags = flags_;
  switch (type_) {
    case Type::Creator:
      // creator can do anything and isn't affected by restrictions
      break;
    case Type::Administrator:
      // administrators aren't affected by restrictions, but if everyone can invite users,
      // pin messages or change info, they also can do that
      if (!is_bot) {
        flags |= default_restrictions.flags_ & ALL_ADMIN_PERMISSION_RIGHTS;
      }
      break;
    case Type::Member:
    case Type::Restricted:
    case Type::Left:
      // members and restricted are affected by default restrictions
      flags &= ~ALL_PERMISSION_RIGHTS | default_restrictions.flags_;
      if (is_bot) {
        flags &= ~ALL_ADMIN_PERMISSION_RIGHTS;
      }
      break;
    case Type::Banned:
      // banned can do nothing, even restrictions allows them to do that
      break;
    default:
      UNREACHABLE();
      break;
  }

  return DialogParticipantStatus(type_, flags, 0, string());
}

void DialogParticipantStatus::update_restrictions() const {
  if (until_date_ != 0 && G()->unix_time() > until_date_) {
    until_date_ = 0;
    if (type_ == Type::Restricted) {
      if (is_member()) {
        type_ = Type::Member;
      } else {
        type_ = Type::Left;
      }
      flags_ |= ALL_PERMISSION_RIGHTS;
    } else if (type_ == Type::Banned) {
      type_ = Type::Left;
    } else {
      UNREACHABLE();
    }
  }
}

bool operator==(const DialogParticipantStatus &lhs, const DialogParticipantStatus &rhs) {
  return lhs.type_ == rhs.type_ && lhs.flags_ == rhs.flags_ && lhs.until_date_ == rhs.until_date_ &&
         lhs.rank_ == rhs.rank_;
}

bool operator!=(const DialogParticipantStatus &lhs, const DialogParticipantStatus &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipantStatus &status) {
  switch (status.type_) {
    case DialogParticipantStatus::Type::Creator:
      string_builder << "Creator";
      if (!status.is_member()) {
        string_builder << "-non-member";
      }
      if (!status.rank_.empty()) {
        string_builder << " [" << status.rank_ << "]";
      }
      return string_builder;
    case DialogParticipantStatus::Type::Administrator:
      string_builder << "Administrator: ";
      if (status.can_change_info_and_settings()) {
        string_builder << "(change)";
      }
      if (status.can_post_messages()) {
        string_builder << "(post)";
      }
      if (status.can_edit_messages()) {
        string_builder << "(edit)";
      }
      if (status.can_delete_messages()) {
        string_builder << "(delete)";
      }
      if (status.can_invite_users()) {
        string_builder << "(invite)";
      }
      if (status.can_restrict_members()) {
        string_builder << "(restrict)";
      }
      if (status.can_pin_messages()) {
        string_builder << "(pin)";
      }
      if (status.can_promote_members()) {
        string_builder << "(promote)";
      }
      if (!status.rank_.empty()) {
        string_builder << " [" << status.rank_ << "]";
      }
      return string_builder;
    case DialogParticipantStatus::Type::Member:
      return string_builder << "Member";
    case DialogParticipantStatus::Type::Restricted:
      string_builder << "Restricted ";
      if (status.until_date_ == 0) {
        string_builder << "forever ";
      } else {
        string_builder << "until " << status.until_date_ << " ";
      }
      if (!status.is_member()) {
        string_builder << "non-";
      }
      string_builder << "member: ";
      if (!status.can_send_messages()) {
        string_builder << "(text)";
      }
      if (!status.can_send_media()) {
        string_builder << "(media)";
      }
      if (!status.can_send_stickers()) {
        string_builder << "(stickers)";
      }
      if (!status.can_send_animations()) {
        string_builder << "(animations)";
      }
      if (!status.can_send_games()) {
        string_builder << "(games)";
      }
      if (!status.can_send_polls()) {
        string_builder << "(polls)";
      }
      if (!status.can_use_inline_bots()) {
        string_builder << "(inline bots)";
      }
      if (!status.can_add_web_page_previews()) {
        string_builder << "(links)";
      }
      if (!status.can_change_info_and_settings()) {
        string_builder << "(change)";
      }
      if (!status.can_invite_users()) {
        string_builder << "(invite)";
      }
      if (!status.can_pin_messages()) {
        string_builder << "(pin)";
      }
      return string_builder;
    case DialogParticipantStatus::Type::Left:
      return string_builder << "Left";
    case DialogParticipantStatus::Type::Banned:
      string_builder << "Banned ";
      if (status.until_date_ == 0) {
        string_builder << "forever";
      } else {
        string_builder << "until " << status.until_date_;
      }
      return string_builder;
    default:
      UNREACHABLE();
      return string_builder << "Impossible";
  }
}

DialogParticipantStatus get_dialog_participant_status(const tl_object_ptr<td_api::ChatMemberStatus> &status) {
  auto constructor_id = status == nullptr ? td_api::chatMemberStatusMember::ID : status->get_id();
  switch (constructor_id) {
    case td_api::chatMemberStatusCreator::ID: {
      auto st = static_cast<const td_api::chatMemberStatusCreator *>(status.get());
      return DialogParticipantStatus::Creator(st->is_member_, st->custom_title_);
    }
    case td_api::chatMemberStatusAdministrator::ID: {
      auto st = static_cast<const td_api::chatMemberStatusAdministrator *>(status.get());
      return DialogParticipantStatus::Administrator(
          st->custom_title_, st->can_be_edited_, st->can_change_info_, st->can_post_messages_, st->can_edit_messages_,
          st->can_delete_messages_, st->can_invite_users_, st->can_restrict_members_, st->can_pin_messages_,
          st->can_promote_members_);
    }
    case td_api::chatMemberStatusMember::ID:
      return DialogParticipantStatus::Member();
    case td_api::chatMemberStatusRestricted::ID: {
      auto st = static_cast<const td_api::chatMemberStatusRestricted *>(status.get());
      auto permissions = st->permissions_.get();
      bool can_send_polls = permissions->can_send_polls_;
      bool can_send_media = permissions->can_send_media_messages_;
      bool can_send_messages = permissions->can_send_messages_ || can_send_media || can_send_polls ||
                               permissions->can_send_other_messages_ || permissions->can_add_web_page_previews_;
      return DialogParticipantStatus::Restricted(
          st->is_member_, st->restricted_until_date_, can_send_messages, can_send_media,
          permissions->can_send_other_messages_, permissions->can_send_other_messages_,
          permissions->can_send_other_messages_, permissions->can_send_other_messages_,
          permissions->can_add_web_page_previews_, permissions->can_send_polls_, permissions->can_change_info_,
          permissions->can_invite_users_, permissions->can_pin_messages_);
    }
    case td_api::chatMemberStatusLeft::ID:
      return DialogParticipantStatus::Left();
    case td_api::chatMemberStatusBanned::ID: {
      auto st = static_cast<const td_api::chatMemberStatusBanned *>(status.get());
      return DialogParticipantStatus::Banned(st->banned_until_date_);
    }
    default:
      UNREACHABLE();
      return DialogParticipantStatus::Member();
  }
}

DialogParticipantStatus get_dialog_participant_status(bool can_be_edited,
                                                      const tl_object_ptr<telegram_api::chatAdminRights> &admin_rights,
                                                      string rank) {
  bool can_change_info = (admin_rights->flags_ & telegram_api::chatAdminRights::CHANGE_INFO_MASK) != 0;
  bool can_post_messages = (admin_rights->flags_ & telegram_api::chatAdminRights::POST_MESSAGES_MASK) != 0;
  bool can_edit_messages = (admin_rights->flags_ & telegram_api::chatAdminRights::EDIT_MESSAGES_MASK) != 0;
  bool can_delete_messages = (admin_rights->flags_ & telegram_api::chatAdminRights::DELETE_MESSAGES_MASK) != 0;
  bool can_invite_users = (admin_rights->flags_ & telegram_api::chatAdminRights::INVITE_USERS_MASK) != 0;
  bool can_restrict_members = (admin_rights->flags_ & telegram_api::chatAdminRights::BAN_USERS_MASK) != 0;
  bool can_pin_messages = (admin_rights->flags_ & telegram_api::chatAdminRights::PIN_MESSAGES_MASK) != 0;
  bool can_promote_members = (admin_rights->flags_ & telegram_api::chatAdminRights::ADD_ADMINS_MASK) != 0;
  return DialogParticipantStatus::Administrator(std::move(rank), can_be_edited, can_change_info, can_post_messages,
                                                can_edit_messages, can_delete_messages, can_invite_users,
                                                can_restrict_members, can_pin_messages, can_promote_members);
}

DialogParticipantStatus get_dialog_participant_status(
    bool is_member, const tl_object_ptr<telegram_api::chatBannedRights> &banned_rights) {
  bool can_view_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::VIEW_MESSAGES_MASK) == 0;
  if (!can_view_messages) {
    return DialogParticipantStatus::Banned(banned_rights->until_date_);
  }
  bool can_send_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_MESSAGES_MASK) == 0;
  bool can_send_media_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_MEDIA_MASK) == 0;
  bool can_send_stickers = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_STICKERS_MASK) == 0;
  bool can_send_animations = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_GIFS_MASK) == 0;
  bool can_send_games = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_GAMES_MASK) == 0;
  bool can_use_inline_bots = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_INLINE_MASK) == 0;
  bool can_add_web_page_previews = (banned_rights->flags_ & telegram_api::chatBannedRights::EMBED_LINKS_MASK) == 0;
  bool can_send_polls = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_POLLS_MASK) == 0;
  bool can_change_info_and_settings = (banned_rights->flags_ & telegram_api::chatBannedRights::CHANGE_INFO_MASK) == 0;
  bool can_invite_users = (banned_rights->flags_ & telegram_api::chatBannedRights::INVITE_USERS_MASK) == 0;
  bool can_pin_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::PIN_MESSAGES_MASK) == 0;
  return DialogParticipantStatus::Restricted(
      is_member, banned_rights->until_date_, can_send_messages, can_send_media_messages, can_send_stickers,
      can_send_animations, can_send_games, can_use_inline_bots, can_add_web_page_previews, can_send_polls,
      can_change_info_and_settings, can_invite_users, can_pin_messages);
}

RestrictedRights::RestrictedRights(bool can_send_messages, bool can_send_media, bool can_send_stickers,
                                   bool can_send_animations, bool can_send_games, bool can_use_inline_bots,
                                   bool can_add_web_page_previews, bool can_send_polls,
                                   bool can_change_info_and_settings, bool can_invite_users, bool can_pin_messages) {
  flags_ = (static_cast<uint32>(can_send_messages) * CAN_SEND_MESSAGES) |
           (static_cast<uint32>(can_send_media) * CAN_SEND_MEDIA) |
           (static_cast<uint32>(can_send_stickers) * CAN_SEND_STICKERS) |
           (static_cast<uint32>(can_send_animations) * CAN_SEND_ANIMATIONS) |
           (static_cast<uint32>(can_send_games) * CAN_SEND_GAMES) |
           (static_cast<uint32>(can_use_inline_bots) * CAN_USE_INLINE_BOTS) |
           (static_cast<uint32>(can_add_web_page_previews) * CAN_ADD_WEB_PAGE_PREVIEWS) |
           (static_cast<uint32>(can_send_polls) * CAN_SEND_POLLS) |
           (static_cast<uint32>(can_change_info_and_settings) * CAN_CHANGE_INFO_AND_SETTINGS) |
           (static_cast<uint32>(can_invite_users) * CAN_INVITE_USERS) |
           (static_cast<uint32>(can_pin_messages) * CAN_PIN_MESSAGES);
}

tl_object_ptr<td_api::chatPermissions> RestrictedRights::get_chat_permissions_object() const {
  return td_api::make_object<td_api::chatPermissions>(
      can_send_messages(), can_send_media(), can_send_polls(),
      can_send_stickers() || can_send_animations() || can_send_games() || can_use_inline_bots(),
      can_add_web_page_previews(), can_change_info_and_settings(), can_invite_users(), can_pin_messages());
}

tl_object_ptr<telegram_api::chatBannedRights> RestrictedRights::get_chat_banned_rights() const {
  int32 flags = 0;
  if (!can_send_messages()) {
    flags |= telegram_api::chatBannedRights::SEND_MESSAGES_MASK;
  }
  if (!can_send_media()) {
    flags |= telegram_api::chatBannedRights::SEND_MEDIA_MASK;
  }
  if (!can_send_stickers()) {
    flags |= telegram_api::chatBannedRights::SEND_STICKERS_MASK;
  }
  if (!can_send_animations()) {
    flags |= telegram_api::chatBannedRights::SEND_GIFS_MASK;
  }
  if (!can_send_games()) {
    flags |= telegram_api::chatBannedRights::SEND_GAMES_MASK;
  }
  if (!can_use_inline_bots()) {
    flags |= telegram_api::chatBannedRights::SEND_INLINE_MASK;
  }
  if (!can_add_web_page_previews()) {
    flags |= telegram_api::chatBannedRights::EMBED_LINKS_MASK;
  }
  if (!can_send_polls()) {
    flags |= telegram_api::chatBannedRights::SEND_POLLS_MASK;
  }
  if (!can_change_info_and_settings()) {
    flags |= telegram_api::chatBannedRights::CHANGE_INFO_MASK;
  }
  if (!can_invite_users()) {
    flags |= telegram_api::chatBannedRights::INVITE_USERS_MASK;
  }
  if (!can_pin_messages()) {
    flags |= telegram_api::chatBannedRights::PIN_MESSAGES_MASK;
  }

  LOG(INFO) << "Create chat banned rights " << flags;
  return make_tl_object<telegram_api::chatBannedRights>(flags, false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                                        false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                                        false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                                        false /*ignored*/, false /*ignored*/, false /*ignored*/, 0);
}

bool operator==(const RestrictedRights &lhs, const RestrictedRights &rhs) {
  return lhs.flags_ == rhs.flags_;
}

bool operator!=(const RestrictedRights &lhs, const RestrictedRights &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const RestrictedRights &status) {
  string_builder << "Restricted: ";
  if (!status.can_send_messages()) {
    string_builder << "(text)";
  }
  if (!status.can_send_media()) {
    string_builder << "(media)";
  }
  if (!status.can_send_stickers()) {
    string_builder << "(stickers)";
  }
  if (!status.can_send_animations()) {
    string_builder << "(animations)";
  }
  if (!status.can_send_games()) {
    string_builder << "(games)";
  }
  if (!status.can_send_polls()) {
    string_builder << "(polls)";
  }
  if (!status.can_use_inline_bots()) {
    string_builder << "(inline bots)";
  }
  if (!status.can_add_web_page_previews()) {
    string_builder << "(links)";
  }
  if (!status.can_change_info_and_settings()) {
    string_builder << "(change)";
  }
  if (!status.can_invite_users()) {
    string_builder << "(invite)";
  }
  if (!status.can_pin_messages()) {
    string_builder << "(pin)";
  }
  return string_builder;
}

RestrictedRights get_restricted_rights(const tl_object_ptr<telegram_api::chatBannedRights> &banned_rights) {
  if (banned_rights == nullptr) {
    return RestrictedRights(false, false, false, false, false, false, false, false, false, false, false);
  }
  bool can_view_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::VIEW_MESSAGES_MASK) == 0;
  if (!can_view_messages) {
    LOG(ERROR) << "Can't view messages in restricted rights " << to_string(banned_rights);
  }
  LOG_IF(ERROR, banned_rights->until_date_ != std::numeric_limits<int32>::max())
      << "Have until date " << banned_rights->until_date_ << " in restricted rights";

  bool can_send_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_MESSAGES_MASK) == 0;
  bool can_send_media_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_MEDIA_MASK) == 0;
  bool can_send_stickers = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_STICKERS_MASK) == 0;
  bool can_send_animations = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_GIFS_MASK) == 0;
  bool can_send_games = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_GAMES_MASK) == 0;
  bool can_use_inline_bots = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_INLINE_MASK) == 0;
  bool can_add_web_page_previews = (banned_rights->flags_ & telegram_api::chatBannedRights::EMBED_LINKS_MASK) == 0;
  bool can_send_polls = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_POLLS_MASK) == 0;
  bool can_change_info_and_settings = (banned_rights->flags_ & telegram_api::chatBannedRights::CHANGE_INFO_MASK) == 0;
  bool can_invite_users = (banned_rights->flags_ & telegram_api::chatBannedRights::INVITE_USERS_MASK) == 0;
  bool can_pin_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::PIN_MESSAGES_MASK) == 0;
  return RestrictedRights(can_send_messages, can_send_media_messages, can_send_stickers, can_send_animations,
                          can_send_games, can_use_inline_bots, can_add_web_page_previews, can_send_polls,
                          can_change_info_and_settings, can_invite_users, can_pin_messages);
}

RestrictedRights get_restricted_rights(const td_api::object_ptr<td_api::chatPermissions> &permissions) {
  bool can_send_polls = permissions->can_send_polls_;
  bool can_send_media = permissions->can_send_media_messages_;
  bool can_send_messages = permissions->can_send_messages_ || can_send_media || can_send_polls ||
                           permissions->can_send_other_messages_ || permissions->can_add_web_page_previews_;
  return RestrictedRights(can_send_messages, can_send_media, permissions->can_send_other_messages_,
                          permissions->can_send_other_messages_, permissions->can_send_other_messages_,
                          permissions->can_send_other_messages_, permissions->can_add_web_page_previews_,
                          permissions->can_send_polls_, permissions->can_change_info_, permissions->can_invite_users_,
                          permissions->can_pin_messages_);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipant &dialog_participant) {
  return string_builder << '[' << dialog_participant.user_id << " invited by " << dialog_participant.inviter_user_id
                        << " at " << dialog_participant.joined_date << " with status " << dialog_participant.status
                        << ']';
}

tl_object_ptr<telegram_api::ChannelParticipantsFilter>
ChannelParticipantsFilter::get_input_channel_participants_filter() const {
  switch (type) {
    case Type::Recent:
      return make_tl_object<telegram_api::channelParticipantsRecent>();
    case Type::Contacts:
      return make_tl_object<telegram_api::channelParticipantsContacts>(query);
    case Type::Administrators:
      return make_tl_object<telegram_api::channelParticipantsAdmins>();
    case Type::Search:
      return make_tl_object<telegram_api::channelParticipantsSearch>(query);
    case Type::Restricted:
      return make_tl_object<telegram_api::channelParticipantsBanned>(query);
    case Type::Banned:
      return make_tl_object<telegram_api::channelParticipantsKicked>(query);
    case Type::Bots:
      return make_tl_object<telegram_api::channelParticipantsBots>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

ChannelParticipantsFilter::ChannelParticipantsFilter(const tl_object_ptr<td_api::SupergroupMembersFilter> &filter) {
  if (filter == nullptr) {
    type = Type::Recent;
    return;
  }
  switch (filter->get_id()) {
    case td_api::supergroupMembersFilterRecent::ID:
      type = Type::Recent;
      return;
    case td_api::supergroupMembersFilterContacts::ID:
      type = Type::Contacts;
      query = static_cast<const td_api::supergroupMembersFilterContacts *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterAdministrators::ID:
      type = Type::Administrators;
      return;
    case td_api::supergroupMembersFilterSearch::ID:
      type = Type::Search;
      query = static_cast<const td_api::supergroupMembersFilterSearch *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterRestricted::ID:
      type = Type::Restricted;
      query = static_cast<const td_api::supergroupMembersFilterRestricted *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterBanned::ID:
      type = Type::Banned;
      query = static_cast<const td_api::supergroupMembersFilterBanned *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterBots::ID:
      type = Type::Bots;
      return;
    default:
      UNREACHABLE();
      type = Type::Recent;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const ChannelParticipantsFilter &filter) {
  switch (filter.type) {
    case ChannelParticipantsFilter::Type::Recent:
      return string_builder << "Recent";
    case ChannelParticipantsFilter::Type::Contacts:
      return string_builder << "Contacts \"" << filter.query << '"';
    case ChannelParticipantsFilter::Type::Administrators:
      return string_builder << "Administrators";
    case ChannelParticipantsFilter::Type::Search:
      return string_builder << "Search \"" << filter.query << '"';
    case ChannelParticipantsFilter::Type::Restricted:
      return string_builder << "Restricted \"" << filter.query << '"';
    case ChannelParticipantsFilter::Type::Banned:
      return string_builder << "Banned \"" << filter.query << '"';
    case ChannelParticipantsFilter::Type::Bots:
      return string_builder << "Bots";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

DialogParticipantsFilter get_dialog_participants_filter(const tl_object_ptr<td_api::ChatMembersFilter> &filter) {
  if (filter == nullptr) {
    return DialogParticipantsFilter::Members;
  }
  switch (filter->get_id()) {
    case td_api::chatMembersFilterContacts::ID:
      return DialogParticipantsFilter::Contacts;
    case td_api::chatMembersFilterAdministrators::ID:
      return DialogParticipantsFilter::Administrators;
    case td_api::chatMembersFilterMembers::ID:
      return DialogParticipantsFilter::Members;
    case td_api::chatMembersFilterRestricted::ID:
      return DialogParticipantsFilter::Restricted;
    case td_api::chatMembersFilterBanned::ID:
      return DialogParticipantsFilter::Banned;
    case td_api::chatMembersFilterBots::ID:
      return DialogParticipantsFilter::Bots;
    default:
      UNREACHABLE();
      return DialogParticipantsFilter::Members;
  }
}

}  // namespace td
