Project Scope
=============
nchat is feature-complete and in maintenance mode. It aims to be a
light-weight terminal-based client covering the essentials of Telegram,
WhatsApp and Signal - not a full-featured client on par with the official ones.

Maintenance mode means the main project activities are:

- Fixing bugs
- Protocol library updates (tdlib, whatsmeow, signalmeow)
- Build, packaging and portability fixes
- Documentation improvements and corrections

Minor feature requests may still be considered in exceptional cases, if they
are:

- Limited in scope (typically < 100 added lines)
- Widely requested (gauged by number of upvotes)
- Supporting all three protocols (Telegram, WhatsApp, Signal)
- Not diverging from existing UI design / concepts
- Not introducing new build/link dependencies
- Not making changes to bundled third-party libraries

Refer to [Contributing](/doc/CONTRIBUTING.md) for how to report or submit
changes.

Forks are welcome
-----------------
If you want nchat with more features, feel free to fork it. The source is MIT
licensed specifically so that you can. If you build something people use,
feel free to share it in a GitHub discussion.

Practical notes:

- **Use your own Telegram API id and hash**, obtained from
  [https://my.telegram.org/](https://my.telegram.org/). nchat ships with its
  own as a default, and a fork distributing binaries built against it would
  get that registration rate-limited or banned. Feel free to ask in the
  `Discussions` section or `nchat-users` telegram group if unsure how to
  customize the id and hash for your fork.

- **State clearly that it is a fork and not affiliated**, so that your users
  file bugs with you rather than here.
