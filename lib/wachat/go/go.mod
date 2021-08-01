module github.com/d99kris/nchat/lib/wachat/go

require github.com/Rhymen/go-whatsapp v0.0.0

replace github.com/Rhymen/go-whatsapp => ./ext/go-whatsapp

require github.com/skip2/go-qrcode v0.0.0

replace github.com/skip2/go-qrcode => ./ext/go-qrcode

go 1.13
