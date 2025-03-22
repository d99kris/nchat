module github.com/d99kris/nchat/lib/wmchat/go

require go.mau.fi/whatsmeow v0.0.0

replace go.mau.fi/whatsmeow => ./ext/whatsmeow

require (
	github.com/mattn/go-sqlite3 v1.14.24
	github.com/mdp/qrterminal v1.0.1
	github.com/skip2/go-qrcode v0.0.0-20200617195104-da1b6568686e
	go.mau.fi/libsignal v0.1.2
	google.golang.org/protobuf v1.36.5
)

require (
	filippo.io/edwards25519 v1.1.0 // indirect
	github.com/google/go-cmp v0.6.0 // indirect
	github.com/google/uuid v1.6.0 // indirect
	github.com/gorilla/websocket v1.5.0 // indirect
	github.com/mattn/go-colorable v0.1.13 // indirect
	github.com/mattn/go-isatty v0.0.19 // indirect
	github.com/rs/zerolog v1.33.0 // indirect
	go.mau.fi/util v0.8.5 // indirect
	golang.org/x/crypto v0.33.0 // indirect
	golang.org/x/net v0.35.0 // indirect
	golang.org/x/sys v0.30.0 // indirect
	rsc.io/qr v0.2.0 // indirect
)

go 1.23.0

toolchain go1.23.6
