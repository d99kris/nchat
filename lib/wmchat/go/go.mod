module github.com/d99kris/nchat/lib/wmchat/go

require go.mau.fi/whatsmeow v0.0.0

replace go.mau.fi/whatsmeow => ./ext/whatsmeow

require (
	github.com/mattn/go-sqlite3 v1.14.12
	github.com/mdp/qrterminal v1.0.1
	github.com/mdp/qrterminal/v3 v3.0.0
	github.com/skip2/go-qrcode v0.0.0-20200617195104-da1b6568686e
	go.mau.fi/libsignal v0.0.0-20221015105917-d970e7c3c9cf
	google.golang.org/protobuf v1.28.1
)

go 1.16
