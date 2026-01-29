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

Use `Alt-Shift-u` to re-transcribe if you want to ignore the cache (like if the first try messed up).

Supports: `.ogg`, `.opus`, `.mp3`, `.m4a`, `.wav`, `.flac`

## Configuration

Edit `~/.config/nchat/ui.conf`:

```conf
audio_transcribe_enabled=1          # Turn it on/off
audio_transcribe_cache=1             # Cache results (saves API costs)
audio_transcribe_inline=1            # Show text below message
audio_transcribe_auto=0              # Don't auto-transcribe (costs $$$)
audio_transcribe_timeout=30          # Wait max 30 seconds
```

The command that does the work:
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

- `Alt-u` - Transcribe message
- `Alt-Shift-u` - Re-transcribe (ignore cache)
- `Ctrl-t` - Toggle visibility

Change them in `~/.config/nchat/key.conf` if you want (see nchat docs for the escape codes).

## Troubleshooting

**"No API key set"**
```bash
export OPENAI_API_KEY='sk-...'  # Add to ~/.bashrc or ~/.zshrc
```

**"Timeout"**

Bump the timeout or use a faster service:
```conf
audio_transcribe_timeout=60
```

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

Turn off auto-transcribe (`audio_transcribe_auto=0`) and use local Whisper instead (see [TRANSCRIPTION-SETUP.md](TRANSCRIPTION-SETUP.md)).

## Privacy

**OpenAI API:** Audio gets sent to their servers. They may keep it for 30 days. Don't use for super sensitive stuff.

**Local Whisper:** Everything stays on your machine. 100% private.

## Cache Management

Transcriptions are cached in `~/.config/nchat/db.sqlite`.

Clear cache if needed:
```bash
sqlite3 ~/.config/nchat/db.sqlite "DELETE FROM transcriptions;"
```

## Tips

- OpenAI API is fastest (2-3 sec/message)
- Keep caching enabled to save money
- Specify language for better accuracy
- Use local for privacy, API for speed
- Don't enable auto-transcribe unless you hate money

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
