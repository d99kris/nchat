# Transcription Setup

How to set up different transcription backends for nchat.

## What You Need

- nchat installed
- FFmpeg (`ffmpeg --version`)

---

## Option 1: whisper.cpp (Recommended)

No Python required. Free, private, offline. `whisper.cpp` is available as a system package on most distributions.

**Install whisper.cpp:**

- **Arch:** `sudo pacman -S whisper.cpp` or AUR: `yay -S whisper.cpp`
- **Debian/Ubuntu (sid):** `sudo apt install whisper.cpp`
- **Fedora:** `sudo dnf install whisper-cpp`
- **macOS:** `brew install whisper-cpp`

**Download a model** (e.g., `base` for speed, `large-v3` for accuracy):

```bash
mkdir -p ~/.local/share/whisper
wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin \
  -O ~/.local/share/whisper/ggml-base.bin
```

(On macOS, use `curl -L` instead of `wget`.)

**Create a transcribe script:**

Create `~/.config/nchat/transcribe.sh`:

```bash
#!/usr/bin/env bash
ffmpeg -loglevel quiet -i "$1" -f wav - \
  | whisper-cli -m ~/.local/share/whisper/ggml-base.bin -l auto -otxt -f -
```

Make it executable:

```bash
chmod +x ~/.config/nchat/transcribe.sh
```

**Configure nchat** (`~/.config/nchat/ui.conf`):

```conf
audio_transcribe_enabled=1
audio_transcribe_command=~/.config/nchat/transcribe.sh '%1'
audio_transcribe_cache=1
```

**Test:**

```bash
~/.config/nchat/transcribe.sh /path/to/test.ogg
```

> **Tip:** The `audio_transcribe_command` is not special — any script that reads an audio file
> path as its first argument and prints the transcription to stdout will work. Write your own!

---

## Option 2: Local Python Whisper (faster-whisper)

Free, private, simple. Uses Python. Slower than whisper.cpp, uses more RAM.

**Setup:**

1. Install ffmpeg and Python 3.13 (or earlier; 3.14+ not yet supported):
   ```bash
   brew install ffmpeg python@3.13          # macOS
   sudo apt install ffmpeg python3.13       # Debian/Ubuntu
   sudo dnf install ffmpeg python3.13       # Fedora
   ```

2. Create a virtual environment with `uv` (recommended; `python -m venv` also works):
   ```bash
   mkdir -p ~/.config/nchat
   cd ~/.config/nchat
   uv venv --python 3.13
   uv pip install faster-whisper
   ```

   > **Note:** Most Linux distributions no longer allow system-wide `pip install`.
   > Use `uv` (install via `curl -LsSf https://astral.sh/uv/install.sh | sh`)
   > or `python -m venv` instead.

3. Configure nchat:
   ```conf
   audio_transcribe_enabled=1
   audio_transcribe_command=~/.config/nchat/.venv/bin/python /usr/local/libexec/nchat/transcribe -f '%1' -s whisper-local -m base
   audio_transcribe_cache=1
   ```

4. Test:
   ```bash
   /usr/local/libexec/nchat/transcribe -f /path/to/test.ogg -s whisper-local
   ```

Models download automatically on first use. Pick a size: `tiny` (fast, meh), `base` (balanced), `small`/`medium`/`large` (slower, better).

Got an NVIDIA GPU? Install CUDA first, then:
```bash
uv pip install faster-whisper[gpu]
```

---

## Option 3: Groq API (Faster than OpenAI, Cheaper)

Cloud-based, free tier available. Fast ($0.001/min vs OpenAI's $0.006/min).

**Setup:**

1. Get API key from https://console.groq.com/

2. Install with uv or system pip:
   ```bash
   uv pip install groq requests
   ```

3. Set the key:
   ```bash
   export GROQ_API_KEY='gsk-...'
   echo 'export GROQ_API_KEY="gsk-..."' >> ~/.bashrc
   echo 'export GROQ_API_KEY="gsk-..."' >> ~/.zshenv  # macOS
   ```

4. You'll need to hack the transcribe script to add Groq support (it's not built-in yet). Or use OpenAI.

---

## Option 4: OpenAI API

Cloud-based, easiest. Fast (2-3 sec), accurate. Costs $0.006/min. Audio goes to OpenAI.

**Setup:**

1. Get API key: https://platform.openai.com/api-keys

2. Install packages:
   ```bash
   uv pip install openai requests
   ```

3. Set the key:
   ```bash
   export OPENAI_API_KEY='sk-...'
   echo 'export OPENAI_API_KEY="sk-..."' >> ~/.bashrc
   ```

   macOS users also need:
   ```bash
   echo 'export OPENAI_API_KEY="sk-..."' >> ~/.zshenv
   ```

4. Configure nchat (`~/.config/nchat/ui.conf`):
   ```conf
   audio_transcribe_enabled=1
   audio_transcribe_cache=1
   ```

5. Test:
   ```bash
   /usr/local/libexec/nchat/transcribe -f /path/to/test.ogg
   ```

Monitor costs at https://platform.openai.com/usage (set a budget limit!)

---

## Testing

After setup:

```bash
# Test your transcription command
~/.config/nchat/transcribe.sh /path/to/test.ogg

# Or for API-based:
/usr/local/libexec/nchat/transcribe -f /path/to/test.ogg

# Check nchat config
grep transcribe ~/.config/nchat/ui.conf
```

In nchat: Select a voice message, press `Alt-u`, see if it works.

---

## Troubleshooting

**"Command not found"**
```bash
ls -l /usr/local/libexec/nchat/transcribe  # Check if it exists
ls -l ~/.config/nchat/transcribe.sh        # Check if script exists
```

**"No module named 'openai'" or other Python imports**
```bash
# If using uv:
uv pip install openai  # or whatever package is missing

# If using venv:
~/.config/nchat/.venv/bin/pip install openai
```

**"API key not set"**
```bash
echo $OPENAI_API_KEY  # Check it's set
echo $GROQ_API_KEY    # Check it's set
echo 'export OPENAI_API_KEY="sk-..."' >> ~/.bashrc
echo 'export OPENAI_API_KEY="sk-..."' >> ~/.zshenv  # macOS
```

**whisper-cli not found**
```bash
which whisper-cli  # Check if installed
# If missing, install via your package manager (pacman, apt, dnf, brew)
```

**Timeouts**
```conf
audio_transcribe_timeout=60  # Increase in ui.conf
```

**Need ffmpeg**
```bash
brew install ffmpeg  # macOS
sudo apt install ffmpeg  # Debian/Ubuntu
sudo dnf install ffmpeg  # Fedora
```

**High CPU/RAM**

Use a smaller model (`-m tiny`), or switch to whisper.cpp, or just use the API.

---

## Quick Comparison

| Option | Speed | Privacy | Cost | Setup | Needs Python |
|--------|-------|---------|------|-------|--------------|
| whisper.cpp | Good | High | Free | Easy | No |
| Whisper Python | OK | High | Free | Easy | Yes |
| Groq | Fast | Low | $0.001/min | Medium | Yes |
| OpenAI | Fast | Low | $0.006/min | Easy | Yes |

Pick what works for you.
