nchat - ncurses chat
====================

nchat is a console-based chat client for Linux and macOS, with support for telegram.

![screenshot](/doc/screenshot.png) 

Usage
=====
Usage:
    nchat [OPTION]

Command-line Options:
    -h, --help        display this help and exit
    -s, --setup       set up chat protocol account
    -v, --version     output version information and exit

Interactive Commands:
    Tab               next chat
    Sh-Tab            previous chat
    PageDn            next page
    PageUp            previous page
    Ctrl-e            enable/disable emoji
    Ctrl-s            send message
    Ctrl-u            next unread chat
    Ctrl-x            exit

Supported Platforms
===================
nchat is developed and tested on Linux and macOS. Current version has been tested on:

- macOS 10.14 Mojave
- Ubuntu 18.04 LTS

Linux / Ubuntu
==============

**Dependencies**

    sudo apt install gperf libreadline-dev libssl-dev libncurses-dev ncurses-doc help2man

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Build**

    mkdir -p build && cd build && cmake .. && make -s

**Install**

    sudo make -s install

macOS
=====

**Dependencies**

    brew install gperf cmake openssl ncurses ccache readline help2man

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Build**

    mkdir -p build && cd build && cmake .. && make -s

**Install**

    sudo make -s install

Technical Details
=================
nchat is implemented in C++. Its source tree includes the source code of the following
third-party libraries that are not available through package managers:

- [apathy](https://github.com/dlecocq/apathy) - MIT License
- [emojicpp](https://github.com/shalithasuranga/emojicpp) - MIT License
- [TDLib](https://github.com/tdlib/td) - Boost Software License

License
=======
nchat is distributed under the MIT license. See LICENSE file.

Keywords
========
command line, console based, linux, macos, chat client, telegram, ncurses, terminal.
