Config Flags
============

Component Loading
-----------------
The main components of nchat are split into separate libraries, which by
default are built as shared libraries and dynamically loaded at run-time.
The rationale is to minimize memory usage, startup time and also build
time.

Using build config flags one can change how the internal components are
loaded.

Link internal libraries dynamically:

    HAS_DYNAMICLOAD=OFF


Link internal libraries statically:

    HAS_SHARED_LIBS=OFF


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

Similarly the `make.sh` script provides options, example:

    ./make.sh --no-telegram build

and

    ./make.sh --no-whatsapp build

