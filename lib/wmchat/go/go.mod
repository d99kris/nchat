module github.com/d99kris/nchat/lib/wmchat/go

require go.mau.fi/whatsmeow v0.0.0

replace go.mau.fi/whatsmeow => ./ext/whatsmeow

require (
	github.com/mattn/go-sqlite3 v1.14.17
	github.com/mdp/qrterminal v1.0.1
	github.com/mdp/qrterminal/v3 v3.0.0
	github.com/skip2/go-qrcode v0.0.0-20200617195104-da1b6568686e
	go.mau.fi/libsignal v0.1.0
	google.golang.org/protobuf v1.31.0
)

go 1.16
