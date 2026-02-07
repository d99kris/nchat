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

package libsignalgo_test

import (
	"os"

	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

type FFILogger struct{}

func (FFILogger) Enabled(target string, level libsignalgo.LogLevel) bool { return true }

func (FFILogger) Log(level libsignalgo.LogLevel, file string, line uint, message string) {
	var evt *zerolog.Event
	switch level {
	case libsignalgo.LogLevelError:
		evt = log.Error()
	case libsignalgo.LogLevelWarn:
		evt = log.Warn()
	case libsignalgo.LogLevelInfo:
		evt = log.Info()
	case libsignalgo.LogLevelDebug:
		evt = log.Debug()
	case libsignalgo.LogLevelTrace:
		evt = log.Trace()
	default:
		panic("invalid log level from libsignal")
	}

	evt.Str("component", "libsignal").
		Str("file", file).
		Uint("line", line).
		Msg(message)
}

func (FFILogger) Flush() {}

var loggingSetup = false

func setupLogging() {
	if !loggingSetup {
		log.Logger = log.Output(zerolog.ConsoleWriter{Out: os.Stderr})
		libsignalgo.InitLogger(libsignalgo.LogLevelTrace, FFILogger{})
		loggingSetup = true
	}
}
