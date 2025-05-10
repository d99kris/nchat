nchat
=====

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
- Reply / delete / edit / forward / send messages
- List dialogs for selecting chats, contacts, emojis, files
- Show user status (online, away, typing)
- Toggle to view textized emojis vs. graphical
- View / save media files (documents, photos, videos)
- Send and display reactions


Usage
=====
Usage:

    nchat [OPTION]

Command-line Options:

    -d, --confdir <DIR>    use a different directory than ~/.config/nchat
    -e, --verbose          enable verbose logging
    -ee, --extra-verbose   enable extra verbose logging
    -h, --help             display this help and exit
    -k, --keydump          key code dump mode
    -m, --devmode          developer mode
    -r, --remove           remove chat protocol account
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
    Ctrl-n      goto chat
    Ctrl-p      toggle show top bar
    Ctrl-q      quit
    Ctrl-s      insert emoji
    Ctrl-t      send file
    Ctrl-x      send message
    Ctrl-y      toggle show emojis
    KeyUp       select message
    Alt-d       delete/leave current chat
    Alt-e       external editor compose
    Alt-n       search contacts
    Alt-t       external telephone call
    Alt-/       find in chat
    Alt-?       find next in chat
    Alt-$       external spell check
    Alt-,       decrease contact list width
    Alt-.       increase contact list width

Interactive Commands for Selected Message:

    Ctrl-d      delete selected message
    Ctrl-r      download attached file
    Ctrl-v      open/view attached file
    Ctrl-w      open link
    Ctrl-x      send reply to selected message
    Ctrl-z      edit selected message
    Alt-c       copy selected message to clipboard
    Alt-q       jump to quoted/replied message
    Alt-r       forward selected message
    Alt-s       add/remove reaction on selected message
    Alt-w       external message viewer

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
    Alt-c       copy input buffer to clipboard (if no message selected)
    Alt-v       paste into input buffer from clipboard
    Alt-x       cut input buffer to clipboard


Supported Platforms
===================
nchat is developed and tested on Linux and macOS. Current version has been
tested on:

- macOS Sonoma 14.5
- Ubuntu 22.04 LTS

Install using Package Manager
=============================

macOS
-----
**Build / Install Stable Release using Brew**

    brew tap d99kris/nchat
    brew install nchat

Optionally one can disable protocols using `--without-whatsapp` and
`--without-telegram`, for example:

    brew install nchat --without-telegram


Arch Linux
----------
**Build / Install Latest Git**

    yay -S nchat-git

**Build / Install Stable Release**

    yay -S nchat

Build from Source
=================
nchat consists of a large code-base (mainly the Telegram library tdlib), so be
prepared for a relatively long first build time.

**Get Source**

    git clone https://github.com/d99kris/nchat && cd nchat

Using make.sh script
--------------------
If using macOS, Alpine, Arch, Fedora, Gentoo, Raspbian, Ubuntu or Void, one
can use the `make.sh` script provided.

**Dependencies**

    ./make.sh deps

**Build / Install**

    ./make.sh build && ./make.sh install

Manually
--------
**Dependencies**

macOS

    brew install gperf cmake openssl ncurses ccache readline help2man sqlite libmagic go

Arch

    sudo pacman -S ccache cmake file go gperf help2man ncurses openssl readline sqlite zlib base-devel

Debian-based (Ubuntu, Raspbian, etc)

    sudo apt install ccache cmake build-essential gperf help2man libreadline-dev libssl-dev libncurses-dev libncursesw5-dev ncurses-doc zlib1g-dev libsqlite3-dev libmagic-dev golang

Fedora

    sudo dnf install git cmake clang golang ccache file-devel file-libs gperf readline-devel openssl-devel ncurses-devel sqlite-devel zlib-devel

Gentoo

    sudo emerge -n dev-util/cmake dev-util/ccache dev-util/gperf sys-apps/help2man sys-libs/readline dev-libs/openssl sys-libs/ncurses sys-libs/zlib dev-db/sqlite sys-apps/file dev-lang/go

Void

    sudo xbps-install base-devel go ccache cmake gperf help2man libmagick-devel readline-devel sqlite-devel file-devel openssl-devel

**Build**

    mkdir -p build && cd build && cmake .. && make -s

**Install**

    sudo make install

Advanced Build Options
----------------------
By default nchat requires ~3.5GB RAM to build using G++ and ~1.5GB RAM with
clang++, but it is possible to reduce the memory needed,
see [Building on Low Memory Systems](/doc/LOWMEMORY.md).

All nchat features are enabled by default, but it's possible to control
inclusion of some features using cmake flags, see [Feature Flags](/doc/FLAGS.md).


Getting Started
===============
In order to configure / setup an account one needs to run nchat in setup mode:

    nchat --setup

The setup mode prompts for phone number, which shall be entered with country
code. Example:

    $ nchat --setup
    Protocols:
    0. Dummy
    1. Telegram
    2. WhatsAppMd
    3. Exit setup
    Select protocol (3): 1
    Enter phone number (ex. +6511111111): +6511111111
    Enter authentication code: xxxxx
    Succesfully set up profile Telegram_+6511111111

If unsure of what phone number to enter, open the Telegram app on the phone
and press the menu button and use the number displayed there (omitting spaces,
so for the below screenshot the number to enter is +6511111111).

![screenshot telegram phone](/doc/screenshot-phone.png)

Once the setup process is completed, the main UI of nchat will be loaded.

In order to set up multiple protocols/profiles, exit nchat and perform the
setup step again.


Troubleshooting
===============
Refer to [Debugging](/doc/DEBUGGING.md) for details.


Telegram Group
==============
A Telegram group [https://t.me/nchatusers](https://t.me/nchatusers) is
available for users to discuss nchat usage and related topics.


Security
========
User data is stored locally in `~/.config/nchat`. Default file permissions
only allow user access, but anyone who can gain access to a user's private
files can also access the user's personal Telegram data. To protect against
the most simple attack vectors it may be suitable to use disk encryption and
to ensure `~/.config/nchat` is not backed up unencrypted.


Configuration
=============
The following configuration files (listed with current default values) can be
used to configure nchat.

~/.config/nchat/app.conf
------------------------
This configuration file holds general application settings. Default content:

    assert_abort=0
    attachment_prefetch=1
    attachment_send_type=1
    cache_enabled=1
    coredump_enabled=0
    downloads_dir=
    emoji_list_all=0
    link_send_preview=1
    logdump_enabled=0
    proxy_host=
    proxy_pass=
    proxy_port=
    proxy_user=
    timestamp_iso=0

### assert_abort

Specifies whether to abort execution (crash) if assertions fail. Primarily
intended for debugging.

### attachment_send_type

Specifies whether to detect file type (audio, video, image, document) and send
attachments as those types, instead of sending all attachments as document
type (which typically leaves original file content intact).

### attachment_prefetch

Specifies level of attachment prefetching:

    0 = no prefetch (download upon open/save)
    1 = selected (download upon message selection) <- default
    2 = all (download when message is received)

### cache_enabled

Specifies whether to enable cache functionality.

### coredump_enabled

Specifies whether to enable core dumps on application crash.

### downloads_dir

Specifies a custom downloads directory path to save attachments to. If not
specified, the default dir is `~/Downloads` if exists, otherwise `~`.

### emoji_list_all

Specifies whether the emoji dialog should list all emojis, it is otherwise
restricted to listing emojis that renders properly in common terminals.

### link_send_preview

Specifies whether to enable preview for links in messages sent (Telegram only).

### logdump_enabled

Specifies whether to dump warning and error log messages to stdout upon exit.

### proxy_

SOCKS5 proxy server details. To enable proxy usage the parameters `host` and
`port` are required, while `user` and `pass` are optional (depending on the
SOCKS server). Note: In order to use a proxy while setting up nchat the first
time, it is recommended to first run nchat without arguments (`nchat`) for its
config dir to be created, and then edit proxy settings in
`~/.config/nchat/app.conf` as needed, before running `nchat -s` to setup an
account.

### timestamp_iso

Specifies whether to use ISO-style timestamps (`YYYY-MM-DD HH:MM`) in the UI
and at export of chat history. By default nchat uses a dynamic "human-friendly"
format:

- `HH:MM` for timestamps on same date as today, e.g. `19:00`
- `DAY HH:MM` for timestamps in the last week, e.g. `Mon 19:00`
- `DD MMM HH:MM` for timestamps in the current year, e.g. `14 Nov 19:00`
- `DD MMM YYYY HH:MM` for timestamps in non-current year, e.g. `14 Nov 2022 19:00`
- `DD MMM YYYY HH:MM` for timestamps during export, e.g. `14 Nov 2022 19:00`

~/.config/nchat/ui.conf
-----------------------
This configuration file holds general user interface settings. Default content:

    attachment_indicator=📎
    attachment_open_command=
    away_status_indication=0
    call_command=
    chat_picker_sorted_alphabetically=0
    confirm_deletion=1
    desktop_notify_active=0
    desktop_notify_command=
    desktop_notify_inactive=0
    downloadable_indicator=+
    emoji_enabled=1
    entry_height=4
    failed_indicator=✗
    file_picker_command=
    file_picker_persist_dir=1
    help_enabled=1
    home_fetch_all=0
    linefeed_on_enter=1
    link_open_command=
    list_enabled=1
    list_width=14
    listdialog_show_filter=1
    mark_read_on_view=1
    mark_read_when_inactive=0
    message_edit_command=
    message_open_command=
    muted_indicate_unread=1
    muted_notify_unread=0
    muted_position_by_timestamp=1
    online_status_share=1
    online_status_dynamic=1
    phone_number_indicator=
    proxy_indicator=🔒
    read_indicator=✓
    reactions_enabled=1
    spell_check_command=
    status_broadcast=1
    syncing_indicator=⇄
    terminal_bell_active=0
    terminal_bell_inactive=1
    terminal_title=
    top_enabled=1
    top_show_version=0
    transfer_send_caption=1
    typing_status_share=1
    unread_indicator=*

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

### away_status_indication

Specifies whether to indicate away status in the top bar while sharing away
status with other users. I.e. the status will read `Away` instead of `Online`
when the terminal is inactive (assuming `online_status_share=1` and
`online_status_dynamic=1`).

### call_command

Specifies a custom command to use for starting a call using an external
tool. The command shall include `%1` which will be replaced by the phone
number of the contact. If not specified, the following default commands
are used:

Linux: `xdg-open >/dev/null 2>&1 'tel://%1' &`

macOS: `open 'tel://%1' &`

### chat_picker_sorted_alphabetically

Specifies whether the chat selection dialog (used when forwarding message)
should be sorted alphabetically. If not, its order follows the main chat
list order.

### confirm_deletion

Specifies whether to prompt the user for confirmation when deleting a message
or a chat.

### desktop_notify_active

Specifies whether new message shall trigger desktop notification when nchat
terminal window is active.

### desktop_notify_command

Specifies a custom command to use for desktop notifications. The command may
include `%1` (will be replaced by `sender name` or `group name - sender name`)
and `%2` (will be replaced by `message text`) enclosed in single quotes (to
prevent shell injection). Default command used, if not specified:

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

### file_picker_persist_dir

Specifies whether the file selection dialog shall persist the directory of
last selected file.

### help_enabled

Specifies whether to display help bar. Controlled by Ctrl-g in run-time.

### home_fetch_all

Specifies whether `home` button shall repeatedly fetch all chat history.

### linefeed_on_enter

Specifies if enter key press should be read as linefeed (LF `\12`).
Otherwise read as carriage return (CR `\15`). This setting is only
relevant if `key.conf` uses numerical key value for enter (LF `\12`,
CR `\15`). The key name `KEY_RETURN` always maps to the one in use.

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

### listdialog_show_filter

Specifies whether list dialogs should display the search filter input by user.

### mark_read_on_view

Specifies whether nchat should send message read receipts upon viewing. If
false nchat will only mark the messages read upon `next_page` (page down),
`end` (end) or upon sending a message/file in the chat.

### mark_read_when_inactive

Controls whether nchat marks messages in the current chat as read while the
terminal is inactive.

### message_edit_command

Specifies a custom command to use for external editor compose. If not
specified, nchat will use `EDITOR` environment variable if set, or
otherwise use `nano`.

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

### phone_number_indicator

Specifies status bar text to indicate phone number of the current chat is
available. This field may contain `%1` which will be replaced with the actual
phone number of the contact. Other examples: `🎧`

### proxy_indicator

Specifies top bar text to indicate proxy is enabled.

### read_indicator

Specifies text to indicate a message has been read by the receiver.

### reactions_enabled

Specifies whether to display reactions.

### spell_check_command

Specifies a custom command to use for spell checking composed messages. If not
specified, nchat checks if `aspell` or `ispell` is available on the system (in
that order), and uses the first found.

### status_broadcast

Specifies (WhatsApp) Status Updates chat level of visibility:

    0 = hidden
    1 = visible and muted  <- default
    2 = visible

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

### top_show_version

Specifies whether to display nchat version in top bar.

### transfer_send_caption

Specifies if entered text should be sent as caption when transferring a file.

### typing_status_share

Specifies whether to share typing status with other user(s) in the
conversation.

### unread_indicator

Specifies the character to suffix chats with unread messages in the chat list.

~/.config/nchat/key.conf
------------------------
This configuration file holds user interface key bindings. Default content:

    backspace=KEY_BACKSPACE
    backspace_alt=KEY_ALT_BACKSPACE
    backward_kill_word=\33\177
    backward_word=
    begin_line=KEY_CTRLA
    cancel=KEY_CTRLC
    clear=KEY_CTRLC
    copy=\33\143
    cut=\33\170
    decrease_list_width=\33\54
    delete=KEY_DC
    delete_chat=\33\144
    delete_line_after_cursor=KEY_CTRLK
    delete_line_before_cursor=KEY_CTRLU
    delete_msg=KEY_CTRLD
    down=KEY_DOWN
    edit_msg=KEY_CTRLZ
    end=KEY_END
    end_line=KEY_CTRLE
    ext_call=\33\164
    ext_edit=\33\145
    find=\33\57
    find_next=\33\77
    forward_msg=\33\162
    forward_word=
    goto_chat=KEY_CTRLN
    home=KEY_HOME
    increase_list_width=\33\56
    jump_quoted=\33\161
    kill_word=
    left=KEY_LEFT
    linebreak=KEY_RETURN
    next_chat=KEY_TAB
    next_page=KEY_NPAGE
    ok=KEY_RETURN
    open=KEY_CTRLV
    open_link=KEY_CTRLW
    open_msg=\33\167
    other_commands_help=KEY_CTRLO
    paste=\33\166
    prev_chat=KEY_BTAB
    prev_page=KEY_PPAGE
    quit=KEY_CTRLQ
    react=\33\163
    right=KEY_RIGHT
    save=KEY_CTRLR
    select_contact=\33\156
    select_emoji=KEY_CTRLS
    send_msg=KEY_CTRLX
    spell=\33\44
    terminal_focus_in=KEY_FOCUS_IN
    terminal_focus_out=KEY_FOCUS_OUT
    terminal_resize=KEY_RESIZE
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

~/.config/nchat/color.conf
--------------------------
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
    history_text_reaction_color_bg=
    history_text_reaction_color_fg=gray
    history_text_recv_color_bg=
    history_text_recv_color_fg=
    history_text_recv_group_color_bg=
    history_text_recv_group_color_fg=
    history_text_sent_color_bg=
    history_text_sent_color_fg=gray
    list_attr=
    list_attr_selected=reverse
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
used to pick a color from the list in `~/.config/nchat/usercolor.conf`.

Themes
------
Example color config files are provided in `$(dirname $(which nchat))/../share/nchat/themes`
and can be used by copying to `~/.config/nchat/`.

### Default Theme

    cp $(dirname $(which nchat))/../share/nchat/themes/default/* ~/.config/nchat/

![screenshot nchat](/doc/screenshot-nchat.png)

### Basic Color Theme

    cp $(dirname $(which nchat))/../share/nchat/themes/basic-color/* ~/.config/nchat/

![screenshot nchat](/doc/screenshot-nchat-basic-color.png)

### Dracula Theme

    cp $(dirname $(which nchat))/../share/nchat/themes/dracula/* ~/.config/nchat/

![screenshot nchat](/doc/screenshot-nchat-dracula.png)

### iTerm2-Color-Schemes Themes

[iTerm2 Color Schemes](https://github.com/mbadolato/iTerm2-Color-Schemes) can
be used to generate themes for nchat. The following themes generated using
iTerm2 Color Schemes are available in `$(dirname $(which nchat))/../share/nchat/themes`:

- Catppuccin Mocha:
  `cp $(dirname $(which nchat))/../share/nchat/themes/catppuccin-mocha/* ~/.config/nchat/`
- Espresso:
  `cp $(dirname $(which nchat))/../share/nchat/themes/espresso/* ~/.config/nchat/`
- Gruvbox Dark:
  `cp $(dirname $(which nchat))/../share/nchat/themes/gruvbox-dark/* ~/.config/nchat/`
- Solarized Dark Higher Contrast:
  `cp $(dirname $(which nchat))/../share/nchat/themes/solarized-dark-higher-contrast/* ~/.config/nchat/`
- Tokyo Night:
  `cp $(dirname $(which nchat))/../share/nchat/themes/tokyo-night/* ~/.config/nchat/`
- Tomorrow Night:
  `cp $(dirname $(which nchat))/../share/nchat/themes/tomorrow-night/* ~/.config/nchat/`
- Zenbones Dark:
  `cp $(dirname $(which nchat))/../share/nchat/themes/zenbones-dark/* ~/.config/nchat/`
- Zenburned:
  `cp $(dirname $(which nchat))/../share/nchat/themes/zenburned/* ~/.config/nchat/`

To generate additional nchat themes and install for use with `nchat`, refer to
[Generating nchat themes from iTerm2 Color Schemes](/themes/templates/iterm2-color-schemes/README.md).

General
-------
Deleting a configuration entry line (while nchat is not running) and starting
nchat will populate the configuration file with the default entry.


Protocol-Specific Configuration
===============================

The following configuration files (listed with current default values) can be
used to configure nchat.

~/.config/nchat/profiles/Telegram_+nnnnn/telegram.conf
------------------------------------------------------
This configuration file holds protocol-specific settings for Telegram. Default
content:

    local_key=
    markdown_enabled=1
    markdown_version=1
    profile_display_name=

### local_key

For internal use by nchat only.

### markdown_enabled

Specifies whether to enable Markdown <-> text conversion for text messages
(default enabled).

### markdown_version

Specifies which Telegram Markdown version to use (default 1).

### profile_display_name

Specifies an optional short/display name in the status bar when using nchat
with multiple profiles. The default profile name is `Telegram` or
`Telegram_+nnnnn` (when more than one Telegram profile is set up) if this
setting is not specified.

~/.config/nchat/profiles/WhatsAppMd_+nnnnn/whatsappmd.conf
----------------------------------------------------------
This configuration file holds protocol-specific settings for WhatsApp. Default
content:

    profile_display_name=

### profile_display_name

Specifies an optional short/display name in the status bar when using nchat
with multiple profiles. The default profile name is `WhatsAppMd` or
`WhatsAppMd_+nnnnn` (when more than one WhatsAppMd profile is set up) if this
setting is not specified.


FAQ
===

### 1. Alt/Opt-keyboard shortcuts are not working?

For Linux please ensure the terminal is configured with
[meta to send escape](https://askubuntu.com/questions/442644/how-to-make-xterm-to-send-the-alt-key-to-emacs-as-meta).
For macOS Terminal ensure that the Terminal profile keyboard setting
"Use Option as Meta key" is enabled.

If issues are still encountered, please use `nchat -k` (keydump mode) to
determine the key codes and modify `~/.config/nchat/key.conf` accordingly.

### 2. Send messages with Enter key?

To simply send on enter key press and skip message compose with linebreaks,
one can just set `send_msg=KEY_RETURN` in `~/.config/nchat/key.conf`.

To also be able to compose messages with linebreaks using Alt/Opt-Enter, edit
`~/.config/nchat/ui.conf` and set `linefeed_on_enter=0`. And in
`~/.config/nchat/key.conf` set `linebreak=\33\15`.

### 3. Custom colors are not shown when running nchat in tmux?

Please try to run nchat with a TERM supporting custom colors, e.g:

    TERM=xterm-256color nchat

### 4. Sent messages are not visible?

For terminals with eight colors (or more) the default color theme displays
sent messages in gray (shaded). Some terminals may wrongly report supporting
more colors than two, or the terminal may be set up with gray mapped to black.
In this case sent / own messages may appear invisible. To avoid nchat using
gray one can edit `~/.config/nchat/color.conf` and remove occurances of `gray`.

### 5. How to use Telegram and WhatsApp concurrently or switch between them?

The **recommended** method is to set up nchat with one config directory per
protocol/phone number, and run each instance in separate terminal windows/tabs.
To simplify such usage one can set up aliases, for example:

    alias telegram='nchat -d ~/.config/nchat-telegram'
    alias whatsapp='nchat -d ~/.config/nchat-whatsapp'

Then use regular setup for them (separately), for example:

    telegram -s
    whatsapp -s

The **alternative** method is to set up multiple protocol accounts in a single
nchat config directory. For each protocol/phone nubmer, run setup mode and exit
after initial sync:

    nchat -s

### 6. How to set up WhatsApp without scanning a QR code?

By default setting up a WhatsApp account will display a QR code to be scanned
using the WhatsApp mobile application on the primary device. As an alternative
one can set an environment flag to have nchat display a pairing code, to be
entered in WhatsApp on the primary device:

    USE_PAIRING_CODE=1 nchat -s


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

- [cereal](https://github.com/USCiLab/cereal) -
  Copyright 2013 Randolph Voorhies, Shane Grant - [BSD-3 License](/ext/cereal/LICENSE)

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


Project Scope
=============

Limitations
-----------
There are no plans to support the following features:
- Facebook Messenger
- Signal
- Telegram secret chats
- Voice / video calls

Additionally, WhatsApp is only supported on macOS and glibc-based Linux
systems. Thus, it is not supported on musl-based operating systems, such
as Alpine Linux. See [issue #204](https://github.com/d99kris/nchat/issues/204)
for technical details on this limitation.

Roadmap
-------
There is currently no concrete roadmap for further feature development of
nchat. It is not intended to be a full-featured client on par with official
Telegram / WhatsApp clients, but rather a light-weight client providing
essential functionality suitable for the terminal. However, feel free to
submit feature requests if there's something missing, or help upvote
[existing feature requests](https://github.com/d99kris/nchat/discussions/categories/ideas?discussions_q=is%3Aopen+category%3AIdeas), and if it's useful and low effort it can probably be added.


License
=======
nchat is distributed under the MIT license. See LICENSE file.


Contributions
=============
Please refer to [Contributing Guidelines](/doc/CONTRIBUTING.md) and
[Design Notes](/doc/DESIGN.md).


Keywords
========
command line, console-based, linux, macos, chat client, ncurses, telegram,
terminal-based.
