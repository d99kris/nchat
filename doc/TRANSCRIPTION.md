# Audio Transcription

nchat can transcribe voice messages to text using Whisper. Press Alt-u on any voice message and boom - you can read it instead of listening.

Works with Telegram and WhatsApp voice notes. You can use OpenAI's API (fast, costs a few cents) or run it locally for free (slower but private).

## Quick Start

**Option 1: OpenAI API (easiest)**

```bash
# Get an API key from https://platform.openai.com/api-keys
export OPENAI_API_KEY='sk-your-key-here'

# Enable in nchat config
echo "audio_transcribe_enabled=1" >> ~/.config/nchat/ui.conf
```

Costs about $0.006 per minute of audio (so like a penny for a 2-minute voice note).

**Option 2: Local Whisper (free, private)**

See [TRANSCRIPTION-SETUP.md](TRANSCRIPTION-SETUP.md) - takes a bit more setup but runs offline.

**Using it:**

1. Select a voice message in nchat
2. Press `Alt-u`
3. Wait for the text to appear

That's it.

## How to Use

Press `Alt-u` on any voice message. The text appears below it:

```
┌─────────────────────────────────────────────┐
│ Alice                         10:30 AM      │
│ 🎤 Voice message (0:15)                     │
│                                             │
│ 📝 Hey, can you pick up groceries on your  │
│    way home? We need milk and eggs.        │
│                                [Transcribed]│
└─────────────────────────────────────────────┘
```

### UI Example

After pressing `Alt-u` on a voice message, the transcription appears inline with the `[Transcribed]` indicator at the start of each transcribed line:

```
 ┌───────────────────────────────────────────────────────────┐
 │ Bob [14:22]                                               │
 │   PTT-20250115-WA0012.opus                                │
 │   [Transcribed] Hey, are you coming to the meeting       │
 │                 at three? Let me know if you need the    │
 │                 dial-in link.                             │
 │                                                           │
 │ Alice [14:23]                                             │
 │   Sure, I'll be there!                                    │
 └───────────────────────────────────────────────────────────┘
```

Long transcriptions are truncated to `audio_transcribe_max_lines` lines (default: 15); the last visible line shows how many lines were hidden.

Press `Alt-u` again on the same message to re-transcribe.

Supports: `.ogg`, `.opus`, `.mp3`, `.m4a`, `.wav`, `.flac`, `.webm`

## Configuration

Edit `~/.config/nchat/ui.conf`:

```conf
audio_transcribe_enabled=1          # Turn it on/off
```

The command that does the work (defaults to the bundled `transcribe` script):
```conf
audio_transcribe_command=/usr/local/libexec/nchat/transcribe -f '%1'
```

You can add flags to it (see below).

## Tweaking It

**Pick a specific service:**
```conf
audio_transcribe_command=/usr/local/libexec/nchat/transcribe -f '%1' -s openai        # OpenAI API
audio_transcribe_command=/usr/local/libexec/nchat/transcribe -f '%1' -s whisper-cpp   # Local server
audio_transcribe_command=/usr/local/libexec/nchat/transcribe -f '%1' -s whisper-local # Local Python
```

**Set the language (better accuracy):**
```conf
audio_transcribe_command=/usr/local/libexec/nchat/transcribe -f '%1' -l en  # English
audio_transcribe_command=/usr/local/libexec/nchat/transcribe -f '%1' -l es  # Spanish
```

Supports 90+ languages (en, es, fr, de, it, pt, ru, zh, ja, ko, etc.)

**Local model sizes:**
```conf
# Pick one based on speed vs accuracy:
... -m tiny     # 75 MB  - fast but meh
... -m base     # 150 MB - good balance
... -m small    # 500 MB - better
... -m medium   # 1.5 GB - pretty good
... -m large    # 3 GB   - best but slow
```

## Keyboard Shortcuts

- `Alt-u` - Transcribe (or re-transcribe) message
- `Alt-l` - Set per-chat transcription language

Change them in `~/.config/nchat/key.conf` if you want (see nchat docs for the escape codes).

## Troubleshooting

**"No API key set"**
```bash
export OPENAI_API_KEY='sk-...'  # Add to ~/.bashrc or ~/.zshrc
```

**"Timeout"**

Switch to a faster service or use a smaller local model. See [TRANSCRIPTION-SETUP.md](TRANSCRIPTION-SETUP.md).

**"Audio format not supported"**

Install ffmpeg:
```bash
brew install ffmpeg  # macOS
sudo apt install ffmpeg  # Linux
```

**Wrong language / bad accuracy**

Specify the language:
```conf
audio_transcribe_command=/usr/local/libexec/nchat/transcribe -f '%1' -l en
```

Or use a bigger model (local) or switch to OpenAI API.

**API costs too high**

Use local Whisper instead (see [TRANSCRIPTION-SETUP.md](TRANSCRIPTION-SETUP.md)).

## Privacy

**OpenAI API:** Audio gets sent to their servers. They may keep it for 30 days. Don't use for super sensitive stuff.

**Local Whisper:** Everything stays on your machine. 100% private.

## Cache Management

Transcriptions are stored per-profile in `~/.config/nchat/history/<profileId>/db.sqlite`.

Clear cached transcriptions if needed:
```bash
sqlite3 ~/.config/nchat/history/<profileId>/db.sqlite "UPDATE messages SET transcription = '' WHERE transcription != '';"
```

## Tips

- OpenAI API is fastest (2-3 sec/message)
- Specify language for better accuracy
- Use local for privacy, API for speed

## FAQ

**Q: Supports video?**
Nope, just audio.

**Q: Offline?**
Yes with local Whisper. No with API.

**Q: How accurate?**
Pretty good (~95%) with clear audio. Gets worse with noise/accents.

**Q: Languages?**
99+ including English, Spanish, French, German, Chinese, Japanese, etc.

## See Also

[TRANSCRIPTION-SETUP.md](TRANSCRIPTION-SETUP.md) - How to set up local Whisper
