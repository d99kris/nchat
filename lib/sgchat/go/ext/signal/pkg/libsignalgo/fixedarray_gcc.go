//go:build !(darwin || android || ios || (windows && arm64))

package libsignalgo

/*
#include "./libsignal-ffi.h"
*/
import "C"

// Hack for https://github.com/golang/go/issues/7270
// The clang version is more correct, but doesn't work with gcc.
type cFixedArray15Compat = *[15]C.uint8_t
type cFixedArray16Compat = *[16]C.uint8_t
type cFixedArray17Compat = *[17]C.uint8_t
type cFixedArray32Compat = *[32]C.uint8_t
type cFixedArray64Compat = *[64]C.uint8_t
type cFixedArray65Compat = *[65]C.uint8_t
type cFixedArray97Compat = *[97]C.uint8_t
type cFixedArray129Compat = *[129]C.uint8_t
type cFixedArray153Compat = *[153]C.uint8_t
type cFixedArray177Compat = *[177]C.uint8_t
type cFixedArray289Compat = *[289]C.uint8_t
type cFixedArray329Compat = *[329]C.uint8_t
type cFixedArray409Compat = *[409]C.uint8_t
type cFixedArray473Compat = *[473]C.uint8_t
type cFixedArray497Compat = *[497]C.uint8_t
