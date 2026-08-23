//go:build darwin || android || ios || (windows && arm64)

package libsignalgo

/*
#include "./libsignal-ffi.h"
*/
import "C"

type cFixedArray15Compat = *C.SignalType_FixedArray15_uint8_t
type cFixedArray16Compat = *C.SignalType_FixedArray16_uint8_t
type cFixedArray17Compat = *C.SignalType_FixedArray17_uint8_t
type cFixedArray32Compat = *C.SignalType_FixedArray32_uint8_t
type cFixedArray64Compat = *C.SignalType_FixedArray64_uint8_t
type cFixedArray65Compat = *C.SignalType_FixedArray65_uint8_t
type cFixedArray97Compat = *C.SignalType_FixedArray97_uint8_t
type cFixedArray129Compat = *C.SignalType_FixedArray129_uint8_t
type cFixedArray153Compat = *C.SignalType_FixedArray153_uint8_t
type cFixedArray177Compat = *C.SignalType_FixedArray177_uint8_t
type cFixedArray289Compat = *C.SignalType_FixedArray289_uint8_t
type cFixedArray329Compat = *C.SignalType_FixedArray329_uint8_t
type cFixedArray409Compat = *C.SignalType_FixedArray409_uint8_t
type cFixedArray473Compat = *C.SignalType_FixedArray473_uint8_t
type cFixedArray497Compat = *C.SignalType_FixedArray497_uint8_t
