# Features & roadmap

* Matrix → Signal
  * [x] Message content
    * [x] Text
    * [x] Formatting
    * [x] Mentions
    * [x] Polls
    * [x] Media
      * [x] Images
      * [x] Audio files
      * [x] Voice messages
      * [x] Files
      * [x] Gifs
      * [x] Locations
      * [x] Stickers
  * [x] Message edits
  * [x] Message reactions
  * [x] Message redactions
  * [x] Group info changes
    * [x] Name
    * [x] Avatar
    * [x] Topic
  * [ ] Membership actions
    * [ ] Join (accepting invites)
    * [x] Invite
    * [x] Leave
    * [x] Kick/Ban/Unban
  * [x] Group permissions
  * [x] Typing notifications
  * [x] Read receipts
  * [x] Delivery receipts (sent after message is bridged)
* Signal → Matrix
  * [ ] Message content
    * [x] Text
    * [x] Formatting
    * [x] Mentions
    * [x] Polls
    * [ ] Media
      * [x] Images
      * [x] Voice notes
      * [x] Files
      * [x] Gifs
      * [x] Stickers
      * [x] Contacts
      * [ ] Payment messages
  * [x] Message edits
  * [x] Message reactions
  * [x] Remote deletions
  * [x] Initial profile/contact info
  * [ ] Profile/contact info changes
    * [x] When restarting bridge or syncing
    * [ ] Real time
  * [x] Group info
    * [x] Name
    * [x] Avatar
    * [x] Topic
  * [x] Membership actions
    * [x] Join
    * [x] Invite
    * [x] Request join (via invite link, requires a client that supports knocks)
    * [x] Leave
    * [x] Kick/Ban/Unban
  * [x] Group permissions
  * [x] Typing notifications
  * [x] Read receipts
  * [ ] Delivery receipts (there's no good way to bridge these)
  * [x] Disappearing messages
* Misc
  * [x] Automatic portal creation
    * [x] After login
    * [x] When receiving message
  * [x] Linking as secondary device
  * [ ] Registering as primary device
  * [x] Private chat/group creation by inviting Matrix puppet of Signal user to new room
  * [x] Option to use own Matrix account for messages sent from other Signal clients
