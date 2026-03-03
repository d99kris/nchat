// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber, Malte Eggers
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

package signalmeow

import (
	"context"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
	"unicode"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/exslices"
	"go.mau.fi/util/ptr"
	"go.mau.fi/util/random"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

type GroupMemberRole int32

const (
	// Note: right now we assume these match the equivalent values in the protobuf (signalpb.Member_Role)
	GroupMember_UNKNOWN       GroupMemberRole = 0
	GroupMember_DEFAULT       GroupMemberRole = 1
	GroupMember_ADMINISTRATOR GroupMemberRole = 2
)

type AccessControl int32

const (
	AccessControl_UNKNOWN       AccessControl = 0
	AccessControl_ANY           AccessControl = 1
	AccessControl_MEMBER        AccessControl = 2
	AccessControl_ADMINISTRATOR AccessControl = 3
	AccessControl_UNSATISFIABLE AccessControl = 4
)

type GroupMember struct {
	ACI              uuid.UUID
	Role             GroupMemberRole
	ProfileKey       libsignalgo.ProfileKey
	JoinedAtRevision uint32
}

func (gm *GroupMember) UserServiceID() libsignalgo.ServiceID {
	return libsignalgo.NewACIServiceID(gm.ACI)
}

type Group struct {
	GroupMasterKey  types.SerializedGroupMasterKey // We should keep this relatively private
	GroupIdentifier types.GroupIdentifier          // This is what we should use to identify a group outside this file

	Title                        string
	AvatarPath                   string
	Members                      []*GroupMember
	Description                  string
	AnnouncementsOnly            bool
	Revision                     uint32
	DisappearingMessagesDuration uint32
	AccessControl                *GroupAccessControl
	PendingMembers               []*PendingMember
	RequestingMembers            []*RequestingMember
	BannedMembers                []*BannedMember
	InviteLinkPassword           *types.SerializedInviteLinkPassword
	//PublicKey                  *libsignalgo.PublicKey
}

func (group *Group) getMemberServiceIDs() []libsignalgo.ServiceID {
	return exslices.CastFunc(group.Members, func(from *GroupMember) libsignalgo.ServiceID {
		return libsignalgo.NewACIServiceID(from.ACI)
	})
}

func (group *Group) GetInviteLink() (string, error) {
	if group.InviteLinkPassword == nil {
		return "", fmt.Errorf("no invite link password set")
	}
	masterKeyBytes := masterKeyToBytes(group.GroupMasterKey)
	inviteLinkPasswordBytes, err := inviteLinkPasswordToBytes(*group.InviteLinkPassword)
	if err != nil {
		return "", fmt.Errorf("couldn't decode invite link password")
	}
	inviteLinkContents := signalpb.GroupInviteLink_ContentsV1{
		ContentsV1: &signalpb.GroupInviteLink_GroupInviteLinkContentsV1{
			GroupMasterKey:     masterKeyBytes[:],
			InviteLinkPassword: inviteLinkPasswordBytes,
		},
	}
	inviteLink := signalpb.GroupInviteLink{Contents: &inviteLinkContents}
	inviteLinkEncoded, err := proto.Marshal(&inviteLink)
	if err != nil {
		return "", fmt.Errorf("failed to marshal invite link")
	}
	inviteLinkPath := base64.URLEncoding.EncodeToString(inviteLinkEncoded)
	return "https://signal.group/#" + inviteLinkPath, nil
}

func (group *Group) findMemberOrEmpty(aci uuid.UUID) *GroupMember {
	for _, member := range group.Members {
		if member.ACI == aci {
			return member
		}
	}
	return &GroupMember{}
}

type GroupAccessControl struct {
	Members           AccessControl
	AddFromInviteLink AccessControl
	Attributes        AccessControl
}

type AddMember struct {
	GroupMember
	JoinFromInviteLink bool
}

type PendingMember struct {
	ServiceID     libsignalgo.ServiceID
	Role          GroupMemberRole
	AddedByUserID uuid.UUID
	Timestamp     uint64
}

type ProfileKeyMember struct {
	ACI        uuid.UUID
	ProfileKey libsignalgo.ProfileKey
}

type RequestingMember struct {
	ACI        uuid.UUID
	ProfileKey libsignalgo.ProfileKey
	Timestamp  uint64
}

type PromotePendingMember struct {
	ACI        uuid.UUID
	ProfileKey libsignalgo.ProfileKey
}

type PromotePendingPniAciMember struct {
	ACI        uuid.UUID
	ProfileKey libsignalgo.ProfileKey
	PNI        uuid.UUID
}

type RoleMember struct {
	ACI  uuid.UUID
	Role GroupMemberRole
}

type BannedMember struct {
	ServiceID libsignalgo.ServiceID
	Timestamp uint64
}

type GroupChange struct {
	GroupMasterKey                     types.SerializedGroupMasterKey
	SourceServiceID                    libsignalgo.ServiceID
	Revision                           uint32
	AddMembers                         []*AddMember
	DeleteMembers                      []*uuid.UUID
	ModifyMemberRoles                  []*RoleMember
	ModifyMemberProfileKeys            []*ProfileKeyMember
	AddPendingMembers                  []*PendingMember
	DeletePendingMembers               []*libsignalgo.ServiceID
	PromotePendingMembers              []*PromotePendingMember
	ModifyTitle                        *string
	ModifyAvatar                       *string
	ModifyDisappearingMessagesDuration *uint32
	ModifyAttributesAccess             *AccessControl
	ModifyMemberAccess                 *AccessControl
	ModifyAddFromInviteLinkAccess      *AccessControl
	AddRequestingMembers               []*RequestingMember
	DeleteRequestingMembers            []*uuid.UUID
	PromoteRequestingMembers           []*RoleMember
	ModifyDescription                  *string
	ModifyAnnouncementsOnly            *bool
	AddBannedMembers                   []*BannedMember
	DeleteBannedMembers                []*libsignalgo.ServiceID
	PromotePendingPniAciMembers        []*PromotePendingPniAciMember
	ModifyInviteLinkPassword           *types.SerializedInviteLinkPassword
}

func (groupChange *GroupChange) isEmpty() bool {
	return len(groupChange.AddMembers) == 0 &&
		len(groupChange.DeleteMembers) == 0 &&
		len(groupChange.ModifyMemberRoles) == 0 &&
		len(groupChange.ModifyMemberProfileKeys) == 0 &&
		len(groupChange.AddPendingMembers) == 0 &&
		len(groupChange.PromotePendingMembers) == 0 &&
		groupChange.ModifyTitle == nil &&
		groupChange.ModifyAvatar == nil &&
		groupChange.ModifyDisappearingMessagesDuration == nil &&
		groupChange.ModifyAttributesAccess == nil &&
		groupChange.ModifyMemberAccess == nil &&
		groupChange.ModifyAddFromInviteLinkAccess == nil &&
		len(groupChange.AddRequestingMembers) == 0 &&
		len(groupChange.DeleteRequestingMembers) == 0 &&
		len(groupChange.PromoteRequestingMembers) == 0 &&
		groupChange.ModifyDescription == nil &&
		groupChange.ModifyAnnouncementsOnly == nil &&
		len(groupChange.AddBannedMembers) == 0
}

func (groupChange *GroupChange) resolveConflict(group *Group) {
	if *groupChange.ModifyTitle == group.Title {
		groupChange.ModifyTitle = nil
	}
	if *groupChange.ModifyDescription == group.Description {
		groupChange.ModifyDescription = nil
	}
	if *groupChange.ModifyAvatar == group.AvatarPath {
		groupChange.ModifyAvatar = nil
	}
	if *groupChange.ModifyDisappearingMessagesDuration == group.DisappearingMessagesDuration {
		groupChange.ModifyDisappearingMessagesDuration = nil
	}
	if *groupChange.ModifyAttributesAccess == group.AccessControl.Attributes {
		groupChange.ModifyAttributesAccess = nil
	}
	if *groupChange.ModifyMemberAccess == group.AccessControl.Members {
		groupChange.ModifyAttributesAccess = nil
	}
	if *groupChange.ModifyAddFromInviteLinkAccess == group.AccessControl.AddFromInviteLink {
		groupChange.ModifyAddFromInviteLinkAccess = nil
	}
	if *groupChange.ModifyAnnouncementsOnly == group.AnnouncementsOnly {
		groupChange.ModifyAnnouncementsOnly = nil
	}
	members := make(map[uuid.UUID]GroupMemberRole)
	for _, member := range group.Members {
		members[member.ACI] = member.Role
	}
	pendingMembers := make(map[libsignalgo.ServiceID]bool)
	for _, pendingMember := range group.PendingMembers {
		pendingMembers[pendingMember.ServiceID] = true
	}
	requestingMembers := make(map[uuid.UUID]bool)
	for _, requestingMember := range group.RequestingMembers {
		requestingMembers[requestingMember.ACI] = true
	}
	for i, member := range groupChange.AddMembers {
		if _, ok := members[member.GroupMember.ACI]; ok {
			groupChange.AddMembers = append(groupChange.AddMembers[:i], groupChange.AddMembers[i+1:]...)
		}
	}
	for i, promotePendingMember := range groupChange.PromotePendingMembers {
		if _, ok := members[promotePendingMember.ACI]; ok {
			groupChange.PromotePendingMembers = append(groupChange.PromotePendingMembers[:i], groupChange.PromotePendingMembers[i+1:]...)
		}
	}
	for i, promoteRequestingMember := range groupChange.PromotePendingMembers {
		if _, ok := members[promoteRequestingMember.ACI]; ok {
			groupChange.PromoteRequestingMembers = append(groupChange.PromoteRequestingMembers[:i], groupChange.PromoteRequestingMembers[i+1:]...)
		}
	}
	for i, pendingMember := range groupChange.AddPendingMembers {
		if pendingMembers[pendingMember.ServiceID] {
			groupChange.AddPendingMembers = append(groupChange.AddPendingMembers[:i], groupChange.AddPendingMembers[i+1:]...)
		}
	}
	for i, requestingMember := range groupChange.AddRequestingMembers {
		if requestingMembers[requestingMember.ACI] {
			groupChange.AddRequestingMembers = append(groupChange.AddRequestingMembers[:i], groupChange.AddRequestingMembers[i+1:]...)
		}
	}
	for i, deletePendingMember := range groupChange.DeletePendingMembers {
		if !pendingMembers[*deletePendingMember] {
			groupChange.DeletePendingMembers = append(groupChange.DeletePendingMembers[:i], groupChange.DeletePendingMembers[i+1:]...)
		}
	}
	for i, deleteRequestingMember := range groupChange.DeleteRequestingMembers {
		if !requestingMembers[*deleteRequestingMember] {
			groupChange.DeleteRequestingMembers = append(groupChange.DeleteRequestingMembers[:i], groupChange.DeleteRequestingMembers[i+1:]...)
		}
	}
	for i, deleteMember := range groupChange.DeleteMembers {
		if _, ok := members[*deleteMember]; !ok {
			groupChange.DeleteMembers = append(groupChange.DeleteMembers[:i], groupChange.DeleteMembers[i+1:]...)
		}
	}
	for i, modifyMemberRole := range groupChange.ModifyMemberRoles {
		if members[modifyMemberRole.ACI] == modifyMemberRole.Role {
			groupChange.ModifyMemberRoles = append(groupChange.ModifyMemberRoles[:i], groupChange.ModifyMemberRoles[i+1:]...)
		}
	}
}

type GroupChangeState struct {
	GroupState  *Group
	GroupChange *GroupChange
}

type GroupAuth struct {
	Username string
	Password string
}

func (cli *Client) fetchNewGroupCreds(ctx context.Context, today time.Time) (*GroupCredentials, error) {
	log := zerolog.Ctx(ctx).With().
		Str("action", "fetch new group creds").
		Logger()
	sevenDaysOut := today.Add(7 * 24 * time.Hour)
	path := fmt.Sprintf("/v1/certificate/auth/group?redemptionStartSeconds=%d&redemptionEndSeconds=%d&pniAsServiceId=true", today.Unix(), sevenDaysOut.Unix())
	resp, err := cli.AuthedWS.SendRequest(ctx, http.MethodGet, path, nil, nil)
	if err != nil {
		return nil, fmt.Errorf("SendRequest error: %w", err)
	}
	if *resp.Status != 200 {
		return nil, fmt.Errorf("bad status code fetching group creds: %d", *resp.Status)
	}

	var creds GroupCredentials
	err = json.Unmarshal(resp.Body, &creds)
	if err != nil {
		log.Err(err).Msg("json.Unmarshal error")
		return nil, err
	}
	if creds.PNI != cli.Store.PNI {
		return nil, fmt.Errorf("mismatching PNI in group credentials: %s != %s", creds.PNI, cli.Store.PNI)
	}
	return &creds, nil
}

func (cli *Client) GetAuthorizationForToday(ctx context.Context, masterKey libsignalgo.GroupMasterKey) (*GroupAuth, error) {
	log := zerolog.Ctx(ctx).With().
		Str("action", "get authorization for today").
		Logger()

	todayCred, err := cli.GroupCache.GetCredentials(ctx, cli.fetchNewGroupCreds)
	if err != nil {
		return nil, fmt.Errorf("failed to get group credentials: %w", err)
	}

	redemptionTime := uint64(todayCred.RedemptionTime)
	credential := todayCred.Credential
	authCredentialResponse, err := libsignalgo.NewAuthCredentialWithPniResponse(credential)
	if err != nil {
		log.Err(err).Msg("NewAuthCredentialWithPniResponse error")
		return nil, err
	}

	// Receive the auth credential
	authCredential, err := libsignalgo.ReceiveAuthCredentialWithPni(
		prodServerPublicParams,
		cli.Store.ACI,
		cli.Store.PNI,
		redemptionTime,
		*authCredentialResponse,
	)
	if err != nil {
		log.Err(err).Msg("ReceiveAuthCredentialWithPni error")
		return nil, err
	}

	// get auth presentation
	groupSecretParams, err := libsignalgo.DeriveGroupSecretParamsFromMasterKey(masterKey)
	if err != nil {
		log.Err(err).Msg("DeriveGroupSecretParamsFromMasterKey error")
		return nil, err
	}
	authCredentialPresentation, err := libsignalgo.CreateAuthCredentialWithPniPresentation(
		prodServerPublicParams,
		libsignalgo.GenerateRandomness(),
		groupSecretParams,
		*authCredential,
	)
	if err != nil {
		log.Err(err).Msg("CreateAuthCredentialWithPniPresentation error")
		return nil, err
	}
	groupPublicParams, err := groupSecretParams.GetPublicParams()
	if err != nil {
		log.Err(err).Msg("GetPublicParams error")
		return nil, err
	}

	return &GroupAuth{
		Username: hex.EncodeToString(groupPublicParams[:]),
		Password: hex.EncodeToString(*authCredentialPresentation),
	}, nil
}

func masterKeyToBytes(groupMasterKey types.SerializedGroupMasterKey) libsignalgo.GroupMasterKey {
	// We are very tricksy, groupMasterKey is just base64 encoded group master key :O
	masterKeyBytes, err := base64.StdEncoding.DecodeString(string(groupMasterKey))
	if err != nil {
		panic(fmt.Errorf("we should always be able to decode groupMasterKey into masterKeyBytes: %w", err))
	}
	return libsignalgo.GroupMasterKey(masterKeyBytes)
}

func masterKeyFromBytes(masterKey libsignalgo.GroupMasterKey) types.SerializedGroupMasterKey {
	return types.SerializedGroupMasterKey(base64.StdEncoding.EncodeToString(masterKey[:]))
}

func inviteLinkPasswordToBytes(inviteLinkPassword types.SerializedInviteLinkPassword) ([]byte, error) {
	inviteLinkPasswordBytes, err := base64.StdEncoding.DecodeString((string(inviteLinkPassword)))
	if err != nil {
		return nil, err
	}
	return inviteLinkPasswordBytes, nil
}

func InviteLinkPasswordFromBytes(inviteLinkPassword []byte) types.SerializedInviteLinkPassword {
	return types.SerializedInviteLinkPassword(base64.StdEncoding.EncodeToString(inviteLinkPassword))
}

func groupIdentifierFromMasterKey(masterKey types.SerializedGroupMasterKey) (types.GroupIdentifier, error) {
	groupIdentifier, err := masterKeyToBytes(masterKey).GroupIdentifier()
	if err != nil {
		return "", err
	}
	return types.BytesToGroupIdentifier(groupIdentifier), nil
}

func decryptGroup(ctx context.Context, encryptedGroup *signalpb.Group, groupMasterKey types.SerializedGroupMasterKey) (*Group, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "decrypt group").Logger()
	decryptedGroup := &Group{
		GroupMasterKey: groupMasterKey,
	}

	groupSecretParams, err := libsignalgo.DeriveGroupSecretParamsFromMasterKey(masterKeyToBytes(groupMasterKey))
	if err != nil {
		log.Err(err).Msg("DeriveGroupSecretParamsFromMasterKey error")
		return nil, err
	}

	gid, err := groupIdentifierFromMasterKey(groupMasterKey)
	if err != nil {
		log.Err(err).Msg("groupIdentifierFromMasterKey error")
		return nil, err
	}
	decryptedGroup.GroupIdentifier = gid

	titleBlob, err := decryptGroupPropertyIntoBlob(groupSecretParams, encryptedGroup.Title)
	if err != nil {
		return nil, err
	}
	// The actual title is in the blob
	decryptedGroup.Title = cleanupStringProperty(titleBlob.GetTitle())

	descriptionBlob, err := decryptGroupPropertyIntoBlob(groupSecretParams, encryptedGroup.Description)
	if err == nil {
		// treat a failure in obtaining the description as non-fatal
		decryptedGroup.Description = cleanupStringProperty(descriptionBlob.GetDescriptionText())
	}

	if encryptedGroup.DisappearingMessagesTimer != nil && len(encryptedGroup.DisappearingMessagesTimer) > 0 {
		timerBlob, err := decryptGroupPropertyIntoBlob(groupSecretParams, encryptedGroup.DisappearingMessagesTimer)
		if err != nil {
			return nil, err
		}
		decryptedGroup.DisappearingMessagesDuration = timerBlob.GetDisappearingMessagesDuration()
	}

	// These aren't encrypted
	decryptedGroup.AvatarPath = encryptedGroup.AvatarUrl
	decryptedGroup.Revision = encryptedGroup.Version

	// Decrypt members
	for _, member := range encryptedGroup.Members {
		if member == nil {
			continue
		}
		decryptedMember, err := decryptMember(ctx, member, groupSecretParams)
		if err != nil {
			return nil, err
		}
		decryptedGroup.Members = append(decryptedGroup.Members, decryptedMember)
	}

	for _, pendingMember := range encryptedGroup.MembersPendingProfileKey {
		if pendingMember == nil {
			continue
		}
		decryptedPendingMember, err := decryptPendingMember(ctx, pendingMember, groupSecretParams)
		if err != nil {
			continue
			// decryptPendingMember returns an error if the userID is a PNI, keep decrypting
		}
		decryptedGroup.PendingMembers = append(decryptedGroup.PendingMembers, decryptedPendingMember)
	}

	for _, requestingMember := range encryptedGroup.MembersPendingAdminApproval {
		if requestingMember == nil {
			continue
		}
		decryptedRequestingMember, err := decryptRequestingMember(ctx, requestingMember, groupSecretParams)
		if err != nil {
			return nil, err
		}
		decryptedGroup.RequestingMembers = append(decryptedGroup.RequestingMembers, decryptedRequestingMember)
	}

	for _, bannedMember := range encryptedGroup.MembersBanned {
		if bannedMember == nil {
			continue
		}
		encryptedUserID := libsignalgo.UUIDCiphertext(bannedMember.UserId)
		serviceID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
		if err != nil {
			log.Err(err).Msg("DecryptUUID UserId error")
			return nil, err
		}
		decryptedGroup.BannedMembers = append(decryptedGroup.BannedMembers, &BannedMember{
			ServiceID: serviceID,
			Timestamp: bannedMember.Timestamp,
		})
	}

	if encryptedGroup.AccessControl != nil {
		decryptedGroup.AccessControl = &GroupAccessControl{
			Members:           (AccessControl)(encryptedGroup.AccessControl.Members),
			Attributes:        (AccessControl)(encryptedGroup.AccessControl.Attributes),
			AddFromInviteLink: (AccessControl)(encryptedGroup.AccessControl.AddFromInviteLink),
		}
	}
	if len(encryptedGroup.InviteLinkPassword) > 0 {
		inviteLinkPassword := InviteLinkPasswordFromBytes(encryptedGroup.InviteLinkPassword)
		decryptedGroup.InviteLinkPassword = &inviteLinkPassword
	}
	return decryptedGroup, nil
}

func decryptGroupPropertyIntoBlob(groupSecretParams libsignalgo.GroupSecretParams, encryptedProperty []byte) (*signalpb.GroupAttributeBlob, error) {
	decryptedProperty, err := groupSecretParams.DecryptBlobWithPadding(encryptedProperty)
	if err != nil {
		return nil, fmt.Errorf("error decrypting blob with padding: %w", err)
	}
	var propertyBlob signalpb.GroupAttributeBlob
	err = proto.Unmarshal(decryptedProperty, &propertyBlob)
	if err != nil {
		return nil, fmt.Errorf("error unmarshalling blob: %w", err)
	}
	return &propertyBlob, nil
}

func encryptBlobIntoGroupProperty(groupSecretParams libsignalgo.GroupSecretParams, attributeBlob *signalpb.GroupAttributeBlob) (*[]byte, error) {
	decryptedProperty, err := proto.Marshal(attributeBlob)
	if err != nil {
		return nil, fmt.Errorf("error marshalling groupProperty: %w", err)
	}
	encryptedProperty, err := groupSecretParams.EncryptBlobWithPaddingDeterministic(libsignalgo.GenerateRandomness(), decryptedProperty, 0)
	if err != nil {
		return nil, fmt.Errorf("error encrypting blob with padding: %w", err)
	}
	return &encryptedProperty, nil
}

func cleanupStringProperty(property string) string {
	// strip non-printable characters from the string
	property = strings.Map(cleanupStringMapping, property)
	// strip \n and \t from start and end of the property if it exists
	return strings.TrimSpace(property)
}

func cleanupStringMapping(r rune) rune {
	if unicode.IsGraphic(r) {
		return r
	}
	return -1
}

func decryptGroupAvatar(encryptedAvatar []byte, groupMasterKey types.SerializedGroupMasterKey) ([]byte, error) {
	groupSecretParams, err := libsignalgo.DeriveGroupSecretParamsFromMasterKey(masterKeyToBytes(groupMasterKey))
	if err != nil {
		return nil, fmt.Errorf("error deriving group secret params from master key: %w", err)
	}
	avatarBlob, err := decryptGroupPropertyIntoBlob(groupSecretParams, encryptedAvatar)
	if err != nil {
		return nil, err
	}
	// The actual avatar is in the blob
	decryptedImage := avatarBlob.GetAvatar()

	return decryptedImage, nil
}

func groupMetadataForDataMessage(group Group) *signalpb.GroupContextV2 {
	masterKey := masterKeyToBytes(group.GroupMasterKey)
	masterKeyBytes := masterKey[:]
	return &signalpb.GroupContextV2{
		MasterKey: masterKeyBytes,
		Revision:  &group.Revision,
	}
}

func (cli *Client) fetchGroupByID(ctx context.Context, gid types.GroupIdentifier) (*Group, error) {
	groupMasterKey, err := cli.Store.GroupStore.MasterKeyFromGroupIdentifier(ctx, gid)
	if err != nil {
		return nil, fmt.Errorf("failed to get group master key: %w", err)
	}
	if groupMasterKey == "" {
		return nil, fmt.Errorf("No group master key found for group identifier %s", gid)
	}
	return cli.fetchGroupWithMasterKey(ctx, groupMasterKey)
}

func (cli *Client) fetchGroupWithMasterKey(ctx context.Context, groupMasterKey types.SerializedGroupMasterKey) (*Group, error) {
	masterKeyBytes := masterKeyToBytes(groupMasterKey)
	groupAuth, err := cli.GetAuthorizationForToday(ctx, masterKeyBytes)
	if err != nil {
		return nil, err
	}
	opts := &web.HTTPReqOpt{
		Username:    &groupAuth.Username,
		Password:    &groupAuth.Password,
		ContentType: web.ContentTypeProtobuf,
	}
	response, err := web.SendHTTPRequest(ctx, web.StorageHostname, http.MethodGet, "/v2/groups", opts)
	defer web.CloseBody(response)
	if err != nil {
		return nil, err
	}
	if response.StatusCode != 200 {
		return nil, fmt.Errorf("fetchGroupByID SendHTTPRequest bad status: %d", response.StatusCode)
	}
	return cli.parseGroupResponse(ctx, response, groupMasterKey)
}

func (cli *Client) parseGroupResponse(ctx context.Context, response *http.Response, masterKey types.SerializedGroupMasterKey) (*Group, error) {
	var groupResponse signalpb.GroupResponse
	groupBytes, err := io.ReadAll(response.Body)
	if err != nil {
		return nil, err
	}
	err = proto.Unmarshal(groupBytes, &groupResponse)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal group: %w", err)
	}

	group, err := decryptGroup(ctx, groupResponse.Group, masterKey)
	if err != nil {
		return nil, fmt.Errorf("failed to decrypt group: %w", err)
	}
	err = cli.GroupCache.Put(group, groupResponse.GroupSendEndorsementsResponse)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to cache group response")
	}

	// Store the profile keys in case they're new
	for _, member := range group.Members {
		err = cli.Store.RecipientStore.StoreProfileKey(ctx, member.ACI, member.ProfileKey)
		if err != nil {
			return nil, fmt.Errorf("failed to store profile key: %w", err)
		}
	}
	for _, requestingMember := range group.RequestingMembers {
		err = cli.Store.RecipientStore.StoreProfileKey(ctx, requestingMember.ACI, requestingMember.ProfileKey)
		if err != nil {
			return nil, fmt.Errorf("failed to store profile key: %w", err)
		}
	}
	return group, nil
}

func (cli *Client) DownloadGroupAvatar(ctx context.Context, avatarPath string, groupMasterKey types.SerializedGroupMasterKey) ([]byte, error) {
	username, password := cli.Store.BasicAuthCreds()
	opts := &web.HTTPReqOpt{
		Username: &username,
		Password: &password,
	}
	resp, err := web.SendHTTPRequest(ctx, web.CDN1Hostname, http.MethodGet, avatarPath, opts)
	defer web.CloseBody(resp)
	if err != nil {
		return nil, fmt.Errorf("failed to send request: %w", err)
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("unexpected response status %d", resp.StatusCode)
	}
	encryptedAvatar, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read response: %w", err)
	}

	decrypted, err := decryptGroupAvatar(encryptedAvatar, groupMasterKey)
	if err != nil {
		return nil, fmt.Errorf("failed to decrypt avatar: %w", err)
	}
	return decrypted, nil
}

func (cli *Client) RetrieveGroupByID(ctx context.Context, gid types.GroupIdentifier, revision uint32) (*Group, *SendEndorsementCache, error) {
	cached, endorsement, ok := cli.GroupCache.Get(gid)
	if ok && cached.Revision >= revision {
		return cached, endorsement, nil
	}
	group, err := cli.fetchGroupByID(ctx, gid)
	if err != nil {
		return nil, nil, err
	}
	cached, endorsement, ok = cli.GroupCache.Get(gid)
	if !ok {
		zerolog.Ctx(ctx).Warn().Msg("Group not found in cache after fetching")
		return group, nil, nil
	}
	return cached, endorsement, nil
}

// We should store the group master key in the group store as soon as we see it,
// then use the group identifier to refer to groups. As a convenience, we return
// the group identifier, which is derived from the group master key.
func (cli *Client) StoreMasterKey(ctx context.Context, groupMasterKey types.SerializedGroupMasterKey) (types.GroupIdentifier, error) {
	groupIdentifier, err := groupIdentifierFromMasterKey(groupMasterKey)
	if err != nil {
		return "", fmt.Errorf("groupIdentifierFromMasterKey error: %w", err)
	}
	err = cli.Store.GroupStore.StoreMasterKey(ctx, groupIdentifier, groupMasterKey)
	if err != nil {
		return groupIdentifier, fmt.Errorf("StoreMasterKey error: %w", err)
	}
	return groupIdentifier, nil
}

func (cli *Client) DecryptGroupChange(ctx context.Context, groupContext *signalpb.GroupContextV2) (*GroupChange, error) {
	masterKeyBytes := libsignalgo.GroupMasterKey(groupContext.MasterKey)
	groupMasterKey := masterKeyFromBytes(masterKeyBytes)

	groupChangeBytes := groupContext.GroupChange
	encryptedGroupChange := &signalpb.GroupChange{}
	err := proto.Unmarshal(groupChangeBytes, encryptedGroupChange)
	if err != nil {
		return nil, fmt.Errorf("Error unmarshalling group change: %w", err)
	}
	return cli.decryptGroupChange(ctx, encryptedGroupChange, groupMasterKey, true)
}

func (cli *Client) decryptGroupChange(ctx context.Context, encryptedGroupChange *signalpb.GroupChange, groupMasterKey types.SerializedGroupMasterKey, verifySignature bool) (*GroupChange, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "decrypt group change").Logger()
	serverSignature := encryptedGroupChange.ServerSignature
	encryptedActionsBytes := encryptedGroupChange.Actions
	var success bool
	defer func() {
		if !success {
			rawGroupID, _ := masterKeyToBytes(groupMasterKey).GroupIdentifier()
			if rawGroupID != nil {
				cli.GroupCache.Delete(types.GroupIdentifier(rawGroupID.String()))
			}
		}
	}()

	var err error
	if verifySignature {
		err = libsignalgo.ServerPublicParamsVerifySignature(prodServerPublicParams, encryptedActionsBytes, libsignalgo.NotarySignature(serverSignature))
		if err != nil {
			return nil, fmt.Errorf("Failed to verify Server Signature: %w", err)
		}
	}

	encryptedActions := signalpb.GroupChange_Actions{}

	err = proto.Unmarshal(encryptedActionsBytes, &encryptedActions)
	if err != nil {
		return nil, fmt.Errorf("Error unmashalling group change actions: %w", err)
	}

	groupSecretParams, err := libsignalgo.DeriveGroupSecretParamsFromMasterKey(masterKeyToBytes(groupMasterKey))
	if err != nil {
		log.Err(err).Msg("DeriveGroupSecretParamsFromMasterKey error")
		return nil, err
	}

	sourceServiceID, err := groupSecretParams.DecryptServiceID(libsignalgo.UUIDCiphertext(encryptedActions.SourceUserId))
	if err != nil {
		log.Err(err).Msg("Couldn't decrypt source serviceID")
		return nil, err
	}
	decryptedGroupChange := &GroupChange{
		GroupMasterKey:  groupMasterKey,
		Revision:        encryptedActions.Version,
		SourceServiceID: sourceServiceID,
	}

	if encryptedActions.ModifyTitle != nil {
		titleBlob, err := decryptGroupPropertyIntoBlob(groupSecretParams, encryptedActions.ModifyTitle.Title)
		if err != nil {
			return nil, err
		}
		// The actual title is in the blob
		newTitle := cleanupStringProperty(titleBlob.GetTitle())
		decryptedGroupChange.ModifyTitle = &newTitle
	}
	if encryptedActions.ModifyAvatar != nil {
		decryptedGroupChange.ModifyAvatar = &encryptedActions.ModifyAvatar.Avatar
	}
	if encryptedActions.ModifyDescription != nil {
		descriptionBlob, err := decryptGroupPropertyIntoBlob(groupSecretParams, encryptedActions.ModifyDescription.Description)
		if err == nil {
			// treat a failure in obtaining the description as non-fatal
			newDescription := cleanupStringProperty(descriptionBlob.GetDescriptionText())
			decryptedGroupChange.ModifyDescription = &newDescription
		}
	}

	for _, addMember := range encryptedActions.AddMembers {
		if addMember == nil {
			continue
		}
		decryptedMember, err := decryptMember(ctx, addMember.Added, groupSecretParams)
		if err != nil {
			return nil, err
		}
		decryptedGroupChange.AddMembers = append(decryptedGroupChange.AddMembers, &AddMember{
			GroupMember:        *decryptedMember,
			JoinFromInviteLink: addMember.JoinFromInviteLink,
		})
		err = cli.Store.RecipientStore.StoreProfileKey(ctx, decryptedMember.ACI, decryptedMember.ProfileKey)
		if err != nil {
			log.Err(err).Msg("failed to store profile key")
			return nil, err
		}
	}

	for _, deleteMember := range encryptedActions.DeleteMembers {
		if deleteMember == nil {
			continue
		}
		encryptedUserID := libsignalgo.UUIDCiphertext(deleteMember.DeletedUserId)
		serviceID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
		if err != nil {
			log.Err(err).Msg("DecryptUUID UserId error for deleteMember")
			return nil, err
		}
		if serviceID.Type != libsignalgo.ServiceIDTypeACI {
			return nil, fmt.Errorf("wrong ServiceID kind for delete member: expected ACI, got PNI")
		}
		decryptedGroupChange.DeleteMembers = append(decryptedGroupChange.DeleteMembers, &serviceID.UUID)
	}

	for _, modifyMemberRole := range encryptedActions.ModifyMemberRoles {
		encryptedUserID := libsignalgo.UUIDCiphertext(modifyMemberRole.UserId)
		serviceID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
		if err != nil {
			log.Err(err).Msg("DecryptUUID UserId error for modifyMemberRole")
			return nil, err
		}
		if serviceID.Type != libsignalgo.ServiceIDTypeACI {
			return nil, fmt.Errorf("wrong ServiceID kind for modify member: expected ACI, got PNI")
		}
		decryptedGroupChange.ModifyMemberRoles = append(decryptedGroupChange.ModifyMemberRoles, &RoleMember{
			ACI:  serviceID.UUID,
			Role: GroupMemberRole(modifyMemberRole.Role),
		})
	}

	for _, modifyProfileKey := range encryptedActions.ModifyMemberProfileKeys {
		if modifyProfileKey == nil {
			continue
		}
		aci, profileKey, err := decryptPKeyAndIDorPresentation(ctx, modifyProfileKey.UserId, modifyProfileKey.ProfileKey, modifyProfileKey.Presentation, groupSecretParams)
		if err != nil {
			return nil, err
		}
		decryptedGroupChange.ModifyMemberProfileKeys = append(decryptedGroupChange.ModifyMemberProfileKeys, &ProfileKeyMember{
			ACI:        *aci,
			ProfileKey: *profileKey,
		})
		err = cli.Store.RecipientStore.StoreProfileKey(ctx, *aci, *profileKey)
		if err != nil {
			log.Err(err).Msg("failed to store profile key")
			return nil, err
		}
	}

	for _, addPendingMember := range encryptedActions.AddMembersPendingProfileKey {
		if addPendingMember == nil {
			continue
		}
		pendingMember := addPendingMember.Added
		decryptedPendingMember, err := decryptPendingMember(ctx, pendingMember, groupSecretParams)
		if err != nil {
			continue
			// decryptPendingMember returns an error if the userID is a PNI, keep decrypting
		}
		decryptedGroupChange.AddPendingMembers = append(decryptedGroupChange.AddPendingMembers, decryptedPendingMember)
	}

	for _, deletePendingMember := range encryptedActions.DeleteMembersPendingProfileKey {
		if deletePendingMember == nil {
			continue
		}
		encryptedUserID := libsignalgo.UUIDCiphertext(deletePendingMember.DeletedUserId)
		userID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
		if err != nil {
			log.Err(err).Msg("DecryptUUID UserId error for deletePendingMember")
			return nil, err
		}
		decryptedGroupChange.DeletePendingMembers = append(decryptedGroupChange.DeletePendingMembers, &userID)
	}

	for _, promotePendingMember := range encryptedActions.PromoteMembersPendingProfileKey {
		if promotePendingMember == nil {
			continue
		}
		aci, profileKey, err := decryptPKeyAndIDorPresentation(ctx, promotePendingMember.UserId, promotePendingMember.ProfileKey, promotePendingMember.Presentation, groupSecretParams)
		if err != nil {
			return nil, err
		}
		decryptedGroupChange.PromotePendingMembers = append(decryptedGroupChange.PromotePendingMembers, &PromotePendingMember{
			ACI:        *aci,
			ProfileKey: *profileKey,
		})
		err = cli.Store.RecipientStore.StoreProfileKey(ctx, *aci, *profileKey)
		if err != nil {
			log.Err(err).Msg("failed to store profile key")
			return nil, err
		}
	}

	for _, promotePendingPniAciMember := range encryptedActions.PromoteMembersPendingPniAciProfileKey {
		// TODO: pretending this is a PendingMember should do for mautrix-signal, but we probably want to treat them separately at some point
		if promotePendingPniAciMember == nil {
			continue
		}
		aci, profileKey, err := decryptPKeyAndIDorPresentation(ctx, promotePendingPniAciMember.UserId, promotePendingPniAciMember.ProfileKey, promotePendingPniAciMember.Presentation, groupSecretParams)
		if err != nil {
			return nil, err
		}
		encryptedUserID := libsignalgo.UUIDCiphertext(promotePendingPniAciMember.Pni)
		pniServiceID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
		if err != nil {
			log.Err(err).Msg("DecryptUUID Pni error for promotePendingPniAciMember")
			return nil, err
		}
		if pniServiceID.Type != libsignalgo.ServiceIDTypePNI {
			return nil, fmt.Errorf("wrong ServiceID kind for promote pending pni->aci: expected PNI, got ACI")
		}
		decryptedGroupChange.PromotePendingPniAciMembers = append(decryptedGroupChange.PromotePendingPniAciMembers, &PromotePendingPniAciMember{
			ACI:        *aci,
			ProfileKey: *profileKey,
			PNI:        pniServiceID.UUID,
		})
		err = cli.Store.RecipientStore.StoreProfileKey(ctx, *aci, *profileKey)
		if err != nil {
			log.Err(err).Msg("failed to store profile key")
			return nil, err
		}
	}

	for _, addRequestingMember := range encryptedActions.AddMembersPendingAdminApproval {
		if addRequestingMember == nil {
			continue
		}
		decryptedRequestingMember, err := decryptRequestingMember(ctx, addRequestingMember.Added, groupSecretParams)
		if err != nil {
			return nil, err
		}
		decryptedGroupChange.AddRequestingMembers = append(decryptedGroupChange.AddRequestingMembers, decryptedRequestingMember)
		err = cli.Store.RecipientStore.StoreProfileKey(ctx, decryptedRequestingMember.ACI, decryptedRequestingMember.ProfileKey)
		if err != nil {
			log.Err(err).Msg("failed to store profile key")
			return nil, err
		}
	}

	for _, deleteRequestingMember := range encryptedActions.DeleteMembersPendingAdminApproval {
		if deleteRequestingMember == nil {
			continue
		}
		encryptedUserID := libsignalgo.UUIDCiphertext(deleteRequestingMember.DeletedUserId)
		serviceID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
		if err != nil {
			log.Err(err).Msg("DecryptUUID UserId error for deleteRequestingMember")
			return nil, err
		}
		decryptedGroupChange.DeleteRequestingMembers = append(decryptedGroupChange.DeleteRequestingMembers, &serviceID.UUID)
	}

	for _, promoteRequestingMember := range encryptedActions.PromoteMembersPendingAdminApproval {
		if promoteRequestingMember == nil {
			continue
		}
		encryptedUserID := libsignalgo.UUIDCiphertext(promoteRequestingMember.UserId)
		serviceID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
		if err != nil {
			log.Err(err).Msg("DecryptUUID UserId error for promoteRequestingMember")
			return nil, err
		}
		decryptedGroupChange.PromoteRequestingMembers = append(decryptedGroupChange.PromoteRequestingMembers, &RoleMember{
			ACI:  serviceID.UUID,
			Role: GroupMemberRole(promoteRequestingMember.Role),
		})
	}

	for _, addBannedMember := range encryptedActions.AddMembersBanned {
		if addBannedMember == nil {
			continue
		}
		bannedMember := addBannedMember.Added
		encryptedUserID := libsignalgo.UUIDCiphertext(bannedMember.UserId)
		serviceID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
		if err != nil {
			log.Err(err).Msg("DecryptUUID UserId error for addBannedMember")
			return nil, err
		}
		decryptedGroupChange.AddBannedMembers = append(decryptedGroupChange.AddBannedMembers, &BannedMember{
			ServiceID: serviceID,
			Timestamp: bannedMember.Timestamp,
		})
	}

	for _, deleteBannedMember := range encryptedActions.DeleteMembersBanned {
		if deleteBannedMember == nil {
			continue
		}
		encryptedUserID := libsignalgo.UUIDCiphertext(deleteBannedMember.DeletedUserId)
		userID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
		if err != nil {
			log.Err(err).Msg("DecryptUUID UserId error for deleteBannedMember")
			return nil, err
		}
		decryptedGroupChange.DeleteBannedMembers = append(decryptedGroupChange.DeleteBannedMembers, &userID)
	}

	if encryptedActions.ModifyAttributesAccess != nil {
		decryptedGroupChange.ModifyAttributesAccess = (*AccessControl)(&encryptedActions.ModifyAttributesAccess.AttributesAccess)
	}

	if encryptedActions.ModifyMemberAccess != nil {
		decryptedGroupChange.ModifyMemberAccess = (*AccessControl)(&encryptedActions.ModifyMemberAccess.MembersAccess)
	}

	if encryptedActions.ModifyAddFromInviteLinkAccess != nil {
		decryptedGroupChange.ModifyAddFromInviteLinkAccess = (*AccessControl)(&encryptedActions.ModifyAddFromInviteLinkAccess.AddFromInviteLinkAccess)
	}

	if encryptedActions.ModifyAnnouncementsOnly != nil {
		decryptedGroupChange.ModifyAnnouncementsOnly = &encryptedActions.ModifyAnnouncementsOnly.AnnouncementsOnly
	}
	if encryptedActions.ModifyDisappearingMessageTimer != nil && len(encryptedActions.ModifyDisappearingMessageTimer.Timer) > 0 {
		timerBlob, err := decryptGroupPropertyIntoBlob(groupSecretParams, encryptedActions.ModifyDisappearingMessageTimer.Timer)
		if err != nil {
			return nil, err
		}
		newDisappaeringMessagesDuration := timerBlob.GetDisappearingMessagesDuration()
		decryptedGroupChange.ModifyDisappearingMessagesDuration = &newDisappaeringMessagesDuration
	}
	if encryptedActions.ModifyInviteLinkPassword != nil {
		inviteLinkPassword := InviteLinkPasswordFromBytes(encryptedActions.ModifyInviteLinkPassword.InviteLinkPassword)
		decryptedGroupChange.ModifyInviteLinkPassword = &inviteLinkPassword
	}

	success = true
	err = cli.GroupCache.ApplyUpdate(decryptedGroupChange, nil)
	if err != nil {
		log.Err(err).Msg("Failed to apply group change to cache")
	}

	return decryptedGroupChange, nil
}

func decryptPKeyAndIDorPresentation(ctx context.Context, userID []byte, profileKeyBytes []byte, presentationBytes []byte, groupSecretParams libsignalgo.GroupSecretParams) (*uuid.UUID, *libsignalgo.ProfileKey, error) {
	log := zerolog.Ctx(ctx)
	var encryptedUserID libsignalgo.UUIDCiphertext
	var encryptedProfileKey libsignalgo.ProfileKeyCiphertext
	if len(userID) == 0 || len(profileKeyBytes) == 0 {
		presentation := libsignalgo.ProfileKeyCredentialPresentation(presentationBytes)
		err := presentation.CheckValidContents()
		if err != nil {
			log.Err(err).Msg("Invalid presentation contents")
			return nil, nil, err
		}
		encryptedUserID, err = presentation.UUIDCiphertext()
		if err != nil {
			log.Err(err).Msg("unable to get UUID from presentation")
			return nil, nil, err
		}
		encryptedProfileKey, err = presentation.ProfileKeyCiphertext()
		if err != nil {
			log.Err(err).Msg("unable to get ProfileKey from presentation")
			return nil, nil, err
		}
	} else {
		encryptedUserID = libsignalgo.UUIDCiphertext(userID)
		encryptedProfileKey = libsignalgo.ProfileKeyCiphertext(profileKeyBytes)
	}
	serviceID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
	if err != nil {
		log.Err(err).Msg("Failed to decrypt ServiceID")
		return nil, nil, err
	}
	profileKey, err := groupSecretParams.DecryptProfileKey(encryptedProfileKey, serviceID.UUID)
	if err != nil {
		return nil, nil, err
	}
	if serviceID.Type == libsignalgo.ServiceIDTypePNI {
		return nil, nil, fmt.Errorf("wrong serviceid kind for profile key: expected ACI, got PNI")
	}
	return &serviceID.UUID, profileKey, nil

}

func decryptMember(ctx context.Context, member *signalpb.Member, groupSecretParams libsignalgo.GroupSecretParams) (*GroupMember, error) {
	aci, profileKey, err := decryptPKeyAndIDorPresentation(ctx, member.UserId, member.ProfileKey, member.Presentation, groupSecretParams)
	if err != nil {
		return nil, err
	}
	return &GroupMember{
		ACI:              *aci,
		ProfileKey:       *profileKey,
		Role:             GroupMemberRole(member.Role),
		JoinedAtRevision: member.JoinedAtVersion,
	}, nil
}

func decryptPendingMember(ctx context.Context, pendingMember *signalpb.MemberPendingProfileKey, groupSecretParams libsignalgo.GroupSecretParams) (*PendingMember, error) {
	log := zerolog.Ctx(ctx)
	encryptedUserID := libsignalgo.UUIDCiphertext(pendingMember.Member.UserId)
	userID, err := groupSecretParams.DecryptServiceID(encryptedUserID)
	if err != nil {
		log.Err(err).Msg("DecryptUUID UserId error for pendingMember")
		return nil, err
	}
	// pendingMembers don't have profile keys
	encryptedAddedByUserID := pendingMember.AddedByUserId
	addedByServiceId, err := groupSecretParams.DecryptServiceID(libsignalgo.UUIDCiphertext(encryptedAddedByUserID))
	if err != nil {
		log.Err(err).Msg("DecryptUUID addedByUserId error for pendingMember")
		return nil, err
	}
	return &PendingMember{
		ServiceID:     userID,
		Role:          GroupMemberRole(pendingMember.Member.Role),
		AddedByUserID: addedByServiceId.UUID,
		Timestamp:     pendingMember.Timestamp,
	}, nil
}

func decryptRequestingMember(ctx context.Context, requestingMember *signalpb.MemberPendingAdminApproval, groupSecretParams libsignalgo.GroupSecretParams) (*RequestingMember, error) {
	aci, profileKey, err := decryptPKeyAndIDorPresentation(ctx, requestingMember.UserId, requestingMember.ProfileKey, requestingMember.Presentation, groupSecretParams)
	if err != nil {
		return nil, err
	}
	return &RequestingMember{
		ACI:        *aci,
		ProfileKey: *profileKey,
		Timestamp:  requestingMember.Timestamp,
	}, nil
}

func (cli *Client) EncryptAndSignGroupChange(ctx context.Context, decryptedGroupChange *GroupChange) (*signalpb.GroupChangeResponse, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "EncryptGroupChange").Logger()
	groupMasterKey := decryptedGroupChange.GroupMasterKey
	masterKeyBytes := masterKeyToBytes(groupMasterKey)
	groupSecretParams, err := libsignalgo.DeriveGroupSecretParamsFromMasterKey(masterKeyBytes)
	if err != nil {
		log.Err(err).Msg("Could not get groupSecretParams from master key")
		return nil, err
	}
	groupChangeActions := &signalpb.GroupChange_Actions{Version: decryptedGroupChange.Revision}
	if decryptedGroupChange.ModifyTitle != nil {
		attributeBlob := signalpb.GroupAttributeBlob{Content: &signalpb.GroupAttributeBlob_Title{Title: *decryptedGroupChange.ModifyTitle}}
		encryptedTitle, err := encryptBlobIntoGroupProperty(groupSecretParams, &attributeBlob)
		if err != nil {
			log.Err(err).Msg("Could not get encrypt Title")
			return nil, err
		}
		groupChangeActions.ModifyTitle = &signalpb.GroupChange_Actions_ModifyTitleAction{Title: *encryptedTitle}
	}
	if decryptedGroupChange.ModifyDescription != nil {
		attributeBlob := signalpb.GroupAttributeBlob{Content: &signalpb.GroupAttributeBlob_DescriptionText{DescriptionText: *decryptedGroupChange.ModifyDescription}}
		encryptedDescription, err := encryptBlobIntoGroupProperty(groupSecretParams, &attributeBlob)
		if err != nil {
			log.Err(err).Msg("Could not get encrypt description")
			return nil, err
		}
		groupChangeActions.ModifyDescription = &signalpb.GroupChange_Actions_ModifyDescriptionAction{Description: *encryptedDescription}
	}
	if decryptedGroupChange.ModifyAvatar != nil {
		groupChangeActions.ModifyAvatar = &signalpb.GroupChange_Actions_ModifyAvatarAction{Avatar: *decryptedGroupChange.ModifyAvatar}
	}
	for _, addMember := range decryptedGroupChange.AddMembers {
		encryptedMember, encryptedPendingMember, err := cli.encryptMember(ctx, &addMember.GroupMember, &groupSecretParams)
		if err != nil {
			log.Err(err).Msg("Failed to encrypt GroupMember")
		}
		if encryptedMember != nil {
			groupChangeActions.AddMembers = append(groupChangeActions.AddMembers, &signalpb.GroupChange_Actions_AddMemberAction{
				Added:              encryptedMember,
				JoinFromInviteLink: addMember.JoinFromInviteLink,
			})
		} else {
			groupChangeActions.AddMembersPendingProfileKey = append(groupChangeActions.AddMembersPendingProfileKey, &signalpb.GroupChange_Actions_AddMemberPendingProfileKeyAction{
				Added: encryptedPendingMember,
			})
		}
	}
	for _, deleteMember := range decryptedGroupChange.DeleteMembers {
		encryptedUserID, err := groupSecretParams.EncryptServiceID(libsignalgo.NewACIServiceID(*deleteMember))
		if err != nil {
			log.Err(err).Msg("Encrypt UserId error for deleteMember")
			return nil, err
		}
		groupChangeActions.DeleteMembers = append(groupChangeActions.DeleteMembers, &signalpb.GroupChange_Actions_DeleteMemberAction{
			DeletedUserId: encryptedUserID[:],
		})
	}
	for _, modifyMemberRoles := range decryptedGroupChange.ModifyMemberRoles {
		encryptedUserID, err := groupSecretParams.EncryptServiceID(libsignalgo.NewACIServiceID(modifyMemberRoles.ACI))
		if err != nil {
			log.Err(err).Msg("Encrypt UserId error for modifyMemberRoles")
			return nil, err
		}
		groupChangeActions.ModifyMemberRoles = append(groupChangeActions.ModifyMemberRoles, &signalpb.GroupChange_Actions_ModifyMemberRoleAction{
			UserId: encryptedUserID[:],
			Role:   signalpb.Member_Role(modifyMemberRoles.Role),
		})
	}
	for _, addPendingMember := range decryptedGroupChange.AddPendingMembers {
		encryptedPendingMember, err := cli.encryptPendingMember(ctx, addPendingMember, &groupSecretParams)
		if err != nil {
			log.Err(err).Msg("Failed to encrypt pendingMember")
			return nil, err
		}
		groupChangeActions.AddMembersPendingProfileKey = append(groupChangeActions.AddMembersPendingProfileKey, &signalpb.GroupChange_Actions_AddMemberPendingProfileKeyAction{
			Added: encryptedPendingMember,
		})
	}
	for _, deletePendingMember := range decryptedGroupChange.DeletePendingMembers {
		encryptedUserID, err := groupSecretParams.EncryptServiceID(*deletePendingMember)
		if err != nil {
			log.Err(err).Msg("Encrypt UserId error for deletePendingMember")
			return nil, err
		}
		groupChangeActions.DeleteMembersPendingProfileKey = append(groupChangeActions.DeleteMembersPendingProfileKey, &signalpb.GroupChange_Actions_DeleteMemberPendingProfileKeyAction{
			DeletedUserId: encryptedUserID[:],
		})
	}
	for _, promotePendingMember := range decryptedGroupChange.PromotePendingMembers {
		expiringProfileKeyCredential, err := cli.FetchExpiringProfileKeyCredentialById(ctx, promotePendingMember.ACI)
		if err != nil {
			log.Err(err).Msg("failed getting expiring profile key credential for addMember")
			return nil, err
		}
		presentation, err := groupSecretParams.CreateExpiringProfileKeyCredentialPresentation(
			prodServerPublicParams,
			*expiringProfileKeyCredential,
		)
		if err != nil {
			log.Err(err).Msg("failed creating expiring profile key credential presentation for addMember")
			return nil, err
		}
		groupChangeActions.PromoteMembersPendingProfileKey = append(groupChangeActions.PromoteMembersPendingProfileKey, &signalpb.GroupChange_Actions_PromoteMemberPendingProfileKeyAction{
			Presentation: *presentation,
		})
	}
	for _, addRequestingMember := range decryptedGroupChange.AddRequestingMembers {
		expiringProfileKeyCredential, err := cli.FetchExpiringProfileKeyCredentialById(ctx, addRequestingMember.ACI)
		if err != nil {
			log.Err(err).Msg("failed getting expiring profile key credential for addMember")
			return nil, err
		}
		presentation, err := groupSecretParams.CreateExpiringProfileKeyCredentialPresentation(
			prodServerPublicParams,
			*expiringProfileKeyCredential,
		)
		if err != nil {
			log.Err(err).Msg("failed creating expiring profile key credential presentation for addMember")
			return nil, err
		}
		groupChangeActions.AddMembersPendingAdminApproval = append(groupChangeActions.AddMembersPendingAdminApproval, &signalpb.GroupChange_Actions_AddMemberPendingAdminApprovalAction{
			Added: &signalpb.MemberPendingAdminApproval{
				Presentation: *presentation,
			},
		})
	}
	for _, deleteRequestingMember := range decryptedGroupChange.DeleteRequestingMembers {
		encryptedUserID, err := groupSecretParams.EncryptServiceID(libsignalgo.NewACIServiceID(*deleteRequestingMember))
		if err != nil {
			log.Err(err).Msg("Encrypt UserId error for deleteRequestingMember")
			return nil, err
		}
		groupChangeActions.DeleteMembersPendingAdminApproval = append(groupChangeActions.DeleteMembersPendingAdminApproval, &signalpb.GroupChange_Actions_DeleteMemberPendingAdminApprovalAction{
			DeletedUserId: encryptedUserID[:],
		})
	}
	for _, promoteRequestingMember := range decryptedGroupChange.PromoteRequestingMembers {
		encryptedUserID, err := groupSecretParams.EncryptServiceID(libsignalgo.NewACIServiceID(promoteRequestingMember.ACI))
		if err != nil {
			log.Err(err).Msg("Encrypt UserId error for promoteRequestingMember")
			return nil, err
		}

		groupChangeActions.PromoteMembersPendingAdminApproval = append(groupChangeActions.PromoteMembersPendingAdminApproval, &signalpb.GroupChange_Actions_PromoteMemberPendingAdminApprovalAction{
			UserId: encryptedUserID[:],
			Role:   signalpb.Member_Role(promoteRequestingMember.Role),
		})
	}
	for _, addBannedMember := range decryptedGroupChange.AddBannedMembers {
		encryptedUserID, err := groupSecretParams.EncryptServiceID(addBannedMember.ServiceID)
		if err != nil {
			log.Err(err).Msg("Encrypt UserId error for promoteRequestingMember")
			return nil, err
		}
		groupChangeActions.AddMembersBanned = append(groupChangeActions.AddMembersBanned, &signalpb.GroupChange_Actions_AddMemberBannedAction{
			Added: &signalpb.MemberBanned{
				UserId:    encryptedUserID[:],
				Timestamp: addBannedMember.Timestamp,
			},
		})
	}
	for _, deleteBannedMember := range decryptedGroupChange.DeleteBannedMembers {
		encryptedUserID, err := groupSecretParams.EncryptServiceID(*deleteBannedMember)
		if err != nil {
			log.Err(err).Msg("Encrypt UserId error for promoteRequestingMember")
			return nil, err
		}
		groupChangeActions.DeleteMembersBanned = append(groupChangeActions.DeleteMembersBanned, &signalpb.GroupChange_Actions_DeleteMemberBannedAction{
			DeletedUserId: encryptedUserID[:],
		})
	}
	if decryptedGroupChange.ModifyAnnouncementsOnly != nil {
		groupChangeActions.ModifyAnnouncementsOnly = &signalpb.GroupChange_Actions_ModifyAnnouncementsOnlyAction{
			AnnouncementsOnly: *decryptedGroupChange.ModifyAnnouncementsOnly,
		}
	}
	if decryptedGroupChange.ModifyAttributesAccess != nil {
		groupChangeActions.ModifyAttributesAccess = &signalpb.GroupChange_Actions_ModifyAttributesAccessControlAction{
			AttributesAccess: signalpb.AccessControl_AccessRequired(*decryptedGroupChange.ModifyAttributesAccess),
		}
	}
	if decryptedGroupChange.ModifyMemberAccess != nil {
		groupChangeActions.ModifyMemberAccess = &signalpb.GroupChange_Actions_ModifyMembersAccessControlAction{
			MembersAccess: signalpb.AccessControl_AccessRequired(*decryptedGroupChange.ModifyMemberAccess),
		}
	}
	if decryptedGroupChange.ModifyAddFromInviteLinkAccess != nil {
		groupChangeActions.ModifyAddFromInviteLinkAccess = &signalpb.GroupChange_Actions_ModifyAddFromInviteLinkAccessControlAction{
			AddFromInviteLinkAccess: signalpb.AccessControl_AccessRequired(*decryptedGroupChange.ModifyAddFromInviteLinkAccess),
		}
	}
	if decryptedGroupChange.ModifyDisappearingMessagesDuration != nil {
		attributeBlob := signalpb.GroupAttributeBlob{Content: &signalpb.GroupAttributeBlob_DisappearingMessagesDuration{DisappearingMessagesDuration: *decryptedGroupChange.ModifyDisappearingMessagesDuration}}
		encryptedTimer, err := encryptBlobIntoGroupProperty(groupSecretParams, &attributeBlob)
		if err != nil {
			log.Err(err).Msg("Could not get encrypt Title")
			return nil, err
		}
		groupChangeActions.ModifyDisappearingMessageTimer = &signalpb.GroupChange_Actions_ModifyDisappearingMessageTimerAction{Timer: *encryptedTimer}
	}
	if decryptedGroupChange.ModifyInviteLinkPassword != nil {
		inviteLinkPasswordBytes, err := inviteLinkPasswordToBytes(*decryptedGroupChange.ModifyInviteLinkPassword)
		if err != nil {
			log.Err(err).Msg("Failed to decode invite link password")
		}
		groupChangeActions.ModifyInviteLinkPassword = &signalpb.GroupChange_Actions_ModifyInviteLinkPasswordAction{
			InviteLinkPassword: inviteLinkPasswordBytes,
		}
	}

	return cli.patchGroup(ctx, groupChangeActions, groupMasterKey, nil)
}

func (cli *Client) encryptMember(ctx context.Context, member *GroupMember, groupSecretParams *libsignalgo.GroupSecretParams) (*signalpb.Member, *signalpb.MemberPendingProfileKey, error) {
	log := zerolog.Ctx(ctx)
	expiringProfileKeyCredential, err := cli.FetchExpiringProfileKeyCredentialById(ctx, member.ACI)
	if err != nil {
		log.Err(err).Msg("failed getting expiring profile key credential for member, trying to encrypt as PendingMember")
		pendingMember := PendingMember{
			ServiceID:     member.UserServiceID(),
			Role:          member.Role,
			AddedByUserID: cli.Store.ACI,
		}
		encryptedPendingMember, err := cli.encryptPendingMember(ctx, &pendingMember, groupSecretParams)
		return nil, encryptedPendingMember, err
	}
	presentation, err := groupSecretParams.CreateExpiringProfileKeyCredentialPresentation(
		prodServerPublicParams,
		*expiringProfileKeyCredential,
	)
	if err != nil {
		log.Err(err).Msg("failed creating expiring profile key credential presentation for addMember")
		return nil, nil, err
	}
	encryptedMember := signalpb.Member{
		Presentation: *presentation,
		Role:         signalpb.Member_Role(member.Role),
	}
	return &encryptedMember, nil, nil
}

func (cli *Client) encryptPendingMember(ctx context.Context, pendingMember *PendingMember, groupSecretParams *libsignalgo.GroupSecretParams) (*signalpb.MemberPendingProfileKey, error) {
	log := zerolog.Ctx(ctx)
	encryptedUserID, err := groupSecretParams.EncryptServiceID(pendingMember.ServiceID)
	if err != nil {
		log.Err(err).Msg("Encrypt UserId error for addPendingMember")
		return nil, err
	}
	encryptedAddedByUserID, err := groupSecretParams.EncryptServiceID(libsignalgo.NewACIServiceID(pendingMember.AddedByUserID))
	if err != nil {
		log.Err(err).Msg("Encrypt AddedByUserId error for addPendingMember")
		return nil, err
	}
	encryptedPendingMember := signalpb.MemberPendingProfileKey{
		AddedByUserId: encryptedAddedByUserID[:],
		Member: &signalpb.Member{
			UserId: encryptedUserID[:],
			Role:   signalpb.Member_Role(pendingMember.Role),
		},
	}
	return &encryptedPendingMember, nil
}

var (
	NoContentError               = RespError{Err: "NoContentError"}
	GroupPatchNotAcceptedError   = RespError{Err: "GroupPatchNotAcceptedError"}
	ConflictError                = RespError{Err: "ConflictError"}
	AuthorizationFailedError     = RespError{Err: "AuthorizationFailedError"}
	NotFoundError                = RespError{Err: "NotFoundError"}
	ContactManifestMismatchError = RespError{Err: "ContactManifestMismatchError"}
	RateLimitError               = RespError{Err: "RateLimitError"}
	DeprecatedVersionError       = RespError{Err: "DeprecatedVersionError"}
	GroupExistsError             = RespError{Err: "GroupExistsError"}
)

type RespError struct {
	Err string
}

func (e RespError) Error() string {
	return e.Err
}

func (cli *Client) patchGroup(ctx context.Context, groupChange *signalpb.GroupChange_Actions, groupMasterKey types.SerializedGroupMasterKey, groupLinkPassword []byte) (*signalpb.GroupChangeResponse, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "patchGroup").Logger()
	groupAuth, err := cli.GetAuthorizationForToday(ctx, masterKeyToBytes(groupMasterKey))
	if err != nil {
		log.Err(err).Msg("Failed to get Authorization for today")
		return nil, err
	}
	var path string
	if groupLinkPassword == nil {
		path = "/v2/groups/"
	} else {
		path = fmt.Sprintf("/v2/groups/?inviteLinkPassword=%s", base64.StdEncoding.EncodeToString(groupLinkPassword))
	}
	requestBody, err := proto.Marshal(groupChange)
	if err != nil {
		log.Err(err).Msg("Failed to marshal request")
		return nil, err
	}
	opts := &web.HTTPReqOpt{
		Username:    &groupAuth.Username,
		Password:    &groupAuth.Password,
		ContentType: web.ContentTypeProtobuf,
		Body:        requestBody,
	}
	resp, err := web.SendHTTPRequest(ctx, web.StorageHostname, http.MethodPatch, path, opts)
	defer web.CloseBody(resp)
	if err != nil {
		return nil, fmt.Errorf("SendRequest error: %w", err)
	}
	switch resp.StatusCode {
	case http.StatusNoContent:
		return nil, NoContentError
	case http.StatusBadRequest:
		return nil, GroupPatchNotAcceptedError
	case http.StatusForbidden:
		return nil, AuthorizationFailedError
	case http.StatusNotFound:
		return nil, NotFoundError
	case http.StatusConflict:
		if resp.Body != nil {
			return nil, ContactManifestMismatchError
		} else {
			return nil, ConflictError
		}
	case http.StatusTooManyRequests:
		return nil, RateLimitError
	case 499:
		return nil, DeprecatedVersionError
	}
	if resp.Body == nil {
		return nil, errors.New("no response body")
	}
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read storage manifest response: %w", err)
	}
	var changeResp signalpb.GroupChangeResponse
	err = proto.Unmarshal(body, &changeResp)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal signed groupChange: %w", err)
	}
	return &changeResp, nil
}

func (cli *Client) UpdateGroup(ctx context.Context, groupChange *GroupChange, gid types.GroupIdentifier) (uint32, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "UpdateGroup").Logger()
	groupMasterKey, err := cli.Store.GroupStore.MasterKeyFromGroupIdentifier(ctx, gid)
	if err != nil {
		return 0, fmt.Errorf("failed to get master key for group: %w", err)
	}
	groupChange.GroupMasterKey = groupMasterKey
	masterKeyBytes := masterKeyToBytes(groupMasterKey)
	var refetchedAddMemberCredentials bool
	var signedGroupChange *signalpb.GroupChangeResponse
	group, _, err := cli.RetrieveGroupByID(ctx, gid, 0)
	if err != nil {
		return 0, fmt.Errorf("failed to fetch group info to update: %w", err)
	}
	if group.InviteLinkPassword == nil && groupChange.ModifyAddFromInviteLinkAccess != nil && groupChange.ModifyInviteLinkPassword != nil {
		groupChange.ModifyInviteLinkPassword = ptr.Ptr(GenerateInviteLinkPassword())
	}
	groupChange.Revision = group.Revision + 1
	for attempt := 0; ; attempt++ {
		signedGroupChange, err = cli.EncryptAndSignGroupChange(ctx, groupChange)
		if err == nil {
			break
		} else if attempt >= 5 {
			return 0, fmt.Errorf("failed to encrypt and sign group change after multiple retries: %w", err)
		} else if errors.Is(err, GroupPatchNotAcceptedError) {
			log.Err(err).Msg("Failed to apply group change, retrying...")
			if len(groupChange.AddMembers) > 0 && !refetchedAddMemberCredentials {
				refetchedAddMemberCredentials = true
				// change = refetchAddMemberCredentials(change); TODO
			} else {
				return 0, fmt.Errorf("failed to update group: %w", err)
			}
		} else if errors.Is(err, ConflictError) {
			cli.GroupCache.Delete(gid)
			group, _, err = cli.RetrieveGroupByID(ctx, gid, 0)
			if err != nil {
				return 0, fmt.Errorf("failed to fetch group after conflict: %w", err)
			}
			groupChange.resolveConflict(group)
			if groupChange.isEmpty() {
				log.Debug().Msg("Change is empty after conflict resolution")
			}
			groupChange.Revision = group.Revision + 1
		} else {
			return 0, fmt.Errorf("unknown error encrypting and signing group change: %w", err)
		}
	}
	if signedGroupChange == nil {
		return 0, fmt.Errorf("no signed group change returned: %w", err)
	}
	err = cli.GroupCache.ApplyUpdate(groupChange, signedGroupChange.GroupSendEndorsementsResponse)
	if err != nil {
		log.Err(err).Msg("Failed to apply group change to cache")
	}
	groupChangeBytes, err := proto.Marshal(signedGroupChange.GroupChange)
	if err != nil {
		return 0, fmt.Errorf("failed to marshal signed group change: %w", err)
	}
	groupContext := &signalpb.GroupContextV2{
		Revision:    &groupChange.Revision,
		GroupChange: groupChangeBytes,
		MasterKey:   masterKeyBytes[:],
	}
	_, err = cli.SendGroupUpdate(ctx, group, groupContext, groupChange)
	if err != nil {
		log.Err(err).Msg("Error sending GroupChange to group members")
	}
	return groupChange.Revision, nil
}

func (cli *Client) EncryptGroup(ctx context.Context, decryptedGroup *Group, groupSecretParams libsignalgo.GroupSecretParams) (*signalpb.Group, error) {
	log := zerolog.Ctx(ctx)
	attributeBlob := signalpb.GroupAttributeBlob{Content: &signalpb.GroupAttributeBlob_Title{Title: decryptedGroup.Title}}
	encryptedTitle, err := encryptBlobIntoGroupProperty(groupSecretParams, &attributeBlob)
	if err != nil {
		log.Err(err).Msg("Could not get encrypt Title")
		return nil, err
	}
	groupPublicParams, err := groupSecretParams.GetPublicParams()
	if err != nil {
		log.Err(err).Msg("Couldn't get public params from GroupSecretParams")
		return nil, err
	}
	encryptedGroup := &signalpb.Group{
		PublicKey:         groupPublicParams[:],
		Title:             *encryptedTitle,
		AvatarUrl:         decryptedGroup.AvatarPath,
		AnnouncementsOnly: decryptedGroup.AnnouncementsOnly,
		Version:           0,
	}
	if decryptedGroup.Description != "" {
		attributeBlob := signalpb.GroupAttributeBlob{Content: &signalpb.GroupAttributeBlob_DescriptionText{DescriptionText: decryptedGroup.Description}}
		encryptedDescription, err := encryptBlobIntoGroupProperty(groupSecretParams, &attributeBlob)
		if err != nil {
			log.Err(err).Msg("Could not get encrypt Description")
			return nil, err
		}
		encryptedGroup.Description = *encryptedDescription
	}
	if decryptedGroup.AccessControl != nil {
		encryptedGroup.AccessControl = &signalpb.AccessControl{
			Members:           signalpb.AccessControl_AccessRequired(decryptedGroup.AccessControl.Members),
			Attributes:        signalpb.AccessControl_AccessRequired(decryptedGroup.AccessControl.Attributes),
			AddFromInviteLink: signalpb.AccessControl_AccessRequired(decryptedGroup.AccessControl.AddFromInviteLink),
		}
		if decryptedGroup.AccessControl.AddFromInviteLink != AccessControl_UNSATISFIABLE {
			encryptedGroup.InviteLinkPassword = random.Bytes(16)
		}
	}
	for _, member := range decryptedGroup.Members {
		encryptedMember, encryptedPendingMember, err := cli.encryptMember(ctx, member, &groupSecretParams)
		if err != nil {
			log.Err(err).Msg("Failed to encrypt GroupMember")
		}
		if encryptedMember != nil {
			encryptedGroup.Members = append(encryptedGroup.Members, encryptedMember)
		} else {
			encryptedGroup.MembersPendingProfileKey = append(encryptedGroup.MembersPendingProfileKey, encryptedPendingMember)
		}
	}
	for _, pendingMember := range decryptedGroup.PendingMembers {
		encryptedPendingMember, err := cli.encryptPendingMember(ctx, pendingMember, &groupSecretParams)
		if err != nil {
			log.Err(err).Msg("Failed to encrypt pendingMember")
			return nil, err
		}
		encryptedGroup.MembersPendingProfileKey = append(encryptedGroup.MembersPendingProfileKey, encryptedPendingMember)
	}
	return encryptedGroup, nil
}

func PrepareGroupCreation(decryptedGroup *Group) (libsignalgo.GroupMasterKey, error) {
	var masterKeyBytes libsignalgo.GroupMasterKey
	if decryptedGroup.GroupMasterKey == "" {
		masterKeyBytes = libsignalgo.GroupMasterKey(random.Bytes(32))
		decryptedGroup.GroupMasterKey = masterKeyFromBytes(masterKeyBytes)
	} else {
		masterKeyBytes = masterKeyToBytes(decryptedGroup.GroupMasterKey)
	}
	if decryptedGroup.GroupIdentifier == "" {
		var err error
		decryptedGroup.GroupIdentifier, err = groupIdentifierFromMasterKey(decryptedGroup.GroupMasterKey)
		if err != nil {
			return masterKeyBytes, err
		}
	}
	return masterKeyBytes, nil
}

func (cli *Client) createGroupOnServer(ctx context.Context, decryptedGroup *Group, avatarBytes []byte) (*Group, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "CreateGroupOnServer").Logger()
	masterKeyBytes, err := PrepareGroupCreation(decryptedGroup)
	if err != nil {
		return nil, err
	}
	err = cli.Store.GroupStore.StoreMasterKey(ctx, decryptedGroup.GroupIdentifier, decryptedGroup.GroupMasterKey)
	if err != nil {
		return nil, fmt.Errorf("StoreMasterKey error: %w", err)
	}
	groupSecretParams, err := libsignalgo.DeriveGroupSecretParamsFromMasterKey(masterKeyBytes)
	if err != nil {
		log.Err(err).Msg("DeriveGroupSecretParamsFromMasterKey error")
		return nil, err
	}
	if len(avatarBytes) > 0 {
		avatarPath, err := cli.UploadGroupAvatar(ctx, avatarBytes, decryptedGroup.GroupIdentifier)
		if err != nil {
			log.Err(err).Msg("Failed to upload group avatar")
			return nil, err
		}
		decryptedGroup.AvatarPath = avatarPath
	}
	encryptedGroup, err := cli.EncryptGroup(ctx, decryptedGroup, groupSecretParams)
	if err != nil {
		log.Err(err).Msg("Failed to encrypt group")
		return nil, err
	}
	groupAuth, err := cli.GetAuthorizationForToday(ctx, masterKeyBytes)
	if err != nil {
		log.Err(err).Msg("Failed to get Authorization for today")
		return nil, err
	}
	path := "/v2/groups/"
	requestBody, err := proto.Marshal(encryptedGroup)
	if err != nil {
		log.Err(err).Msg("Failed to marshal request")
		return nil, err
	}
	opts := &web.HTTPReqOpt{
		Username:    &groupAuth.Username,
		Password:    &groupAuth.Password,
		ContentType: web.ContentTypeProtobuf,
		Body:        requestBody,
	}
	resp, err := web.SendHTTPRequest(ctx, web.StorageHostname, http.MethodPut, path, opts)
	defer web.CloseBody(resp)
	if err != nil {
		return nil, fmt.Errorf("SendRequest error: %w", err)
	}
	switch resp.StatusCode {
	case http.StatusNoContent:
		return nil, NoContentError
	case http.StatusForbidden:
		return nil, AuthorizationFailedError
	case http.StatusNotFound:
		return nil, NotFoundError
	case http.StatusConflict:
		return nil, GroupExistsError
	case http.StatusTooManyRequests:
		return nil, RateLimitError
	case 499:
		return nil, DeprecatedVersionError
	case http.StatusBadRequest:
		return nil, fmt.Errorf("failed to put new group: bad request")
	}
	return cli.parseGroupResponse(ctx, resp, decryptedGroup.GroupMasterKey)
}

func GenerateInviteLinkPassword() types.SerializedInviteLinkPassword {
	return InviteLinkPasswordFromBytes(random.Bytes(16))
}

func (cli *Client) CreateGroup(ctx context.Context, decryptedGroup *Group, avatarBytes []byte) (*Group, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "CreateGroup").Logger()
	group, err := cli.createGroupOnServer(ctx, decryptedGroup, avatarBytes)
	if err != nil {
		log.Err(err).Msg("Error creating group on server")
		return nil, err
	}
	masterKeyBytes := masterKeyToBytes(group.GroupMasterKey)
	groupContext := &signalpb.GroupContextV2{Revision: &group.Revision, MasterKey: masterKeyBytes[:]}
	_, err = cli.SendGroupUpdate(ctx, group, groupContext, nil)
	if err != nil {
		log.Err(err).Msg("Error sending GroupUpdate to group members")
		return nil, err
	}
	return group, nil
}

func (cli *Client) GetGroupHistoryPage(ctx context.Context, gid types.GroupIdentifier, fromRevision uint32, includeFirstState bool) ([]*GroupChangeState, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "GetGroupHistoryPage").Logger()
	groupMasterKey, err := cli.Store.GroupStore.MasterKeyFromGroupIdentifier(ctx, gid)
	if err != nil {
		log.Err(err).Msg("Failed to get group master key")
		return nil, err
	}
	if groupMasterKey == "" {
		return nil, fmt.Errorf("No group master key found for group identifier %s", gid)
	}
	masterKeyBytes := masterKeyToBytes(groupMasterKey)
	groupAuth, err := cli.GetAuthorizationForToday(ctx, masterKeyBytes)
	if err != nil {
		return nil, err
	}
	opts := &web.HTTPReqOpt{
		Username:    &groupAuth.Username,
		Password:    &groupAuth.Password,
		ContentType: web.ContentTypeProtobuf,
		Headers: map[string]string{
			// TODO actually cache the data and provide real expiry timestamp
			"Cached-Send-Endorsements": "0",
		},
	}
	// highest known epoch seems to always be 5, but that may change in the future. includeLastState is always false
	path := fmt.Sprintf("/v2/groups/logs/%d?maxSupportedChangeEpoch=%d&includeFirstState=%t&includeLastState=false", fromRevision, 5, includeFirstState)
	response, err := web.SendHTTPRequest(ctx, web.StorageHostname, http.MethodGet, path, opts)
	defer web.CloseBody(response)
	if err != nil {
		return nil, err
	}
	if response.StatusCode != 200 {
		return nil, fmt.Errorf("fetchGroupByID SendHTTPRequest bad status: %d", response.StatusCode)
	}
	var encryptedGroupChanges signalpb.GroupChanges
	groupChangesBytes, err := io.ReadAll(response.Body)
	if err != nil {
		return nil, err
	}
	err = proto.Unmarshal(groupChangesBytes, &encryptedGroupChanges)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal group: %w", err)
	}

	groupChanges, err := cli.decryptGroupChanges(ctx, &encryptedGroupChanges, groupMasterKey)
	if err != nil {
		return nil, fmt.Errorf("failed to decrypt group: %w", err)
	}
	return groupChanges, nil
}

func (cli *Client) decryptGroupChanges(ctx context.Context, encryptedGroupChanges *signalpb.GroupChanges, groupMasterKey types.SerializedGroupMasterKey) ([]*GroupChangeState, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "decryptGroupChanges").Logger()
	var groupChanges []*GroupChangeState
	for _, groupChangeState := range encryptedGroupChanges.GroupChanges {
		var group *Group
		var err error
		// GroupState == nil is normal, except for first and last, depending on the parameters it was fetched with
		if groupChangeState.GroupState != nil {
			group, err = decryptGroup(ctx, groupChangeState.GroupState, groupMasterKey)
			if err != nil {
				log.Err(err).Msg("Failed to decrypt Group")
				return nil, err
			}
		}
		var groupChange *GroupChange
		// GroupChange shouldn't be nil - if it is, something will probably go wrong
		if groupChangeState.GroupChange == nil {
			return nil, fmt.Errorf("received group change state without group change")
		}
		groupChange, err = cli.decryptGroupChange(ctx, groupChangeState.GroupChange, groupMasterKey, false)
		if err != nil {
			log.Err(err).Msg("Failed to decrypt GroupChange")
			return nil, err
		}
		groupChanges = append(groupChanges, &GroupChangeState{
			GroupState:  group,
			GroupChange: groupChange,
		})
	}
	return groupChanges, nil
}
