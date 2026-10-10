# EDOproextend

My personal mod of [EDOPro](https://github.com/edo9300/edopro), the Yu-Gi-Oh! duel simulator. It adds a full-screen **Card Browser**, a local **AI card search**, a **Genesys** mode and a **most played** sort. 64-bit, Windows.

The game itself is still EDOPro: card scripts, databases and the duel core belong to the [Project Ignis](https://github.com/ProjectIgnis) team. This repository is their client source plus my changes.

## What I added

- **Card Browser** - a "Card Browser" button on the main menu opens every card in one full-screen grid, with the same filters as the deck editor. Hover a card to preview it, click to pin it, double-click to add it, right-click to remove it. The `-`/`+` and `0`-`3` buttons set how many copies are in the deck, `A-`/`A+` change the text size, and the switch button jumps between the Card Browser and the Deck Editor.
- **No grey loading cards** - all card art (full size) and the small deck editor pictures are preloaded when the game starts. A warm start takes about 3 seconds.
- **High resolution art** - on startup the game checks every official card and bulk-downloads the 813x1185 picture from YGOPRODeck when it is missing or low resolution. A lock screen with a progress bar blocks the game until it is done (about 2 GB on a fresh install). Fast: many parallel connections, up to 600 requests per second, backs off by itself if the server pushes back.
- **AI search** - type a request in plain English under the grid, for example `dragon monsters that do stuff in hand when added`, and press Ask AI. A local model (llama.cpp with Qwen2.5-3B and nomic embeddings) turns it into filters and ranks the cards by meaning. It runs on my PC, nothing is sent online.
- **Genesys mode** - tick the Genesys box in the deck panel. Each card shows its point cost as a black tab, the deck shows a total out of 100 with a bar, every card is allowed 3 copies, and Link and Pendulum monsters are blocked. The point list is refreshed from YGOPRODeck every time the game starts (the game restarts once from the main menu if it changed).
- **Most played sort** - "Most played" in the sort dropdown orders cards by competitive usage (YugiohMeta ranking, TCG first). It is refreshed at every start too.

## My setup

| Thing | Where |
|---|---|
| Game install (64-bit) | `C:\ProjectIgnis64` |
| Original 32-bit install | `C:\ProjectIgnis` (untouched) |
| AI files | `C:\ProjectIgnis64\ai` (`llama\`, `models\`, `prompts\`) |
| Data files made by the game | `genesys_points.json`, `popularity.json`, `cardcache\cards.pack` |
| Source and build scripts | this repository |

The AI models and card art are not in this repository. If the `ai` folder is missing, the AI box simply doesn't appear.

## Installing (also for friends)

Download `EDOproextend-1.3.2-x64-full.zip` from the [Releases](../../releases) page, extract the whole zip (do not run it from inside the zip viewer) and start `EDOPro.exe`. It is the normal EDOPro 41.0.2 game files with my 64-bit exe and a 64-bit `ocgcore.dll` already in place, so nothing else has to be installed. The first start needs internet (card data and pictures are downloaded by the game).

To update only the mod later, use the "Update available" button on the main menu (v1.3 and newer), or replace `EDOPro.exe` with the one from `release/` in this repository.

## Building

Needs Visual Studio 2022 (MSVC v143, Windows SDK 10.0.26100). The script expects a `build-tools/` folder (not in this repository, it is several GB) that holds `premake5.exe`, the DirectX SDK, the vcpkg `x64-windows-static` libraries in `vcpkg-x64` and a 64-bit ocgcore import library in `core-x64`. Copy `mod-tools/build-x64.sh` into `build-tools/` and run from the repository root:

```bash
bash build-tools/build-x64.sh
```

The output is `bin/x64/release/ygoprodll.exe`. `patches/` holds the Irrlicht fix that made the card preload fast (the texture list was sorted on every insert).

## Helper scripts (`mod-tools/`)

- `update_genesys.py` - download the Genesys point list by hand.
- `import_card_art.py` - download high resolution art for the official cards.
- `ai/` - the prompt, JSON schema and the test script used to tune the AI search.

## Credits

EDOPro by [edo9300](https://github.com/edo9300/edopro) and Project Ignis (AGPL-3.0, see `LICENSE`). Card data and prices from YGOPRODeck and YugiohMeta. AI models: Qwen2.5-3B-Instruct and nomic-embed-text through llama.cpp.
