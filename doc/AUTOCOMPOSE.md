Auto-Compose
============

The nchat bundled default auto-compose utility `compose` uses external services
for chat completion to suggest your next reply in conversations. Most services
require an API key set in environment variables.

Authentication
---------------

The following services require an API key (provided via `-k/--api-key`):

- **OpenAI**: Use `-k "sk-..."` with your OpenAI API key
- **Google Gemini**: Use `-k "AIza..."` with your Gemini API key
- **Ollama Cloud**: Use `-k "your-key"` with your Ollama Cloud API key (accessed via local Ollama client)

These services do NOT require authentication:

- **Ollama (local)**: No API key needed
- **Custom OpenAI-compatible servers**: No API key needed (unless the server requires it)


Command-Line Options
--------------------

The `compose` utility accepts the following arguments:

- `-c, --chat-completion <PATH>` **(required)**
  Path to input chat file. The last line must contain only your name followed
  by a colon (e.g., `Stanley:`) with no message content.

- `-s, --service <SERVICE>` (default: `openai`)
  Service provider: `openai`, `gemini`, `ollama` (local), `ollama-cloud`, or a 
  custom OpenAI-compatible server URL (e.g., `http://localhost:8000` or 
  `192.168.10.159:8080`).
  
  Note: `ollama-cloud` connects via your local Ollama client at 
  `http://localhost:11434/api/chat` and requires an API key.

- `-m, --model <MODEL>`
  Model name for the service. Defaults depend on the selected service:
  - OpenAI: `gpt-4o-mini`
  - Gemini: `gemini-2.0-flash`
  - Ollama: `gemma3` (example local model)
  - Ollama Cloud: `kimi-k2:1t-cloud` (example cloud model)
  - Custom servers: `gpt-4o-mini`

- `-p, --prompt <PROMPT>` (default: `Suggest {your_name}'s next reply.`)
  Custom instruction prompt. Use `{your_name}` placeholder to reference the
  user's name dynamically. Example:
  `"Suggest {your_name}'s next reply in a joking manner."`

- `-t, --temperature <VALUE>`
  Sampling temperature for response randomness (e.g., `0.2` for deterministic,
  `0.9` for creative). Higher values = more random responses.

- `-M, --max-tokens <NUMBER>`
  Maximum number of output tokens to generate. Limits response length.

- `-T, --timeout <SECONDS>` (default: `10`)
  Network timeout in seconds. Increase for slow connections or complex requests.

- `-k, --api-key <KEY>` **(required for OpenAI, Gemini, Ollama Cloud)**
  API key for authenticated services. Required when using OpenAI, Google Gemini,
  or Ollama Cloud. Not needed for local Ollama or custom servers.
  Example: `-k "sk-1234567890abcdef"` for OpenAI.

- `--api-url <URL>`
  Custom API endpoint URL (overrides default for the service). Useful for
  custom Ollama Cloud endpoints or other compatible services.
  Example: `--api-url "http://localhost:8000/v1/chat/completions"`

- `-v, --verbose`
  Print request payload and raw responses to stderr for debugging purposes.

- `-h, --help`
  Display help message and exit.


Chat File Format
----------------

The compose utility expects a chat file with a specific format:

```
Alice: Hello, how are you?
Bob: I'm doing great, thanks for asking!
Alice: That's wonderful to hear!
Alice:
```

Rules:
- Each line must follow the format: `Name: message content`
- The last line must contain only your name followed by a colon (e.g., `Alice:`)
- Empty lines are ignored
- The utility will suggest what your character (the last name) should reply next


Basic Testing
-------------

An [example chat history file](/doc/example-history.txt) is provided for
testing chat completion standalone.

Basic usage with defaults (OpenAI, gpt-4o-mini):

    ./src/compose -c doc/example-history.txt


Service Usage Examples
----------------------

**OpenAI with default model:**

    ./src/compose -s openai -k "sk-your-api-key" -c doc/example-history.txt

**OpenAI with custom model and longer timeout:**

    ./src/compose -s openai -k "sk-your-api-key" -m gpt-4-turbo -T 60 -c doc/example-history.txt

**Google Gemini:**

    ./src/compose -s gemini -k "AIza-your-api-key" -c doc/example-history.txt

**Google Gemini with custom model:**

    ./src/compose -s gemini -k "AIza-your-api-key" -m gemini-2.5-flash -c doc/example-history.txt

**Ollama (local, no auth required):**

    ./src/compose -s ollama -c doc/example-history.txt

**Ollama with custom model:**

```bash
./src/compose -s ollama -m gemma3 -c doc/example-history.txt
```

**Ollama Cloud (with API key via local client):**

    ./src/compose -s ollama-cloud -k "your-ollama-cloud-api-key" -m kimi-k2:1t-cloud -c doc/example-history.txt

**Ollama Cloud with custom timeout:**

    ./src/compose -s ollama-cloud -k "your-ollama-cloud-api-key" -m kimi-k2:1t-cloud -T 60 -c doc/example-history.txt

**Custom OpenAI-compatible server (via URL):**

    ./src/compose -s "http://192.168.10.159:8080" -c doc/example-history.txt

**Custom llama.cpp server:**

    llama-server --port 8080 -m ./models/llama-2-13b-chat.Q4_K_M.gguf &
    ./src/compose -s "http://localhost:8080" -c doc/example-history.txt

**Advanced example with custom prompt and sampling:**

    ./src/compose -s openai -m gpt-4o -p "Suggest {your_name}'s next reply as if they were a pirate." \
      -t 0.8 -M 150 -c doc/example-history.txt

**Verbose mode for debugging:**

    ./src/compose -s gemini -c doc/example-history.txt -v


Configuring for Auto-Compose in nchat
--------------------------------------

Edit the nchat configuration file `ui.conf` to set the auto-compose command.

First, determine the path of `compose` based on your nchat installation:

    realpath $(dirname $(which nchat))/../libexec/nchat/compose

Then configure `ui.conf` with one of the following examples:

**Using Google Gemini with custom model:**

    auto_compose_command=/usr/local/libexec/nchat/compose -s gemini -k "AIza-your-key" -m gemini-2.0-flash -c '%1'

**Using OpenAI with custom model and longer timeout:**

    auto_compose_command=/usr/local/libexec/nchat/compose -s openai -k "sk-your-key" -m gpt-4-turbo -T 60 -c '%1'

**Using custom prompt with token limit:**

    auto_compose_command=/usr/local/libexec/nchat/compose -p "Suggest {your_name}'s next reply in a professional tone." -M 100 -c '%1'

**Using local Ollama (no auth needed):**

```bash
auto_compose_command=/usr/local/libexec/nchat/compose -s ollama -m gemma3 -c '%1'
```

**Using Ollama Cloud (with API key):**

    auto_compose_command=/usr/local/libexec/nchat/compose -s ollama-cloud -k "your-ollama-cloud-api-key" -m kimi-k2:1t-cloud -c '%1'

**Using custom OpenAI-compatible server:**

    auto_compose_command=/usr/local/libexec/nchat/compose -s "http://192.168.1.100:8000" -c '%1'

Note: The `%1` placeholder will be replaced with the chat file path when nchat
invokes the auto-compose command.

