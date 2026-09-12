package libsignalgo

/*
#cgo LDFLAGS: -lsignal_ffi -ldl -lm -lz -lstdc++
*/
import "C"

import (
	"go.mau.fi/mautrix-signal/pkg/libsignalgo/signalversion"
)

const Version = signalversion.Version
