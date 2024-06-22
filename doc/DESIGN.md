Design
======

Nchat is a multi-threaded application. Each protocol (i.e. Telegram)
should implement a thread processing `RequestMessage` it received through
its `SendRequest()` method.

The protocol passes information to the main application by passing
`ServiceMessage` to its message handler, which is processed in a worker
thread.

The interface each protocol need to implement is defined in the abstract
`class Protocol` and `protocol.h` contains definitions of all messages
passed between main application and the protocol.

The core application logic is implemented in `uimodel.cpp` and the display
is handled by `ui*view.cpp` files.
