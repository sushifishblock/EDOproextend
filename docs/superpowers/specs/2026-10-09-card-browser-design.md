# Card Browser (full-screen) for EDOPro: design

Date: 2026-10-09
Status: approved by the user in chat (mockup v3). Implementation starts immediately.
Target: local fork of `edo9300/edopro` (shallow clone in `edopro-src`), Windows x86 Release build.

## Goal

Add a **Card Browser** button to the EDOPro main menu. It opens a full-screen browser of every card in the
card database, with the same filters as the deck editor, a big hover preview with readable card text, and
the ability to see and change how many copies of a card are in a chosen deck.

## Non-goals

- No server, duel, or networking changes. Nothing about online play changes.
- No changes to `ocgcore` or card scripts.
- Side deck editing is not in v1. Added cards go to Main or Extra automatically.
- No new card database or new filter types. Reuse the deck editor's.

## Approach: "browser mode" inside the existing DeckBuilder

`DeckBuilder` (`gframe/deck_con.cpp`, `deck_con.h`) already owns what this feature needs:

| Needed | Existing piece |
| --- | --- |
| Filtering and search | `StartFilter`, `FilterCards`, `CheckCardProperties`, `results` |
| Sorting | `SortList`, `cbSortType` |
| Add / remove to deck | `push_main`, `push_extra`, `pop_main`, `pop_extra` |
| Ban list limits | `filterList`, `check_limit`, `RefreshLimitationStatus*` |
| Deck load / save | `SetCurrentDeckFromFile`, `BUTTON_SAVE_DECK` handler, `cbDBDecks` |
| Card info panel | `Game::ShowCardInfo`, `wCardImg`, `wInfos`, `stText` |

A new flag `bool browser_mode` on `DeckBuilder` switches the editor's layout and input handling. When it is
false, the deck editor must behave exactly as before. This avoids duplicating filter and limit logic.

Entry: a new main menu button `btnCardBrowser` (below Decks) with id `BUTTON_CARD_BROWSER`. Its handler in
`menu_handler.cpp` mirrors `BUTTON_DECK_EDIT` (load the selected deck) and then calls
`deckBuilder.Initialize()` followed by `deckBuilder.EnterBrowserMode()`.

Exit: the existing leave button (`btnLeaveGame`, relabelled "Back" in browser mode) calls
`DeckBuilder::Terminate`, which clears `browser_mode` and restores the normal layout. Unsaved-change
handling reuses the existing deck editor leave flow.

## Layout (virtual 1024x640 coordinates, scaled by `Game::Resize`)

```
+--------------------------------------------------------------+
| Back | Deck: [picker] [Save]            Card size [-] 6 [+]  |  y 0..30
| Filter bar (existing wFilter, moved to the top, widened)     |  y 30..~130
| Main 40/60  Extra 15/15   Ban list: <name>                   |  y ~132..150
+-------------------------------------------+------------------+
| Card grid (results, thumbnails, scroll)   | Info panel       |
| cols = 4..12, default 6                   | art, name, stats |
|                                           | text (A- / A+)   |
|                                           | In deck n of m   |
|                                           | [-] n [+]  0 1 2 3|
+-------------------------------------------+------------------+
```

- The grid occupies the left ~70% and the existing `scrFilter` scrollbar is moved to its right edge.
- The info panel reuses `wCardImg` and `wInfos` repositioned and widened (about 300 wide in virtual
  coordinates, versus today's layout where text is cramped). `stText` gets a dedicated, larger font.
- In browser mode the main/extra/side pile panels are replaced by the grid. The deck picker, ban list, Save,
  Save as, filters and sort stay as they are in the deck editor. The hand test and YDKE buttons are hidden.
- Implemented layout note: the grid reuses the deck editor region (x 310..995, y 160..630) and the existing
  left info column, instead of a wider custom info panel. The stepper, text size buttons and copy count label
  sit at the bottom of the "Card info" tab, and the card size and "Only cards in deck" controls sit in the
  grid header row.

## Behaviour

- **Grid:** `results` drawn as cards in a grid. Thumbnails use `imgType::ART` (177x254) so large cells stay
  sharp; fall back to `THUMB` while art loads. Each cell shows the ban list marker (existing limit icon logic
  from `DrawThumb`) and, when copies > 0, a count badge `x<n>`.
- **Hover:** hovering a cell calls `ShowCardInfo(code)` and the info panel shows "In deck: n of m allowed".
  Leaving the grid returns the panel to the pinned card.
- **Click:** pins the card. **Double-click** adds one copy. **Right-click** removes one copy.
  **Shift+click** fills to the allowed limit. **Ctrl+wheel** changes column count. Plain wheel scrolls.
- **Stepper:** `-`, count, `+` buttons and quick-set buttons `0 1 2 3` in the info panel. Values above the
  card's allowed maximum do nothing and show a short message ("limit is m on this ban list").
- **Counting:** copies = same passcode across Main + Extra + Side of `current_deck` (matches `check_limit`).
  Allowed max `m` = ban list limit for the card (`LFList::GetLimitationIterator`), 3 by default.
- **Add destination:** monsters that belong in the Extra Deck go to Extra, everything else to Main, using
  the existing `push_main` / `push_extra` (which enforce pile size caps and `check_limit`).
- **Remove:** takes the last matching copy, searching Main, then Extra, then Side.
- **Totals bar:** Main n/60 (min 40) and Extra n/15, refreshed after every change, plus the ban list name.
- **Only cards in deck:** checkbox that filters `results` to cards currently in `current_deck`.
- **Deck picker and Save:** reuse `cbDBDecks` and the existing save handler.

## Persistence

Two new config options in `gframe/game_config.inl` (saved automatically with the existing config file):

- `browserTextSize` (default 13): card text size in the info panel, adjusted by `A-` / `A+` (11 to 20).
- `browserColumns` (default 6): grid columns (4 to 12).

The text font is a separate `CGUITTFont` created for `stText` in browser mode and recreated when the size
changes. The global `textFont` (used by the rest of the game) is not changed.

## Components and files

| File | Change |
| --- | --- |
| `gframe/game.h`, `game.cpp` | New `btnCardBrowser`, plus browser-only widgets (card size buttons, stepper buttons, text size buttons, "only in deck" checkbox) created hidden. Layout helper to move/resize shared widgets for browser mode. |
| `gframe/menu_handler.h`, `menu_handler.cpp` | `BUTTON_CARD_BROWSER` id and handler; show/hide of new button with the main menu. |
| `gframe/deck_con.h`, `deck_con.cpp` | `browser_mode` flag, `EnterBrowserMode` / `LeaveBrowserMode`, hover and click hit-testing for the grid, add/remove-by-code helpers, "only in deck" filter. |
| `gframe/drawing.cpp` | `DrawCardBrowser()` called instead of `DrawDeckBd()` when `browser_mode` is set. |
| `gframe/event_handler.cpp` | Route the new button ids to `DeckBuilder::OnEvent` if needed. |
| `gframe/game_config.inl` | `browserTextSize`, `browserColumns`. |

Each unit has one job: `DrawCardBrowser` only draws; `DeckBuilder` browser helpers only hit-test and mutate
the deck; layout code only positions widgets.

## Error handling

- Missing card art: draw the existing `tUnknown` placeholder (already what `DrawThumb` does).
- Deck add failures (`push_*` returns false because of pile size or limit): show the same short message,
  never crash or change counts.
- No results: grid shows the existing empty state (result count 0).
- Config values out of range are clamped on load.

## Build and test plan

- Build with `bash build-tools/build.sh` (Win32 Release, v141 toolset, prebuilt vcpkg libs).
- Run only from `edopro-test` (isolated copy of the user's install). Never write into `C:\ProjectIgnis`.
- Verify by screenshot at each phase: menu button visible; browser opens and Back returns to the menu;
  grid scrolls and filters work; hover/click/double-click/right-click change counts and badges; deck saves
  and re-loads with the new counts; text size persists across restarts; the normal deck editor and online
  lobby are unaffected.
- No online duels or network actions are used during testing.

## Phases

1. Menu button, browser mode enter/leave, empty full-screen layout, Back works.
2. Grid drawing, scrolling, hover info, filters repositioned.
3. Copy counts: badges, stepper, shortcuts, totals, deck picker and save.
4. Text size and column persistence, "only in deck", polish.

## Risks

- EDOPro draws the deck editor in immediate mode with hard-coded coordinates; layout changes need care so
  the normal editor is untouched. Mitigation: all browser layout is gated by `browser_mode`.
- Loading many full-size card images at once could stutter. Mitigation: load only visible cells plus one
  row, fall back to thumbnails.
- The user's official updater would overwrite a patched `EDOPro.exe`. Mitigation: patched exe is only used
  from the isolated test copy until the user decides how to deploy it.
- EDOPro is AGPL-3.0. A personal local build is fine; sharing the binary would require sharing the source.
