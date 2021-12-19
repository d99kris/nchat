nchat - ncurses chat
====================

| **Linux** | **Mac** |
|-----------|---------|
| [![Linux](https://github.com/d99kris/nchat/workflows/Linux/badge.svg)](https://github.com/d99kris/nchat/actions?query=workflow%3ALinux) | [![macOS](https://github.com/d99kris/nchat/workflows/macOS/badge.svg)](https://github.com/d99kris/nchat/actions?query=workflow%3AmacOS) |

nchat is a terminal-based chat client for Linux and macOS with support for
Telegram. 

![screenshot nchat](/doc/screenshot-nchat.png) 

Features
--------
- Message history cache (sqlite db backed)
- View/save media files: documents, photos
- Show user status (online, away, typing)
- Message read receipt
- List dialogs (with text filter) for selecting files, emojis, contacts
- Reply / delete / send messages
- Jump to unread chat
- Toggle to view textized emojis vs. graphical (default)
- Toggle to hide/show UI elements (top bar, status bar, help bar, contact list)
- Receive / send markdown formatted messages
- Customizable color schemes and key bindings


Usage
=====
Usage:

    nchat [OPTION]

Command-line Options:

    -d, --confdir <DIR>    use a different directory than ~/.nchat
    -e, --verbose          enable verbose logging
    -ee, --extra-verbose   enable extra verbose logging
    -h, --help             display this help and exit
    -s, --setup            set up chat protocol account
    -v, --version          output version information and exit
    -x, --export <DIR>     export message cache to specified dir

Interactive Commands:

    PageDn      history next page
    PageUp      history previous page
    Tab         next chat
    Sh-Tab      previous chat
    Ctrl-e      insert emoji
    Ctrl-g      toggle show help bar
    Ctrl-l      toggle show contact list
    Ctrl-p      toggle show top bar
    Ctrl-q      quit
    Ctrl-s      search contacts
    Ctrl-t      send file
    Ctrl-u      jump to unread chat
    Ctrl-x      send message
    Ctrl-y      toggle show emojis
    KeyUp       select message

Interactive Commands for Selected Message:

    Ctrl-d      delete selected message
    Ctrl-r      download attached file
    Ctrl-v      open/view attached file
    Ctrl-w      open link in sponsored message
    Ctrl-x      reply to selected message


Supported Platforms
===================
nchat is developed and tested on Linux and macOS. Current version has been
tested on:

- macOS Big Sur 11.5
- Ubuntu 20.04 LTS


Build / Install
===============
Nchat consists of a large code-base (mainly the Telegram client library), so be
prepared for a relatively long first build time. Subsequent builds will be
faster.

Linux / Ubuntu
--------------
**Dependencies**

    sudo apt install ccache cmake build-essential gperf help2man libreadline-dev libssl-dev libncurses-dev libncursesw5-dev ncurses-doc zlib1g-dev libsqlite3-dev libmagic-dev

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Build**

    mkdir -p build && cd build && cmake .. && make -s

**Install**

    sudo make install

macOS
-----
**Dependencies**

    brew install gperf cmake openssl ncurses ccache readline help2man sqlite libmagic

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Build**

    mkdir -p build && cd build && cmake .. && make -s

**Install**

    make install

Arch Linux
----------
**Source**

    git clone https://aur.archlinux.org/nchat-git.git && cd nchat-git

**Build**

    makepkg -s

**Install**

    makepkg -i

Low Memory / RAM Systems
------------------------
The Telegram client library subcomponent requires relatively large amount of RAM to
build by default (3.5GB using g++, and 1.5 GB for clang++). It is possible to adjust the
Telegram client library source code so that it requires less RAM (but takes longer time).
Doing so reduces the memory requirement to around 1GB under g++ and 0.5GB for clang++. Also, it
is recommended to build nchat in release mode (which is default if downloading zip/tar release
package - but with a git/svn clone it defaults to debug mode), to minimize memory usage.
Steps to build nchat on a low memory system:

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Build**

    cd lib/tgchat/ext/td ; php SplitSource.php ; cd -
    mkdir -p build && cd build
    CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -DCMAKE_BUILD_TYPE=Release .. && make -s

**Install**

    sudo make install

**Revert Source Code Split (Optional)**

    cd ../lib/tgchat/ext/td ; php SplitSource.php --undo ; cd -   # optional step to revert source split

Arch Linux
----------
**Source**

    git clone https://aur.archlinux.org/nchat-git.git && cd nchat-git

**Prepare**

    Open PKGBUILD in your favourite editor.
    Add `php` and `clang` on depends array.
    Change the `_install_mode` to `slow`.

**Build**

    makepkg -s

**Install**

    makepkg -i


Getting Started
===============
In order to configure / setup an account one needs to run nchat in setup mode:

    nchat --setup

The setup mode prompts for phone number, which shall be entered with country
code. Example:

    $ nchat --setup
    Protocols:
    0. Telegram
    1. Exit setup
    Select protocol (1): 0
    Enter phone number (ex. +6511111111): +6511111111
    Enter authentication code: xxxxx
    Succesfully set up profile Telegram_+6511111111

If you are not sure what phone number to enter, open Telegram on your phone
and press the menu button and use the number displayed there (omitting spaces,
so for the below screenshot the number to enter is +6511111111).

![screenshot telegram phone](/doc/screenshot-phone.png) 

Once the setup process is completed nchat will exit, and can now be restarted
in normal mode:

    nchat


Troubleshooting
===============
If any issues are observed, try running nchat with verbose logging

    nchat --verbose

and provide a copy of ~/.nchat/main.log when reporting the issue. The
preferred way of reporting issues and asking questions is by opening 
[a Github issue](https://github.com/d99kris/nchat/issues/new). 


Telegram Group
==============
A Telegram group [https://t.me/nchatusers](https://t.me/nchatusers) is
available for users to discuss nchat usage and related topics.

Bug reports, feature requests and usage questions directed at the nchat
maintainer(s) should however be reported using
[Github issues](https://github.com/d99kris/nchat/issues/new) to ensure they
are properly tracked and get addressed.


Security
========
User data is stored locally in `~/.nchat`. Default file permissions
only allow user access, but anyone who can gain access to a user's private
files can also access the user's personal Telegram data. To protect against
the most simple attack vectors it may be suitable to use disk encryption and
to ensure `~/.nchat` is not backed up unencrypted.


Configuration
=============
The following configuration files (listed with current default values) can be
used to configure nchat.

~/.nchat/app.conf
-----------------
This configuration file holds general application settings. Default content:

    cache_enabled=1

~/.nchat/ui.conf
----------------
This configuration file holds general user interface settings. Default content:

    confirm_deletion=1
    emoji_enabled=1
    help_enabled=1
    home_fetch_all=0
    list_enabled=1
    top_enabled=1

~/.nchat/key.conf
-----------------
This configuration file holds user interface key bindings. Default content:

    backspace=KEY_BACKSPACE
    cancel=KEY_CTRLC
    delete=KEY_DC
    delete_msg=KEY_CTRLD
    down=KEY_DOWN
    end=KEY_END
    home=KEY_HOME
    left=KEY_LEFT
    next_chat=KEY_TAB
    next_page=KEY_NPAGE
    open=KEY_CTRLV
    open_link=KEY_CTRLW
    other_commands_help=KEY_CTRLO
    prev_chat=KEY_BTAB
    prev_page=KEY_PPAGE
    quit=KEY_CTRLQ
    return=KEY_RETURN
    right=KEY_RIGHT
    save=KEY_CTRLR
    select_contact=KEY_CTRLS
    select_emoji=KEY_CTRLE
    send_msg=KEY_CTRLX
    toggle_emoji=KEY_CTRLY
    toggle_help=KEY_CTRLG
    toggle_list=KEY_CTRLL
    toggle_top=KEY_CTRLP
    transfer=KEY_CTRLT
    unread_chat=KEY_CTRLU
    up=KEY_UP

Refer to function UiKeyConfig::GetKeyCode() in
[uikeyconfig.cpp](https://github.com/d99kris/nchat/blob/master/src/uikeyconfig.cpp)
for a list of supported key names to use in the config file. Alternatively
key codes may be entered in hex format (e.g. 0x9).

~/.nchat/color.conf
-------------------
This configuration file holds user interface color settings. Default content:

    dialog_attr=
    dialog_attr_selected=reverse
    dialog_color_bg=
    dialog_color_fg=
    entry_attr=
    entry_color_bg=
    entry_color_fg=
    help_attr=reverse
    help_color_bg=black
    help_color_fg=white
    history_name_attr=bold
    history_name_attr_selected=reverse
    history_name_recv_color_bg=
    history_name_recv_color_fg=
    history_name_sent_color_bg=
    history_name_sent_color_fg=gray
    history_text_attr=
    history_text_attr_selected=reverse
    history_text_recv_color_bg=
    history_text_recv_color_fg=
    history_text_sent_color_bg=
    history_text_sent_color_fg=gray
    list_attr=
    list_attr_selected=bold
    list_color_bg=
    list_color_fg=
    listborder_attr=
    listborder_color_bg=
    listborder_color_fg=
    status_attr=reverse
    status_color_bg=
    status_color_fg=
    top_attr=reverse
    top_color_bg=
    top_color_fg=

Supported text attributes `_attr` (defaults to `normal` if not specified):

    normal
    underline
    reverse
    bold
    italic

Supported text background `_bg` and foreground `_fg` colors:

    black
    red
    green
    yellow
    blue
    magenta
    cyan
    white
    gray
    bright_black (same as gray)
    bright_red
    bright_green
    bright_yellow
    bright_blue
    bright_magenta
    bright_cyan
    bright_white

Custom colors may be specified using hex RGB code, for example `0xff8937`.

General
-------
Deleting a configuration entry line (while nchat is not running) and starting
nchat will populate the configuration file with the default entry.


Technical Details
=================

Third-party Libraries
---------------------
nmail is primarily implemented in C++ with some parts in Go. Its source tree
includes the source code of the following third-party libraries:

- [apathy](https://github.com/dlecocq/apathy) -
  Copyright 2013 Dan Lecocq - [MIT License](/ext/apathy/LICENSE)

- [emojicpp](https://github.com/99x/emojicpp) -
  Copyright 2018 Shalitha Suranga - [MIT License](/ext/emojicpp/LICENSE)

- [go-qrcode](https://github.com/skip2/go-qrcode) -
  Copyright 2014 Tom Harwood -
  [MIT License](/lib/wachat/go/ext/go-qrcode/LICENSE)

- [go-whatsapp](https://github.com/Rhymen/go-whatsapp) -
  Copyright 2018 Lucas Engelke -
  [MIT License](/lib/wachat/go/ext/go-whatsapp/LICENSE)

- [sqlite_modern_cpp](https://github.com/SqliteModernCpp/sqlite_modern_cpp) -
  Copyright 2017 aminroosta - [MIT License](/ext/sqlite_modern_cpp/License.txt)

- [tdlib](https://github.com/tdlib/td) -
  Copyright 2014 Aliaksei Levin, Arseny Smirnov -
  [Boost License](/lib/tgchat/ext/td/LICENSE_1_0.txt)

Code Formatting
---------------
Uncrustify is used to maintain consistent source code formatting, example:

    ./make.sh src


License
=======
nchat is distributed under the MIT license. See LICENSE file.


Alternatives
============
Other terminal-based Telegram clients:

- [tg](https://github.com/paul-nameless/tg)


Keywords
========
command line, console-based, linux, macos, chat client, ncurses, telegram, terminal-based.
