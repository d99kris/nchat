nchat - ncurses chat
====================

| **Linux** | **Mac** |
|-----------|---------|
| [![Linux](https://github.com/d99kris/nchat/workflows/Linux/badge.svg)](https://github.com/d99kris/nchat/actions?query=workflow%3ALinux) | [![macOS](https://github.com/d99kris/nchat/workflows/macOS/badge.svg)](https://github.com/d99kris/nchat/actions?query=workflow%3AmacOS) |

nchat is a terminal-based chat client for Linux and macOS with support for
Telegram and WhatsApp.

![screenshot nchat](/doc/screenshot-nchat.png)

Features
--------
- Customizable color schemes and key bindings
- Jump to unread chat
- Message history cache with support for text export
- Message read receipt
- Receive / send markdown formatted messages
- Reply / delete / edit / send messages
- Searchable list dialogs for selecting files, emojis, contacts
- Show user status (online, away, typing)
- Toggle to view textized emojis vs. graphical
- Toggle to view UI elements (top bar, status bar, help bar, contact list)
- View / save media files (documents, photos, videos)
- Customizable color schemes and key bindings

Limitations
-----------
There are no plans to support the following features:
- Facebook Messenger
- Signal
- Telegram secret chats
- Voice / video calls

Roadmap
-------
There is currently no concrete roadmap for further feature development of
nchat. It is not intended to be a full-featured client on par with official
Telegram / WhatsApp clients, but rather a light-weight client providing
essential functionality suitable for the terminal. However, feel free to
submit feature requests if there's something missing, and if it's useful
and low effort it can probably be added.


Usage
=====
Usage:

    nchat [OPTION]

Command-line Options:

    -d, --confdir <DIR>    use a different directory than ~/.nchat
    -e, --verbose          enable verbose logging
    -ee, --extra-verbose   enable extra verbose logging
    -h, --help             display this help and exit
    -k, --keydump          key code dump mode
    -m, --devmode          developer mode
    -s, --setup            set up chat protocol account
    -v, --version          output version information and exit
    -x, --export <DIR>     export message cache to specified dir

Interactive Commands:

    PageDn      history next page
    PageUp      history previous page
    Tab         next chat
    Sh-Tab      previous chat
    Ctrl-f      jump to unread chat
    Ctrl-g      toggle show help bar
    Ctrl-l      toggle show contact list
    Ctrl-n      search contacts
    Ctrl-p      toggle show top bar
    Ctrl-q      quit
    Ctrl-s      insert emoji
    Ctrl-t      send file
    Ctrl-x      send message
    Ctrl-y      toggle show emojis
    KeyUp       select message

Interactive Commands for Selected Message:

    Ctrl-d      delete selected message
    Ctrl-r      download attached file
    Ctrl-v      open/view attached file
    Ctrl-w      open link
    Ctrl-x      reply to selected message
    Ctrl-z      edit selected message

Interactive Commands for Text Input:

    Ctrl-a      move cursor to start of line
    Ctrl-c      clear input buffer
    Ctrl-e      move cursor to end of line
    Ctrl-k      delete from cursor to end of line
    Ctrl-u      delete from cursor to start of line
    Alt-Left    move cursor backward one word
    Alt-Right   move cursor forward one word
    Alt-Backsp  delete previous word
    Alt-Delete  delete next word
    Alt-e       external editor compose
    Alt-s       external spell check
    Alt-c       copy
    Alt-v       paste
    Alt-w       external message viewer
    Alt-x       cut


Supported Platforms
===================
nchat is developed and tested on Linux and macOS. Current version has been
tested on:

- macOS Big Sur 11.5
- Ubuntu 20.04 LTS


Build / Install
===============
Nchat consists of a large code-base (mainly the Telegram client library), so be
prepared for a relatively long first build time.

Ubuntu
------
**Dependencies**

    sudo apt install ccache cmake build-essential gperf help2man libreadline-dev libssl-dev libncurses-dev libncursesw5-dev ncurses-doc zlib1g-dev libsqlite3-dev libmagic-dev golang

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Build**

    mkdir -p build && cd build && cmake .. && make -s

**Install**

    sudo make install

macOS
-----

There are two methods to install nchat on macOS: through Homebrew or building from source.

### Using Homebrew

**Add tap**

    brew tap d99kris/nchat

**Install**

    brew install nchat

### Building from Source

**Dependencies**

    brew install gperf cmake openssl ncurses ccache readline help2man sqlite libmagic go

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

Fedora
------
**Dependencies**

    sudo dnf install git cmake clang golang ccache file-devel file-libs gperf readline-devel openssl-devel ncurses-devel sqlite-devel zlib-devel

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Build**

    mkdir -p build && cd build && cmake .. && make -s

**Install**

    sudo make install

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

Low Memory / RAM Systems
------------------------
The Telegram client library subcomponent requires relatively large amount of
RAM to build by default (3.5GB using g++, and 1.5 GB for clang++). It is
possible to adjust the Telegram client library source code so that it requires
less RAM (but takes longer time). Doing so reduces the memory requirement to
around 1GB under g++ and 0.5GB for clang++.

Steps to build nchat on a low memory system:

**Extra Dependencies (Linux)**

    sudo apt install php-cli clang

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Setup**

    mkdir -p build && cd build
    CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake ..

**Split source (optional)**

    cmake --build . --target prepare_cross_compiling
    cd ../lib/tgchat/ext/td ; php SplitSource.php ; cd -

**Build**

    make -s

**Install**

    sudo make install

**Revert Source Code Split (Optional)**

    cd ../lib/tgchat/ext/td ; php SplitSource.php --undo ; cd -

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

Fedora
------
**Extra Dependencies**

    sudo dnf install php-cli

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Build**

    mkdir -p build && cd build
    CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake ..
    cmake --build . --target prepare_cross_compiling
    cd ../lib/tgchat/ext/td ; php SplitSource.php ; cd -
    make -s

**Install**

    sudo make install

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

Once the setup process is completed, the main UI of nchat will be loaded.


Troubleshooting
===============
If any issues are observed, try running nchat with verbose logging

    nchat --verbose

and provide a copy of `~/.nchat/log.txt` when reporting a bug.


Telegram Group
==============
A Telegram group [https://t.me/nchatusers](https://t.me/nchatusers) is
available for users to discuss nchat usage and related topics.


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

    attachment_prefetch=1
    cache_enabled=1
    downloads_dir=
    proxy_host=
    proxy_pass=
    proxy_port=
    proxy_user=

### attachment_prefetch

Specifies level of attachment prefetching:

    0 = no prefetch (download upon open/save)
    1 = selected (download upon message selection) <- default
    2 = all (download when message is received)

### cache_enabled

Specifies whether to enable (experimental) cache functionality.

### downloads_dir

Specifies a custom downloads directory path to save attachments to. If not
specified, the default dir is `~/Downloads` if exists, otherwise `~`.

### proxy_

SOCKS5 proxy server details. To enable proxy usage the parameters `host` and
`port` are required, while `user` and `pass` are optional (depending on the
SOCKS server). Note: In order to use a proxy while setting up nchat the first
time, it is recommended to first run nchat without arguments (`nchat`) for its
config dir to be created, and then edit proxy settings in `~/.nchat/app.conf`
as needed, before running `nchat -s` to setup an account.

~/.nchat/ui.conf
----------------
This configuration file holds general user interface settings. Default content:

    attachment_indicator=📎
    attachment_open_command=
    confirm_deletion=1
    desktop_notify_active=0
    desktop_notify_command=
    desktop_notify_inactive=0
    downloadable_indicator=+
    emoji_enabled=1
    entry_height=4
    failed_indicator=✗
    file_picker_command=
    help_enabled=1
    home_fetch_all=0
    link_open_command=
    list_enabled=1
    list_width=14
    mark_read_on_view=1
    message_open_command=
    muted_indicate_unread=1
    muted_notify_unread=0
    muted_position_by_timestamp=1
    online_status_share=1
    online_status_dynamic=0
    proxy_indicator=🔒
    read_indicator=✓
    spell_check_command=
    syncing_indicator=⇄
    terminal_bell_active=0
    terminal_bell_inactive=1
    terminal_title=
    top_enabled=1
    typing_status_share=1

### attachment_indicator

Specifies text to prefix attachment filenames in message view.

### attachment_open_command

Specifies a custom command to use for opening/viewing attachments. The
command shall include `%1` which will be replaced by the filename
to open. If not specified, the following default commands are used:

Linux: `xdg-open >/dev/null 2>&1 '%1' &`

macOS: `open '%1' &`

Note: Omit the trailing `&` for commands taking over the terminal, for
example `w3m -o confirm_qq=false '%1'` and `see '%1'`.

### confirm_deletion

Specifies whether to prompt the user for confirmation when deleting a message.

### desktop_notify_active

Specifies whether new message shall trigger desktop notification when nchat
terminal window is active.

### desktop_notify_command

Specifies a custom command to use for desktop notifications. The command may
include `%1` (will be replaced by sender name) and `%2` (will be replaced
by message text) enclosed in single quotes (to prevent shell injection).
Default command used, if not specified:

Linux: `notify-send 'nchat' '%1: %2'`

macOS: `osascript -e 'display notification "%1: %2" with title "nchat"'`

### desktop_notify_inactive

Specifies whether new message shall trigger desktop notification when nchat
terminal window is inactive.

### downloadable_indicator

Specifies text to suffix attachment filenames in message view for attachments
not yet downloaded. This is only shown for `attachment_prefetch` < 2.

### emoji_enabled

Specifies whether to display emojis. Controlled by Ctrl-y in run-time.

### entry_height

Specifies height of text entry area.

### failed_indicator

Specifies text to suffix attachment filenames in message view for failed
downloads.

### file_picker_command

Specifies a command to use for file selection, in place of the internal file
selection dialog used when sending files. The command shall include `%1` (a
temporary file path) which the command should write its result to. Examples:

nnn: `nnn -p '%1'`

ranger: `ranger --choosefiles='%1'`

### help_enabled

Specifies whether to display help bar. Controlled by Ctrl-g in run-time.

### home_fetch_all

Specifies whether `home` button shall repeatedly fetch all chat history.

### link_open_command

Specifies a custom command to use for opening/viewing links. The
command shall include `%1` which will be replaced by the url
to open. If not specified, the following default commands are used:

Linux: `xdg-open >/dev/null 2>&1 '%1' &`

macOS: `open '%1' &`

Note: Omit the trailing `&` for commands taking over the terminal, for
example `w3m -o confirm_qq=false '%1'` and `see '%1'`.

### list_enabled

Specifies whether to display chat list. Controlled by Ctrl-l in run-time.

### list_width

Specifies width of chat list.

### mark_read_on_view

Specifies whether nchat should send message read receipts upon viewing. If
false nchat will only mark the messages read upon `next_page` (page down),
`end` (end) or upon sending a message/file in the chat.

### message_open_command

Specifies a custom command to use for opening/viewing message text part. If
not specified, nchat will use `PAGER` environment variable if set, or
otherwise use `less`.

### muted_indicate_unread

Specifies whether chat list should indicate unread status `*` for muted chats.
This also determines whether the such chats are included in jump to unread.

### muted_notify_unread

Specifies whether to notify (terminal bell) new unread messages in muted chats.

### muted_position_by_timestamp

Specifies whether chat list position of muted chats should reflect the time of
their last received/sent message. Otherwise muted chats are listed last.

### online_status_share

Share online status with other users.
Note: Disabling this stops updates on other users online/typing status for
WhatsApp.

### online_status_dynamic

Dynamically update online status based on terminal active state.
Note: Enabling this stops updates on other users online/typing status for
WhatsApp when the terminal is not active.

### spell_check_command

Specifies a custom command to use for spell checking composed messages. If not
specified, nchat checks if `aspell` or `ispell` is available on the system (in
that order), and uses the first found.

### read_indicator

Specifies text to indicate a message has been read by the receiver.

### syncing_indicator

Specifies text to suffix attachment filenames in message view for downloads
in progress.

### terminal_bell_active

Specifies whether new message shall trigger terminal bell when nchat terminal
window is active.

### terminal_bell_inactive

Specifies whether new message shall trigger terminal bell when nchat terminal
window is inactive.

### terminal_title

Specifies custom terminal title, ex: `terminal_title=nchat - telegram`.

### top_enabled

Specifies whether to display top bar. Controlled by Ctrl-p in run-time.

### typing_status_share

Specifies whether to share typing status with other user(s) in the
conversation.

~/.nchat/key.conf
-----------------
This configuration file holds user interface key bindings. Default content:

    backspace=KEY_BACKSPACE
    backspace_alt=KEY_ALT_BACKSPACE
    backward_kill_word=
    backward_word=
    begin_line=KEY_CTRLA
    cancel=KEY_CTRLC
    clear=KEY_CTRLC
    copy=
    cut=
    delete=KEY_DC
    delete_line_after_cursor=KEY_CTRLK
    delete_line_before_cursor=KEY_CTRLU
    delete_msg=KEY_CTRLD
    down=KEY_DOWN
    edit_msg=KEY_CTRLZ
    end=KEY_END
    end_line=KEY_CTRLE
    forward_word=
    home=KEY_HOME
    kill_word=
    left=KEY_LEFT
    next_chat=KEY_TAB
    next_page=KEY_NPAGE
    open=KEY_CTRLV
    open_link=KEY_CTRLW
    open_msg=
    other_commands_help=KEY_CTRLO
    paste=
    prev_chat=KEY_BTAB
    prev_page=KEY_PPAGE
    quit=KEY_CTRLQ
    return=KEY_RETURN
    right=KEY_RIGHT
    save=KEY_CTRLR
    select_contact=KEY_CTRLN
    select_emoji=KEY_CTRLS
    send_msg=KEY_CTRLX
    toggle_emoji=KEY_CTRLY
    toggle_help=KEY_CTRLG
    toggle_list=KEY_CTRLL
    toggle_top=KEY_CTRLP
    transfer=KEY_CTRLT
    unread_chat=KEY_CTRLF
    up=KEY_UP

The key bindings may be specified in the following formats:
- Ncurses macro (ex: `KEY_CTRLK`)
- Hex key code (ex: `0x22e`)
- Octal key code sequence (ex: `\033\177`)
- Plain-text single-char ASCII (ex: `r`)
- Disable key binding (`KEY_NONE`)

To determine the key code sequence for a key, one can run nchat in key code
dump mode `nchat -k` which will output the octal code, and ncurses macro name
(if present).

~/.nchat/color.conf
-------------------
This configuration file holds user interface color settings. Default content:

    default_color_bg=
    default_color_fg=
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
    history_name_recv_group_color_bg=
    history_name_recv_group_color_fg=
    history_name_sent_color_bg=
    history_name_sent_color_fg=gray
    history_text_attachment_color_bg=
    history_text_attachment_color_fg=gray
    history_text_attr=
    history_text_attr_selected=reverse
    history_text_quoted_color_bg=
    history_text_quoted_color_fg=gray
    history_text_recv_color_bg=
    history_text_recv_color_fg=
    history_text_recv_group_color_bg=
    history_text_recv_group_color_fg=
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

The `history_name_recv_group_color` and `history_text_recv_group_color`
parameters also supports the special value `usercolor`. When set, nchat will
determine which color to use for a user, based on a hash of their user id
used to pick a color from the list in `~/.nchat/usercolor.conf`.

Themes
------
Example color config files are provided in `/usr/local/share/nchat/themes`
and can be used by copying to `~/.nchat/`.

### Default Theme

    cp /usr/local/share/nchat/themes/default/* ~/.nchat/

![screenshot nchat](/doc/screenshot-nchat.png)

### Basic Color Theme

    cp /usr/local/share/nchat/themes/basic-color/* ~/.nchat/

![screenshot nchat](/doc/screenshot-nchat-basic-color.png)

### Dracula Theme

    cp /usr/local/share/nchat/themes/dracula/* ~/.nchat/

![screenshot nchat](/doc/screenshot-nchat-dracula.png)

### iTerm2-Color-Schemes Themes

[iTerm2 Color Schemes](https://github.com/mbadolato/iTerm2-Color-Schemes) can
be used to generate themes for nchat. The following themes generated using
iTerm2 Color Schemes are available in `/usr/local/share/nchat/themes`:

- Catppuccin Mocha:
  `cp /usr/local/share/nchat/themes/catppuccin-mocha/* ~/.nchat/`
- Espresso:
  `cp /usr/local/share/nchat/themes/espresso/* ~/.nchat/`
- Gruvbox Dark:
  `cp /usr/local/share/nchat/themes/gruvbox-dark/* ~/.nchat/`
- Solarized Dark Higher Contrast:
  `cp /usr/local/share/nchat/themes/solarized-dark-higher-contrast/* ~/.nchat/`
- Tokyo Night:
  `cp /usr/local/share/nchat/themes/tokyo-night/* ~/.nchat/`
- Tomorrow Night:
  `cp /usr/local/share/nchat/themes/tomorrow-night/* ~/.nchat/`
- Zenbones Dark:
  `cp /usr/local/share/nchat/themes/zenbones-dark/* ~/.nchat/`
- Zenburned:
  `cp /usr/local/share/nchat/themes/zenburned/* ~/.nchat/`

To generate additional nchat themes and install for use with `nchat`, refer to
[Generating nchat themes from iTerm2 Color Schemes](/themes/templates/iterm2-color-schemes/README.md).

General
-------
Deleting a configuration entry line (while nchat is not running) and starting
nchat will populate the configuration file with the default entry.


Technical Details
=================

Custom API Id / Hash
--------------------
nchat uses its own Telegram API id and hash by default. To use custom id/hash,
obtained from [https://my.telegram.org/](https://my.telegram.org/) one may set
environment variables `TG_APIID` and `TG_APIHASH` when setting up a new Telegram
account. Example (below values must be changed to valid api id/hash):

    TG_APIID="123456" TG_APIHASH="aaeaeab342aaa23423" nchat -s

Third-party Libraries
---------------------
nchat is primarily implemented in C++ with some parts in Go. Its source tree
includes the source code of the following third-party libraries:

- [apathy](https://github.com/dlecocq/apathy) -
  Copyright 2013 Dan Lecocq - [MIT License](/ext/apathy/LICENSE)

- [clip](https://github.com/dacap/clip) -
  Copyright 2015 David Capello - [MIT License](/ext/clip/LICENSE.txt)

- [sqlite_modern_cpp](https://github.com/SqliteModernCpp/sqlite_modern_cpp) -
  Copyright 2017 aminroosta - [MIT License](/ext/sqlite_modern_cpp/License.txt)

- [tdlib](https://github.com/tdlib/td) -
  Copyright 2014 Aliaksei Levin, Arseny Smirnov -
  [Boost License](/lib/tgchat/ext/td/LICENSE_1_0.txt)

- [whatsmeow](https://github.com/tulir/whatsmeow) -
  Copyright 2022 Tulir Asokan -
  [MPL License](/lib/wmchat/go/ext/whatsmeow/LICENSE)

The [tdlib](https://github.com/tdlib/td) and
[whatsmeow](https://github.com/tulir/whatsmeow) libraries are actively
developed and need to be updated and integrated into nchat on a regular
basis by nchat maintainer(s). To facilitate this there are scripts available
to update to latest (or a specific) version of these libraries. Example usages:

    ./utils/tdlib-update 8517026

    ./utils/whatsmeow-update 7aedaa1

Code Formatting
---------------
Uncrustify is used to maintain consistent source code formatting, example:

    ./make.sh src


License
=======
nchat is distributed under the MIT license. See LICENSE file.


Contributions
=============
Please refer to [Contributing Guidelines](CONTRIBUTING.md) and
[Design Notes](DESIGN.md).


Alternatives
============
Other terminal-based Telegram clients:

- [tg](https://github.com/paul-nameless/tg)


Keywords
========
command line, console-based, linux, macos, chat client, ncurses, telegram,
terminal-based.
