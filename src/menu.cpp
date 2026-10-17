#include "menu.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace {

namespace fs = std::filesystem;

struct ComplevelOption {
    const char* value;
    const char* label;
    int suggested_cap_idx;
};

constexpr ComplevelOption kComplevelOptions[] = {
    { "",   "Default / Auto",     0 },
    { "2",  "2 (Doom v1.9)",      0 },
    { "3",  "3 (Ultimate Doom)",  0 },
    { "4",  "4 (Final Doom)",     0 },
    { "9",  "9 (Boom)",           2 },
    { "11", "11 (MBF)",           3 },
    { "21", "21 (MBF21)",         4 }
};
constexpr int kNumComplevelOptions = static_cast<int>(sizeof(kComplevelOptions) / sizeof(kComplevelOptions[0]));

constexpr const char* kCapabilityOptions[] = {
    "vanilla",
    "limit-removing",
    "boom",
    "mbf",
    "mbf21",
    "gzdoom"
};
constexpr int kNumCapabilityOptions = static_cast<int>(sizeof(kCapabilityOptions) / sizeof(kCapabilityOptions[0]));

std::string to_lower_str(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return res;
}

bool is_supported_wad_ext(const fs::path& p) {
    std::string ext = to_lower_str(p.extension().string());
    return ext == ".wad" || ext == ".pk3" || ext == ".ipk3";
}

std::string format_file_size(uintmax_t bytes) {
    char buf[64];
    if (bytes >= 1024ULL * 1024ULL) {
        double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
    } else if (bytes >= 1024ULL) {
        double kb = static_cast<double>(bytes) / 1024.0;
        std::snprintf(buf, sizeof(buf), "%.1f KB", kb);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return std::string(buf);
}

std::string prettify_stem_title(const std::string& stem) {
    std::string out;
    bool cap_next = true;
    for (char c : stem) {
        if (c == '_' || c == '-') {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
            cap_next = true;
        } else if (cap_next) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            cap_next = false;
        } else {
            out.push_back(c);
        }
    }
    return out.empty() ? stem : out;
}

} // namespace

Menu::Menu(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* smallFont, int screenWidth, int screenHeight, TTF_Font* altSmallFont)
    : renderer(renderer), font(font), smallFont(smallFont), altSmallFont(altSmallFont), useAlternateFont(false),
      screenWidth(screenWidth), screenHeight(screenHeight), selectedItem(0), quitSelected(false) {
}

Menu::~Menu() {
    clearTextCache();
}

void Menu::toggleOsdFont() {
    useAlternateFont = !useAlternateFont;
    statusMessage = useAlternateFont ? "[ OSD Font: Classic Doom HUD (STCFN) - Press F9 to toggle ]"
                                     : "[ OSD Font: Clean Monospace (Hack) - Press F9 to toggle ]";
}

void Menu::setMenuItems(const std::vector<doomlauncher::ResolvedMenuItem>& items) {
    menuItems = items;
    for (auto& item : menuItems) {
        if (item.cmd.empty() && !item.compatible_engines.empty()) {
            item.cmd = item.get_command();
        }
    }

    doomlauncher::ResolvedMenuItem quit_item;
    quit_item.title = "Quit";
    quit_item.module_name = "quit";
    menuItems.push_back(quit_item);

    selectedItem = 0;
}

void Menu::refreshMenuFromRegistry(const std::string& selectModuleName) {
    if (!moduleRegistry) return;

    auto items = moduleRegistry->resolve_menu_items();
    setMenuItems(items);

    if (!selectModuleName.empty()) {
        for (size_t i = 0; i < menuItems.size(); ++i) {
            if (menuItems[i].module_name == selectModuleName) {
                selectedItem = static_cast<int>(i);
                break;
            }
        }
    }
}

void Menu::openAddWadDialog(const std::string& startDir) {
    std::string target = startDir;
    if (target.empty()) {
        if (!browserCurrentDir.empty() && fs::exists(browserCurrentDir)) {
            target = browserCurrentDir;
        } else if (moduleRegistry && !moduleRegistry->get_user_config().wad_dirs.empty()) {
            for (const auto& wd : moduleRegistry->get_user_config().wad_dirs) {
                std::string exp = doomlauncher::AssetResolver::expand_path(wd);
                if (fs::exists(exp) && fs::is_directory(exp)) {
                    target = exp;
                    break;
                }
            }
        }
    }
    if (target.empty()) {
        const char* home = std::getenv("HOME");
        if (home && fs::exists(home)) {
            target = home;
        } else {
            target = fs::current_path().string();
        }
    }

    loadBrowserDirectory(target);
    overlayState = OverlayState::AddWadDialog;
    statusMessage.clear();
}

void Menu::loadBrowserDirectory(const std::string& dirPath) {
    std::error_code ec;
    fs::path p = fs::absolute(doomlauncher::AssetResolver::expand_path(dirPath), ec).lexically_normal();
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) {
        p = fs::current_path(ec);
    }

    browserCurrentDir = p.string();
    browserEntries.clear();
    browserCursorIndex = 0;

    if (p.has_parent_path() && p.parent_path() != p) {
        BrowserEntry parentEntry;
        parentEntry.name = "[..] (Parent Directory)";
        parentEntry.full_path = p.parent_path().string();
        parentEntry.is_dir = true;
        parentEntry.is_parent = true;
        browserEntries.push_back(parentEntry);
    }

    std::vector<BrowserEntry> dirs;
    std::vector<BrowserEntry> files;

    for (const auto& entry : fs::directory_iterator(p, ec)) {
        std::string filename = entry.path().filename().string();
        if (filename.empty() || filename[0] == '.') {
            continue;
        }
        std::error_code entry_ec;
        if (entry.is_directory(entry_ec)) {
            BrowserEntry be;
            be.name = filename + "/";
            be.full_path = entry.path().lexically_normal().string();
            be.is_dir = true;
            dirs.push_back(be);
        } else if (entry.is_regular_file(entry_ec) && is_supported_wad_ext(entry.path())) {
            BrowserEntry be;
            be.name = filename;
            be.full_path = entry.path().lexically_normal().string();
            be.is_dir = false;
            be.file_size = entry.file_size(entry_ec);
            files.push_back(be);
        }
    }

    auto caseInsensitiveLess = [](const BrowserEntry& a, const BrowserEntry& b) {
        return to_lower_str(a.name) < to_lower_str(b.name);
    };
    std::sort(dirs.begin(), dirs.end(), caseInsensitiveLess);
    std::sort(files.begin(), files.end(), caseInsensitiveLess);

    browserEntries.insert(browserEntries.end(), dirs.begin(), dirs.end());
    browserEntries.insert(browserEntries.end(), files.begin(), files.end());
}

void Menu::confirmSelectedWads() {
    std::vector<std::string> selectedPaths;
    for (const auto& entry : browserEntries) {
        if (!entry.is_dir && entry.selected) {
            selectedPaths.push_back(entry.full_path);
        }
    }

    if (selectedPaths.empty()) {
        statusMessage = "No WAD files selected";
        return;
    }

    if (!moduleRegistry) {
        overlayState = OverlayState::None;
        return;
    }

    std::vector<doomlauncher::WadFile> addedWads;
    std::vector<std::string> matchedModules;

    for (const auto& path : selectedPaths) {
        const doomlauncher::WadFile* wf_ptr = moduleRegistry->register_wad_file(path, true, configSavePath);
        if (wf_ptr) {
            doomlauncher::WadFile wf = *wf_ptr;
            addedWads.push_back(wf);
            auto mods = moduleRegistry->find_modules_for_wad(wf);
            for (const auto& m : mods) {
                if (std::find(matchedModules.begin(), matchedModules.end(), m) == matchedModules.end()) {
                    matchedModules.push_back(m);
                }
            }
        }
    }

    if (addedWads.empty()) {
        statusMessage = "Failed to add selected WAD file(s)";
        overlayState = OverlayState::None;
        return;
    }

    if (!matchedModules.empty()) {
        refreshMenuFromRegistry(matchedModules[0]);
        std::string modTitle = matchedModules[0];
        if (const auto* mod = moduleRegistry->get_module(matchedModules[0])) {
            if (!mod->title.empty()) modTitle = mod->title;
        }
        statusMessage = "Activated module: " + modTitle;
        overlayState = OverlayState::None;
    } else {
        openCreateModuleForm(addedWads);
    }
}

void Menu::openCreateModuleForm(const std::vector<doomlauncher::WadFile>& wads) {
    pendingFormWads = wads;
    if (pendingFormWads.empty()) {
        overlayState = OverlayState::None;
        return;
    }

    formTitle = prettify_stem_title(pendingFormWads[0].stem_lower);
    formFieldIndex = 0;
    formComplevelIndex = 0;
    formCapabilityIndex = 0;
    formBaseOptions.clear();
    formBaseIndex = 0;
    formExtraWads.clear();
    formExtraSelected.clear();
    formExtraCursorIndex = 0;

    bool isFirstStandaloneCandidate = pendingFormWads[0].is_iwad_header && pendingFormWads.size() == 1;
    if (isFirstStandaloneCandidate) {
        BaseWadOption standaloneOpt;
        standaloneOpt.label = "(Standalone IWAD - " + pendingFormWads[0].filename + ")";
        standaloneOpt.wad = pendingFormWads[0];
        standaloneOpt.is_standalone = true;
        formBaseOptions.push_back(standaloneOpt);
    }

    if (moduleRegistry) {
        auto prioritized = moduleRegistry->get_wad_catalog().get_prioritized_dependency_wads();
        for (const auto& wf : prioritized) {
            bool isPending = false;
            for (const auto& pw : pendingFormWads) {
                if (pw.path == wf.path) {
                    isPending = true;
                    break;
                }
            }
            if (isPending) continue;

            BaseWadOption opt;
            opt.wad = wf;
            opt.label = wf.filename + " (" + wf.path + ")";

            // Check if this WAD corresponds to a standalone base game module (e.g. doom2, doom)
            auto matchingMods = moduleRegistry->find_modules_for_wad(wf);
            for (const auto& mName : matchingMods) {
                const auto* mod = moduleRegistry->get_module(mName);
                if (mod && mod->pwads.empty() && mod->pwad_sha1.empty()) {
                    opt.base_game_module = mName;
                    break;
                }
            }
            if (opt.base_game_module.empty()) {
                if (wf.stem_lower == "doom2") opt.base_game_module = "doom2";
                else if (wf.stem_lower == "doom" || wf.stem_lower == "doom1" || wf.stem_lower == "doomu") opt.base_game_module = "doom";
            }

            formBaseOptions.push_back(opt);
            formExtraWads.push_back(wf);
            formExtraSelected.push_back(false);
        }
    }

    if (formBaseOptions.empty()) {
        BaseWadOption fallbackOpt;
        fallbackOpt.label = "DOOM2.WAD (doom2)";
        fallbackOpt.base_game_module = "doom2";
        fallbackOpt.wad.filename = "DOOM2.WAD";
        formBaseOptions.push_back(fallbackOpt);
    }

    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        SDL_StartTextInput();
    }
    overlayState = OverlayState::CreateModuleForm;
}

bool Menu::submitCreateModuleForm() {
    if (!moduleRegistry || pendingFormWads.empty()) {
        overlayState = OverlayState::None;
        return false;
    }

    doomlauncher::UserModuleSpec spec;
    spec.title = formTitle.empty() ? prettify_stem_title(pendingFormWads[0].stem_lower) : formTitle;

    bool isStandalone = false;
    if (formBaseIndex >= 0 && formBaseIndex < static_cast<int>(formBaseOptions.size())) {
        const auto& baseOpt = formBaseOptions[formBaseIndex];
        isStandalone = baseOpt.is_standalone;
        if (isStandalone) {
            spec.iwad = pendingFormWads[0].filename;
            if (!pendingFormWads[0].sha1.empty()) {
                spec.iwad_sha1.push_back(pendingFormWads[0].sha1);
            }
        } else if (!baseOpt.base_game_module.empty() && moduleRegistry->has_module(baseOpt.base_game_module)) {
            spec.base_game = baseOpt.base_game_module;
        } else if (!baseOpt.wad.filename.empty()) {
            spec.iwad = baseOpt.wad.filename;
            if (!baseOpt.wad.sha1.empty()) {
                spec.iwad_sha1.push_back(baseOpt.wad.sha1);
            }
        }
    }

    if (!isStandalone) {
        for (size_t i = 0; i < formExtraWads.size() && i < formExtraSelected.size(); ++i) {
            if (formExtraSelected[i]) {
                spec.pwads.push_back(formExtraWads[i].filename);
                if (!formExtraWads[i].sha1.empty()) {
                    spec.pwad_sha1.push_back(formExtraWads[i].sha1);
                }
            }
        }
        for (const auto& pw : pendingFormWads) {
            spec.pwads.push_back(pw.filename);
            if (!pw.sha1.empty()) {
                spec.pwad_sha1.push_back(pw.sha1);
            }
        }
    }

    if (formComplevelIndex >= 0 && formComplevelIndex < kNumComplevelOptions) {
        spec.compat_level = kComplevelOptions[formComplevelIndex].value;
    }
    if (formCapabilityIndex >= 0 && formCapabilityIndex < kNumCapabilityOptions) {
        spec.requires_capability = { kCapabilityOptions[formCapabilityIndex] };
    }

    std::string createdPath;
    bool ok = moduleRegistry->create_user_module(spec, createdPath, userModulesDir);
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        SDL_StopTextInput();
    }

    if (ok) {
        std::string slug = fs::path(createdPath).stem().string();
        refreshMenuFromRegistry(slug);
        statusMessage = "Created & activated module: " + spec.title;
        overlayState = OverlayState::None;
        return true;
    } else {
        statusMessage = "Failed to create module file";
        overlayState = OverlayState::None;
        return false;
    }
}

void Menu::handleEvent(const SDL_Event& event) {
    if (menuItems.empty()) return;

    int totalItems = static_cast<int>(menuItems.size());
    int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages < 1) totalPages = 1;
    int currentPage = selectedItem / ITEMS_PER_PAGE;

    if (overlayState != OverlayState::None) {
        if (overlayState == OverlayState::CreateModuleForm && event.type == SDL_TEXTINPUT) {
            if (formFieldIndex == 0 && formTitle.size() < 48) {
                for (const char* p = event.text.text; *p != '\0'; ++p) {
                    if (static_cast<unsigned char>(*p) >= 32 && formTitle.size() < 48) {
                        formTitle.push_back(*p);
                    }
                }
            }
            return;
        }

        if (event.type == SDL_KEYDOWN) {
            SDL_Keycode key = event.key.keysym.sym;
            switch (overlayState) {
                case OverlayState::Help:
                    if (key == SDLK_F1 || key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_SPACE) {
                        overlayState = OverlayState::None;
                    }
                    break;
                case OverlayState::ModuleInfo:
                    if (key == SDLK_F3 || key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_SPACE) {
                        overlayState = OverlayState::None;
                    }
                    break;
                case OverlayState::EngineSelect: {
                    if (selectedItem >= 0 && selectedItem < totalItems) {
                        auto& item = menuItems[selectedItem];
                        int count = static_cast<int>(item.compatible_engines.size());
                        if (key == SDLK_UP) {
                            if (count > 0) modalCursorIndex = (modalCursorIndex - 1 + count) % count;
                        } else if (key == SDLK_DOWN) {
                            if (count > 0) modalCursorIndex = (modalCursorIndex + 1) % count;
                        } else if (key == SDLK_RETURN || key == SDLK_SPACE) {
                            if (count > 0 && modalCursorIndex >= 0 && modalCursorIndex < count) {
                                item.selected_engine_index = static_cast<size_t>(modalCursorIndex);
                                item.cmd = item.get_command();
                                const auto* eng = item.get_selected_engine();
                                statusMessage = "Engine set to " + (eng ? eng->title : "selected");
                            }
                            overlayState = OverlayState::None;
                        } else if (key == SDLK_ESCAPE || key == SDLK_F4) {
                            overlayState = OverlayState::None;
                        }
                    } else {
                        overlayState = OverlayState::None;
                    }
                    break;
                }
                case OverlayState::AddWadDialog: {
                    int entryCount = static_cast<int>(browserEntries.size());
                    if (key == SDLK_UP) {
                        if (entryCount > 0) browserCursorIndex = (browserCursorIndex - 1 + entryCount) % entryCount;
                    } else if (key == SDLK_DOWN) {
                        if (entryCount > 0) browserCursorIndex = (browserCursorIndex + 1) % entryCount;
                    } else if (key == SDLK_PAGEUP) {
                        if (entryCount > 0) browserCursorIndex = std::max(0, browserCursorIndex - BROWSER_ITEMS_PER_PAGE);
                    } else if (key == SDLK_PAGEDOWN) {
                        if (entryCount > 0) browserCursorIndex = std::min(entryCount - 1, browserCursorIndex + BROWSER_ITEMS_PER_PAGE);
                    } else if (key == SDLK_SPACE) {
                        if (browserCursorIndex >= 0 && browserCursorIndex < entryCount) {
                            auto& entry = browserEntries[browserCursorIndex];
                            if (!entry.is_dir) {
                                entry.selected = !entry.selected;
                            }
                        }
                    } else if (key == SDLK_a) {
                        if (browserCursorIndex >= 0 && browserCursorIndex < entryCount) {
                            bool anySelected = false;
                            for (const auto& e : browserEntries) {
                                if (!e.is_dir && e.selected) {
                                    anySelected = true;
                                    break;
                                }
                            }
                            if (!anySelected && !browserEntries[browserCursorIndex].is_dir) {
                                browserEntries[browserCursorIndex].selected = true;
                            }
                        }
                        confirmSelectedWads();
                    } else if (key == SDLK_RETURN) {
                        if (browserCursorIndex >= 0 && browserCursorIndex < entryCount) {
                            auto& entry = browserEntries[browserCursorIndex];
                            if (entry.is_dir) {
                                std::string nextDir = entry.full_path;
                                loadBrowserDirectory(nextDir);
                            } else {
                                bool anySelected = false;
                                for (const auto& e : browserEntries) {
                                    if (!e.is_dir && e.selected) {
                                        anySelected = true;
                                        break;
                                    }
                                }
                                if (!anySelected) {
                                    entry.selected = true;
                                }
                                confirmSelectedWads();
                            }
                        }
                    } else if (key == SDLK_BACKSPACE) {
                        if (!browserEntries.empty() && browserEntries[0].is_parent) {
                            std::string parentDir = browserEntries[0].full_path;
                            loadBrowserDirectory(parentDir);
                        }
                    } else if (key == SDLK_ESCAPE || key == SDLK_F5) {
                        overlayState = OverlayState::None;
                    }
                    break;
                }
                case OverlayState::CreateModuleForm: {
                    constexpr int kNumFormFields = 6;
                    if (key == SDLK_UP) {
                        formFieldIndex = (formFieldIndex - 1 + kNumFormFields) % kNumFormFields;
                    } else if (key == SDLK_DOWN || key == SDLK_TAB) {
                        formFieldIndex = (formFieldIndex + 1) % kNumFormFields;
                    } else if (key == SDLK_BACKSPACE) {
                        if (formFieldIndex == 0 && !formTitle.empty()) {
                            formTitle.pop_back();
                        }
                    } else if (key == SDLK_LEFT) {
                        if (formFieldIndex == 1 && !formBaseOptions.empty()) {
                            int n = static_cast<int>(formBaseOptions.size());
                            formBaseIndex = (formBaseIndex - 1 + n) % n;
                        } else if (formFieldIndex == 2 && !formExtraWads.empty()) {
                            int n = static_cast<int>(formExtraWads.size());
                            formExtraCursorIndex = (formExtraCursorIndex - 1 + n) % n;
                        } else if (formFieldIndex == 3) {
                            formComplevelIndex = (formComplevelIndex - 1 + kNumComplevelOptions) % kNumComplevelOptions;
                            formCapabilityIndex = kComplevelOptions[formComplevelIndex].suggested_cap_idx;
                        } else if (formFieldIndex == 4) {
                            formCapabilityIndex = (formCapabilityIndex - 1 + kNumCapabilityOptions) % kNumCapabilityOptions;
                        }
                    } else if (key == SDLK_RIGHT) {
                        if (formFieldIndex == 1 && !formBaseOptions.empty()) {
                            int n = static_cast<int>(formBaseOptions.size());
                            formBaseIndex = (formBaseIndex + 1) % n;
                        } else if (formFieldIndex == 2 && !formExtraWads.empty()) {
                            int n = static_cast<int>(formExtraWads.size());
                            formExtraCursorIndex = (formExtraCursorIndex + 1) % n;
                        } else if (formFieldIndex == 3) {
                            formComplevelIndex = (formComplevelIndex + 1) % kNumComplevelOptions;
                            formCapabilityIndex = kComplevelOptions[formComplevelIndex].suggested_cap_idx;
                        } else if (formFieldIndex == 4) {
                            formCapabilityIndex = (formCapabilityIndex + 1) % kNumCapabilityOptions;
                        }
                    } else if (key == SDLK_SPACE) {
                        if (formFieldIndex == 2 && formExtraCursorIndex >= 0 && formExtraCursorIndex < static_cast<int>(formExtraSelected.size())) {
                            formExtraSelected[formExtraCursorIndex] = !formExtraSelected[formExtraCursorIndex];
                        } else if (formFieldIndex == 5) {
                            submitCreateModuleForm();
                        }
                    } else if (key == SDLK_RETURN) {
                        if (formFieldIndex == 2 && formExtraCursorIndex >= 0 && formExtraCursorIndex < static_cast<int>(formExtraSelected.size())) {
                            formExtraSelected[formExtraCursorIndex] = !formExtraSelected[formExtraCursorIndex];
                        } else if (formFieldIndex == 5) {
                            submitCreateModuleForm();
                        } else {
                            formFieldIndex = (formFieldIndex + 1) % kNumFormFields;
                        }
                    } else if (key == SDLK_F2) {
                        submitCreateModuleForm();
                    } else if (key == SDLK_ESCAPE) {
                        if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
                            SDL_StopTextInput();
                        }
                        overlayState = OverlayState::None;
                    }
                    break;
                }
                case OverlayState::QuitConfirm:
                    if (key == SDLK_y || key == SDLK_RETURN) {
                        quitSelected = true;
                        overlayState = OverlayState::None;
                    } else if (key == SDLK_n || key == SDLK_ESCAPE || key == SDLK_F10) {
                        overlayState = OverlayState::None;
                    }
                    break;
                default:
                    overlayState = OverlayState::None;
                    break;
            }
        }
        return;
    }

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_UP:
                selectedItem = (selectedItem - 1 + totalItems) % totalItems;
                statusMessage.clear();
                break;
            case SDLK_DOWN:
                selectedItem = (selectedItem + 1) % totalItems;
                statusMessage.clear();
                break;
            case SDLK_PAGEUP:
                if (currentPage > 0) {
                    selectedItem = std::max(0, selectedItem - ITEMS_PER_PAGE);
                } else {
                    int targetPage = totalPages - 1;
                    int slot = selectedItem % ITEMS_PER_PAGE;
                    int targetItem = targetPage * ITEMS_PER_PAGE + slot;
                    if (targetItem >= totalItems) targetItem = totalItems - 1;
                    selectedItem = targetItem;
                }
                statusMessage.clear();
                break;
            case SDLK_PAGEDOWN:
                if (currentPage < totalPages - 1) {
                    int targetItem = selectedItem + ITEMS_PER_PAGE;
                    if (targetItem >= totalItems) targetItem = totalItems - 1;
                    selectedItem = targetItem;
                } else {
                    selectedItem = selectedItem % ITEMS_PER_PAGE;
                }
                statusMessage.clear();
                break;
            case SDLK_LEFT:
                if (selectedItem >= 0 && selectedItem < totalItems) {
                    if (menuItems[selectedItem].select_prev_engine()) {
                        statusMessage.clear();
                    }
                }
                break;
            case SDLK_RIGHT:
                if (selectedItem >= 0 && selectedItem < totalItems) {
                    if (menuItems[selectedItem].select_next_engine()) {
                        statusMessage.clear();
                    }
                }
                break;
            case SDLK_RETURN:
                if (menuItems[selectedItem].module_name == "quit") {
                    quitSelected = true;
                } else {
                    launchSelected();
                }
                break;
            case SDLK_F1:
                overlayState = OverlayState::Help;
                statusMessage.clear();
                break;
            case SDLK_F2:
                saveSettings();
                break;
            case SDLK_F3:
                if (selectedItem >= 0 && selectedItem < totalItems && menuItems[selectedItem].module_name != "quit") {
                    overlayState = OverlayState::ModuleInfo;
                    statusMessage.clear();
                }
                break;
            case SDLK_F4:
                if (selectedItem >= 0 && selectedItem < totalItems && menuItems[selectedItem].module_name != "quit") {
                    modalCursorIndex = static_cast<int>(menuItems[selectedItem].selected_engine_index);
                    overlayState = OverlayState::EngineSelect;
                    statusMessage.clear();
                }
                break;
            case SDLK_F5:
                openAddWadDialog();
                break;
            case SDLK_F9:
                if (altSmallFont) {
                    toggleOsdFont();
                }
                break;
            case SDLK_F10:
            case SDLK_ESCAPE:
                overlayState = OverlayState::QuitConfirm;
                statusMessage.clear();
                break;
        }
    }
}

void Menu::saveSettings() {
    std::string savedPath;
    bool success = false;
    if (saveCallback) {
        success = saveCallback(menuItems, savedPath);
    } else if (moduleRegistry) {
        success = moduleRegistry->save_state(menuItems, savedPath);
    } else {
        success = doomlauncher::ModuleRegistry::save_state_from_items(menuItems, savedPath);
    }

    if (success) {
        std::string displayPath = savedPath;
        const char* home = std::getenv("HOME");
        if (home && displayPath.rfind(home, 0) == 0) {
            displayPath = "~" + displayPath.substr(std::strlen(home));
        }
        statusMessage = "Settings saved to " + displayPath;
    } else {
        statusMessage = "Failed to save settings to " + (!savedPath.empty() ? savedPath : "state.toml");
    }
}

void Menu::launchSelected() {
    if (selectedItem < 0 || selectedItem >= static_cast<int>(menuItems.size())) return;

    const auto& item = menuItems[selectedItem];
    if (item.status == doomlauncher::GameStatus::MissingAssets || !item.assets_found) {
        statusMessage = "Cannot launch: " + (!item.status_details.empty() ? item.status_details : "missing asset");
        std::cout << statusMessage << std::endl;
        return;
    }

    if (item.status == doomlauncher::GameStatus::NoCompatibleEngine) {
        statusMessage = "Cannot launch: " + (!item.status_details.empty() ? item.status_details : "no compatible engine");
        std::cout << statusMessage << std::endl;
        return;
    }

    std::string cmd = item.get_command();
    if (cmd.empty()) {
        cmd = item.cmd;
    }

    if (cmd.empty()) {
        statusMessage = "No CMD defined for " + item.title;
        std::cout << statusMessage << std::endl;
        return;
    }

    if (!item.save_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(item.save_dir, ec);
    }

    statusMessage = "Launching: " + cmd;
    std::cout << statusMessage << std::endl;

    int res = std::system(cmd.c_str());
    (void)res;
}

void Menu::update() {
}

void Menu::render() {
    if (menuItems.empty()) return;

    ++cacheTick;

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color red = { 255, 0, 0, 255 };
    SDL_Color yellow = { 255, 255, 0, 255 };
    SDL_Color gray = { 130, 130, 130, 255 };
    SDL_Color darkRed = { 180, 50, 50, 255 };
    SDL_Color green = { 100, 255, 100, 255 };

    int totalItems = static_cast<int>(menuItems.size());
    int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages < 1) totalPages = 1;
    int currentPage = selectedItem / ITEMS_PER_PAGE;
    int pageStartIndex = currentPage * ITEMS_PER_PAGE;
    int pageEndIndex = std::min(pageStartIndex + ITEMS_PER_PAGE, totalItems);

    TTF_Font* activeSmallFont = (useAlternateFont && altSmallFont) ? altSmallFont : smallFont;
    int smallFontHeight = activeSmallFont ? TTF_FontHeight(activeSmallFont) : 24;
    int lineSpacing = smallFontHeight + 4;
    int osdStartY = screenHeight - (5 * lineSpacing + 12);
    int osdX = 30;

    int fontHeight = font ? TTF_FontHeight(font) : 40;
    int totalMenuHeight = ITEMS_PER_PAGE * fontHeight;
    int pageIndicatorHeight = smallFontHeight + 8;
    int maxAvailableForMenu = osdStartY - 20 - pageIndicatorHeight - 10;
    int itemSpacing = fontHeight;
    if (totalMenuHeight > maxAvailableForMenu && maxAvailableForMenu > 0) {
        itemSpacing = maxAvailableForMenu / ITEMS_PER_PAGE;
    }
    int effectiveMenuHeight = (ITEMS_PER_PAGE - 1) * itemSpacing + fontHeight;
    int startY = (osdStartY - pageIndicatorHeight - 20 - effectiveMenuHeight) / 2 + 10;
    if (startY < 15) startY = 15;

    for (int i = pageStartIndex; i < pageEndIndex; ++i) {
        const auto& item = menuItems[i];
        bool isSelected = (i == selectedItem);
        bool isPlayable = (item.status == doomlauncher::GameStatus::Ready && item.assets_found);
        SDL_Color color;
        if (!isPlayable) {
            color = isSelected ? darkRed : gray;
        } else {
            color = isSelected ? red : white;
        }

        int slotIndex = i - pageStartIndex;
        int y = startY + slotIndex * itemSpacing;
        renderText(font, item.title, 0, y, color, true);
    }

    // Render page indicator under menu
    if (activeSmallFont) {
        std::string pageIndicator = "[ Page " + std::to_string(currentPage + 1) + " of " + std::to_string(totalPages) + " ]";
        int pageIndicatorY = startY + (ITEMS_PER_PAGE - 1) * itemSpacing + fontHeight + 8;
        if (pageIndicatorY + smallFontHeight >= osdStartY) {
            pageIndicatorY = osdStartY - smallFontHeight - 4;
        }
        renderText(activeSmallFont, pageIndicator, 0, pageIndicatorY, gray, true);
    }

    // Render OSD / HUD Block at bottom left
    if (activeSmallFont && selectedItem >= 0 && selectedItem < totalItems) {
        const auto& item = menuItems[selectedItem];

        std::string fontHint = altSmallFont ? "F9: Font | " : "";
        std::string line1;
        SDL_Color line1Color = yellow;

        std::string line2;
        SDL_Color line2Color = white;

        std::string line3;
        SDL_Color line3Color = gray;

        if (item.module_name == "quit") {
            line1 = "ACTION: Exit Doom Launcher";
            line1Color = yellow;
            line2 = "Press Enter or F10 to exit (or F5 to add WAD files)";
            line2Color = gray;
            line3 = "F1: Help | F5: Add WAD | " + fontHint + "Enter: Exit | F10: Fast Quit";
        } else if (item.status == doomlauncher::GameStatus::MissingAssets || (!item.assets_found && !item.missing_assets.empty())) {
            line1 = "STATUS: Cannot launch - missing asset";
            if (!item.missing_assets.empty()) {
                line1 += " (" + item.missing_assets[0] + ")";
            } else if (!item.status_details.empty()) {
                line1 += " (" + item.status_details + ")";
            }
            line1Color = red;
            line2 = "CMD: [Disabled - missing assets]";
            line2Color = gray;
            line3 = "F1: Help | F2: Save | F3: Info | F4: Engine | F5: Add WAD | " + fontHint + "F10: Quit";
        } else if (item.status == doomlauncher::GameStatus::NoCompatibleEngine) {
            line1 = "STATUS: Cannot launch - no compatible engine installed";
            if (!item.status_details.empty()) {
                line1 += " (" + item.status_details + ")";
            }
            line1Color = red;
            line2 = "CMD: [Disabled - no compatible engine]";
            line2Color = gray;
            line3 = "F1: Help | F2: Save | F3: Info | F4: Engine | F5: Add WAD | " + fontHint + "F10: Quit";
        } else {
            const auto* eng = item.get_selected_engine();
            std::string engTitle = eng ? eng->title : "Unknown";
            line1 = "ENGINE: " + engTitle;
            if (item.compatible_engines.size() > 1) {
                line1 += " [" + std::to_string(item.compatible_engines.size()) + " compatible | F4: Change]";
            } else if (item.compatible_engines.size() == 1) {
                line1 += " [1 compatible]";
            }
            line1Color = yellow;

            const std::string& cmd = !item.cmd.empty() ? item.cmd : item.get_command();
            line2 = "CMD: " + cmd;
            line2Color = white;

            line3 = "F1: Help | F2: Save | F3: Info | F4: Engine | F5: Add WAD | " + fontHint + "F10: Quit";
        }

        if (!statusMessage.empty()) {
            line3 = statusMessage;
            line3Color = (statusMessage.find("Saved") != std::string::npos ||
                          statusMessage.find("saved") != std::string::npos ||
                          statusMessage.find("Activated") != std::string::npos ||
                          statusMessage.find("Created") != std::string::npos) ? green : yellow;
        }

        renderText(activeSmallFont, line1, osdX, osdStartY, line1Color, false);
        renderText(activeSmallFont, line2, osdX, osdStartY + lineSpacing, line2Color, false);
        renderText(activeSmallFont, line3, osdX, osdStartY + 2 * lineSpacing, line3Color, false);
    }

    if (overlayState != OverlayState::None && activeSmallFont) {
        switch (overlayState) {
            case OverlayState::Help:
                renderHelpOverlay(activeSmallFont);
                break;
            case OverlayState::ModuleInfo:
                renderModuleInfoOverlay(activeSmallFont);
                break;
            case OverlayState::EngineSelect:
                renderEngineSelectOverlay(activeSmallFont);
                break;
            case OverlayState::AddWadDialog:
                renderAddWadOverlay(activeSmallFont);
                break;
            case OverlayState::CreateModuleForm:
                renderCreateModuleOverlay(activeSmallFont);
                break;
            case OverlayState::QuitConfirm:
                renderQuitConfirmOverlay(activeSmallFont);
                break;
            default:
                break;
        }
    }

    if (textCache.size() > MAX_TEXT_CACHE_SIZE) {
        pruneTextCache();
    }
}

bool Menu::isQuitSelected() const {
    return quitSelected;
}

const Menu::CachedText* Menu::getOrCreateTextTexture(TTF_Font* f, const std::string& text, SDL_Color color) {
    if (!renderer || !f || text.empty()) return nullptr;

    Uint32 rgba = (static_cast<Uint32>(color.r) << 24) |
                  (static_cast<Uint32>(color.g) << 16) |
                  (static_cast<Uint32>(color.b) << 8) |
                  static_cast<Uint32>(color.a);
    TextCacheKey key{ f, text, rgba };

    auto it = textCache.find(key);
    if (it != textCache.end()) {
        it->second.lastUsedTick = cacheTick;
        return &it->second;
    }

    SDL_Surface* surface = TTF_RenderText_Solid(f, text.c_str(), color);
    if (surface == nullptr) {
        std::cerr << "Unable to render text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    int w = surface->w;
    int h = surface->h;
    SDL_FreeSurface(surface);

    if (texture == nullptr) {
        std::cerr << "Unable to create texture from rendered text! SDL Error: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    CachedText entry;
    entry.texture = texture;
    entry.width = w;
    entry.height = h;
    entry.lastUsedTick = cacheTick;

    auto [ins_it, _] = textCache.emplace(std::move(key), entry);
    return &ins_it->second;
}

void Menu::getTextDimensions(TTF_Font* f, const std::string& text, SDL_Color color, int* outW, int* outH) {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (!f || text.empty()) return;

    if (const CachedText* cached = getOrCreateTextTexture(f, text, color)) {
        if (outW) *outW = cached->width;
        if (outH) *outH = cached->height;
    } else {
        TTF_SizeText(f, text.c_str(), outW, outH);
    }
}

void Menu::pruneTextCache() {
    for (auto it = textCache.begin(); it != textCache.end(); ) {
        if (it->second.lastUsedTick < cacheTick) {
            if (it->second.texture) {
                SDL_DestroyTexture(it->second.texture);
            }
            it = textCache.erase(it);
        } else {
            ++it;
        }
    }
}

void Menu::clearTextCache() {
    for (auto& [_, entry] : textCache) {
        if (entry.texture) {
            SDL_DestroyTexture(entry.texture);
            entry.texture = nullptr;
        }
    }
    textCache.clear();
}

void Menu::renderText(TTF_Font* f, const std::string& text, int x, int y, SDL_Color color, bool centered, int maxAllowedWidth) {
    const CachedText* cached = getOrCreateTextTexture(f, text, color);
    if (!cached || !cached->texture) return;

    int textWidth = cached->width;
    int textHeight = cached->height;
    int finalX = centered ? (screenWidth - textWidth) / 2 : x;
    int maxW = (maxAllowedWidth > 0) ? maxAllowedWidth : (screenWidth - finalX - 10);
    if (maxW > 0 && textWidth > maxW) {
        SDL_Rect srcRect = { 0, 0, maxW, textHeight };
        SDL_Rect renderQuad = { finalX, y, maxW, textHeight };
        SDL_RenderCopy(renderer, cached->texture, &srcRect, &renderQuad);
    } else {
        SDL_Rect renderQuad = { finalX, y, textWidth, textHeight };
        SDL_RenderCopy(renderer, cached->texture, nullptr, &renderQuad);
    }
}

void Menu::renderDialogBox(int x, int y, int w, int h, const std::string& title, TTF_Font* osdFont) {
    if (!renderer || !osdFont) return;

    // Dim background behind dialog for focus
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_Rect fullScreen = { 0, 0, screenWidth, screenHeight };
    SDL_RenderFillRect(renderer, &fullScreen);

    // Dialog backdrop (dark charcoal)
    SDL_SetRenderDrawColor(renderer, 18, 18, 24, 242);
    SDL_Rect dialogRect = { x, y, w, h };
    SDL_RenderFillRect(renderer, &dialogRect);

    // Outer border (maroon)
    SDL_SetRenderDrawColor(renderer, 150, 30, 30, 255);
    SDL_RenderDrawRect(renderer, &dialogRect);

    // Inner border (crimson)
    SDL_SetRenderDrawColor(renderer, 200, 50, 50, 255);
    SDL_Rect innerRect = { x + 2, y + 2, w - 4, h - 4 };
    SDL_RenderDrawRect(renderer, &innerRect);

    // Header divider line and title if provided
    if (!title.empty()) {
        SDL_Color yellow = { 255, 255, 0, 255 };
        int titleW = 0, titleH = 0;
        getTextDimensions(osdFont, title, yellow, &titleW, &titleH);
        int titleX = x + (w - titleW) / 2;
        int titleY = y + 10;
        renderText(osdFont, title, titleX, titleY, yellow, false, w - 20);

        int sepY = y + (titleH > 0 ? titleH : 24) + 14;
        SDL_SetRenderDrawColor(renderer, 150, 30, 30, 255);
        SDL_RenderDrawLine(renderer, x + 4, sepY, x + w - 4, sepY);
        SDL_SetRenderDrawColor(renderer, 200, 50, 50, 255);
        SDL_RenderDrawLine(renderer, x + 4, sepY + 1, x + w - 4, sepY + 1);
    }
}

void Menu::renderHelpOverlay(TTF_Font* osdFont) {
    if (!renderer || !osdFont) return;

    int w = 680;
    int h = 470;
    int x = (screenWidth - w) / 2;
    int y = (screenHeight - h) / 2;

    renderDialogBox(x, y, w, h, "=== DOOM LAUNCHER CONTROLS ===", osdFont);

    struct HelpRow {
        const char* key;
        const char* desc;
    };

    static constexpr HelpRow kHelpRows[] = {
        { "Up / Down",           "Navigate games / options" },
        { "PageUp / PageDown",   "Switch menu page" },
        { "Left / Right",        "Quick engine toggle" },
        { "Enter",               "Launch selected game" },
        { "F1",                  "Toggle this help screen" },
        { "F2",                  "Save engine preferences" },
        { "F3",                  "Selected module information" },
        { "F4",                  "Engine selection menu" },
        { "F5",                  "Add WAD file / Create module" },
        { "F9",                  "Toggle OSD font (Mono / Doom HUD)" },
        { "F10 / Esc",           "Fast quit launcher / Back" }
    };
    constexpr size_t kNumHelpRows = sizeof(kHelpRows) / sizeof(kHelpRows[0]);

    SDL_Color colKey = { 255, 255, 0, 255 };
    SDL_Color colDesc = { 220, 220, 220, 255 };
    SDL_Color colFoot = { 150, 150, 150, 255 };

    int startY = y + 48;
    int rowSpacing = 31;

    for (size_t i = 0; i < kNumHelpRows; ++i) {
        int curY = startY + static_cast<int>(i) * rowSpacing;
        renderText(osdFont, kHelpRows[i].key, x + 35, curY, colKey, false, 190);
        renderText(osdFont, kHelpRows[i].desc, x + 235, curY, colDesc, false, w - 255);
    }

    std::string footer = "[ Press F1, Esc, or Enter to Close ]";
    int footW = 0;
    getTextDimensions(osdFont, footer, colFoot, &footW, nullptr);
    renderText(osdFont, footer, x + (w - footW) / 2, y + h - 35, colFoot, false);
}

void Menu::renderModuleInfoOverlay(TTF_Font* osdFont) {
    if (!renderer || !osdFont) return;
    if (selectedItem < 0 || selectedItem >= static_cast<int>(menuItems.size())) return;

    const auto& item = menuItems[selectedItem];

    int w = 740;
    int h = 480;
    int x = (screenWidth - w) / 2;
    int y = (screenHeight - h) / 2;

    std::string headerTitle = "=== MODULE: " + item.title + " ===";
    if (headerTitle.length() > 45) {
        headerTitle = "=== " + item.title.substr(0, 38) + "... ===";
    }
    renderDialogBox(x, y, w, h, headerTitle, osdFont);

    SDL_Color colLabel = { 255, 255, 0, 255 };
    SDL_Color colVal = { 220, 220, 220, 255 };
    SDL_Color green = { 100, 255, 100, 255 };
    SDL_Color red = { 255, 80, 80, 255 };
    SDL_Color colFoot = { 150, 150, 150, 255 };

    std::string statusStr = "Ready / Playable";
    SDL_Color statusColor = green;
    if (item.status == doomlauncher::GameStatus::MissingAssets || (!item.assets_found && !item.missing_assets.empty())) {
        statusStr = "Missing Asset";
        if (!item.missing_assets.empty()) {
            statusStr += " (" + item.missing_assets[0] + ")";
        } else if (!item.status_details.empty()) {
            statusStr += " (" + item.status_details + ")";
        }
        statusColor = red;
    } else if (item.status == doomlauncher::GameStatus::NoCompatibleEngine) {
        statusStr = "No Compatible Engine Installed";
        if (!item.status_details.empty()) {
            statusStr += " (" + item.status_details + ")";
        }
        statusColor = red;
    }

    std::string iwadStr;
    if (!item.resolved_iwad_path.empty()) {
        iwadStr = item.resolved_iwad_path;
    } else if (item.vars.find("IWAD") != item.vars.end()) {
        iwadStr = item.vars.at("IWAD").to_string();
    } else {
        iwadStr = "(None)";
    }

    std::string pwadsStr;
    if (!item.resolved_pwad_paths.empty()) {
        for (size_t i = 0; i < item.resolved_pwad_paths.size(); ++i) {
            if (i > 0) pwadsStr += ", ";
            pwadsStr += item.resolved_pwad_paths[i];
        }
    } else if (item.vars.find("PWADS") != item.vars.end()) {
        pwadsStr = item.vars.at("PWADS").to_string();
    } else {
        pwadsStr = "(None)";
    }

    std::string complevelStr = "Default / Auto";
    if (item.vars.find("COMPAT_LEVEL") != item.vars.end()) {
        complevelStr = item.vars.at("COMPAT_LEVEL").to_string();
    }
    if (complevelStr == "2") complevelStr = "2 (Doom v1.9)";
    else if (complevelStr == "3") complevelStr = "3 (Ultimate Doom)";
    else if (complevelStr == "4") complevelStr = "4 (Final Doom)";
    else if (complevelStr == "9") complevelStr = "9 (Boom)";
    else if (complevelStr == "11") complevelStr = "11 (MBF)";
    else if (complevelStr == "21") complevelStr = "21 (MBF21)";

    std::string reqCaps = "vanilla";
    if (item.vars.find("requires_capability") != item.vars.end()) {
        reqCaps = item.vars.at("requires_capability").to_string();
    } else if (complevelStr.find("Boom") != std::string::npos) {
        reqCaps = "boom";
    } else if (complevelStr.find("MBF21") != std::string::npos) {
        reqCaps = "mbf21";
    }

    std::string saveDirStr = !item.save_dir.empty() ? item.save_dir : "Default";
    if (saveDirStr == "Default" && item.vars.find("SAVEDIR") != item.vars.end()) {
        saveDirStr = item.vars.at("SAVEDIR").to_string();
    }

    std::string installedEnginesStr;
    if (item.compatible_engines.empty()) {
        installedEnginesStr = "None installed";
    } else {
        for (size_t i = 0; i < item.compatible_engines.size(); ++i) {
            if (i > 0) installedEnginesStr += ", ";
            installedEnginesStr += item.compatible_engines[i].title;
        }
    }

    std::string activeEngineStr;
    const auto* activeEng = item.get_selected_engine();
    if (activeEng) {
        std::string bin = !activeEng->installed_binary_path.empty() ? activeEng->installed_binary_path : activeEng->binary;
        activeEngineStr = activeEng->title + " (" + bin + ")";
    } else {
        activeEngineStr = "None";
    }

    std::string cmdStr = !item.cmd.empty() ? item.cmd : item.get_command();
    if (cmdStr.empty()) cmdStr = "(None)";

    struct InfoRow {
        const char* label;
        std::string val;
        SDL_Color color;
    };

    InfoRow infoRows[] = {
        { "Module ID:",         item.module_name,     colVal },
        { "Status:",            statusStr,            statusColor },
        { "IWAD Asset:",        iwadStr,              colVal },
        { "PWAD Addons:",       pwadsStr,             colVal },
        { "Complevel:",         complevelStr,         colVal },
        { "Capabilities:",      reqCaps,              colVal },
        { "Save Directory:",    saveDirStr,           colVal },
        { "Installed Engines:", installedEnginesStr,  colVal },
        { "Active Engine:",     activeEngineStr,      colVal },
        { "Launch Command:",    cmdStr,               colVal }
    };
    constexpr size_t kNumInfoRows = sizeof(infoRows) / sizeof(infoRows[0]);

    int startY = y + 46;
    int rowSpacing = 37;
    int labelX = x + 25;
    int valX = x + 215;
    int valMaxW = w - 240;

    for (size_t i = 0; i < kNumInfoRows; ++i) {
        int curY = startY + static_cast<int>(i) * rowSpacing;
        renderText(osdFont, infoRows[i].label, labelX, curY, colLabel, false, 185);
        renderText(osdFont, infoRows[i].val, valX, curY, infoRows[i].color, false, valMaxW);
    }

    std::string footer = "[ Press F3, Esc, or Enter to Close ]";
    int footW = 0;
    getTextDimensions(osdFont, footer, colFoot, &footW, nullptr);
    renderText(osdFont, footer, x + (w - footW) / 2, y + h - 32, colFoot, false);
}

void Menu::renderEngineSelectOverlay(TTF_Font* osdFont) {
    if (!renderer || !osdFont) return;
    if (selectedItem < 0 || selectedItem >= static_cast<int>(menuItems.size())) return;

    const auto& item = menuItems[selectedItem];
    int count = static_cast<int>(item.compatible_engines.size());

    int w = 680;
    int rowH = 38;
    int contentH = std::max(1, count) * rowH;
    int h = std::max(200, 100 + contentH + 35);
    int x = (screenWidth - w) / 2;
    int y = (screenHeight - h) / 2;

    std::string headerTitle = "Select Engine: " + item.title;
    if (headerTitle.length() > 42) {
        headerTitle = "Select Engine: " + item.title.substr(0, 35) + "...";
    }
    renderDialogBox(x, y, w, h, headerTitle, osdFont);

    SDL_Color yellow = { 255, 255, 0, 255 };
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color gray = { 150, 150, 150, 255 };
    SDL_Color red = { 255, 80, 80, 255 };
    SDL_Color colFoot = { 150, 150, 150, 255 };

    if (count == 0) {
        renderText(osdFont, "No compatible engines installed for this game.", x + 35, y + 65, red, false, w - 70);
    } else {
        int startY = y + 50;
        for (int i = 0; i < count; ++i) {
            int rowY = startY + i * rowH;
            bool isCursor = (i == modalCursorIndex);
            bool isCurrent = (i == static_cast<int>(item.selected_engine_index));

            if (isCursor) {
                SDL_Rect selRect = { x + 16, rowY - 2, w - 32, rowH - 4 };
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 90, 25, 25, 220);
                SDL_RenderFillRect(renderer, &selRect);
                SDL_SetRenderDrawColor(renderer, 220, 60, 60, 255);
                SDL_RenderDrawRect(renderer, &selRect);
            }

            const auto& eng = item.compatible_engines[i];
            std::string path = !eng.installed_binary_path.empty() ? eng.installed_binary_path : eng.binary;
            std::string prefix = isCursor ? "> " : "  ";
            std::string radio = isCurrent ? "(*) " : "( ) ";
            std::string rowText = prefix + radio + eng.title + " (" + path + ")";

            SDL_Color textColor = isCursor ? yellow : (isCurrent ? white : gray);
            renderText(osdFont, rowText, x + 25, rowY + 3, textColor, false, w - 50);
        }
    }

    std::string footer = "[ Up/Down: Select | Enter: Confirm | Esc: Cancel ]";
    int footW = 0;
    getTextDimensions(osdFont, footer, colFoot, &footW, nullptr);
    renderText(osdFont, footer, x + (w - footW) / 2, y + h - 32, colFoot, false);
}

void Menu::renderAddWadOverlay(TTF_Font* osdFont) {
    if (!renderer || !osdFont) return;

    int w = 740;
    int h = 480;
    int x = (screenWidth - w) / 2;
    int y = (screenHeight - h) / 2;

    renderDialogBox(x, y, w, h, "=== ADD WAD FILES ===", osdFont);

    SDL_Color yellow = { 255, 255, 0, 255 };
    SDL_Color white = { 220, 220, 220, 255 };
    SDL_Color cyan = { 120, 210, 255, 255 };
    SDL_Color green = { 100, 255, 100, 255 };
    SDL_Color gray = { 150, 150, 150, 255 };

    std::string dirLabel = "Current Dir: " + browserCurrentDir;
    renderText(osdFont, dirLabel, x + 24, y + 46, yellow, false, w - 48);

    int entryCount = static_cast<int>(browserEntries.size());
    int page = (entryCount > 0) ? (browserCursorIndex / BROWSER_ITEMS_PER_PAGE) : 0;
    int startIdx = page * BROWSER_ITEMS_PER_PAGE;
    int endIdx = std::min(startIdx + BROWSER_ITEMS_PER_PAGE, entryCount);

    int listStartY = y + 82;
    int rowH = 36;

    for (int i = startIdx; i < endIdx; ++i) {
        const auto& entry = browserEntries[i];
        int rowY = listStartY + (i - startIdx) * rowH;
        bool isCursor = (i == browserCursorIndex);

        if (isCursor) {
            SDL_Rect selRect = { x + 16, rowY - 2, w - 32, rowH - 4 };
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 90, 25, 25, 220);
            SDL_RenderFillRect(renderer, &selRect);
            SDL_SetRenderDrawColor(renderer, 220, 60, 60, 255);
            SDL_RenderDrawRect(renderer, &selRect);
        }

        std::string prefix = isCursor ? "> " : "  ";
        std::string rowText;
        SDL_Color rowColor = white;

        if (entry.is_parent) {
            rowText = prefix + entry.name;
            rowColor = isCursor ? yellow : cyan;
        } else if (entry.is_dir) {
            rowText = prefix + "[DIR] " + entry.name;
            rowColor = isCursor ? yellow : cyan;
        } else {
            std::string mark = entry.selected ? "[x] " : "[ ] ";
            rowText = prefix + mark + entry.name;
            rowColor = entry.selected ? green : (isCursor ? yellow : white);
        }

        renderText(osdFont, rowText, x + 24, rowY + 3, rowColor, false, w - 160);

        if (!entry.is_dir) {
            std::string sizeStr = "(" + format_file_size(entry.file_size) + ")";
            int sizeW = 0;
            getTextDimensions(osdFont, sizeStr, gray, &sizeW, nullptr);
            renderText(osdFont, sizeStr, x + w - sizeW - 28, rowY + 3, gray, false);
        }
    }

    int selectedCount = 0;
    for (const auto& e : browserEntries) {
        if (!e.is_dir && e.selected) ++selectedCount;
    }

    std::string selSummary = "Selected: " + std::to_string(selectedCount) + " file(s)";
    renderText(osdFont, selSummary, x + 24, y + h - 85, selectedCount > 0 ? green : gray, false);

    std::string foot1 = "[ Up/Down: Navigate | Space: Toggle Select | Enter: Open Dir / Add ]";
    std::string foot2 = "[ A: Confirm Selected | Backspace: Up Dir | Esc: Cancel ]";
    int f1W = 0, f2W = 0;
    getTextDimensions(osdFont, foot1, gray, &f1W, nullptr);
    getTextDimensions(osdFont, foot2, gray, &f2W, nullptr);
    renderText(osdFont, foot1, x + (w - f1W) / 2, y + h - 55, gray, false);
    renderText(osdFont, foot2, x + (w - f2W) / 2, y + h - 28, gray, false);
}

void Menu::renderCreateModuleOverlay(TTF_Font* osdFont) {
    if (!renderer || !osdFont) return;

    int w = 740;
    int h = 480;
    int x = (screenWidth - w) / 2;
    int y = (screenHeight - h) / 2;

    std::string wadName = !pendingFormWads.empty() ? pendingFormWads[0].filename : "WAD";
    std::string headerTitle = "=== CREATE MODULE FOR " + wadName + " ===";
    if (headerTitle.length() > 45) {
        headerTitle = "=== CREATE MODULE ===";
    }
    renderDialogBox(x, y, w, h, headerTitle, osdFont);

    SDL_Color yellow = { 255, 255, 0, 255 };
    SDL_Color white = { 220, 220, 220, 255 };
    SDL_Color green = { 100, 255, 100, 255 };
    SDL_Color gray = { 150, 150, 150, 255 };

    std::string wadSummary;
    for (size_t i = 0; i < pendingFormWads.size(); ++i) {
        if (i > 0) wadSummary += ", ";
        wadSummary += pendingFormWads[i].filename;
        if (!pendingFormWads[i].sha1.empty() && pendingFormWads.size() == 1) {
            wadSummary += " (SHA1: " + pendingFormWads[i].sha1.substr(0, 10) + "...)";
        }
    }
    renderText(osdFont, "WAD File(s):      " + wadSummary, x + 28, y + 52, gray, false, w - 56);

    int startY = y + 95;
    int rowH = 44;
    int labelX = x + 28;
    int valX = x + 235;
    int valMaxW = w - 260;

    auto drawFieldHighlight = [&](int idx, int rowY) {
        if (formFieldIndex == idx) {
            SDL_Rect selRect = { x + 16, rowY - 4, w - 32, rowH - 6 };
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 90, 25, 25, 220);
            SDL_RenderFillRect(renderer, &selRect);
            SDL_SetRenderDrawColor(renderer, 220, 60, 60, 255);
            SDL_RenderDrawRect(renderer, &selRect);
        }
    };

    // Field 0: Title
    {
        int rowY = startY + 0 * rowH;
        drawFieldHighlight(0, rowY);
        std::string prefix = (formFieldIndex == 0) ? "> " : "  ";
        std::string titleVal = formTitle + (formFieldIndex == 0 ? "_" : "");
        renderText(osdFont, prefix + "Title:", labelX, rowY, yellow, false, 200);
        renderText(osdFont, titleVal, valX, rowY, formFieldIndex == 0 ? yellow : white, false, valMaxW);
    }

    // Field 1: Base WAD (IWAD)
    {
        int rowY = startY + 1 * rowH;
        drawFieldHighlight(1, rowY);
        std::string prefix = (formFieldIndex == 1) ? "> " : "  ";
        std::string baseVal = (!formBaseOptions.empty() && formBaseIndex >= 0 && formBaseIndex < static_cast<int>(formBaseOptions.size()))
            ? ("< " + formBaseOptions[formBaseIndex].label + " >")
            : "< None >";
        renderText(osdFont, prefix + "Base WAD:", labelX, rowY, yellow, false, 200);
        renderText(osdFont, baseVal, valX, rowY, formFieldIndex == 1 ? yellow : white, false, valMaxW);
    }

    // Field 2: Extra Dependencies
    {
        int rowY = startY + 2 * rowH;
        drawFieldHighlight(2, rowY);
        std::string prefix = (formFieldIndex == 2) ? "> " : "  ";
        std::string extraVal;
        if (formExtraWads.empty()) {
            extraVal = "[ None available ]";
        } else {
            int idx = std::clamp(formExtraCursorIndex, 0, static_cast<int>(formExtraWads.size()) - 1);
            bool isSel = formExtraSelected[idx];
            extraVal = std::string("< ") + (isSel ? "[x] " : "[ ] ") + formExtraWads[idx].filename + " >";
        }
        renderText(osdFont, prefix + "Extra WADs:", labelX, rowY, yellow, false, 200);
        renderText(osdFont, extraVal, valX, rowY, formFieldIndex == 2 ? yellow : white, false, valMaxW);
    }

    // Field 3: Complevel
    {
        int rowY = startY + 3 * rowH;
        drawFieldHighlight(3, rowY);
        std::string prefix = (formFieldIndex == 3) ? "> " : "  ";
        int idx = std::clamp(formComplevelIndex, 0, kNumComplevelOptions - 1);
        std::string clVal = std::string("< ") + kComplevelOptions[idx].label + " >";
        renderText(osdFont, prefix + "Complevel:", labelX, rowY, yellow, false, 200);
        renderText(osdFont, clVal, valX, rowY, formFieldIndex == 3 ? yellow : white, false, valMaxW);
    }

    // Field 4: Engine Capability
    {
        int rowY = startY + 4 * rowH;
        drawFieldHighlight(4, rowY);
        std::string prefix = (formFieldIndex == 4) ? "> " : "  ";
        int idx = std::clamp(formCapabilityIndex, 0, kNumCapabilityOptions - 1);
        std::string capVal = std::string("< ") + kCapabilityOptions[idx] + " >";
        renderText(osdFont, prefix + "Capability:", labelX, rowY, yellow, false, 200);
        renderText(osdFont, capVal, valX, rowY, formFieldIndex == 4 ? yellow : white, false, valMaxW);
    }

    // Field 5: Save Button
    {
        int rowY = startY + 5 * rowH + 8;
        drawFieldHighlight(5, rowY);
        std::string btnText = formFieldIndex == 5 ? "> [ SAVE & ACTIVATE MODULE ] <" : "  [ SAVE & ACTIVATE MODULE ]  ";
        SDL_Color btnColor = formFieldIndex == 5 ? green : yellow;
        int btnW = 0;
        getTextDimensions(osdFont, btnText, btnColor, &btnW, nullptr);
        renderText(osdFont, btnText, x + (w - btnW) / 2, rowY, btnColor, false);
    }

    std::string foot1 = "[ Up/Down: Field | Left/Right: Cycle Option | Type: Edit Title ]";
    std::string foot2 = "[ F2 or Enter on Save: Create Module | Esc: Cancel ]";
    int f1W = 0, f2W = 0;
    getTextDimensions(osdFont, foot1, gray, &f1W, nullptr);
    getTextDimensions(osdFont, foot2, gray, &f2W, nullptr);
    renderText(osdFont, foot1, x + (w - f1W) / 2, y + h - 55, gray, false);
    renderText(osdFont, foot2, x + (w - f2W) / 2, y + h - 28, gray, false);
}

void Menu::renderQuitConfirmOverlay(TTF_Font* osdFont) {
    if (!renderer || !osdFont) return;

    int w = 560;
    int h = 160;
    int x = (screenWidth - w) / 2;
    int y = (screenHeight - h) / 2;

    renderDialogBox(x, y, w, h, "=== QUIT DOOM LAUNCHER ===", osdFont);

    SDL_Color yellow = { 255, 255, 0, 255 };
    SDL_Color white = { 220, 220, 220, 255 };

    std::string prompt1 = "Quit Doom Launcher?";
    int p1W = 0;
    getTextDimensions(osdFont, prompt1, yellow, &p1W, nullptr);
    renderText(osdFont, prompt1, x + (w - p1W) / 2, y + 58, yellow, false);

    std::string prompt2 = "Press Y or Enter to Exit, Esc or N to Cancel";
    int p2W = 0;
    getTextDimensions(osdFont, prompt2, white, &p2W, nullptr);
    renderText(osdFont, prompt2, x + (w - p2W) / 2, y + 102, white, false);
}
