//go:build darwin || android || ios || (windows && arm64)

package libsignalgo

/*
#include "./libsignal-ffi.h"
#include <stdlib.h>
*/
import "C"

type cPNIType = *C.SignalServiceIdFixedWidthBinaryBytes
