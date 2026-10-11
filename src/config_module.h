#ifndef CONFIG_MODULE_H
#define CONFIG_MODULE_H

#include "asset_resolver.h"
#include "template_engine.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace doomlauncher {

struct Module {
    std::string name;
    std::string title;
    std::string module_dir;

    std::vector<std::string> requires;
    std::vector<std::string> includes;

    Scope vars;
};

struct ResolvedMenuItem {
    std::string module_name;
    std::string title;
    std::string cmd;
    Scope vars;

    bool assets_found = true;
    std::vector<std::string> missing_assets;
    std::string resolved_iwad_path;
    std::vector<std::string> resolved_pwad_paths;
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

    void add_module(const Module& module);
    const Module* get_module(const std::string& name) const;
    bool has_module(const std::string& name) const;

    std::vector<ResolvedMenuItem> resolve_menu_items();

private:
    std::unordered_map<std::string, Module> modules;
    AssetResolver asset_resolver;

    void resolve_dependencies_recursive(
        const std::string& module_name,
        std::vector<std::string>& ordered_deps,
        std::unordered_map<std::string, bool>& visited,
        std::unordered_map<std::string, bool>& in_stack
    );
};

} // namespace doomlauncher

#endif // CONFIG_MODULE_H
