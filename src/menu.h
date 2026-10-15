#ifndef MENU_H
#define MENU_H

#include "config_module.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <functional>
#include <string>
#include <vector>

enum class OverlayState {
    None,
    Help,
    ModuleInfo,
    EngineSelect,
    QuitConfirm
};

class Menu {
public:
    static constexpr int ITEMS_PER_PAGE = 6;

    Menu(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* smallFont, int screenWidth, int screenHeight, TTF_Font* altSmallFont = nullptr);
    ~Menu();

    void setMenuItems(const std::vector<doomlauncher::ResolvedMenuItem>& items);
    void handleEvent(const SDL_Event& event);
    void update();
    void render();
    bool isQuitSelected() const;

    int getSelectedItem() const { return selectedItem; }
    int getCurrentPage() const { return selectedItem / ITEMS_PER_PAGE; }
    int getTotalPages() const {
        return menuItems.empty() ? 1 : static_cast<int>((menuItems.size() + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE);
    }
    const std::vector<doomlauncher::ResolvedMenuItem>& getMenuItems() const { return menuItems; }
    void setStatusMessage(const std::string& msg) { statusMessage = msg; }
    const std::string& getStatusMessage() const { return statusMessage; }

    void setUseAlternateFont(bool use) { useAlternateFont = use; }
    bool isUsingAlternateFont() const { return useAlternateFont; }
    void toggleOsdFont();

    OverlayState getOverlayState() const { return overlayState; }
    void setOverlayState(OverlayState state) { overlayState = state; }
    int getModalCursorIndex() const { return modalCursorIndex; }
    void setModalCursorIndex(int idx) { modalCursorIndex = idx; }

    void setModuleRegistry(doomlauncher::ModuleRegistry* reg) { moduleRegistry = reg; }
    doomlauncher::ModuleRegistry* getModuleRegistry() const { return moduleRegistry; }

    void setSaveCallback(std::function<bool(const std::vector<doomlauncher::ResolvedMenuItem>&, std::string&)> cb) {
        saveCallback = cb;
    }
    void saveSettings();

private:
    SDL_Renderer* renderer;
    TTF_Font* font;
    TTF_Font* smallFont;
    TTF_Font* altSmallFont;
    bool useAlternateFont;
    int screenWidth;
    int screenHeight;
    std::vector<doomlauncher::ResolvedMenuItem> menuItems;
    int selectedItem;
    bool quitSelected;
    std::string statusMessage;
    OverlayState overlayState = OverlayState::None;
    int modalCursorIndex = 0;
    doomlauncher::ModuleRegistry* moduleRegistry = nullptr;
    std::function<bool(const std::vector<doomlauncher::ResolvedMenuItem>&, std::string&)> saveCallback;

    void renderText(TTF_Font* f, const std::string& text, int x, int y, SDL_Color color, bool centered = false, int maxAllowedWidth = 0);
    void launchSelected();
    void renderDialogBox(int x, int y, int w, int h, const std::string& title, TTF_Font* osdFont);
    void renderHelpOverlay(TTF_Font* osdFont);
    void renderModuleInfoOverlay(TTF_Font* osdFont);
    void renderEngineSelectOverlay(TTF_Font* osdFont);
    void renderQuitConfirmOverlay(TTF_Font* osdFont);
};

#endif // MENU_H
