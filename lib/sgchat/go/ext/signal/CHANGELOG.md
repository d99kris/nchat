# v26.02

* Bumped minimum Go version to 1.25.
* Updated libsignal to v0.87.1.
* Added automatic recovery for the session not found error from libsignal.
* Fixed sender key state not being cleared on logout properly.

# v26.01

* Updated libsignal to v0.86.12.
* Changed automatic contact list sync option to only sync every 3 days rather
  than on every restart.
* Fixed sending messages to groups with no other registered members.
* Fixed sender key sends failing if some users had changed devices.
* Fixed timestamps of outgoing typing notifications in DMs.

# v25.12

* Updated libsignal to v0.86.8.
* Updated Docker image to Alpine 3.23.
* Added support for dropping incoming DMs from blocked contacts on Signal.
* Added support for sender key encryption when sending to groups, which makes
  sending much faster and enables sending typing notifications.
* Added support for encryption retry receipts.
* Fixed bugs with handling poll votes.
* Fixed history transfer option not showing up when pairing with Signal Android.
* Fixed nicknames being cleared not being bridged
  (thanks to [@Enzime] in [#623]).

[#623]: https://github.com/mautrix/signal/pull/623
[@Enzime]: https://github.com/Enzime

# v25.11

* Updated libsignal to v0.86.4.
* Added support for bridging invite state in groups for phone number invites.
* Added support for polls.
* Fixed PNI signature not being sent when replying to message requests.
* Fixed unnecessary repeating error notices when Signal is down.
* Fixed sticker size metadata on Matrix not matching how native Signal Desktop
  renders them.

# v25.10

* Switched to calendar versioning.
* Updated libsignal to v0.84.0.
* Fixed backfill creating incorrect disappearing timer change notices.

# v0.8.7 (2025-09-16)

* Removed legacy provisioning API and database legacy migration.
  Upgrading directly from versions prior to v0.7.0 is not supported.
  * If you've been using the bridge since before v0.7.0 and have prevented the
    bridge from writing to the config, you must either update the config
    manually or allow the bridge to update it for you **before** upgrading to
    this release (i.e. run v0.8.6 once with config writing allowed).
* Updated libsignal to v0.80.3.
* Added support for `com.beeper.disappearing_timer` state event, which stores
  the disappearing setting of chats and allows changing the setting from Matrix.
* Added support for nicknames in displayname templates.
  * Like contact list names, nicknames are not safe to use on multi-user instances.
* Added support for creating Signal groups.
* Fixed certain types of logouts not being detected properly.

# v0.8.6 (2025-08-16)

* Deprecated legacy provisioning API. The `/_matrix/provision/v2` endpoints will
  be deleted in the next release.
* Bumped minimum Go version to 1.24.
* Updated libsignal to v0.78.2.
* Added support for "delete to me" of chats and messages.
* Added support for latest Signal backup/transfer protocol.

# v0.8.5 (2025-07-16)

* Updated libsignal to v0.76.1.

# v0.8.4 (2025-06-16)

* Updated libsignal to v0.74.1.
* Updated Docker image to Alpine 3.22.
* Fixed avatars when using direct media.
* Fixed starting chats with non-contact users.
* Fixed Matrix media being rejected if the mime type isn't specified.

# v0.8.3 (2025-05-16)

* Updated libsignal to v0.72.1.
* Added initial support for direct media access.
  * Note that media is only kept on the Signal servers for 45 days, after which
    any direct media links will permanently stop working.
* Added buffer for decrypted events to prevent losing messages if the bridge is
  stopped in the middle of event handling.
* Fixed backfilling messages in existing portals after relogining.

# v0.8.2 (2025-04-16)

* Updated libsignal to v0.70.0.
* Fixed panics in some cases when the bridge was under heavy load.

# v0.8.1 (2025-03-16)

* Added QR refreshing when logging in.
* Updated libsignal to v0.67.4.

# v0.8.0 (2025-02-16)

* Bumped minimum Go version to 1.23.
* Added support for history transfer.
* Updated libsignal to v0.66.2.

# v0.7.5 (2025-01-16)

* Added support for bridging mp4 gifs in both directions.
* Added support for signaling supported features to clients using the
  `com.beeper.room_features` state event.
* Updated Signal websocket authentication method.
* Fixed some cases where websocket would get stuck after a ping timeout.

# v0.7.4 (2024-12-16)

* Fixed syncing server-side storage after Signal login.
* Added support for new SSRE2 method of receiving the server-side storage key.
* Updated libsignal to v0.64.1.
* Updated Docker image to Alpine 3.21.

# v0.7.3 (2024-11-16)

* Updated libsignal to v0.62.0.
  * Note for bridges running in systemd: the new version of libsignal may be
    incompatible with the `MemoryDenyWriteExecute=true` option (see [#750]).
* Added basic support for Signal's new file upload protocol.

[#750]: https://github.com/mautrix/signal/issues/570

# v0.7.2 (2024-10-16)

* Updated to libsignal v0.58.3.
* Fixed spurious decryption error notices for Signal messages when the
  websocket reconnects and receives old already-bridged messages.
* Fixed signalmeow not respecting account settings for choosing sender
  certificate.
* Fixed bugs in storage service decryption, which could cause issues with
  missing contact names among other things.
* Fixed call start notices only working once per direct chat.

# v0.7.1 (2024-09-16)

* Updated to libsignal v0.57.1.
* Dropped support for unauthenticated media on Matrix.
* Added support for Matrix->Signal power level bridging
  (thanks to [@maltee1] in [#531]).
* Changed voice message conversion to convert to aac instead of m4a,
  because Signal iOS doesn't appear to like ffmpeg's m4a files.
* Fixed outgoing sync messages not including disappearing start timestamp,
  which would cause native clients to disappear messages at the wrong time.
* Re-added notices about decryption errors.

[#531]: https://github.com/mautrix/signal/pull/531

# v0.7.0 (2024-08-16)

* Bumped minimum Go version to 1.22.
* Updated to libsignal v0.55.0.
* Rewrote bridge using bridgev2 architecture.
  * It is recommended to check the config file after upgrading. If you have
    prevented the bridge from writing to the config, you should update it
    manually.
  * Thanks to [@maltee1] for reimplementing Matrix -> Signal membership
    handling in the rewrite.
  * If you are still somehow using a pre-v0.5.0 versions, upgrading to v0.6.3
    is required before upgrading to v0.7.0 or higher.

# v0.6.3 (2024-07-16)

* Updated to libsignal v0.52.0.
* Fixed bridge losing track of user phone numbers in some cases.
* Fixed edge cases in handling new outgoing DMs started from other devices.
* Added `sync groups` command (thanks to [@maltee1] in [#490]).
* Fixed typo in location bridging example config
  (thanks to [@AndrewFerr] in [#516]).

[#490]: https://github.com/mautrix/signal/pull/490
[#516]: https://github.com/mautrix/signal/pull/516
[@AndrewFerr]: https://github.com/mautrix/signal/pull/516

# v0.6.2 (2024-06-16)

* Updated to libsignal v0.51.0.
* Fixed voice messages not being rendered correctly in Element X.
* Fixed contact avatars not being bridged correctly even when enabled in
  the bridge config.
* Implemented connector for the upcoming bridgev2 architecture.

# v0.6.1 (2024-05-16)

* Added support for bridging location messages from Matrix to Signal
  (thanks to [@maltee1] in [#504]).
  * Note that Signal doesn't support real location messages, so they're just
    bridged as links. The link template is configurable.
* Fixed bridging long text messages from Signal
  (thanks to [@maltee1] in [#506]).
* Improved handling of ping timeouts in Signal websocket.

[#504]: https://github.com/mautrix/signal/pull/504
[#506]: https://github.com/mautrix/signal/pull/506

# v0.6.0 (2024-04-16)

* Updated to libsignal v0.44.0.
* Refactored bridge to support Signal's new phone number identifier (PNI)
  system in order to fix starting new chats and receiving messages from new
  users.
  * When starting a chat with a user you haven't talked to before, the portal
    room will not have a ghost user for the recipient until they accept the
    message request.
* Added support for syncing existing groups on login instead of having to wait
  for new messages.
* Added notices if decrypting incoming message from Signal fails.
* Added bridging of group metadata from Matrix to Signal
  (thanks to [@maltee1] in [#461]).
* Added command to create new Signal group for Matrix room
  (thanks to [@maltee1] in [#461] and [#491]).
* Added commands for inviting users to Signal groups by phone number
  (thanks to [@maltee1] in [#495]).
* Improved handling of missed Signal group metadata changes
  (thanks to [@maltee1] in [#488]).

[#461]: https://github.com/mautrix/signal/pull/461
[#488]: https://github.com/mautrix/signal/pull/488
[#491]: https://github.com/mautrix/signal/pull/491
[#495]: https://github.com/mautrix/signal/pull/495

# v0.5.1 (2024-03-16)

* Updated to libsignal v0.41.0.
* Fixed sending messages to groups.
* Fixed some cases of ghost user info changing repeatedly on multi-user
  instances.
* Fixed migrating SQLite databases from Python version.

# v0.5.0 (2024-02-16)

* Rewrote bridge in Go.
  * To migrate the bridge, simply upgrade in-place. The database and config
    will be migrated automatically, although some parts of the config aren't
    migrated (e.g. log config). If you prevented the bridge from writing to
    the config file, you'll have to temporarily allow it or update it yourself.
  * The bridge doesn't use signald anymore, all users will have to re-link the
    bridge. signald can be deleted after upgrading.
  * Primary device mode is no longer supported, signal-cli is recommended if
    you don't want to use the official Signal mobile apps.
  * Some old features are not yet supported (e.g. group management features).
* Renamed main branch from `master` to `main`.
* Added support for edits and message formatting.

# v0.4.3 (2023-05-17)

Target signald version: [v0.23.2](https://gitlab.com/signald/signald/-/releases/0.23.2)

* Added option to not set name/avatar for DM rooms even if the room is encrypted.
* Added options to automatically ratchet/delete megolm sessions to minimize
  access to old messages.
* Added command to request group/contact sync from primary device.
* Added error notices if incoming attachments are dropped.
* Fixed bugs with creating groups.
* Fixed handling changes to disappearing message timer in groups.

## Changes by [@maltee1]

* Added bridging of group join requests on Signal to knocks on Matrix ([#275]).
* Added bridging of banned users from Signal to Matrix ([#275]).
* Added admin command to force logout other Matrix users from the bridge ([#359]).
* Added `submit-challenge` command to submit captcha codes when encountering
  ratelimits on sending messages ([#320]).
* Added invite command for inviting Signal users to a group by phone number ([#285]).
* Added support for bridging Matrix invites to Signal via relay user ([#285]).
* Added automatic group creation when inviting multiple Signal ghosts to a
  non-DM room ([#294]).
* Fixed ghost user getting kicked from Matrix room when trying to invite a user
  who's already in the group on Signal ([#345]).
* Fixed bridging power levels from Signal for users who are logged into the
  bridge, but don't have double puppeting enabled ([#333]).

[#275]: https://github.com/mautrix/signal/pull/275
[#285]: https://github.com/mautrix/signal/pull/285
[#294]: https://github.com/mautrix/signal/pull/294
[#320]: https://github.com/mautrix/signal/pull/320
[#333]: https://github.com/mautrix/signal/pull/333
[#345]: https://github.com/mautrix/signal/pull/345
[#359]: https://github.com/mautrix/signal/pull/359

# v0.4.2 (2022-12-03)

Target signald version: [v0.23.0](https://gitlab.com/signald/signald/-/releases/0.23.0)

* Fixed database schema upgrade for users who used SQLite before it was
  stabilized in v0.4.1.
* Fixed error in commands that use phone numbers (like `!signal pm`).
* Fixed updating private chat portal metadata when Signal user info changes.
* Updated Docker image to Alpine 3.17.

# v0.4.1 (2022-10-28)

Target signald version: [v0.23.0](https://gitlab.com/signald/signald/-/releases/0.23.0)

* Dropped support for phone numbers as Signal user identifiers.
* Dropped support for v1 groups.
* Promoted SQLite support to non-experimental level.
* Fixed call notices not having a plaintext `body` field.
* "Implicit" messages from Signal (things like read receipts) that fail to
  decrypt will no longer send a notice to the Matrix room.
* The docker image now has an option to bypass the startup script by setting
  the `MAUTRIX_DIRECT_STARTUP` environment variable. Additionally, it will
  refuse to run as a non-root user if that variable is not set (and print an
  error message suggesting to either set the variable or use a custom command).

# v0.4.0 (2022-09-17)

Target signald version: [v0.21.1](https://gitlab.com/signald/signald/-/releases/0.21.1)

**N.B.** This release requires a homeserver with Matrix v1.1 support, which
bumps up the minimum homeserver versions to Synapse 1.54 and Dendrite 0.8.7.
Minimum Conduit version remains at 0.4.0.

### Added
* Added provisioning API for checking if a phone number is registered on Signal
* Added admin command for linking to an existing account in signald.
* Added Matrix -> Signal bridging for invites, kicks, bans and unbans
  (thanks to [@maltee1] in [#246] and [#257]).
* Added command to create Signal group for existing Matrix room
  (thanks to [@maltee1] in [#250]).
* Added Matrix -> Signal power level change bridging
  (thanks to [@maltee1] in [#260] and [#263]).
* Added join rule bridging in both directions (thanks to [@maltee1] in [#268]).
* Added Matrix -> Signal bridging of location messages
  (thanks to [@maltee1] in [#287]).
  * Since Signal doesn't have actual location messages, they're just bridged as
    map links. The link template is configurable.
* Added command to link devices when the bridge is the primary device
  (thanks to [@Craeckie] in [#221]).
* Added command to bridge existing Matrix rooms to existing Signal groups
  (thanks to [@MaximilianGaedig] in [#288]).
* Added config option to auto-enable relay mode when a specific user is invited
  (thanks to [@maltee1] in [#293]).
* Added options to make encryption more secure.
  * The `encryption` -> `verification_levels` config options can be used to
    make the bridge require encrypted messages to come from cross-signed
    devices, with trust-on-first-use validation of the cross-signing master
    key.
  * The `encryption` -> `require` option can be used to make the bridge ignore
    any unencrypted messages.
  * Key rotation settings can be configured with the `encryption` -> `rotation`
    config.

### Improved
* Improved/fixed handling of disappearing message timer changes.
* Improved handling profile/contact names and prevented them from being
  downgraded (switching from profile name to contact name or phone number).
* Updated contact list provisioning API to not block if signald needs to update
  profiles.
* Trying to start a direct chat with a non-existent phone number will now reply
  with a proper error message instead of throwing an exception
  (thanks to [@maltee1] in [#265]).
* Syncing chat members will no longer be interrupted if one of the member
  profiles is unavailable (thanks to [@maltee1] in [#270]).
* Group metadata changes are now bridged based on the event itself rather than
  resyncing the whole group, which means changes will use the correct ghost
  user instead of always using the bridge bot (thanks to [@maltee1] in [#283]).
* Added proper captcha error handling when registering
  (thanks to [@maltee1] in [#280]).
* Added user's phone number as topic in private chat portals
  (thanks to [@maltee1] in [#282]).

### Fixed
* Call start notices work again

[@Craeckie]: https://github.com/Craeckie
[@MaximilianGaedig]: https://github.com/MaximilianGaedig
[#221]: https://github.com/mautrix/signal/pull/221
[#246]: https://github.com/mautrix/signal/pull/246
[#250]: https://github.com/mautrix/signal/pull/250
[#257]: https://github.com/mautrix/signal/pull/257
[#260]: https://github.com/mautrix/signal/pull/260
[#263]: https://github.com/mautrix/signal/pull/263
[#265]: https://github.com/mautrix/signal/pull/265
[#268]: https://github.com/mautrix/signal/pull/268
[#270]: https://github.com/mautrix/signal/pull/270
[#280]: https://github.com/mautrix/signal/pull/280
[#282]: https://github.com/mautrix/signal/pull/282
[#283]: https://github.com/mautrix/signal/pull/283
[#287]: https://github.com/mautrix/signal/pull/287
[#288]: https://github.com/mautrix/signal/pull/288
[#293]: https://github.com/mautrix/signal/pull/293

# v0.3.0 (2022-04-20)

Target signald version: [v0.18.0](https://gitlab.com/signald/signald/-/releases/0.18.0)

### Important changes
* Both the signald and mautrix-signal docker images have been changed to run as
  UID 1337 by default. The migration should work automatically as long as you
  update both containers at the same time.
  * Also note that the `finn/signald` image is deprecated, you should use `signald/signald`.
    <https://signald.org/articles/install/docker/>
* Old homeservers which don't support the new `/v3` API endpoints are no longer
  supported. Synapse 1.48+, Dendrite 0.6.5+ and Conduit 0.4.0+ are supported.
  Legacy `r0` API support can be temporarily re-enabled with `pip install mautrix==0.16.0`.
  However, this option will not be available in future releases.

### Added
* Support for creating DM portals by inviting user (i.e. just making a DM the
  normal Matrix way).
* Leaving groups is now bridged to Signal (thanks to [@maltee1] in [#245]).
* Signal group descriptions are now bridged into Matrix room topics.
* Signal announcement group status is now bridged into power levels on Matrix
  (the group will be read-only for everyone except admins).
* Added optional parameter to `mark-trusted` command to set trust level
  (instead of always using `TRUSTED_VERIFIED`).
* Added option to use [MSC2246] async media uploads.
* Added provisioning API for listing contacts and starting private chats.

### Improved
* Dropped Python 3.7 support.
* Files bridged to Matrix now include the `size` field in the file `info`.
* Removed redundant `msgtype` field in sticker events sent to Matrix.
* Users who have left the group on Signal will now be removed from Matrix too.

### Fixed
* Logging into the bridge with double puppeting no longer removes your Signal
  user's Matrix ghost from DM portals with other bridge users.
* Fixed identity failure error message always saying "while sending message to
  None" instead of specifying the problematic phone number.
* Fixed `channel` -> `id` field in `m.bridge` events.

[MSC2246]: https://github.com/matrix-org/matrix-spec-proposals/pull/2246
[@maltee1]: https://github.com/maltee1
[#245]: https://github.com/mautrix/signal/pull/245

# v0.2.3 (2022-02-17)

Target signald version: [v0.17.0](https://gitlab.com/signald/signald/-/releases/0.17.0)

**N.B.** This will be the last release to support Python 3.7. Future versions
will require Python 3.8 or higher. In general, the mautrix bridges will only
support the lowest Python version in the latest Debian or Ubuntu LTS.

### Added
* New v2 link API to provide immediate feedback after the QR code is scanned.

### Improved
* Added automatic retrying for failed Matrix->Signal reactions.
* Commands using phone numbers will now try to resolve the UUID first
  (especially useful for `pm` so the portal is created with the correct ghost
  immediately)
* Improved signald socket handling to catch weird errors and reconnect.

### Fixed
* Fixed catching errors when connecting to Signal (e.g. if the account was
  deleted from signald's database, but not the bridge's).
* Fixed handling message deletions from Signal.
* Fixed race condition in incoming message deduplication.

# v0.2.2 (2022-01-15)

Target signald version: [v0.16.1](https://gitlab.com/signald/signald/-/releases/0.16.1)

### Added
* Support for disappearing messages.
  * Disabled by default in group chats, as there's no way to delete messages
    from the view of a single Matrix user. For single-user bridges, it's safe
    to enable the `enable_disappearing_messages_in_groups` config option.
* Notifications about incoming calls.
* Support for voice messages with [MSC3245].
* Support for incoming contact share messages.
* Support for long text messages from Signal.

### Improved
* Formatted all code using [black](https://github.com/psf/black)
  and [isort](https://github.com/PyCQA/isort).
* Moved most relay mode code to mautrix-python to be shared with other bridges.
* The bridge will now queue messages temporarily if signald is down while sending.
* Removed legacy community-related features.
* Updated remaining things to use signald's v1 API.

### Fixed
* Fixed empty DM rooms being unnecessarily created when receiving
  non-bridgeable events (e.g. profile key updates).
* Fixed duplicate rooms being created in certain cases due to the room mapping
  cache not working.
* Fixed replies to attachments not rendering on Signal iOS properly.

[MSC3245]: https://github.com/matrix-org/matrix-doc/pull/3245

# v0.2.1 (2021-11-28)

Target signald version: [v0.15.0](https://gitlab.com/signald/signald/-/releases/0.15.0)

### Added
* Support for Matrix->Signal redactions.
* Error messages to Matrix when sending to Signal fails.
* Custom flag to invite events that will be auto-accepted with double puppeting.
* Command to get group invite link.
* Support for custom bridge bot welcome messages
  (thanks to [@justinbot] in [#146]).
* Option to disable federation in portal rooms.
* Option to prevent users from registering the bridge as their primary device
  (thanks to [@tadzik] in [#153]).
* Extremely experimental support for SQLite. It's probably broken in some
  cases, so don't use it.

### Improved
* Increased line length limit for signald socket (was causing the connection to
  fail when there was too much data going through).
* Improved Signal disconnection detection (mostly affects prometheus metrics).
* Updated provisioning API `/link/wait` endpoint to return HTTP 400 if signald
  loses connection to Signal.

### Fixed
* Fixed bugs with automatic migration of Matrix ghosts from phone number to
  UUID format.
* Fixed handling empty Signal avatar files.

[@justinbot]: https://github.com/justinbot
[@tadzik]: https://github.com/tadzik
[#146]: https://github.com/mautrix/signal/pull/146
[#153]: https://github.com/mautrix/signal/pull/153

# v0.2.0 (2021-08-07)

Target signald version: [v0.14.1](https://gitlab.com/signald/signald/-/releases/0.14.1)

**N.B.** Docker images have moved from `dock.mau.dev/tulir/mautrix-signal` to
`dock.mau.dev/mautrix/signal`. New versions are only available at the new path.

### Added
* Relay mode (see [docs](https://docs.mau.fi/bridges/general/relay-mode.html)).
* Added captcha parameter to help text of register command.
* Option to delete unknown accounts from signald when starting the bridge.

### Improved
* Contact info is now synced when other devices send contact list updates.
* Contact avatars will now be used if profile avatar isn't available and
  contact names are allowed.
* Linking a new device or registering now uses the `overwrite` param in
  signald, which means it will force-login even if there is an existing
  signald session with the same phone number.
* Updated Docker image to Alpine 3.14.

### Fixed
* Fixed Signal delivery receipts being handled as read receipts.
* Fixed logging out causing signald to get into a bad state.
* Fixed handling conflicting puppet rows when finding UUID.

# v0.1.1 (2021-04-07)

Target signald version: [v0.13.1](https://gitlab.com/signald/signald/-/tags/0.13.1)

### Added
* Support for group v2 avatars.
* Syncing of group permissions from Signal.
* Support for accepting Signal group invites.
* Support for Matrix->Signal group name and avatar changes.
* Support for real-time group info updates from Signal.
* Hidden captcha support in register command.
* Command to mark safety numbers as trusted.
* Workaround for Element iOS image rendering bug
  (thanks to [@celogeek] in [#57]).

### Improved
* Commands that take phone numbers now tolerate unnecessary characters a bit better.
* Updated to signald v1 protocol for most requests.

### Fixed
* Failure to bridge attachments if the `outgoing_attachment_dir` didn't exist.
* Errors with no messages from signald not being parsed properly.

[@celogeek]: https://github.com/celogeek
[#57]: https://github.com/mautrix/signal/pull/57

# v0.1.0 (2021-02-05)

Initial release.
