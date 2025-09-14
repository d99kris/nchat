module github.com/d99kris/nchat/lib/wmchat/go

require go.mau.fi/whatsmeow v0.0.0

replace go.mau.fi/whatsmeow => ./ext/whatsmeow

require (
	github.com/mattn/go-sqlite3 v1.14.32
	github.com/mdp/qrterminal v1.0.1
	github.com/skip2/go-qrcode v0.0.0-20200617195104-da1b6568686e
	go.mau.fi/libsignal v0.2.0
	google.golang.org/protobuf v1.36.7
)

require (
	filippo.io/edwards25519 v1.1.0 // indirect
	github.com/beeper/argo-go v1.1.2 // indirect
	github.com/elliotchance/orderedmap/v3 v3.1.0 // indirect
	github.com/google/uuid v1.6.0 // indirect
	github.com/gorilla/websocket v1.5.0 // indirect
	github.com/mattn/go-colorable v0.1.14 // indirect
	github.com/mattn/go-isatty v0.0.20 // indirect
	github.com/petermattis/goid v0.0.0-20250813065127-a731cc31b4fe // indirect
	github.com/rs/zerolog v1.34.0 // indirect
	github.com/vektah/gqlparser/v2 v2.5.27 // indirect
	go.mau.fi/util v0.9.1-0.20250912114103-419604f95907 // indirect
	golang.org/x/crypto v0.41.0 // indirect
	golang.org/x/exp v0.0.0-20250813145105-42675adae3e6 // indirect
	golang.org/x/net v0.43.0 // indirect
	golang.org/x/sys v0.35.0 // indirect
	golang.org/x/text v0.28.0 // indirect
	rsc.io/qr v0.2.0 // indirect
)

go 1.24.0
