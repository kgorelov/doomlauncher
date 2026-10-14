# Technical Implementation Plan: Doom Launcher Paging, OSD & Keybinding Enhancements

This document establishes the technical design and phased implementation roadmap for the features described in [`docs/2026-10-14-improvements.md`](file:///home/kgorelov/git/doomlauncher/docs/2026-10-14-improvements.md).

---

## 1. Objectives & Architectural Blueprint

The improvements focus on refining the user experience in four core areas:
1. **Paging System**: Restrict the menu display to a maximum of 6 items per page, with seamless wrapping/navigation and a page indicator.
2. **On-Screen Display (OSD)**: Remove engine tags from game titles to restore a clean, authentic Doom aesthetic, displaying engine details and command strings in a dedicated bottom-left HUD/OSD above the fire effect.
3. **Dedicated OSD Font**: Replace the stylized title font with a small, highly readable Doom text font for command strings, paths, and metadata.
4. **Function Key Shortcuts & Overlays (F1–F12)**: Implement classic Doom-style function keys:
   - **F1**: On-screen Help overlay (keybinding reference table)
   - **F2**: Save current engine settings and preferences to [`state.toml`](file:///home/kgorelov/.config/doomlauncher/state.toml)
   - **F3**: Selected module details modal (WAD paths, complevel, capabilities, engines)
   - **F4**: Module engine picker modal
   - **F10**: Fast quit shortcut

```
+-----------------------------------------------------------------------------------+
|                                  SDL WINDOW (800x600)                             |
|                                                                                   |
|                                  [ MAIN MENU ]                                    |
|                              The Ultimate Doom                                    |
|                         > Doom II: Hell on Earth <                                |
|                        Final Doom: TNT Evilution                                  |
|                     Final Doom: The Plutonia Experiment                           |
|                                  Plutonia 2                                       |
|                                    Sigil                                          |
|                                Ancient Aliens                                     |
|                                                                                   |
|                              [ Page 1 of 2 ]                                      |
|                                                                                   |
|  +-----------------------------------------------------------------------------+  |
|  | OSD / STATS HUD (Bottom Left, above flames)                                 |  |
|  | ENGINE: DSDA-Doom  [F4: Switch Engine | F3: Details]                         |  |
|  | CMD: /usr/games/dsda-doom -iwad /path/to/DOOM2.WAD -complevel 2             |  |
|  +-----------------------------------------------------------------------------+  |
|  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ FLAME BUFFER ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  |
+-----------------------------------------------------------------------------------+
```

---

## 2. Technical Architecture & Component Design

### 2.1 Paging Engine in [`Menu`](file:///home/kgorelov/git/doomlauncher/src/menu.h)

- **Constants**:
  ```cpp
  constexpr int ITEMS_PER_PAGE = 6;
  ```
- **State Computation**:
  - `int totalItems = static_cast<int>(menuItems.size());`
  - `int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;`
  - `int currentPage = selectedItem / ITEMS_PER_PAGE;`
  - `int pageStartIndex = currentPage * ITEMS_PER_PAGE;`
  - `int pageEndIndex = std::min(pageStartIndex + ITEMS_PER_PAGE, totalItems);`
- **Navigation Mechanics**:
  - `SDLK_DOWN`: Increments `selectedItem` globally (`(selectedItem + 1) % totalItems`). When moving past the 6th item on page $K$, `currentPage` automatically becomes $K+1$. Moving past the last item of the last page wraps to index `0` (page 0).
  - `SDLK_UP`: Decrements `selectedItem` globally (`(selectedItem - 1 + totalItems) % totalItems`). Moving above the 1st item of page $K$ transitions to page $K-1$. Moving above item `0` wraps to the last item on the last page.
  - `SDLK_PAGEDOWN`: Advances cursor by `ITEMS_PER_PAGE`. If on the last page, wraps to the first page.
  - `SDLK_PAGEUP`: Retreats cursor by `ITEMS_PER_PAGE`. If on the first page, wraps to the last page.
- **Page Indicator**:
  - Rendered below the 6th menu slot in the OSD text font: `[ Page X of Y ]` or `< Page X of Y >`.

---

### 2.2 OSD & HUD Layout

- **Menu List Cleaning**:
  - Game row displays strictly `item.title` (dimmed red/gray if missing assets or incompatible).
  - The inline tag `< DSDA-Doom >` is removed from the menu row.
- **HUD / OSD Block**:
  - Located at bottom-left: `x = 30`, `y = screenHeight - 95`.
  - Left-aligned rendering.
  - **Row 1 (Engine / Status)**:
    - Normal item: `ENGINE: DSDA-Doom` (in yellow) + ` [3 compatible | F4 to change]` (in white/gray).
    - Missing asset: `STATUS: Cannot launch - missing asset (DOOM2.WAD)` (in red).
    - Incompatible: `STATUS: No compatible engine installed` (in red).
  - **Row 2 (Command Line)**:
    - `CMD: /usr/games/dsda-doom -iwad /home/kgorelov/opt/doom/doom2/DOOM2.WAD -complevel 2`
  - **Row 3 (Shortcut Guide / Feedback)**:
    - `F1: Help | F2: Save | F3: Info | F4: Engine | F10: Quit`
    - Temporarily overridden when user triggers an action (e.g. `[ Settings saved to ~/.config/doomlauncher/state.toml ]` in green).

---

### 2.3 OSD Font Integration

- **Current Font vs. New OSD Font**:
  - Main menu title font: `fonts/Doom2016Left-RpJDA.ttf` (size 72-84, large angular uppercase logo font).
  - OSD / HUD font: `fonts/Doom2016Text-GOlBq.ttf` (size 16-18, proportional text font with full ASCII lowercase, numbers, and symbols).
- **Self-Contained Embedded Font**:
  - Embed `Doom2016Text-GOlBq.ttf` into [`src/font_data.h`](file:///home/kgorelov/git/doomlauncher/src/font_data.h) as `fonts_Doom2016Text_GOlBq_ttf` (27,600 bytes) alongside the existing title font.
  - Fall back to loading from disk if the binary is run in a development workspace.
  - Instantiate in [`src/main.cpp`](file:///home/kgorelov/git/doomlauncher/src/main.cpp) as `TTF_Font* osdFont` and pass to [`Menu`](file:///home/kgorelov/git/doomlauncher/src/menu.h).

---

### 2.4 Modal / Overlay Subsystem in [`Menu`](file:///home/kgorelov/git/doomlauncher/src/menu.h)

To implement F1, F3, F4, and F10 cleanly, `Menu` will manage an explicit overlay state:

```cpp
enum class OverlayState {
    None,
    Help,          // F1: Keybindings & Controls guide
    ModuleInfo,    // F3: Comprehensive WAD metadata
    EngineSelect,  // F4: Engine switcher dialog
    QuitConfirm    // F10: Classic Doom quit prompt
};
```

#### F1: Help Overlay
- Centers a classic Doom bordered dialog:
  - Header: `=== DOOM LAUNCHER CONTROLS ===`
  - Controls table:
    - `Up / Down`: Move selection
    - `PageUp / PageDown`: Previous / Next page
    - `Left / Right`: Quick engine toggle
    - `Enter`: Launch selected game
    - `F1`: Toggle this help screen
    - `F2`: Save settings & engine choices
    - `F3`: View selected game details
    - `F4`: Select engine from list
    - `F10 / Esc`: Quit launcher
  - Pressing `F1`, `Esc`, or `Enter` closes the overlay.

#### F2: Save Settings
- Directly writes the currently selected engines for all configured modules to:
  [`~/.config/doomlauncher/state.toml`](file:///home/kgorelov/.config/doomlauncher/state.toml)
  ```toml
  [engines]
  doom = "dsda-doom"
  doom2 = "gzdoom"
  sunlust = "dsda-doom"
  ```
- Displays a prominent status message in the OSD:
  `"Settings saved to ~/.config/doomlauncher/state.toml"`
- On launcher startup, [`doomlauncher::ModuleRegistry`](file:///home/kgorelov/git/doomlauncher/src/config_module.h) automatically reads `state.toml` and applies previously saved engine preferences.

#### F3: Selected Module Info Overlay
- Centers an informational dialog displaying:
  - **Title**: Game Title & Module ID
  - **Status**: Playable / Missing Asset / Incompatible Engine
  - **IWAD**: Resolved path on disk & logical name
  - **PWADs**: List of resolved patch files
  - **Compatibility Level**: e.g., `2 (Doom v1.9)`, `9 (Boom)`, etc.
  - **Required Capabilities**: e.g., `vanilla`, `boom`, `mbf21`
  - **Save Directory**: Resolved save game path
  - **Installed Engines**: List of compatible engines on system
  - **Active Engine & Binary**: Selected engine and executable path
  - **Command Preview**: Full rendered launch command
- Pressing `F3`, `Esc`, or `Enter` closes the dialog.

#### F4: Engine Selection Modal
- Centers an interactive selection popup:
  - Header: `Select Engine for <Game Title>`
  - Items list:
    - `( ) Boom / PrBoom+ (/usr/games/boom)`
    - `(*) DSDA-Doom (/usr/games/dsda-doom)`
    - `( ) GZDoom (/snap/bin/gzdoom)`
  - `Up / Down`: Select option
  - `Enter / Space`: Confirm selection and regenerate command
  - `Esc / F4`: Dismiss modal

#### F10: Fast Quit
- Pressing `F10` opens a classic Doom confirmation prompt:
  `"Quit Doom Launcher? (Y/Enter to Exit, Esc/N to Cancel)"`
- Pressing `Y` or `Enter` sets `quitSelected = true`.
- Pressing `Esc` or `N` cancels.

---

## 3. Phased Implementation Roadmap

### Phase 1: Paging Architecture
- Refactor [`Menu`](file:///home/kgorelov/git/doomlauncher/src/menu.h) / [`src/menu.cpp`](file:///home/kgorelov/git/doomlauncher/src/menu.cpp) to support `ITEMS_PER_PAGE = 6`.
- Implement page window slicing: only render items `[pageStartIndex, pageEndIndex)`.
- Implement `SDLK_PAGEUP` and `SDLK_PAGEDOWN` handlers.
- Update `SDLK_UP` and `SDLK_DOWN` to seamlessly cross page boundaries.
- Render page indicator `[ Page X of Y ]` beneath the menu items.

### Phase 2: OSD / HUD Redesign
- Remove inline `< Engine >` string formatting from menu item title rendering.
- Re-architect bottom screen area into a multi-line left-aligned OSD block:
  - Line 1: `ENGINE: <Name> [<N> compatible | F4: Change]`
  - Line 2: `CMD: <full rendered command>`
  - Line 3: Function key hints (`F1: Help | F2: Save | F3: Info | F4: Engine | F10: Quit`)
- Handle status messages and asset failure alerts cleanly within the OSD block.

### Phase 3: OSD Font Integration
- Generate binary byte array for `fonts/Doom2016Text-GOlBq.ttf` in [`src/font_data.h`](file:///home/kgorelov/git/doomlauncher/src/font_data.h).
- Load `osdFont` in [`src/main.cpp`](file:///home/kgorelov/git/doomlauncher/src/main.cpp) with size ~16-18.
- Pass `osdFont` to `Menu` and use it exclusively for OSD lines, page indicator, and dialog text.

### Phase 4: Function Key Subsystem & Modal Dialogs
- Add `OverlayState` state machine in [`src/menu.h`](file:///home/kgorelov/git/doomlauncher/src/menu.h).
- Implement semi-transparent dialog box renderer (`renderDialogBox`).
- Implement F1 Help screen.
- Implement F3 Module Info dialog.
- Implement F4 Engine Picker modal with selection cursor.
- Implement F10 Quit confirmation.

### Phase 5: State Persistence (`state.toml`)
- Implement `save_state(const std::string& filepath)` in [`doomlauncher::ModuleRegistry`](file:///home/kgorelov/git/doomlauncher/src/config_module.h).
- Implement `load_state(const std::string& filepath)` in [`doomlauncher::ModuleRegistry`](file:///home/kgorelov/git/doomlauncher/src/config_module.h).
- Bind F2 in `Menu` to trigger `registry.save_state()` and display a success notification in the OSD.
- Update [`src/main.cpp`](file:///home/kgorelov/git/doomlauncher/src/main.cpp) to automatically load `~/.config/doomlauncher/state.toml` on startup.

### Phase 6: Automated Testing & Verification
- Add unit tests in [`src/test_config.cpp`](file:///home/kgorelov/git/doomlauncher/src/test_config.cpp):
  - `state.toml` saving and loading verification.
  - Verification of engine preference restoration from state.
- Build and verify all unit tests pass with zero warnings.
- Launch GUI in test environment to verify paging, OSD layout, and modal interactions.

---

## 4. Execution Checklist

- [x] **Step 1: Paging System**
  - [x] Add `ITEMS_PER_PAGE = 6` in [`src/menu.h`](file:///home/kgorelov/git/doomlauncher/src/menu.h).
  - [x] Implement slice rendering and page indicator in [`src/menu.cpp`](file:///home/kgorelov/git/doomlauncher/src/menu.cpp).
  - [x] Implement `SDLK_PAGEUP` / `SDLK_PAGEDOWN` and wrap-around page boundary navigation in [`Menu::handleEvent`](file:///home/kgorelov/git/doomlauncher/src/menu.cpp).
- [ ] **Step 2: OSD Redesign**
  - [ ] Clean up game title row rendering (remove inline engine tags).
  - [ ] Implement multi-line left-aligned OSD block above flames.
- [ ] **Step 3: Font Integration**
  - [ ] Embed `Doom2016Text-GOlBq.ttf` in [`src/font_data.h`](file:///home/kgorelov/git/doomlauncher/src/font_data.h).
  - [ ] Initialize `osdFont` in [`src/main.cpp`](file:///home/kgorelov/git/doomlauncher/src/main.cpp) and route to `Menu`.
- [ ] **Step 4: Function Key Handlers & Modal Dialogs**
  - [ ] Add `OverlayState` enum in [`src/menu.h`](file:///home/kgorelov/git/doomlauncher/src/menu.h).
  - [ ] Implement F1 Help overlay.
  - [ ] Implement F3 Module Info dialog.
  - [ ] Implement F4 Engine Picker modal.
  - [ ] Implement F10 Quit confirmation.
- [ ] **Step 5: State Persistence**
  - [ ] Implement `save_state()` / `load_state()` in [`src/config_module.cpp`](file:///home/kgorelov/git/doomlauncher/src/config_module.cpp).
  - [ ] Connect F2 in [`Menu::handleEvent`](file:///home/kgorelov/git/doomlauncher/src/menu.cpp) to persist settings.
  - [ ] Restore state on startup in [`src/main.cpp`](file:///home/kgorelov/git/doomlauncher/src/main.cpp).
- [ ] **Step 6: Testing & Verification**
  - [ ] Add unit tests for state serialization in [`src/test_config.cpp`](file:///home/kgorelov/git/doomlauncher/src/test_config.cpp).
  - [ ] Verify clean compilation with `-Wall -Wextra`.
  - [ ] Verify test suite execution.
