# EDOPro Card Browser mod

A client-side mod of [EDOPro](https://github.com/edo9300/edopro) (branch `card-browser`).

## Features
- **Card Browser** (main menu): every card in a full-screen grid, same filters as the deck editor, hover preview, add / remove / set copies, adjustable text size, switch button to the Deck Editor and back.
- Everything preloaded at startup with full-size art (no grey loading cards), also the small pictures of the deck editor. Fast preload (texture-sort fix in Irrlicht, JPEG pack cache, DCT-scaled decoding). 64-bit build.
- **AI search** (optional): type English ("dragon monsters that do stuff in hand when added"); a local model (llama.cpp + Qwen2.5-3B, nomic embeddings) turns it into filters and ranks cards by meaning. Models are not in this repo: put `llama-server.exe`, the two `.gguf` models in `ai/llama` and `ai/models` of the install and the prompt/schema from `mod-tools/ai` in `ai/prompts`.
- **Genesys mode**: checkbox in the deck panel; point costs as a black tab on the cards, total with bar, 3 copies of everything, Link/Pendulum blocked. Point list refreshed from YGOPRODeck at every start (`genesys_points.json`, restarts the game once if it changed).
- **Most played** sort from the YugiohMeta card usage ranking (`popularity.json`, refreshed at start).

## Building (Windows, 64-bit)
`mod-tools/build-x64.sh` (needs MSVC v143, vcpkg x64-windows-static libs and a 64-bit ocgcore; see the script). `patches/` holds the Irrlicht fix. `mod-tools/update_genesys.py` and `import_card_art.py` are helper scripts.

`release/EDOPro-x64.exe` is the compiled build; copy it over `EDOPro.exe` of an EDOPro install (with a 64-bit `ocgcore.dll`).
