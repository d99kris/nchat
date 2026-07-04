module github.com/d99kris/nchat/lib/sgchat/go

go 1.25.0

require go.mau.fi/mautrix-signal v0.0.0

replace go.mau.fi/mautrix-signal => ./ext/signal

require (
	github.com/google/uuid v1.6.0
	github.com/mattn/go-sqlite3 v1.14.45
	github.com/mdp/qrterminal v1.0.1
	github.com/rs/zerolog v1.35.1
	github.com/skip2/go-qrcode v0.0.0-20200617195104-da1b6568686e
	go.mau.fi/util v0.9.10
	google.golang.org/protobuf v1.36.11
)

require (
	github.com/coder/websocket v1.8.15 // indirect
	github.com/mattn/go-colorable v0.1.14 // indirect
	github.com/mattn/go-isatty v0.0.20 // indirect
	github.com/mattn/go-pointer v0.0.1 // indirect
	github.com/petermattis/goid v0.0.0-20260330135022-df67b199bc81 // indirect
	github.com/tidwall/gjson v1.19.0 // indirect
	github.com/tidwall/match v1.2.0 // indirect
	github.com/tidwall/pretty v1.2.1 // indirect
	golang.org/x/crypto v0.53.0 // indirect
	golang.org/x/exp v0.0.0-20260611194520-c48552f49976 // indirect
	golang.org/x/sync v0.21.0 // indirect
	golang.org/x/sys v0.46.0 // indirect
	rsc.io/qr v0.2.0 // indirect
)
