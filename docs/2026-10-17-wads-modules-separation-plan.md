# Technical Implementation Plan: WADs & Modules Separation, SHA-1 Discovery & Interactive Add/Create Workflows

This document establishes the technical design and phased implementation roadmap for the architecture described in [`docs/2026-10-17-wads-modules-separation.md`](file:///home/kgorelov/git/doomlauncher/docs/2026-10-17-wads-modules-separation.md).

> [!IMPORTANT]
> **Design Principle — Simplicity Over Backward Compatibility**:
> Because Doom Launcher has not been released yet, this plan drops all legacy config formats, variable aliases, `{{MODULE_DIR}}` asset resolution, and per-module `savedir` overrides in favor of a single, clean, canonical schema.

---

## 1. Objectives & Architectural Shift

The goal of this redesign is to completely decouple **WAD files**, **Game Modules**, and **Save Directories**, inverting the control flow from module-centric directory structures to WAD-driven activation:

1. **Inversion of Control (WADs Activate Modules)**:
   - Modules are strictly declarative game/mod definitions stored in system and user module directories. They never contain WAD files or save directories.
   - The launcher discovers WADs from system directories, user `wad_dirs`, and explicitly added `iwads`.
   - A module becomes **active** (and appears as a menu entry) **if and only if** all of its required WAD dependencies (IWAD + PWADs) are satisfied by discovered WADs on the user's system.
2. **Multi-Directory Module Loading**:
   - `paths.modules` in `~/.config/doomlauncher/config.toml` is strictly an array of directories, loading pre-installed modules (`/usr/share/doomlauncher/modules`, `./modules`) and user-created modules (`~/.config/doomlauncher/modules`).
3. **Centralized Save Directories**:
   - Modules never define `savedir`.
   - All save games automatically resolve to `~/.config/doomlauncher/saves/<module_name>/` (created automatically before launch).
4. **SHA-1 WAD Identification & Caching**:
   - Every discovered or added WAD has its SHA-1 hash computed (and cached by file path + `mtime` + file size in `state.toml` for instant startup).
   - Modules match WADs by **SHA-1 hash** (or filename when SHA-1 is not specified in the module).
5. **Interactive In-Launcher WAD Addition & Module Creation**:
   - **Add WAD Dialog (<kbd>F5</kbd>)**: Built-in filesystem browser allowing the user to navigate directories, select one or multiple `.wad` / `.pk3` files, and add them to `~/.config/doomlauncher/config.toml`.
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
|  - config.toml: [iwads]                    - User-created:                        |
|         |                                    ~/.config/doomlauncher/modules       |
|         v                                           |                             |
|  +-------------------------------+                  |                             |
|  | WAD Catalog (Path, Name, SHA1)| -----------------+                             |
|  +-------------------------------+                  |                             |
|         |                                           v                             |
|         |            +-----------------------------------------------+            |
|         +----------> |     Module Matcher & Dependency Resolver      |            |
|                      |  - Matches WADs by SHA-1 / filename           |            |
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

## 2. Clean-Slate Configuration & State Schemas

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
# Explicitly registered WAD files (added manually or via the F5 "Add WAD" dialog)
doom = "/home/user/Games/Doom/DOOM.WAD"
doom2 = "/home/user/Games/Doom/DOOM2.WAD"
ancient_aliens = "/home/user/Downloads/aaliens.wad"

[preferences]
default_engine = "dsda-doom"
osd_font = "mono"
```

- **Removed Legacy Baggage**:
  - Remove single-string `paths.modules_path` and `UserConfig::modules_path`; use `std::vector<std::string> module_dirs` exclusively.
  - Default `module_dirs` when not specified in `config.toml`:
    1. `/usr/share/doomlauncher/modules`
    2. `./modules`
    3. `~/.config/doomlauncher/modules`
  - Modules loaded from later directories override modules of the same name from earlier directories.
- **Persisting Config Changes**:
  - `AssetResolver::save_config(const UserConfig& config, const std::string& explicit_path = "")` writes the updated `UserConfig` to `~/.config/doomlauncher/config.toml` when the user adds WADs via the UI.

### 2.2 State & SHA-1 Cache (`~/.config/doomlauncher/state.toml`)

`state.toml` stores per-module engine selections and a SHA-1 cache keyed by canonical file path, file size, and last modification timestamp (`mtime`):

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

### 2.3 Canonical Module Schema (`modules/games/*.toml` & `modules/engines/*.toml`)

Drop legacy `[vars]`, `[templates]`, `{{MODULE_DIR}}`, `savedir`, and duplicate uppercase/lowercase variable aliases. Standardize on a single set of fields:

#### Game Module (`modules/games/doom2.toml`)
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

#### Mod / PWAD Module (`modules/games/sigil.toml`)
```toml
[sigil]
type = "game"
title = "Sigil"
base_game = "doom"
pwads = ["SIGIL.WAD"]
pwad_sha1 = [
    "c19ce88f5dc9f2cab6f4fbd7db1eb6ab4f00b892"
]
compat_level = 3
requires_capability = ["vanilla"]
```

#### Engine Module (`modules/engines/dsda_doom.toml`)
```toml
[dsda-doom]
type = "engine"
title = "DSDA-Doom"
binary = "dsda-doom"
binaries = ["dsda-doom"]
capabilities = ["vanilla", "limit-removing", "boom", "mbf", "mbf21", "complevel"]
cmd = "{{BIN}} -iwad {{IWAD}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %} -savedir {{SAVEDIR}}"
```

Standardized template variables injected for engine command rendering:
- `{{BIN}}`: Resolved engine executable path
- `{{IWAD}}`: Resolved absolute path to the IWAD file
- `{{PWADS}}`: Array of resolved absolute paths to PWAD files
- `{{COMPAT_LEVEL}}`: Compatibility level string (if defined)
- `{{SAVEDIR}}`: `~/.config/doomlauncher/saves/<module_name>`

---

## 3. Component Design

### 3.1 Built-in SHA-1 & WAD Catalog (`src/wad_catalog.h`, `src/wad_catalog.cpp`)

Implement a self-contained RFC 3174 `sha1` utility (~120 lines of C++17) inside `doomlauncher::WadCatalog` with zero external library dependencies.

```cpp
namespace doomlauncher {

struct WadFile {
    std::string path;            // Canonical absolute path
    std::string filename;        // e.g. "DOOM2.WAD"
    std::string stem_lower;      // e.g. "doom2"
    std::string sha1;            // 40-char lowercase hex SHA-1 digest
    uintmax_t file_size = 0;
    int64_t mtime = 0;
    bool is_iwad_header = false; // True if first 4 bytes == "IWAD"
};

class WadCatalog {
public:
    void scan_From_config(const UserConfig& config);
    const WadFile* add_wad_file(const std::string& filepath, const std::string& alias = "");

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
  - Reads the first 4 bytes of `.wad` files (`"IWAD"` vs `"PWAD"`) to identify base IWADs vs patch PWADs.
- **Prioritized Dependency List (`get_prioritized_dependency_wads`)**:
  - Returns all known WADs sorted with main IWADs at the very top:
    1. `DOOM2.WAD` (`doom2`)
    2. `DOOM.WAD` (`doom`)
    3. `PLUTONIA.WAD`, `TNT.WAD`, `FREEDOOM2.WAD`, `FREEDOOM1.WAD`, `HERETIC.WAD`, `HEXEN.WAD`
    4. Other discovered IWADs (where header == `"IWAD"`)
    5. Other discovered PWADs alphabetically

---

### 3.2 Module Matching, Activation & Centralized Saves (`src/config_module.h`, `src/config_module.cpp`)

#### Simplified `Module` Struct
```cpp
struct Module {
    std::string name;
    std::string title;
    std::string type;                  // "game" or "engine"
    std::string base_game;             // e.g. "doom2" or "doom"
    std::vector<std::string> requires; // Additional module dependencies

    std::string iwad;
    std::vector<std::string> iwad_sha1;
    std::vector<std::string> pwads;
    std::vector<std::string> pwad_sha1;

    std::string compat_level;
    std::vector<std::string> requires_capability;
};
```

#### Matching & Activation Rules in `ModuleRegistry::resolve_menu_items()`
1. **IWAD Resolution**:
   - Inherit `iwad` / `iwad_sha1` from `base_game` if not defined on the module itself.
   - If `iwad_sha1` is non-empty, look up each hash in `WadCatalog::find_by_sha1()`.
   - If not matched by SHA-1 (or `iwad_sha1` is empty), look up `iwad` via `WadCatalog::find_by_filename()` / `find_by_alias()`.
2. **PWAD Resolution**:
   - Collect PWADs from any required dependency modules first, followed by the module's own `pwads` / `pwad_sha1`.
   - Match against `WadCatalog` by SHA-1 first, then by filename/alias.
3. **Activation Filter**:
   - A module is **activated** if and only if its IWAD and all required PWADs resolve to existing files in `WadCatalog`.
   - Unsatisfied modules are omitted from the menu.
4. **Centralized Save Directory**:
   - Set `item.save_dir = get_module_save_dir(mod.name)` (`~/.config/doomlauncher/saves/<module_name>`).
   - Inject `SAVEDIR` into the template scope for engine command generation.
   - In [`Menu::launchSelected()`](file:///home/kgorelov/git/doomlauncher/src/menu.cpp), call `std::filesystem::create_directories(item.save_dir)` before executing the command.

---

### 3.3 UI Dialogs: "Add WAD" Browser & "Create Module" Form (`src/menu.h`, `src/menu.cpp`)

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
| [ Up/Down: Navigate | Space: Toggle Select | Enter: Open Dir / Confirm ] |
| [ A: Confirm Selected | Esc: Cancel ]                                    |
+--------------------------------------------------------------------------+
```
- **Navigation & Controls**:
  - Lists `[..]` (parent directory), subdirectories `[DIR]`, and `.wad` / `.pk3` files.
  - `Up` / `Down` / `PageUp` / `PageDown`: Navigate entries.
  - `Space`: Toggle selection `[x]` on one or multiple WAD files.
  - `Enter`:
    - On `[..]` or `[DIR]`: enters that directory.
    - On a WAD file: if no files are multi-selected, selects that file and confirms; if files are already marked `[x]`, confirms all marked files.
  - `A`: Confirms all marked `[x]` WAD files.
  - `Esc` / `F5`: Cancels and closes dialog.
- **Post-Selection Flow**:
  1. Adds the selected WAD file(s) to `UserConfig.iwads` (saved to `~/.config/doomlauncher/config.toml`) and indexes/hashes them in `WadCatalog` (cached in `state.toml`).
  2. Checks if any loaded module matches the newly added WAD(s) (by SHA-1 or filename).
  3. **If a module is found**: re-runs `resolve_menu_items()`, highlights the newly activated entry, and shows `"Activated module '<Title>'"`.
  4. **If no module is found**: opens `OverlayState::CreateModuleForm` for the added WAD(s).

#### 2. Create Module Form Dialog (`OverlayState::CreateModuleForm`)
```
+--------------------------------------------------------------------------+
|                 === CREATE MODULE FOR EVITERNITY.WAD ===                 |
|--------------------------------------------------------------------------|
|   WAD File(s):       Eviternity.wad (SHA1: 8f3a91c4...)                  |
| > Title:             Eviternity_                                         |
|   Base WAD (IWAD):   < DOOM2.WAD (/home/user/Games/Doom/DOOM2.WAD) >     |
|   Extra Dependencies:[ None ] (Enter to toggle known WADs)               |
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
  1. **Title**: Text input (`SDL_TEXTINPUT` / `SDLK_BACKSPACE`), pre-filled from the WAD filename stem.
  2. **Base WAD (IWAD)**: Cycles through `WadCatalog::get_prioritized_dependency_wads()` (`DOOM2.WAD` and `DOOM.WAD` first, plus `(Standalone IWAD)` if the WAD itself has an `"IWAD"` header).
  3. **Extra Dependencies**: Optional selection of additional known WADs.
  4. **Complevel**: Cycles `Default`, `2 (Doom v1.9)`, `3 (Ultimate Doom)`, `4 (Final Doom)`, `9 (Boom)`, `11 (MBF)`, `21 (MBF21)`.
  5. **Engine Capability**: Cycles `vanilla`, `limit-removing`, `boom`, `mbf`, `mbf21`, `gzdoom`.
  6. **`[ SAVE & ACTIVATE MODULE ]`**: Writes `~/.config/doomlauncher/modules/games/<slug>.toml` (including the WAD's `pwad_sha1` / `iwad_sha1`), reloads modules, refreshes the menu, and selects the newly created game.

---

## 4. Phased Implementation Roadmap

### Phase 1: Clean Config Schema, Module Directory Array & Centralized Saves
- Replace `modules_path` in [`UserConfig`](file:///home/kgorelov/git/doomlauncher/src/asset_resolver.h) with `std::vector<std::string> module_dirs`.
- Implement `AssetResolver::save_config()` to write `~/.config/doomlauncher/config.toml`.
- Standardize engine templates (`modules/engines/*.toml`) to use `{{BIN}}`, `{{IWAD}}`, `{{PWADS}}`, `{{COMPAT_LEVEL}}`, and `{{SAVEDIR}}`.
- Remove `savedir` from `modules/games/*.toml` and remove `{{MODULE_DIR}}` expansion and legacy variable aliases from [`src/config_module.cpp`](file:///home/kgorelov/git/doomlauncher/src/config_module.cpp).
- Route all module save directories to `~/.config/doomlauncher/saves/<module_name>/` and create on launch.

### Phase 2: SHA-1 Hashing, WAD Catalog & Cache Persistence
- Create `src/wad_catalog.h` and `src/wad_catalog.cpp`:
  - Built-in RFC 3174 SHA-1 implementation.
  - WAD header inspection (`IWAD` vs `PWAD`).
  - Directory scanning across default system paths + user `wad_dirs` + explicit `[iwads]`.
  - `[wad_cache]` persistence in `state.toml`.
  - Prioritized dependency sorting (`DOOM2.WAD` and `DOOM.WAD` at top).

### Phase 3: WAD-Driven Module Activation & User Module Creation
- Update [`ModuleRegistry`](file:///home/kgorelov/git/doomlauncher/src/config_module.h) to load modules from all directories in `UserConfig::module_dirs` (system pre-installed + `~/.config/doomlauncher/modules`).
- Match module WAD requirements against `WadCatalog` via `iwad_sha1` / `pwad_sha1` and filename.
- Activate and return only satisfied modules in `resolve_menu_items()`.
- Implement `ModuleRegistry::create_user_module()` to write `~/.config/doomlauncher/modules/games/<slug>.toml`.

### Phase 4: Interactive "Add WAD" & "Create Module" UI Dialogs
- Add `OverlayState::AddWadDialog` (<kbd>F5</kbd>) and `OverlayState::CreateModuleForm` to [`Menu`](file:///home/kgorelov/git/doomlauncher/src/menu.h).
- Implement filesystem navigation, `.wad`/`.pk3` filtering, multi-file selection, and config saving.
- Connect post-add module matching: activate existing module if matched, or transition to `CreateModuleForm` if unmatched.
- Update F1 Help overlay and bottom OSD shortcut hint bar (`F5: Add WAD`).

### Phase 5: Unit Tests & Verification
- Update [`src/test_config.cpp`](file:///home/kgorelov/git/doomlauncher/src/test_config.cpp) to test the clean-slate architecture:
  - SHA-1 calculation and `[wad_cache]` persistence in `state.toml`.
  - `config.toml` array `modules = [...]` and `[iwads]` loading/saving.
  - Centralized save directory (`~/.config/doomlauncher/saves/<module_name>`) injection.
  - WAD-driven module activation (modules activate only when their required WADs are present, matched by SHA-1 or filename).
  - Prioritized dependency ordering (`DOOM2.WAD` and `DOOM.WAD` first).
  - User module creation and activation flow.

---

## 5. Execution Checklist

- [x] **Step 1: Clean Config & Save Directory Separation**
  - [x] Replace `modules_path` with `module_dirs` array in `UserConfig` and implement `AssetResolver::save_config()`.
  - [x] Remove legacy variable aliases and `{{MODULE_DIR}}` from `config_module.cpp`.
  - [x] Standardize `modules/engines/*.toml` and `modules/games/*.toml` (remove `savedir`).
  - [x] Route all save directories to `~/.config/doomlauncher/saves/<modulename>/` and auto-create on launch.
- [x] **Step 2: WAD Catalog & SHA-1 Engine**
  - [x] Implement `src/wad_catalog.h` and `src/wad_catalog.cpp` with built-in SHA-1 calculation and `[wad_cache]` in `state.toml`.
  - [x] Implement prioritized dependency list placing `doom2.wad` and `doom.wad` at the top.
- [ ] **Step 3: WAD-Driven Module Activation**
  - [ ] Add `iwad_sha1` / `pwad_sha1` matching in `ModuleRegistry`.
  - [ ] Activate and display only modules whose WAD requirements are satisfied.
  - [ ] Implement `ModuleRegistry::create_user_module()` saving to `~/.config/doomlauncher/modules/games/`.
- [ ] **Step 4: "Add WAD" & "Create Module" UI**
  - [ ] Implement `OverlayState::AddWadDialog` (F5 filesystem browser with multi-WAD selection).
  - [ ] Implement `OverlayState::CreateModuleForm` (interactive dialog for unmatched WADs).
  - [ ] Update F1 Help and OSD hints with `F5: Add WAD`.
- [ ] **Step 5: Testing & Build Verification**
  - [ ] Update unit tests in `src/test_config.cpp` for the simplified schema, SHA-1 matching, module activation, save dirs, and module creation.
  - [ ] Verify clean compilation with `-Wall -Wextra` and 100% test pass rate.
