//go:build !(darwin || android || ios || (windows && arm64))

package libsignalgo

/*
#include "./libsignal-ffi.h"
#include <stdlib.h>
*/
import "C"

// Hack for https://github.com/golang/go/issues/7270
// The clang version is more correct, but doesn't work with gcc

type cPNIType = *[17]C.uint8_t
