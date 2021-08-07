Wachat
======
Wachat provides implementation of WhatsApp functionality. The main
component is a copy of go-whatsapp - in `go/ext/go-whatsapp` directory.

WhatsApp is not officially supported by this project. Bug reports and
feature requests relating to it will be closed as out of scope.

Wachat is disabled by default in standard nchat builds. It can be enabled with:

    mkdir -p build && cd build && cmake -DHAS_WHATSAPP=ON .. && make -s
