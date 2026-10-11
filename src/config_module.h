#ifndef CONFIG_MODULE_H
#define CONFIG_MODULE_H

#include "asset_resolver.h"
#include "engine_registry.h"
#include "template_engine.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace doomlauncher {

struct Module {
    std::string name;
    std::string title;
    std::string module_dir;
    std::string type;

    std::vector<std::string> requires;
    std::vector<std::string> includes;
    std::vector<std::string> requires_capability;

    Scope vars;
};

enum class GameStatus {
    Ready,
    MissingAssets,
    NoCompatibleEngine
};

struct ResolvedMenuItem {
    std::string module_name;
    std::string title;
    std::string cmd;
    Scope vars;

    GameStatus status = GameStatus::Ready;
    std::string status_details;

    bool assets_found = true;
    std::vector<std::string> missing_assets;
    std::string resolved_iwad_path;
    std::vector<std::string> resolved_pwad_paths;

    std::vector<EngineInfo> compatible_engines;
    size_t selected_engine_index = 0;

    std::string get_command() const;
    std::string get_command_for_engine(size_t engine_idx) const;
    const EngineInfo* get_selected_engine() const;
    bool select_next_engine();
    bool select_prev_engine();
    bool select_engine(const std::string& engine_name);
};

class ModuleRegistry {
public:
    ModuleRegistry() = default;

    bool load_file(const std::string& filepath);
    bool load_directory(const std::string& dirpath);
    bool load_user_config(const std::string& config_path = "");

    void set_asset_resolver(const AssetResolver& resolver);
    AssetResolver& get_asset_resolver();
    const AssetResolver& get_asset_resolver() const;

    void set_engine_registry(const EngineRegistry& registry);
    EngineRegistry& get_engine_registry();
    const EngineRegistry& get_engine_registry() const;

    void set_user_config(const UserConfig& config);
    const UserConfig& get_user_config() const;

    void add_module(const Module& module);
    const Module* get_module(const std::string& name) const;
    bool has_module(const std::string& name) const;

    std::vector<ResolvedMenuItem> resolve_menu_items();

private:
    std::unordered_map<std::string, Module> modules;
    AssetResolver asset_resolver;
    EngineRegistry engine_registry;
    UserConfig user_config;

    void resolve_dependencies_recursive(
        const std::string& module_name,
        std::vector<std::string>& ordered_deps,
        std::unordered_map<std::string, bool>& visited,
        std::unordered_map<std::string, bool>& in_stack
    );
};

} // namespace doomlauncher

#endif // CONFIG_MODULE_H
