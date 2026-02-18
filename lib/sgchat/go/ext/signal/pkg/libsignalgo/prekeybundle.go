// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Sumner Evans
// Copyright (C) 2025 Tulir Asokan
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

package libsignalgo

/*
#include "./libsignal-ffi.h"
*/
import "C"
import (
	"context"
	"runtime"
	"time"
)

func ProcessPreKeyBundle(ctx context.Context, bundle *PreKeyBundle, forAddress *Address, sessionStore SessionStore, identityStore IdentityKeyStore) error {
	callbackCtx := NewCallbackContext(ctx)
	defer callbackCtx.Unref()
	var now C.uint64_t = C.uint64_t(time.Now().Unix())
	signalFfiError := C.signal_process_prekey_bundle(
		bundle.constPtr(),
		forAddress.constPtr(),
		callbackCtx.wrapSessionStore(sessionStore),
		callbackCtx.wrapIdentityKeyStore(identityStore),
		now,
	)
	runtime.KeepAlive(bundle)
	runtime.KeepAlive(forAddress)
	return callbackCtx.wrapError(signalFfiError)
}

type PreKeyBundle struct {
	nc  noCopy
	ptr *C.SignalPreKeyBundle
}

func wrapPreKeyBundle(ptr *C.SignalPreKeyBundle) *PreKeyBundle {
	bundle := &PreKeyBundle{ptr: ptr}
	runtime.SetFinalizer(bundle, (*PreKeyBundle).Destroy)
	return bundle
}

func NewPreKeyBundle(
	registrationID uint32,
	deviceID uint32,
	preKeyID uint32,
	preKey *PublicKey,
	signedPreKeyID uint32,
	signedPreKey *PublicKey,
	signedPreKeySignature []byte,
	kyberPreKeyID uint32,
	kyberPreKey *KyberPublicKey,
	kyberPreKeySignature []byte,
	identityKey *IdentityKey,
) (*PreKeyBundle, error) {
	var pkb C.SignalMutPointerPreKeyBundle
	var zero uint32 = 0
	var kyberSignatureBuffer = EmptyBorrowedBuffer()
	if preKey == nil {
		preKey = &PublicKey{ptr: nil}
		preKeyID = ^zero
	}
	if kyberPreKey == nil {
		kyberPreKey = &KyberPublicKey{ptr: nil}
		kyberPreKeyID = ^zero
	} else {
		kyberSignatureBuffer = BytesToBuffer(kyberPreKeySignature)
	}
	signalFfiError := C.signal_pre_key_bundle_new(
		&pkb,
		C.uint32_t(registrationID),
		C.uint32_t(deviceID),
		C.uint32_t(preKeyID),
		preKey.constPtr(),
		C.uint32_t(signedPreKeyID),
		signedPreKey.constPtr(),
		BytesToBuffer(signedPreKeySignature),
		identityKey.publicKey.constPtr(),
		C.uint32_t(kyberPreKeyID),
		kyberPreKey.constPtr(),
		kyberSignatureBuffer,
	)
	runtime.KeepAlive(preKey)
	runtime.KeepAlive(signedPreKey)
	runtime.KeepAlive(signedPreKeySignature)
	runtime.KeepAlive(kyberPreKey)
	runtime.KeepAlive(kyberPreKeySignature)
	runtime.KeepAlive(identityKey)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPreKeyBundle(pkb.raw), nil
}

func (pkb *PreKeyBundle) mutPtr() C.SignalMutPointerPreKeyBundle {
	return C.SignalMutPointerPreKeyBundle{pkb.ptr}
}

func (pkb *PreKeyBundle) constPtr() C.SignalConstPointerPreKeyBundle {
	return C.SignalConstPointerPreKeyBundle{pkb.ptr}
}

func (pkb *PreKeyBundle) Clone() (*PreKeyBundle, error) {
	var cloned C.SignalMutPointerPreKeyBundle
	signalFfiError := C.signal_pre_key_bundle_clone(&cloned, pkb.constPtr())
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	runtime.KeepAlive(pkb)
	return wrapPreKeyBundle(cloned.raw), nil
}

func (pkb *PreKeyBundle) Destroy() error {
	pkb.CancelFinalizer()
	return wrapError(C.signal_pre_key_bundle_destroy(pkb.mutPtr()))
}

func (pkb *PreKeyBundle) CancelFinalizer() {
	runtime.SetFinalizer(pkb, nil)
}

func (pkb *PreKeyBundle) GetIdentityKey() (*IdentityKey, error) {
	var pk C.SignalMutPointerPublicKey
	signalFfiError := C.signal_pre_key_bundle_get_identity_key(&pk, pkb.constPtr())
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	runtime.KeepAlive(pkb)
	return NewIdentityKeyFromPublicKey(wrapPublicKey(pk.raw))
}
