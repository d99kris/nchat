// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Sumner Evans
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
#include <./libsignal-ffi.h>

extern void signal_log_callback(void *ctx, SignalLogLevel level, char *file, uint32_t line, char *message);
extern void signal_log_flush_callback(void *ctx);
*/
import "C"
import (
	"unsafe"
)

// ffiLogger is the global logger object.
var ffiLogger Logger

//export signal_log_callback
func signal_log_callback(ctx unsafe.Pointer, level C.SignalLogLevel, file *C.char, line C.uint32_t, message *C.char) {
	ffiLogger.Log(LogLevel(int(level)), C.GoString(file), uint(line), C.GoString(message))
}

//export signal_log_flush_callback
func signal_log_flush_callback(ctx unsafe.Pointer) {
	ffiLogger.Flush()
}

type LogLevel int

const (
	LogLevelError LogLevel = iota + 1
	LogLevelWarn
	LogLevelInfo
	LogLevelDebug
	LogLevelTrace
)

type Logger interface {
	Log(level LogLevel, file string, line uint, message string)
	Flush()
}

func InitLogger(level LogLevel, logger Logger) {
	ffiLogger = logger
	C.signal_init_logger(C.SignalLogLevel(level), C.SignalFfiLogger{
		log:   C.SignalLogCallback(C.signal_log_callback),
		flush: C.SignalLogFlushCallback(C.signal_log_flush_callback),
	})
}
