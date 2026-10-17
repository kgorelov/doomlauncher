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
    AddWadDialog,
    CreateModuleForm,
    QuitConfirm
};

struct BrowserEntry {
    std::string name;
    std::string full_path;
    bool is_dir = false;
    bool is_parent = false;
    uintmax_t file_size = 0;
    bool selected = false;
};

struct BaseWadOption {
    std::string label;
    std::string base_game_module;
    doomlauncher::WadFile wad;
    bool is_standalone = false;
};

class Menu {
public:
    static constexpr int ITEMS_PER_PAGE = 6;
    static constexpr int BROWSER_ITEMS_PER_PAGE = 8;

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

    void setConfigSavePath(const std::string& path) { configSavePath = path; }
    void setUserModulesDir(const std::string& path) { userModulesDir = path; }

    void openAddWadDialog(const std::string& startDir = "");
    void loadBrowserDirectory(const std::string& dirPath);
    const std::string& getBrowserCurrentDir() const { return browserCurrentDir; }
    const std::vector<BrowserEntry>& getBrowserEntries() const { return browserEntries; }
    int getBrowserCursorIndex() const { return browserCursorIndex; }
    void setBrowserCursorIndex(int idx) { browserCursorIndex = idx; }

    void openCreateModuleForm(const std::vector<doomlauncher::WadFile>& wads);
    bool submitCreateModuleForm();
    int getFormFieldIndex() const { return formFieldIndex; }
    void setFormFieldIndex(int idx) { formFieldIndex = idx; }
    const std::string& getFormTitle() const { return formTitle; }
    void setFormTitle(const std::string& title) { formTitle = title; }
    const std::vector<BaseWadOption>& getFormBaseOptions() const { return formBaseOptions; }
    int getFormBaseIndex() const { return formBaseIndex; }
    int getFormComplevelIndex() const { return formComplevelIndex; }
    int getFormCapabilityIndex() const { return formCapabilityIndex; }

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
    std::string configSavePath;
    std::string userModulesDir;
    std::function<bool(const std::vector<doomlauncher::ResolvedMenuItem>&, std::string&)> saveCallback;

    // Add WAD Browser state
    std::string browserCurrentDir;
    std::vector<BrowserEntry> browserEntries;
    int browserCursorIndex = 0;

    // Create Module Form state
    std::vector<doomlauncher::WadFile> pendingFormWads;
    std::string formTitle;
    std::vector<BaseWadOption> formBaseOptions;
    int formBaseIndex = 0;
    std::vector<doomlauncher::WadFile> formExtraWads;
    std::vector<bool> formExtraSelected;
    int formExtraCursorIndex = 0;
    int formComplevelIndex = 0;
    int formCapabilityIndex = 0;
    int formFieldIndex = 0;

    void confirmSelectedWads();
    void refreshMenuFromRegistry(const std::string& selectModuleName = "");

    void renderText(TTF_Font* f, const std::string& text, int x, int y, SDL_Color color, bool centered = false, int maxAllowedWidth = 0);
    void launchSelected();
    void renderDialogBox(int x, int y, int w, int h, const std::string& title, TTF_Font* osdFont);
    void renderHelpOverlay(TTF_Font* osdFont);
    void renderModuleInfoOverlay(TTF_Font* osdFont);
    void renderEngineSelectOverlay(TTF_Font* osdFont);
    void renderAddWadOverlay(TTF_Font* osdFont);
    void renderCreateModuleOverlay(TTF_Font* osdFont);
    void renderQuitConfirmOverlay(TTF_Font* osdFont);
};

#endif // MENU_H
