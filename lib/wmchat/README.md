Wmchat
======
Wmchat provides implementation of WhatsApp functionality. The main
component is a copy of whatsmeow - in `go/ext/whatsmeow` directory.

WhatsApp is not officially supported by this project. Bug reports and
feature requests relating to it may be closed as out of scope.

Wmchat is disabled by default in standard nchat builds. It can be enabled with:

    mkdir -p build && cd build && cmake -DHAS_WHATSAPP=ON .. && make -s
