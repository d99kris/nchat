package store

import (
	"context"
	"fmt"
	"sync"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/dbutil"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

type sqlStore struct {
	*Container
	AccountID uuid.UUID

	contactLock sync.Mutex

	blockCacheLock sync.RWMutex
	blockCache     map[uuid.UUID]bool
}

type scopedSQLStore struct {
	*Container
	AccountID uuid.UUID
	ServiceID libsignalgo.ServiceID
}

type DeviceData struct {
	ACIIdentityKeyPair *libsignalgo.IdentityKeyPair
	PNIIdentityKeyPair *libsignalgo.IdentityKeyPair
	ACIRegistrationID  int
	PNIRegistrationID  int
	ACI                uuid.UUID
	PNI                uuid.UUID
	DeviceID           int
	Number             string
	Password           string
	MasterKey          []byte
	AccountRecord      *signalpb.AccountRecord
	AccountEntropyPool libsignalgo.AccountEntropyPool
	EphemeralBackupKey *libsignalgo.BackupKey
	MediaRootBackupKey *libsignalgo.BackupKey
}

func (d *DeviceData) ACIServiceID() libsignalgo.ServiceID {
	return libsignalgo.NewACIServiceID(d.ACI)
}

func (d *DeviceData) PNIServiceID() libsignalgo.ServiceID {
	return libsignalgo.NewPNIServiceID(d.PNI)
}

func (d *DeviceData) BasicAuthCreds() (string, string) {
	username := fmt.Sprintf("%s.%d", d.ACI, d.DeviceID)
	password := d.Password
	return username, password
}

// Device is a wrapper for a signalmeow session, including device data,
// and interfaces for operating on the DB within the session.
type Device struct {
	DeviceData

	// NOTE: when adding a new store interface, make sure to assing it below
	// (search for "innerStore" further down in this file)

	// libsignalgo store interfaces
	ACIPreKeyStore   PreKeyStore
	PNIPreKeyStore   PreKeyStore
	ACISessionStore  SessionStore
	PNISessionStore  SessionStore
	ACIIdentityStore libsignalgo.IdentityKeyStore
	PNIIdentityStore libsignalgo.IdentityKeyStore
	IdentityKeyStore IdentityKeyStore
	SenderKeyStore   SenderKeyStore

	GroupStore     GroupStore
	RecipientStore RecipientStore
	DeviceStore    DeviceStore
	BackupStore    BackupStore
	EventBuffer    EventBuffer

	sqlStore *sqlStore
	db       *dbutil.Database
}

type contextKey int64

const (
	contextKeyContactLock contextKey = 1
)

func (d *Device) DoContactTxn(ctx context.Context, fn func(context.Context) error) error {
	d.sqlStore.contactLock.Lock()
	defer d.sqlStore.contactLock.Unlock()
	ctx = context.WithValue(ctx, dbutil.ContextKeyDoTxnCallerSkip, 1)
	ctx = context.WithValue(ctx, contextKeyContactLock, true)
	return d.db.DoTxn(ctx, nil, fn)
}

func (d *Device) DoDecryptionTxn(ctx context.Context, fn func(context.Context) error) error {
	ctx = context.WithValue(ctx, dbutil.ContextKeyDoTxnCallerSkip, 2)
	return d.db.DoTxn(ctx, nil, fn)
}

func (d *Device) ClearDeviceKeys(ctx context.Context) error {
	// We need to clear out keys associated with the Signal device that no longer has valid credentials
	if d == nil {
		zerolog.Ctx(ctx).Warn().Msg("ClearDeviceKeys called with nil device")
		return nil
	}
	err := d.ACIPreKeyStore.DeleteAllPreKeys(ctx)
	if err != nil {
		return err
	}
	err = d.ACISessionStore.RemoveAllSessions(ctx)
	return err
}

func (d *Device) IsDeviceLoggedIn() bool {
	return d != nil &&
		d.ACI != uuid.Nil &&
		d.DeviceID != 0 &&
		d.Password != ""
}

func (d *Device) ClearPassword(ctx context.Context) error {
	d.Password = ""
	return d.DeviceStore.PutDevice(ctx, &d.DeviceData)
}

func (d *Device) PreKeyStore(serviceID libsignalgo.ServiceID) PreKeyStore {
	if serviceID == d.ACIServiceID() {
		return d.ACIPreKeyStore
	} else if serviceID == d.PNIServiceID() {
		return d.PNIPreKeyStore
	}
	return nil
}

func (d *Device) SessionStore(serviceID libsignalgo.ServiceID) SessionStore {
	if serviceID == d.ACIServiceID() {
		return d.ACISessionStore
	} else if serviceID == d.PNIServiceID() {
		return d.PNISessionStore
	}
	return nil
}

func (d *Device) IdentityStore(serviceID libsignalgo.ServiceID) libsignalgo.IdentityKeyStore {
	if serviceID == d.ACIServiceID() {
		return d.ACIIdentityStore
	} else if serviceID == d.PNIServiceID() {
		return d.PNIIdentityStore
	}
	return nil
}
