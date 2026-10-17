#include "asset_resolver.h"
#include "toml.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace doomlauncher {

namespace fs = std::filesystem;

namespace {

std::string to_lower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return lower;
}

} // namespace

bool AssetResolver::iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](unsigned char c1, unsigned char c2) {
        return std::tolower(c1) == std::tolower(c2);
    });
}

std::string AssetResolver::expand_path(const std::string& path) {
    if (path.empty()) return "";

    std::string result = path;
    const char* home = std::getenv("HOME");

    if (result == "~" && home) {
        return home;
    }
    if (result.rfind("~/", 0) == 0 && home) {
        result = std::string(home) + result.substr(1);
    }

    // Expand $VARIABLE or ${VARIABLE}
    size_t pos = 0;
    while ((pos = result.find('$', pos)) != std::string::npos) {
        size_t end_pos = pos + 1;
        std::string var_name;
        if (end_pos < result.size() && result[end_pos] == '{') {
            size_t close_brace = result.find('}', end_pos + 1);
            if (close_brace != std::string::npos) {
                var_name = result.substr(end_pos + 1, close_brace - end_pos - 1);
                end_pos = close_brace + 1;
            } else {
                break;
            }
        } else {
            while (end_pos < result.size() && (std::isalnum(result[end_pos]) || result[end_pos] == '_')) {
                end_pos++;
            }
            var_name = result.substr(pos + 1, end_pos - pos - 1);
        }

        if (!var_name.empty()) {
            const char* env_val = std::getenv(var_name.c_str());
            std::string replacement = env_val ? env_val : "";
            result.replace(pos, end_pos - pos, replacement);
            pos += replacement.size();
        } else {
            pos++;
        }
    }

    return result;
}

std::string AssetResolver::find_file_in_dir(const std::string& dir, const std::string& filename, bool recursive) {
    std::error_code ec;
    fs::path dir_path(dir);

    if (!fs::exists(dir_path, ec) || !fs::is_directory(dir_path, ec)) {
        return "";
    }

    // Direct check (fast path)
    fs::path direct_path = dir_path / filename;
    if (fs::exists(direct_path, ec) && !fs::is_directory(direct_path, ec)) {
        return direct_path.lexically_normal().string();
    }

    // Case-insensitive check in current directory
    std::vector<fs::path> subdirs;
    for (const auto& entry : fs::directory_iterator(dir_path, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec)) {
            if (iequals(entry.path().filename().string(), filename)) {
                return entry.path().lexically_normal().string();
            }
        } else if (recursive && entry.is_directory(ec)) {
            subdirs.push_back(entry.path());
        }
    }

    // 1-level subdirectory check if requested
    if (recursive) {
        for (const auto& sub : subdirs) {
            std::string found = find_file_in_dir(sub.string(), filename, false);
            if (!found.empty()) {
                return found;
            }
        }
    }

    return "";
}

std::string AssetResolver::get_default_config_path() {
    const char* env_cfg = std::getenv("DOOMLAUNCHER_CONFIG");
    if (env_cfg && *env_cfg) {
        return env_cfg;
    }
    const char* xdg_cfg = std::getenv("XDG_CONFIG_HOME");
    if (xdg_cfg && *xdg_cfg) {
        return std::string(xdg_cfg) + "/doomlauncher/config.toml";
    }
    return expand_path("~/.config/doomlauncher/config.toml");
}

std::vector<std::string> AssetResolver::get_default_module_dirs() {
    const char* xdg_cfg = std::getenv("XDG_CONFIG_HOME");
    std::string user_mod_dir = (xdg_cfg && *xdg_cfg)
        ? (std::string(xdg_cfg) + "/doomlauncher/modules")
        : "~/.config/doomlauncher/modules";

    return {
        "/usr/share/doomlauncher/modules",
        "modules",
        user_mod_dir
    };
}

std::vector<std::string> AssetResolver::get_default_system_search_paths() {
    std::vector<std::string> paths;

    // Check DOOMWADDIR
    const char* env_dir = std::getenv("DOOMWADDIR");
    if (env_dir && *env_dir) {
        paths.push_back(env_dir);
    }

    // Check DOOMWADPATH
    const char* env_path = std::getenv("DOOMWADPATH");
    if (env_path && *env_path) {
        std::stringstream ss(env_path);
        std::string token;
        while (std::getline(ss, token, ':')) {
            if (!token.empty()) {
                paths.push_back(token);
            }
        }
    }

    // Standard user and system Doom directories
    paths.push_back("~/.local/share/doom");
    paths.push_back("~/.local/share/games/doom");
    paths.push_back("/usr/share/games/doom");
    paths.push_back("/usr/share/doom");
    paths.push_back("/usr/local/share/games/doom");
    paths.push_back("/usr/local/share/doom");

    // Standard Steam default paths
    paths.push_back("~/.steam/steam/steamapps/common/Doom 2/base");
    paths.push_back("~/.steam/steam/steamapps/common/Ultimate Doom/base");
    paths.push_back("~/.steam/steam/steamapps/common/Final Doom/base");
    paths.push_back("~/.local/share/Steam/steamapps/common/Doom 2/base");
    paths.push_back("~/.local/share/Steam/steamapps/common/Ultimate Doom/base");
    paths.push_back("~/.local/share/Steam/steamapps/common/Final Doom/base");

    return paths;
}

UserConfig AssetResolver::load_config(const std::string& explicit_path) {
    UserConfig config;
    std::string config_file = explicit_path.empty() ? get_default_config_path() : explicit_path;
    std::string resolved_file = expand_path(config_file);

    std::error_code ec;
    if (!fs::exists(resolved_file, ec)) {
        config.module_dirs = get_default_module_dirs();
        return config;
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(resolved_file);
    } catch (const toml::parse_error& err) {
        std::cerr << "Warning: Error parsing user config '" << resolved_file << "': " << err << std::endl;
        config.module_dirs = get_default_module_dirs();
        return config;
    }

    if (auto paths_tbl = tbl.get_as<toml::table>("paths")) {
        if (auto arr = paths_tbl->get_as<toml::array>("modules")) {
            for (const auto& elem : *arr) {
                if (auto str = elem.as_string()) {
                    config.module_dirs.push_back(std::string(str->get()));
                }
            }
        }

        if (auto arr = paths_tbl->get_as<toml::array>("wad_dirs")) {
            for (const auto& elem : *arr) {
                if (auto str = elem.as_string()) {
                    config.wad_dirs.push_back(std::string(str->get()));
                }
            }
        }
    }

    if (config.module_dirs.empty()) {
        config.module_dirs = get_default_module_dirs();
    }

    if (auto prefs_tbl = tbl.get_as<toml::table>("preferences")) {
        if (auto node = prefs_tbl->get("default_engine")) {
            if (auto str = node->as_string()) {
                config.default_engine = std::string(str->get());
            }
        }
        if (auto node = prefs_tbl->get("osd_font")) {
            if (auto str = node->as_string()) {
                config.osd_font = std::string(str->get());
            }
        }
    }

    if (auto iwads_tbl = tbl.get_as<toml::table>("iwads")) {
        for (const auto& [key, val] : *iwads_tbl) {
            if (auto str = val.as_string()) {
                config.iwads[to_lower(std::string(key.str()))] = std::string(str->get());
            }
        }
    }

    return config;
}

bool AssetResolver::save_config(const UserConfig& config, const std::string& explicit_path) {
    std::string config_file = explicit_path.empty() ? get_default_config_path() : explicit_path;
    std::string resolved_file = expand_path(config_file);

    try {
        fs::path p(resolved_file);
        if (p.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }

        toml::table root;

        toml::table paths_tbl;
        toml::array mods_arr;
        for (const auto& m : config.module_dirs) {
            mods_arr.push_back(m);
        }
        paths_tbl.insert_or_assign("modules", mods_arr);

        toml::array wads_arr;
        for (const auto& w : config.wad_dirs) {
            wads_arr.push_back(w);
        }
        paths_tbl.insert_or_assign("wad_dirs", wads_arr);
        root.insert_or_assign("paths", paths_tbl);

        if (!config.iwads.empty()) {
            toml::table iwads_tbl;
            for (const auto& [k, v] : config.iwads) {
                iwads_tbl.insert_or_assign(k, v);
            }
            root.insert_or_assign("iwads", iwads_tbl);
        }

        toml::table prefs_tbl;
        if (!config.default_engine.empty()) {
            prefs_tbl.insert_or_assign("default_engine", config.default_engine);
        }
        if (!config.osd_font.empty()) {
            prefs_tbl.insert_or_assign("osd_font", config.osd_font);
        }
        if (!prefs_tbl.empty()) {
            root.insert_or_assign("preferences", prefs_tbl);
        }

        std::ofstream out(resolved_file);
        if (!out.is_open()) {
            return false;
        }
        out << root << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to save user config '" << resolved_file << "': " << e.what() << std::endl;
        return false;
    }
}

AssetResolver::AssetResolver() {
    init_defaults();
}

AssetResolver::AssetResolver(const UserConfig& config) {
    init_defaults();

    for (const auto& dir : config.wad_dirs) {
        add_search_dir(dir);
    }

    for (const auto& [alias, path] : config.iwads) {
        set_iwad_alias(alias, path);
    }
}

void AssetResolver::init_defaults() {
    std::vector<std::string> defaults = get_default_system_search_paths();
    for (const auto& d : defaults) {
        add_search_dir(d);
    }
}

void AssetResolver::add_search_dir(const std::string& path) {
    if (path.empty()) return;
    std::string expanded = expand_path(path);
    if (std::find(search_dirs.begin(), search_dirs.end(), expanded) == search_dirs.end()) {
        search_dirs.push_back(expanded);
    }
}

const std::vector<std::string>& AssetResolver::get_search_dirs() const {
    return search_dirs;
}

void AssetResolver::set_iwad_alias(const std::string& alias, const std::string& path) {
    iwad_aliases[to_lower(alias)] = expand_path(path);
}

std::string AssetResolver::get_iwad_alias(const std::string& alias) const {
    auto it = iwad_aliases.find(to_lower(alias));
    if (it != iwad_aliases.end()) {
        return it->second;
    }
    return "";
}

std::string AssetResolver::resolve(const std::string& file_or_path) const {
    if (file_or_path.empty()) return "";

    std::string expanded_input = expand_path(file_or_path);
    std::error_code ec;

    // 1. Check if input is an existing absolute path or relative to CWD
    fs::path input_path(expanded_input);
    if (fs::exists(input_path, ec) && !fs::is_directory(input_path, ec)) {
        return input_path.lexically_normal().string();
    }

    std::string filename = input_path.filename().string();

    // 2. Check IWAD aliases
    std::string lower_filename = to_lower(filename);
    std::string lower_stem = to_lower(input_path.stem().string());

    auto it_full = iwad_aliases.find(lower_filename);
    if (it_full != iwad_aliases.end() && fs::exists(it_full->second, ec)) {
        return it_full->second;
    }

    auto it_stem = iwad_aliases.find(lower_stem);
    if (it_stem != iwad_aliases.end() && fs::exists(it_stem->second, ec)) {
        return it_stem->second;
    }

    // 3. Check registered search directories
    for (const auto& dir : search_dirs) {
        std::string match = find_file_in_dir(dir, filename, true);
        if (!match.empty()) {
            return match;
        }
    }

    return "";
}

} // namespace doomlauncher
