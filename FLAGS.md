Feature Flags
=============

Enabling / Disabling Protocol Support
-------------------------------------
The protocols supported by nchat is controlled by the following cmake flags:

    HAS_DUMMY=ON
    HAS_TELEGRAM=ON
    HAS_WHATSAPP=ON

It is possible to enable / disable protocols by passing one or multiple flags
to cmake:

    mkdir -p build && cd build
    cmake -DHAS_WHATSAPP=OFF .. && make -s

