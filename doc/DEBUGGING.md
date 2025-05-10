Debugging
=========
If any issues are observed, try running nchat with verbose logging

    nchat --verbose

and provide a copy of `~/.config/nchat/log.txt` when reporting a bug.

If nchat **crashes** it's very useful to obtain a core dump and extract
information from it (such as the callstacks). See details below.

If nchat **hangs** one can trigger a core dump for example by running:

    killall -SIGQUIT nchat

**Note:** Always review log files and callstacks for sensitive information
before posting/sharing them online. Alternatively get direct contact details
(such as email address) of a maintainer and share with them privately.


Core Dumps - macOS
==================
First ensure that `/cores` is writable for current user. One can make it
writable for all users by running:

    sudo chmod og+w /cores

Then enable core dumps only for nchat by editing `~/.config/nchat/app.conf`
and setting:

    coredump_enabled=1

Alternatively enable core dumps for all processes in the current shell:

    ulimit -c unlimited

Then start nchat and reproduce the crash. Once a crash is encountered there
will be a message in the terminal like `Quit: 3 (core dumped)`. List files
under `/cores` to idenfify which core dump was just created:

    ls -lhrt /cores

Open the core dump in the debugger `lldb`, example:

    lldb --core /cores/core.63711 $(which nchat)

Obtain callstacks from all threads and then exit:

    bt all
    quit

Maintainers of nchat may need help to extract more information from the core
dump, so it's not recommended to delete it immediately. But once the bug has
been fixed, it's a good idea to remove the core dump as may take up a
substantial amount of disk space:

    rm -f /cores/core.63771


Core Dumps - Linux
==================
Enable core dumps only for nchat by editing `~/.config/nchat/app.conf` and
setting:

    coredump_enabled=1

Alternatively enable core dumps for all processes in the current shell:

    ulimit -c unlimited

Then start nchat and reproduce the crash. Once a crash is encountered there
will be a message in the terminal like `Quit (core dumped)`. Assuming the
system has `coredumpctl`, one can list core dumps using:

    coredumpctl list nchat

Obtain the process id from the PID column and start the debugger `gdb` using
it, for example:

    coredumpctl debug 35919

Optionally, enable logging (to ./gdb.txt by default)

    set logging enabled on

Obtain callstacks from all threads and then exit:

    thread apply all bt
    quit

Core dumps are automatically cleaned / rotated by systemd (commonly after
three days), so typically no active step is needed to delete them.

Linux Manually Managed Core Dumps
---------------------------------
When `coredumpctl` is not available, one can determine default core dump
filename and path using:

    cat /proc/sys/kernel/core_pattern

Common values are `|/usr/share/apport/apport ...` (for Ubuntu) and `core`.
For `coredumpctl` the value may be `|/lib/systemd/systemd-coredump ...`.

### core

The core dump should be written to current working directory, and be possible
to open using:

    gdb $(which nchat) core

Optionally, enable logging (to ./gdb.txt by default)

    set logging enabled on

Obtain callstacks from all threads and then exit:

    thread apply all bt
    quit

### |/usr/share/apport/apport

List core dumps:

    ls -lhrt /var/lib/apport/coredump

Identify the core dump and open it using the debugger `gdb`, for example:

    gdb $(which nchat) /var/lib/apport/coredump/core._usr_local_bin_nchat.1000.1fd09cc0-3f67-4bcc-8cfe-d2bc27766b69.11359.131042

Optionally, enable logging (to ./gdb.txt by default)

    set logging enabled on

Obtain callstacks from all threads and then exit:

    thread apply all bt
    quit

