module github.com/d99kris/nchat/lib/wmchat/go

require go.mau.fi/whatsmeow v0.0.0

replace go.mau.fi/whatsmeow => ./ext/whatsmeow

require (
	github.com/mattn/go-sqlite3 v1.14.22
	github.com/mdp/qrterminal v1.0.1
	github.com/mdp/qrterminal/v3 v3.0.0
	github.com/skip2/go-qrcode v0.0.0-20200617195104-da1b6568686e
	go.mau.fi/libsignal v0.1.0
	google.golang.org/protobuf v1.33.0
)

require (
	filippo.io/edwards25519 v1.0.0 // indirect
	github.com/DATA-DOG/go-sqlmock v1.5.2 // indirect
	github.com/coreos/go-systemd/v22 v22.5.0 // indirect
	github.com/davecgh/go-spew v1.1.1 // indirect
	github.com/godbus/dbus/v5 v5.0.4 // indirect
	github.com/golang/protobuf v1.5.0 // indirect
	github.com/google/go-cmp v0.5.8 // indirect
	github.com/google/uuid v1.6.0 // indirect
	github.com/gorilla/websocket v1.5.0 // indirect
	github.com/kisielk/sqlstruct v0.0.0-20201105191214-5f3e10d3ab46 // indirect
	github.com/mattn/go-colorable v0.1.13 // indirect
	github.com/mattn/go-isatty v0.0.19 // indirect
	github.com/pkg/errors v0.9.1 // indirect
	github.com/pmezard/go-difflib v1.0.0 // indirect
	github.com/rs/xid v1.5.0 // indirect
	github.com/rs/zerolog v1.32.0 // indirect
	github.com/stretchr/objx v0.5.2 // indirect
	github.com/stretchr/testify v1.9.0 // indirect
	github.com/yuin/goldmark v1.4.13 // indirect
	go.mau.fi/util v0.4.1 // indirect
	golang.org/x/crypto v0.23.0 // indirect
	golang.org/x/exp v0.0.0-20240314144324-c7f7c6466f7f // indirect
	golang.org/x/mod v0.15.0 // indirect
	golang.org/x/net v0.25.0 // indirect
	golang.org/x/sync v0.6.0 // indirect
	golang.org/x/sys v0.20.0 // indirect
	golang.org/x/telemetry v0.0.0-20240208230135-b75ee8823808 // indirect
	golang.org/x/term v0.20.0 // indirect
	golang.org/x/text v0.15.0 // indirect
	golang.org/x/tools v0.18.0 // indirect
	golang.org/x/xerrors v0.0.0-20191204190536-9bdfabe68543 // indirect
	gopkg.in/check.v1 v0.0.0-20161208181325-20d25e280405 // indirect
	gopkg.in/yaml.v3 v3.0.1 // indirect
	rsc.io/qr v0.2.0 // indirect
)

go 1.21

toolchain go1.22.2
