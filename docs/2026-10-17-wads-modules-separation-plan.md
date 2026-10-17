# Technical Implementation Plan: WADs & Modules Separation, SHA-1 Discovery & Interactive Add/Create Workflows

This document establishes the technical design and phased implementation roadmap for the architecture described in [`docs/2026-10-17-wads-modules-separation.md`](file:///home/kgorelov/git/doomlauncher/docs/2026-10-17-wads-modules-separation.md).

---

## 1. Objectives & Architectural Shift

The goal of this redesign is to decouple **WAD files**, **Game Modules**, and **Save Directories**, inverting the control flow from module-centric directory structures to WAD-driven activation:

1. **Inversion of Control (WADs Activate Modules)**:
   - Previously, every loaded game module appeared in the menu (often red/disabled if WADs were missing) and assumed WADs and `.saves` lived inside `{{MODULE_DIR}}`.
   - Now, pre-installed and user modules act as a **knowledge base / library of game definitions**.
   - The launcher discovers WADs from system directories, user `wad_dirs`, and explicitly added `iwads` / `wads`.
   - A module becomes **active** (and appears as a menu entry) **if and only if** all of its required WAD dependencies (IWAD + PWADs) are satisfied by discovered WADs on the user's system.
2. **Multi-Directory Module Loading**:
   - `paths.modules` in `~/.config/doomlauncher/config.toml` becomes an array of directories, combining pre-installed system modules (`/usr/share/doomlauncher/modules`, `./modules`) and user-created modules (`~/.config/doomlauncher/modules`).
3. **Centralized Save Directories**:
   - Remove `savedir = "{{MODULE_DIR}}/.saves"` from modules.
   - All save games automatically resolve to `~/.config/doomlauncher/saves/<module_name>/` (created on launch if not already present).
4. **SHA-1 WAD Identification & Caching**:
   - Every discovered or added WAD has its SHA-1 hash computed (and cached by file path + `mtime` + file size in `state.toml` to keep startup fast).
   - Modules can match WADs by **SHA-1 hash** (supporting multiple known revisions/releases of a WAD) with fallback to case-insensitive filename matching.
5. **Interactive In-Launcher WAD Addition & Module Creation**:
   - **Add WAD Dialog (`F5` or `+ Add WAD...` menu action)**: Built-in filesystem browser allowing the user to navigate directories, select one or multiple `.wad` / `.pk3` files, and add them to `~/.config/doomlauncher/config.toml`.
   - **Automatic Module Matching**: Immediately after adding WAD(s), SHA-1 calculation and module discovery run. If a pre-installed or user module matches the added WAD(s), it activates immediately.
   - **Create Module Dialog**: If no existing module matches an added WAD, the launcher prompts the user with an interactive form to create a new module under `~/.config/doomlauncher/modules/games/<slug>.toml` (setting Title, Base IWAD / Dependencies with `DOOM2.WAD` and `DOOM.WAD` prioritized at the top, Compatibility Level, and Required Engine Capabilities).

```
+-----------------------------------------------------------------------------------+
|                        INVERTED CONTROL FLOW ARCHITECTURE                         |
+-----------------------------------------------------------------------------------+
|                                                                                   |
|  [ WAD Discovery ]                         [ Module Library ]                     |
|  - System default dirs                     - Pre-installed:                       |
|  - config.toml: paths.wad_dirs               /usr/share/doomlauncher/modules      |
|  - config.toml: [iwads] / paths.wads       - User-created:                        |
|         |                                    ~/.config/doomlauncher/modules       |
|         v                                           |                             |
|  +-------------------------------+                  |                             |
|  | WAD Catalog (Path, Name, SHA1)| -----------------+                             |
|  +-------------------------------+                  |                             |
|         |                                           v                             |
|         |            +-----------------------------------------------+            |
|         +----------> |     Module Matcher & Dependency Resolver      |            |
|                      |  - Matches WADs by SHA-1 (or filename)        |            |
|                      |  - Checks base_game / requires chain          |            |
|                      |  - Injects ~/.config/doomlauncher/saves/<mod> |            |
|                      +-----------------------------------------------+            |
|                                             |                                     |
|                        +--------------------+--------------------+                |
|                        | (Match Found)                           | (Unmatched WAD)|
|                        v                                         v                |
|         +-----------------------------+           +-----------------------------+ |
|         |   Active Menu Entry         |           | "Create Module" Form Dialog | |
|         |   (WAD + Module + Engine)   |           | -> Saves ~/.config/.../toml | |
|         +-----------------------------+           +-----------------------------+ |
+-----------------------------------------------------------------------------------+
```

---

## 2. Configuration & State Specification

### 2.1 User Config (`~/.config/doomlauncher/config.toml`)

```toml
[paths]
# Ordered list of module directories (pre-installed system dirs + user dir)
modules = [
    "/usr/share/doomlauncher/modules",
    "~/.config/doomlauncher/modules"
]

# Directories scanned automatically for WAD/PK3 files
wad_dirs = [
    "~/Games/Doom/wads",
    "~/.local/share/games/doom"
]

[iwads]
# Explicitly registered WAD files (added manually or via the "Add WAD" dialog)
doom = "/home/user/Games/Doom/DOOM.WAD"
doom2 = "/home/user/Games/Doom/DOOM2.WAD"
ancient_aliens = "/home/user/Downloads/aaliens.wad"

[preferences]
default_engine = "dsda-doom"
osd_font = "mono"
```

- **Backward Compatibility**:
  - In [`AssetResolver::load_config`](file:///home/kgorelov/git/doomlauncher/src/asset_resolver.cpp), `paths.modules` will accept **both** a `toml::array` of strings (`std::vector<std::string> module_dirs`) and a single `toml::value<std::string>` for legacy configs.
  - Default `module_dirs` when not specified in `config.toml`:
    1. `/usr/share/doomlauncher/modules` (if exists)
    2. `/usr/local/share/doomlauncher/modules` (if exists)
    3. `./modules` (local repo/portable fallback, if exists)
    4. `~/.config/doomlauncher/modules` (always included; auto-created when saving user modules)
  - Modules loaded from later directories (e.g., `~/.config/doomlauncher/modules`) override modules of the same name from earlier directories.
- **Persisting Config Changes**:
  - Add `AssetResolver::save_config(const UserConfig& config, const std::string& explicit_path = "")` so the "Add WAD" dialog can non-destructively add newly selected WAD paths into `[iwads]` (or `paths.wad_dirs`) in `~/.config/doomlauncher/config.toml`.

### 2.2 State & SHA-1 Cache (`~/.config/doomlauncher/state.toml`)

Calculating SHA-1 across gigabytes of WADs on every startup could introduce latency if uncached. `state.toml` will store both per-module engine selections and a lightweight SHA-1 cache keyed by canonical file path, file size, and last modification timestamp (`mtime`):

```toml
[engines]
doom = "dsda-doom"
doom2 = "gzdoom"
sigil = "dsda-doom"

[wad_cache."/home/user/Games/Doom/DOOM2.WAD"]
size = 14604584
mtime = 1729180000
sha1 = "7ec7652fcfce8ddc6e801839291f0e28ef1d5ae7"
```

---

## 3. Component Design

### 3.1 Built-in SHA-1 & WAD Catalog (`src/wad_catalog.h`, `src/wad_catalog.cpp`)

To avoid external OpenSSL/crypto library build dependencies, implement a self-contained, zero-dependency RFC 3174 `sha1` utility (~120 lines of C++17) inside `doomlauncher::WadCatalog`.

#### Data Structures
```cpp
namespace doomlauncher {

struct WadFile {
    std::string path;           // Canonical/normalized absolute path
    std::string filename;       // e.g. "DOOM2.WAD"
    std::string stem_lower;     // e.g. "doom2"
    std::string sha1;           // 40-char lowercase hex SHA-1 digest
    uintmax_t file_size = 0;
    int64_t mtime = 0;
    bool is_iwad_header = false; // True if first 4 bytes == "IWAD" (vs "PWAD" or PK3)
};

class WadCatalog {
public:
    void scan_directories(const std::vector<std::string>& dirs);
    const WadFile* add_wad_file(const std::string& filepath);

    const WadFile* find_by_sha1(const std::string& sha1) const;
    const WadFile* find_by_filename(const std::string& filename) const;
    const WadFile* find_by_alias(const std::string& alias) const;

    const std::vector<WadFile>& get_all_wads() const;
    std::vector<WadFile> get_prioritized_dependency_wads() const;

    static std::string compute_file_sha1(const std::string& filepath);
    static bool inspect_wad_header(const std::string& filepath, bool& out_is_iwad);
};

} // namespace doomlauncher
```

- **WAD Header Inspection (`IWAD` vs `PWAD`)**:
  - Reading the first 4 bytes of `.wad` files (`"IWAD"` vs `"PWAD"`) allows the catalog to automatically identify base IWADs vs patch PWADs when populating the dependency selector in the "Create Module" dialog.
- **Prioritized Dependency List (`get_prioritized_dependency_wads`)**:
  - Returns all known WADs sorted with main IWADs at the very top:
    1. `DOOM2.WAD` (`doom2`)
    2. `DOOM.WAD` (`doom`)
    3. `PLUTONIA.WAD`, `TNT.WAD`, `FREEDOOM2.WAD`, `FREEDOOM1.WAD`, `HERETIC.WAD`, `HEXEN.WAD`
    4. Other discovered IWADs (where header == `"IWAD"`)
    5. Other discovered PWADs alphabetically

---

### 3.2 Module Definition & SHA-1 Matching (`src/config_module.h`, `src/config_module.cpp`)

#### Module Schema Enhancements
Modules in `/usr/share/doomlauncher/modules/games/*.toml` and `~/.config/doomlauncher/modules/games/*.toml` no longer specify `savedir` and can declare expected SHA-1 hashes alongside filenames:

```toml
[doom2]
type = "game"
title = "Doom II: Hell on Earth"
iwad = "DOOM2.WAD"
iwad_sha1 = [
    "7ec7652fcfce8ddc6e801839291f0e28ef1d5ae7", # v1.9
    "30e3c2d0350b67bfbf47271970b74b2f6fb8ec8b"  # BFG Edition
]
compat_level = 2
requires_capability = ["vanilla"]
```

```toml
[sigil]
type = "game"
title = "Sigil"
base_game = "doom"
pwads = ["SIGIL.WAD"]
pwad_sha1 = [
    "c19ce88f5dc9f2cab6f4fbd7db1eb6ab4f00b892" # v1.21
]
compat_level = 3
requires_capability = ["vanilla"]
```

#### Matching & Activation Rules in `ModuleRegistry::resolve_menu_items()`
1. **IWAD Resolution**:
   - If the module specifies `iwad_sha1` (string or array of strings), `WadCatalog` searches for a discovered WAD matching any listed SHA-1 hash.
   - If not matched by SHA-1 (or `iwad_sha1` is omitted/empty), fallback to `WadCatalog::find_by_filename(iwad)` / `AssetResolver::resolve(iwad)`.
2. **PWAD Resolution**:
   - For each PWAD entry (or `pwad_sha1` list), match against `WadCatalog` by SHA-1 first, then by filename/path via `AssetResolver`.
3. **Dependency Satisfaction & Visibility Filter**:
   - A module's `base_game` / `requires` modules must also have their WAD requirements satisfied.
   - **Activation Rule**: Only modules whose IWAD and PWAD requirements are **all found** (`assets_found == true`) become active and are returned in `resolve_menu_items()` as visible menu entries. Modules for WADs the user does not own remain hidden.
4. **Unmatched WAD Detection**:
   - Provide `ModuleRegistry::find_modules_for_wad(const WadFile& wad)` which checks if any registered module references `wad.sha1` or `wad.filename` as its primary asset (`pwads` if the module has a `base_game`, or `iwad` if standalone).
   - Used immediately after the user adds WAD(s) in the UI to decide whether to activate an existing module or open the **Create Module** dialog.

---

### 3.3 Centralized Save Directories

- Every active module automatically receives:
  ```
  ~/.config/doomlauncher/saves/<module_name>
  ```
  (respecting `$XDG_CONFIG_HOME/doomlauncher/saves/<module_name>` if `$XDG_CONFIG_HOME` is set).
- In `ModuleRegistry::resolve_menu_items()`:
  - If `merged_scope` does not explicitly override `savedir` (or if it uses legacy `{{MODULE_DIR}}/.saves`), set `SAVEDIR` and `savedir` to `ModuleRegistry::get_module_save_dir(mod.name)`.
- In [`Menu::launchSelected()`](file:///home/kgorelov/git/doomlauncher/src/menu.cpp):
  - Before invoking `std::system(cmd.c_str())`, ensure `std::filesystem::create_directories(save_dir)` is called so engines like `dsda-doom`, `boom`, `chocolate-doom`, and `gzdoom` never fail due to a missing target directory.
- Update all built-in game modules (`modules/games/doom1.toml`, `doom2.toml`, `plutonia.toml`, `tnt.toml`, `sigil.toml`) to remove `savedir = "{{MODULE_DIR}}/.saves"`.

---

### 3.4 UI Dialogs: "Add WAD" Browser & "Create Module" Form (`src/menu.h`, `src/menu.cpp`)

Extend `OverlayState` in [`src/menu.h`](file:///home/kgorelov/git/doomlauncher/src/menu.h):

```cpp
enum class OverlayState {
    None,
    Help,
    ModuleInfo,
    EngineSelect,
    AddWadDialog,     // F5: Filesystem browser to select & add WAD(s)
    CreateModuleForm, // Prompted when added WAD has no matching module
    QuitConfirm
};
```

#### 1. Add WAD Dialog (`OverlayState::AddWadDialog`, Shortcut: <kbd>F5</kbd>)
```
+--------------------------------------------------------------------------+
|                         === ADD WAD FILES ===                            |
|--------------------------------------------------------------------------|
| Current Dir: /home/kgorelov/Downloads/wads                               |
|                                                                          |
|   [..] (Parent Directory)                                                |
|   [DIR] eviternity/                                                      |
| > [x] Eviternity.wad                                         (32.4 MB)   |
|   [ ] sunlust.wad                                            (28.1 MB)   |
|   [ ] scythe2.wad                                            (14.7 MB)   |
|                                                                          |
| Selected: 1 file(s)                                                      |
| [ Up/Down: Navigate | Space: Select/Toggle | Enter: Open Dir / Confirm ] |
| [ A: Confirm Selected | Esc: Cancel ]                                    |
+--------------------------------------------------------------------------+
```
- **Navigation & Controls**:
  - Starts in the last browsed directory, the first configured `wad_dir`, or `$HOME`.
  - Lists `[..]` (parent directory), subdirectories `[DIR]`, and `.wad` / `.pk3` / `.zip` files (case-insensitive extension filter).
  - `Up` / `Down` / `PageUp` / `PageDown`: Move cursor through directory entries.
  - `Space`: Toggle selection checkbox `[x]` on a WAD file (supports selecting single or multiple WADs).
  - `Enter`:
    - On `[..]` or `[DIR]`: enters that directory.
    - On a file: if no files are multi-selected with `Space`, selects that single file and triggers **Add & Discover**; if files are already marked `[x]`, confirms all marked files.
  - `A` (or `F2`): Confirms all currently selected `[x]` WAD files.
  - `Esc` / `F5`: Cancels and returns to the main menu.
- **Post-Selection Flow**:
  1. Selected WAD file(s) are registered in `UserConfig.iwads` (persisted to `~/.config/doomlauncher/config.toml`) and hashed via `WadCatalog::add_wad_file()` (cached in `state.toml`).
  2. `ModuleRegistry` runs module discovery across all loaded modules.
  3. **Case A — Matching Module Found**:
     - Re-resolves menu items (`resolve_menu_items()`), selects the newly activated module in the menu, and displays OSD status: `"Activated module '<Title>' for <filename>"`.
  4. **Case B — No Matching Module Found**:
     - Transitions directly to `OverlayState::CreateModuleForm` pre-populated with the selected WAD file(s).

#### 2. Create Module Form Dialog (`OverlayState::CreateModuleForm`)
```
+--------------------------------------------------------------------------+
|                 === CREATE MODULE FOR EVITERNITY.WAD ===                 |
|--------------------------------------------------------------------------|
|   WAD File(s):       Eviternity.wad (SHA1: 8f3a91c4...)                  |
| > Title:             Eviternity_                                         |
|   Base WAD (IWAD):   < DOOM2.WAD (/home/user/Games/Doom/DOOM2.WAD) >     |
|   Extra Dependencies:[ None ] (Enter to select from known WADs)          |
|   Complevel:         < 11 (MBF) >                                        |
|   Engine Capability: < mbf >                                             |
|                                                                          |
|                     [ SAVE & ACTIVATE MODULE ]                           |
|                                                                          |
| [ Up/Down: Field | Left/Right: Cycle Option | Type: Edit Title ]         |
| [ Enter on Save: Create ~/.config/doomlauncher/modules/games/...toml ]   |
+--------------------------------------------------------------------------+
```
- **Form Fields**:
  1. **Title** (editable text input via `SDL_TEXTINPUT` / `SDLK_BACKSPACE`, initialized to a human-readable title derived from the WAD filename stem, e.g., `eviternity.wad` -> `"Eviternity"`).
  2. **Base WAD / IWAD Dependency**:
     - Cycles through discovered IWADs/WADs from `WadCatalog::get_prioritized_dependency_wads()`, with **`DOOM2.WAD`** and **`DOOM.WAD`** prioritized first, plus a `(Standalone IWAD)` option if the added WAD is itself an IWAD.
  3. **Additional WAD Dependencies**:
     - Optional multi-select toggle list of other known WADs if the module needs extra PWADs loaded before it.
  4. **Compatibility Level (`compat_level`)**:
     - Cycles through standard Doom complevels:
       - `Default / Auto`
       - `2 (Doom II v1.9)`
       - `3 (Ultimate Doom)`
       - `4 (Final Doom)`
       - `9 (Boom)`
       - `11 (MBF)`
       - `21 (MBF21)`
  5. **Engine Capability / Supported Engines (`requires_capability`)**:
     - Auto-synced when `compat_level` changes, or manually cycled via `Left`/`Right`:
       - `vanilla` (All engines)
       - `limit-removing`
       - `boom` (DSDA-Doom, Boom, GZDoom)
       - `mbf` (DSDA-Doom, Boom, GZDoom)
       - `mbf21` (DSDA-Doom, GZDoom)
       - `gzdoom` (GZDoom only)
  6. **`[ SAVE & ACTIVATE MODULE ]` Button**:
     - Writes `~/.config/doomlauncher/modules/games/<slug>.toml` containing `title`, `base_game` / `iwad`, `pwads`, `pwad_sha1`, `compat_level`, and `requires_capability`.
     - Reloads the module into `ModuleRegistry`, refreshes the menu items, highlights the new game entry, and shows `"Created module ~/.config/doomlauncher/modules/games/<slug>.toml"`.

---

## 4. Phased Implementation Roadmap

### Phase 1: Config Array Support & Centralized Save Directories
- Update [`UserConfig`](file:///home/kgorelov/git/doomlauncher/src/asset_resolver.h) to store `std::vector<std::string> module_dirs` (supporting both `modules = [...]` array and legacy string in `config.toml`) and `std::vector<std::string> manual_wads`.
- Implement `AssetResolver::save_config()` to persist `[paths]`, `[iwads]`, and `[preferences]` to `~/.config/doomlauncher/config.toml`.
- Implement `ModuleRegistry::get_default_user_modules_dir()` (`~/.config/doomlauncher/modules`) and `ModuleRegistry::get_module_save_dir(module_name)` (`~/.config/doomlauncher/saves/<module_name>`).
- Automatically inject `SAVEDIR` = `~/.config/doomlauncher/saves/<module_name>` during module resolution and create the directory prior to game launch.
- Remove `savedir = "{{MODULE_DIR}}/.saves"` from `modules/games/*.toml`.

### Phase 2: SHA-1 Hashing, WAD Catalog & Cache Persistence
- Create `src/wad_catalog.h` and `src/wad_catalog.cpp`:
  - Implement RFC 3174 SHA-1 hashing (`WadCatalog::compute_file_sha1`).
  - Implement WAD header check (`IWAD` vs `PWAD` magic bytes).
  - Implement directory scanning across default system paths + user `wad_dirs` + explicit `[iwads]` entries.
  - Implement `WadCatalog` SHA-1 cache serialization in `state.toml` (`[wad_cache]`) keyed by path, file size, and `mtime`.
  - Implement prioritized WAD sorting (`DOOM2.WAD` and `DOOM.WAD` at the top of the dependency list).

### Phase 3: Inverse Control Flow & SHA-1 Module Activation
- Extend [`Module`](file:///home/kgorelov/git/doomlauncher/src/config_module.h) to parse `iwad_sha1`, `pwad_sha1`, and `sha1` fields (supporting single string or array of strings).
- Update [`ModuleRegistry::resolve_menu_items()`](file:///home/kgorelov/git/doomlauncher/src/config_module.cpp):
  - Resolve module WAD requirements against `WadCatalog` (by SHA-1 first, then filename/alias).
  - Filter `resolve_menu_items()` so only modules whose WAD dependencies are satisfied become active menu items (with an optional flag `include_inactive = false` for internal inspection/testing).
- Add `ModuleRegistry::find_modules_for_wad(const WadFile& wad)` and `ModuleRegistry::create_user_module(...)` which writes `~/.config/doomlauncher/modules/games/<name>.toml`.
- Update [`src/main.cpp`](file:///home/kgorelov/git/doomlauncher/src/main.cpp) to load all configured `module_dirs` (system pre-installed + `~/.config/doomlauncher/modules`).

### Phase 4: Interactive "Add WAD" & "Create Module" UI Dialogs
- Add `OverlayState::AddWadDialog` and `OverlayState::CreateModuleForm` to [`Menu`](file:///home/kgorelov/git/doomlauncher/src/menu.h).
- Bind <kbd>F5</kbd> (and update the F1 Help overlay and OSD shortcut bar) to open the **Add WAD** filesystem browser dialog.
- Implement directory traversal, `.wad`/`.pk3` filtering, multi-file selection (`Space`), and confirmation (`Enter` / `A`) in `Menu`.
- Wire post-selection WAD hashing, config persistence, and automatic module activation.
- Implement the **Create Module** modal form when an added WAD has no matching module, including text input for Title, prioritized IWAD dependency picker (`doom2.wad` and `doom.wad` at top), complevel selector, and capability selector.

### Phase 5: Unit Tests & Verification
- Add comprehensive tests in [`src/test_config.cpp`](file:///home/kgorelov/git/doomlauncher/src/test_config.cpp):
  - SHA-1 calculation and `state.toml` cache hit/invalidation tests.
  - `config.toml` array `modules = [...]` loading and saving tests.
  - Centralized save directory (`~/.config/doomlauncher/saves/<module_name>`) resolution tests.
  - Inverse control flow tests: modules only activate when their required WADs (matched by SHA-1 or filename) are present in `WadCatalog`.
  - Prioritized dependency ordering test (`DOOM2.WAD` and `DOOM.WAD` first).
  - User module generation and TOML serialization test (`~/.config/doomlauncher/modules/games/<slug>.toml`).

---

## 5. Execution Checklist

- [ ] **Step 1: Config & Save Directory Separation**
  - [ ] Support `modules` array in `UserConfig` and `AssetResolver::load_config()` / `save_config()`.
  - [ ] Route all module save directories to `~/.config/doomlauncher/saves/<modulename>/` and auto-create on launch.
  - [ ] Strip legacy `savedir = "{{MODULE_DIR}}/.saves"` from `modules/games/*.toml`.
- [ ] **Step 2: WAD Catalog & SHA-1 Engine**
  - [ ] Implement `src/wad_catalog.h` and `src/wad_catalog.cpp` with built-in SHA-1 calculation and `[wad_cache]` in `state.toml`.
  - [ ] Implement prioritized dependency list placing `doom2.wad` and `doom.wad` at the top.
- [ ] **Step 3: WAD-Driven Module Activation**
  - [ ] Add `iwad_sha1` / `pwad_sha1` matching in `ModuleRegistry`.
  - [ ] Activate and display only modules whose WAD requirements are satisfied.
  - [ ] Implement `ModuleRegistry::create_user_module()` saving to `~/.config/doomlauncher/modules/games/`.
- [ ] **Step 4: "Add WAD" & "Create Module" UI**
  - [ ] Implement `OverlayState::AddWadDialog` (F5 filesystem browser with multi-WAD selection).
  - [ ] Implement `OverlayState::CreateModuleForm` (interactive dialog for unmatched WADs).
  - [ ] Update F1 Help and OSD hints with `F5: Add WAD`.
- [ ] **Step 5: Testing & Build Verification**
  - [ ] Add unit tests in `src/test_config.cpp` for SHA-1 matching, module activation, save dirs, and module creation.
  - [ ] Verify clean compilation with `-Wall -Wextra` and 100% test pass rate.
