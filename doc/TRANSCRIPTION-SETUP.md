# Transcription Setup

How to set up different transcription backends for nchat.

## What You Need

- nchat installed
- Python 3.7+ (`python3 --version`)
- pip (`pip3 --version`)

---

## Option 1: OpenAI API (Easiest)

Fast (2-3 sec), accurate, easy. Costs $0.006/min. Audio goes to OpenAI.

**Setup:**

1. Get API key: https://platform.openai.com/api-keys

2. Install packages:
   ```bash
   pip3 install openai requests
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

## Option 2: whisper.cpp (Local Server)

Free, private, offline. Bit more setup. Fast with GPU.

**Setup:**

1. Install deps:
   ```bash
   # macOS
   brew install ffmpeg cmake

   # Linux
   sudo apt install build-essential ffmpeg cmake git  # Debian/Ubuntu
   sudo dnf install gcc-c++ ffmpeg cmake git          # Fedora
   ```

2. Build it:
   ```bash
   mkdir -p ~/whisper.cpp && cd ~/whisper.cpp
   git clone https://github.com/ggerganov/whisper.cpp.git .
   mkdir build && cd build
   cmake .. -DWHISPER_BUILD_SERVER=ON
   cmake --build . --config Release
   ```

3. Download a model:
   ```bash
   cd ~/whisper.cpp
   bash ./models/download-ggml-model.sh base  # or tiny/small/medium/large
   ```

4. Start the server:
   ```bash
   cd ~/whisper.cpp
   ./build/bin/server --model models/ggml-base.bin --host 127.0.0.1 --port 8080 --convert
   ```

   Run in background:
   ```bash
   nohup ./build/bin/server --model models/ggml-base.bin --host 127.0.0.1 --port 8080 --convert > server.log 2>&1 &
   ```

5. Install Python package:
   ```bash
   pip3 install requests
   ```

6. Configure nchat:
   ```conf
   audio_transcribe_enabled=1
   audio_transcribe_command=/usr/local/libexec/nchat/transcribe -f '%1' -s whisper-cpp
   audio_transcribe_cache=1
   ```

7. Test:
   ```bash
   curl http://localhost:8080/health
   /usr/local/libexec/nchat/transcribe -f /path/to/test.ogg -s whisper-cpp
   ```

**Want auto-start on boot?** Set up a systemd service (Linux) or launchd (macOS) - Google it.

---

## Option 3: Whisper Python (Local)

Free, private, simple. Slower than whisper.cpp, uses more RAM.

**Setup:**

1. Install ffmpeg:
   ```bash
   brew install ffmpeg  # macOS
   sudo apt install ffmpeg python3-dev  # Debian/Ubuntu
   sudo dnf install ffmpeg python3-devel  # Fedora
   ```

2. Install Whisper:
   ```bash
   pip3 install faster-whisper  # Faster (recommended)
   # or
   pip3 install openai-whisper  # Original (slower)
   ```

3. Configure nchat:
   ```conf
   audio_transcribe_enabled=1
   audio_transcribe_command=/usr/local/libexec/nchat/transcribe -f '%1' -s whisper-local -m base
   audio_transcribe_cache=1
   ```

4. Test:
   ```bash
   /usr/local/libexec/nchat/transcribe -f /path/to/test.ogg -s whisper-local
   ```

Models download automatically on first use. Pick a size: `tiny` (fast, meh), `base` (balanced), `small`/`medium`/`large` (slower, better).

Got an NVIDIA GPU? Use `pip3 install faster-whisper[gpu]` after installing CUDA.

---

## Option 4: Groq API (Cheaper Alternative)

Like OpenAI but cheaper ($0.001/min vs $0.006/min). Still fast.

**Setup:**

1. Get API key from https://console.groq.com/

2. Install:
   ```bash
   pip3 install groq requests
   ```

3. Set key:
   ```bash
   export GROQ_API_KEY='gsk-...'
   echo 'export GROQ_API_KEY="gsk-..."' >> ~/.bashrc
   echo 'export GROQ_API_KEY="gsk-..."' >> ~/.zshenv  # macOS
   ```

4. You'll need to hack the transcribe script to add Groq support (it's not built-in yet). Or just use OpenAI.

---

## Testing

After setup:

```bash
# Test the script
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
```

**"No module named 'openai'"**
```bash
pip3 install openai  # or whatever package is missing
```

**"API key not set"**
```bash
echo $OPENAI_API_KEY  # Check it's set
echo 'export OPENAI_API_KEY="sk-..."' >> ~/.bashrc
echo 'export OPENAI_API_KEY="sk-..."' >> ~/.zshenv  # macOS
```

**whisper.cpp server not responding**
```bash
curl http://localhost:8080/health  # Check if running
cd ~/whisper.cpp && ./build/bin/server --model models/ggml-base.bin --host 127.0.0.1 --port 8080 --convert
```

**Timeouts**
```conf
audio_transcribe_timeout=60  # Increase in ui.conf
```

**Need ffmpeg**
```bash
brew install ffmpeg  # macOS
sudo apt install ffmpeg  # Linux
```

**High CPU/RAM**

Use a smaller model (`-m tiny`), or switch to whisper.cpp, or just use the API.

---

## Quick Comparison

| Option | Speed | Privacy | Cost | Setup |
|--------|-------|---------|------|-------|
| OpenAI | Fast | Low | $0.006/min | Easy |
| whisper.cpp | Good | High | Free | Medium |
| Whisper Python | OK | High | Free | Easy |
| Groq | Fast | Low | $0.001/min | Easy |

Pick what works for you.
