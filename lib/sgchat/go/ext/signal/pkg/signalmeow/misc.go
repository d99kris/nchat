// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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
	_ "embed"

	"github.com/rs/zerolog"
	"go.mau.fi/util/exerrors"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

var loggingSetup = false

func SetLogger(l zerolog.Logger) {
	if loggingSetup {
		return
	}
	libsignalgo.InitLogger(libsignalgo.LogLevelInfo, FFILogger{
		logger: l,
	})
	loggingSetup = true
}

type FFILogger struct {
	logger zerolog.Logger
}

func (l FFILogger) Log(level libsignalgo.LogLevel, file string, line uint, message string) {
	var evt *zerolog.Event
	switch level {
	case libsignalgo.LogLevelError:
		evt = l.logger.Error()
	case libsignalgo.LogLevelWarn:
		evt = l.logger.Warn()
	case libsignalgo.LogLevelInfo:
		evt = l.logger.Info()
	case libsignalgo.LogLevelDebug:
		evt = l.logger.Debug()
	case libsignalgo.LogLevelTrace:
		evt = l.logger.Trace()
	default:
		panic("invalid log level from libsignal")
	}

	evt.Str("component", "libsignal").
		Str("file", file).
		Uint("line", line).
		Msg(message)
}

func (FFILogger) Flush() {}

// Ensure FFILogger implements the Logger interface
var _ libsignalgo.Logger = FFILogger{}

//go:embed prod-server-public-params.dat
var prodServerPublicParamsSlice []byte
var prodServerPublicParams *libsignalgo.ServerPublicParams

func init() {
	prodServerPublicParams = exerrors.Must(libsignalgo.DeserializeServerPublicParams(prodServerPublicParamsSlice))
}
