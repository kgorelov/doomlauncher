#include "menu.h"
#include <cstdlib>
#include <iostream>

Menu::Menu(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* smallFont, int screenWidth, int screenHeight, TTF_Font* altSmallFont)
    : renderer(renderer), font(font), smallFont(smallFont), altSmallFont(altSmallFont), useAlternateFont(false),
      screenWidth(screenWidth), screenHeight(screenHeight), selectedItem(0), quitSelected(false) {
}

Menu::~Menu() {
}

void Menu::toggleOsdFont() {
    useAlternateFont = !useAlternateFont;
    statusMessage = useAlternateFont ? "[ OSD Font: Classic Doom HUD (STCFN) - Press F9 to toggle ]"
                                     : "[ OSD Font: Clean Monospace (Hack) - Press F9 to toggle ]";
}

void Menu::setMenuItems(const std::vector<doomlauncher::ResolvedMenuItem>& items) {
    menuItems = items;

    doomlauncher::ResolvedMenuItem quit_item;
    quit_item.title = "Quit";
    quit_item.module_name = "quit";
    menuItems.push_back(quit_item);

    selectedItem = 0;
}

void Menu::handleEvent(const SDL_Event& event) {
    if (menuItems.empty()) return;

    int totalItems = static_cast<int>(menuItems.size());
    int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages < 1) totalPages = 1;
    int currentPage = selectedItem / ITEMS_PER_PAGE;

    if (overlayState != OverlayState::None) {
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

    statusMessage = "Launching: " + cmd;
    std::cout << statusMessage << std::endl;

    int res = std::system(cmd.c_str());
    (void)res;
}

void Menu::update() {
}

void Menu::render() {
    if (menuItems.empty()) return;

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
        std::string displayText = item.title;
        renderText(font, displayText, 0, y, color, true);
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
            line2 = "Press Enter or F10 to exit";
            line2Color = gray;
            line3 = "F1: Help | " + fontHint + "Enter: Exit | F10: Fast Quit";
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
            line3 = "F1: Help | F2: Save | F3: Info | F4: Engine | " + fontHint + "F10: Quit";
        } else if (item.status == doomlauncher::GameStatus::NoCompatibleEngine) {
            line1 = "STATUS: Cannot launch - no compatible engine installed";
            if (!item.status_details.empty()) {
                line1 += " (" + item.status_details + ")";
            }
            line1Color = red;
            line2 = "CMD: [Disabled - no compatible engine]";
            line2Color = gray;
            line3 = "F1: Help | F2: Save | F3: Info | F4: Engine | " + fontHint + "F10: Quit";
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

            std::string cmd = item.get_command();
            if (cmd.empty()) cmd = item.cmd;
            line2 = "CMD: " + cmd;
            line2Color = white;

            line3 = "F1: Help | F2: Save | F3: Info | F4: Engine | " + fontHint + "F10: Quit";
        }

        if (!statusMessage.empty()) {
            line3 = statusMessage;
            line3Color = (statusMessage.find("Saved") != std::string::npos || statusMessage.find("saved") != std::string::npos) ? green : yellow;
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
            case OverlayState::QuitConfirm:
                renderQuitConfirmOverlay(activeSmallFont);
                break;
            default:
                break;
        }
    }
}

bool Menu::isQuitSelected() const {
    return quitSelected;
}

void Menu::renderText(TTF_Font* f, const std::string& text, int x, int y, SDL_Color color, bool centered, int maxAllowedWidth) {
    if (!renderer || !f || text.empty()) return;

    SDL_Surface* surface = TTF_RenderText_Solid(f, text.c_str(), color);
    if (surface == nullptr) {
        std::cerr << "Unable to render text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == nullptr) {
        std::cerr << "Unable to create texture from rendered text! SDL Error: " << SDL_GetError() << std::endl;
    } else {
        int textWidth = surface->w;
        int finalX = centered ? (screenWidth - textWidth) / 2 : x;
        int maxW = (maxAllowedWidth > 0) ? maxAllowedWidth : (screenWidth - finalX - 10);
        if (maxW > 0 && surface->w > maxW) {
            SDL_Rect srcRect = { 0, 0, maxW, surface->h };
            SDL_Rect renderQuad = { finalX, y, maxW, surface->h };
            SDL_RenderCopy(renderer, texture, &srcRect, &renderQuad);
        } else {
            SDL_Rect renderQuad = { finalX, y, surface->w, surface->h };
            SDL_RenderCopy(renderer, texture, nullptr, &renderQuad);
        }
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
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
        int titleW = 0, titleH = 0;
        TTF_SizeText(osdFont, title.c_str(), &titleW, &titleH);
        int titleX = x + (w - titleW) / 2;
        int titleY = y + 10;
        SDL_Color yellow = { 255, 255, 0, 255 };
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
    int h = 440;
    int x = (screenWidth - w) / 2;
    int y = (screenHeight - h) / 2;

    renderDialogBox(x, y, w, h, "=== DOOM LAUNCHER CONTROLS ===", osdFont);

    struct HelpRow {
        std::string key;
        std::string desc;
    };

    std::vector<HelpRow> rows = {
        { "Up / Down",           "Navigate games / options" },
        { "PageUp / PageDown",   "Switch menu page" },
        { "Left / Right",        "Quick engine toggle" },
        { "Enter",               "Launch selected game" },
        { "F1",                  "Toggle this help screen" },
        { "F2",                  "Save engine preferences" },
        { "F3",                  "Selected module information" },
        { "F4",                  "Engine selection menu" },
        { "F9",                  "Toggle OSD font (Mono / Doom HUD)" },
        { "F10 / Esc",           "Fast quit launcher / Back" }
    };

    SDL_Color colKey = { 255, 255, 0, 255 };
    SDL_Color colDesc = { 220, 220, 220, 255 };
    SDL_Color colFoot = { 150, 150, 150, 255 };

    int startY = y + 50;
    int rowSpacing = 32;

    for (size_t i = 0; i < rows.size(); ++i) {
        int curY = startY + static_cast<int>(i) * rowSpacing;
        renderText(osdFont, rows[i].key, x + 35, curY, colKey, false, 190);
        renderText(osdFont, rows[i].desc, x + 235, curY, colDesc, false, w - 255);
    }

    std::string footer = "[ Press F1, Esc, or Enter to Close ]";
    int footW = 0;
    TTF_SizeText(osdFont, footer.c_str(), &footW, nullptr);
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
    } else if (item.vars.find("iwad") != item.vars.end()) {
        iwadStr = item.vars.at("iwad").to_string();
    } else {
        iwadStr = "(None)";
    }

    std::string pwadsStr;
    if (!item.resolved_pwad_paths.empty()) {
        for (size_t i = 0; i < item.resolved_pwad_paths.size(); ++i) {
            if (i > 0) pwadsStr += ", ";
            pwadsStr += item.resolved_pwad_paths[i];
        }
    } else if (item.vars.find("pwads") != item.vars.end()) {
        pwadsStr = item.vars.at("pwads").to_string();
    } else {
        pwadsStr = "(None)";
    }

    std::string complevelStr = "Default / Auto";
    if (item.vars.find("complevel") != item.vars.end()) {
        complevelStr = item.vars.at("complevel").to_string();
    } else if (item.vars.find("COMPAT_LEVEL") != item.vars.end()) {
        complevelStr = item.vars.at("COMPAT_LEVEL").to_string();
    } else if (item.vars.find("compat_level") != item.vars.end()) {
        complevelStr = item.vars.at("compat_level").to_string();
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

    std::string saveDirStr = "Default";
    if (item.vars.find("savedir") != item.vars.end()) {
        saveDirStr = item.vars.at("savedir").to_string();
    } else if (item.vars.find("SAVEDIR") != item.vars.end()) {
        saveDirStr = item.vars.at("SAVEDIR").to_string();
    } else if (item.vars.find("save") != item.vars.end()) {
        saveDirStr = item.vars.at("save").to_string();
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

    std::string cmdStr = item.get_command();
    if (cmdStr.empty()) cmdStr = item.cmd;
    if (cmdStr.empty()) cmdStr = "(None)";

    struct InfoRow {
        std::string label;
        std::string val;
        SDL_Color color;
    };

    std::vector<InfoRow> infoRows = {
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

    int startY = y + 46;
    int rowSpacing = 37;
    int labelX = x + 25;
    int valX = x + 215;
    int valMaxW = w - 240;

    for (size_t i = 0; i < infoRows.size(); ++i) {
        int curY = startY + static_cast<int>(i) * rowSpacing;
        renderText(osdFont, infoRows[i].label, labelX, curY, colLabel, false, 185);
        renderText(osdFont, infoRows[i].val, valX, curY, infoRows[i].color, false, valMaxW);
    }

    std::string footer = "[ Press F3, Esc, or Enter to Close ]";
    int footW = 0;
    TTF_SizeText(osdFont, footer.c_str(), &footW, nullptr);
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
    TTF_SizeText(osdFont, footer.c_str(), &footW, nullptr);
    renderText(osdFont, footer, x + (w - footW) / 2, y + h - 32, colFoot, false);
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
    TTF_SizeText(osdFont, prompt1.c_str(), &p1W, nullptr);
    renderText(osdFont, prompt1, x + (w - p1W) / 2, y + 58, yellow, false);

    std::string prompt2 = "Press Y or Enter to Exit, Esc or N to Cancel";
    int p2W = 0;
    TTF_SizeText(osdFont, prompt2.c_str(), &p2W, nullptr);
    renderText(osdFont, prompt2, x + (w - p2W) / 2, y + 102, white, false);
}
