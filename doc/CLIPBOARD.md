Clipboard
=========
On supported platforms (macOS, X11, Wayland) nchat uses the system
clipboard. For other platforms, or custom behavior, one may configure below
parameters in `app.conf`.

**Note:** The examples provided below are mainly for reference, not useful
as actual configuration values (except the `File-based` examples), as the
main platforms should work with the built-in support in nchat.

### clipboard_copy_command

Specifies a custom copy (and cut) command to be used instead of system
clipboard. Examples:

| Platform    | Command                                                   |
|-------------|-----------------------------------------------------------|
| File-based  | `tee ~/.clipboard`                                        |
| macOS       | `pbcopy`                                                  |
| Wayland     | `wl-copy`                                                 |
| X11         | `xclip -selection clipboard`                              |

### clipboard_has_image_command

Specifies a custom command for checking if the clipboard contains an image.
The command shall output `1` if image is present, otherwise `0`. Examples:

| Platform    | Command                                                   |
|-------------|-----------------------------------------------------------|
| Wayland     | `wl-paste --list-types \| grep -m1 'image/png' \| wc -l`  |

### clipboard_paste_command

Specifies a custom paste command to be used instead of system clipboard.
Examples:

| Platform    | Command                                                   |
|-------------|-----------------------------------------------------------|
| File-based  | `cat ~/.clipboard`                                        |
| macOS       | `pbpaste`                                                 |
| Wayland     | `wl-paste`                                                |
| X11         | `xclip -o -selection clipboard`                           |

### clipboard_paste_image_command

Specifies a custom image paste command to be used instead of system clipboard.
Examples:

| Platform    | Command                                                   |
|-------------|-----------------------------------------------------------|
| Wayland     | `wl-paste --type image/png`                               |

