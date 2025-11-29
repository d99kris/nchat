Auto-Compose
============
The nchat bundled default auto-compose utility `compose` uses external
services for chat completion, and generally requires an API key
(set in environment) to work. Environment variables to set:

    OpenAI: OPENAI_API_KEY
    Gemini: GEMINI_API_KEY


Basic Testing
-------------
An [example chat history file](/doc/example-history.txt) is provided for
testing chat completion standalone. Example:

    ./src/compose -c doc/example-history.txt


Testing Services / Models
-------------------------
The utility `compose` may be used with OpenAI-compatible services.

Example usage with default service (OpenAI) and default model (gpt-4o-mini):

    ./src/compose -c doc/example-history.txt

Example usage with OpenAI:

    ./src/compose -s openai -c doc/example-history.txt

Example usage with OpenAI and custom model and longer timeout of 60 secs:

    ./src/compose -s openai -m gpt-5-nano -T 60 -c doc/example-history.txt

Example usage with Google Gemini:

    ./src/compose -s gemini -c doc/example-history.txt

Example usage with Google Gemini and custom model:

    ./src/compose -s gemini -m gemini-2.5-flash -c doc/example-history.txt

Example usage with llama.cpp:

    ./src/compose -s "http://192.168.10.159:8080" -c doc/example-history.txt

Example starting llama.cpp server:

    llama-server --port 8080 -m ./models/llama-2-13b-chat.Q4_K_M.gguf


Configuring Custom Service / Model
----------------------------------
Edit `ui.conf` to match the desired compose path and usage.

Determine the path of `compose` based on nchat install path:

    realpath $(dirname $(which nchat))/../libexec/nchat/compose

Example usage with Google Gemini and custom model:

    auto_compose_command=/usr/local/libexec/nchat/compose -s gemini -m gemini-2.0-flash -c '%1'

Example usage with OpenAI and custom model and longer timeout of 60 secs:

    auto_compose_command=/usr/local/libexec/nchat/compose -s openai -m gpt-5-nano -T 60 -c '%1'

Example usage with custom prompt and max token limit of 100:

    auto_compose_command=/usr/local/libexec/nchat/compose -p "Suggest {your_name}'s next reply in a joking manner." -M 100 -c '%1'

