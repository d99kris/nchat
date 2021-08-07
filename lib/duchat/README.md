Duchat
======
Duchat provides implementation of a dummy chat protocol, used for
development testing of nchat.

Duchat is disabled by default in standard nchat builds. It can be enabled with:

    mkdir -p build && cd build && cmake -DHAS_DUMMY=ON .. && make -s

