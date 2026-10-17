#ifndef ASSET_RESOLVER_H
#define ASSET_RESOLVER_H

#include <string>
#include <unordered_map>
#include <vector>

namespace doomlauncher {

struct UserConfig {
    std::vector<std::string> module_dirs;
    std::vector<std::string> wad_dirs;
    std::unordered_map<std::string, std::string> iwads;
    std::string default_engine;
    std::string osd_font = "mono";
};

class AssetResolver {
public:
    AssetResolver();
    explicit AssetResolver(const UserConfig& config);

    void add_search_dir(const std::string& path);
    const std::vector<std::string>& get_search_dirs() const;

    void set_iwad_alias(const std::string& alias, const std::string& path);
    std::string get_iwad_alias(const std::string& alias) const;

    // Resolves a file reference. Checks:
    // 1. Direct path if existing
    // 2. IWAD aliases
    // 3. Registered search directories (direct, case-insensitive, and 1-level subdirectories)
    std::string resolve(const std::string& file_or_path) const;

    // Utility: case-insensitive search for filename inside dir (and optional 1-level subdirectories)
    static std::string find_file_in_dir(const std::string& dir, const std::string& filename, bool recursive = true);

    // Utility: expands ~ to $HOME and environment variables like $DOOMWADDIR
    static std::string expand_path(const std::string& path);

    // Utility: case-insensitive string equality
    static bool iequals(const std::string& a, const std::string& b);

    // Default paths
    static std::string get_default_config_path();
    static std::vector<std::string> get_default_module_dirs();
    static std::vector<std::string> get_default_system_search_paths();

    // Load / Save UserConfig
    static UserConfig load_config(const std::string& explicit_path = "");
    static bool save_config(const UserConfig& config, const std::string& explicit_path = "");

private:
    std::vector<std::string> search_dirs;
    std::unordered_map<std::string, std::string> iwad_aliases;

    void init_defaults();
};

} // namespace doomlauncher

#endif // ASSET_RESOLVER_H
