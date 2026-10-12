#include "engine_registry.h"
#include "asset_resolver.h"
#include "toml.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <unistd.h>

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

Value parse_toml_node(const toml::node& node) {
    if (auto s = node.as_string()) {
        return Value(std::string(s->get()));
    } else if (auto b = node.as_boolean()) {
        return Value(b->get());
    } else if (auto i = node.as_integer()) {
        return Value(static_cast<double>(i->get()));
    } else if (auto f = node.as_floating_point()) {
        return Value(f->get());
    } else if (auto arr = node.as_array()) {
        Array res;
        for (const auto& elem : *arr) {
            res.push_back(parse_toml_node(elem));
        }
        return Value(res);
    }
    return Value();
}

} // namespace

bool EngineInfo::has_capability(const std::string& cap) const {
    std::string target = to_lower(cap);
    for (const auto& c : capabilities) {
        if (to_lower(c) == target) {
            return true;
        }
    }
    return false;
}

bool EngineInfo::supports_all(const std::vector<std::string>& required_caps) const {
    for (const auto& req : required_caps) {
        if (!has_capability(req)) {
            return false;
        }
    }
    return true;
}

std::string EngineRegistry::find_binary(const std::string& binary_name) {
    if (binary_name.empty()) return "";

    std::error_code ec;

    // Direct path specified (e.g. /usr/bin/gzdoom or ./gzdoom)
    if (binary_name.find('/') != std::string::npos) {
        std::string expanded = AssetResolver::expand_path(binary_name);
        fs::path p(expanded);
        if (fs::exists(p, ec) && !fs::is_directory(p, ec) && access(expanded.c_str(), X_OK) == 0) {
            return p.lexically_normal().string();
        }
        return "";
    }

    // Search PATH environment variable
    const char* path_env = std::getenv("PATH");
    std::vector<std::string> search_dirs;

    if (path_env && *path_env) {
        std::stringstream ss(path_env);
        std::string token;
        while (std::getline(ss, token, ':')) {
            if (!token.empty()) {
                search_dirs.push_back(token);
            }
        }
    }

    // Fallbacks commonly containing game binaries
    search_dirs.push_back("/usr/games");
    search_dirs.push_back("/usr/local/games");
    search_dirs.push_back("/snap/bin");

    for (const auto& dir : search_dirs) {
        fs::path candidate = fs::path(dir) / binary_name;
        if (fs::exists(candidate, ec) && !fs::is_directory(candidate, ec)) {
            std::string path_str = candidate.string();
            if (access(path_str.c_str(), X_OK) == 0) {
                return candidate.lexically_normal().string();
            }
        }
    }

    return "";
}

bool EngineRegistry::is_binary_available(const std::string& binary_name) {
    return !find_binary(binary_name).empty();
}

void EngineRegistry::register_engine(const EngineInfo& engine) {
    EngineInfo eng = engine;

    // Detect installation status if not already explicitly marked installed
    if (!eng.is_installed) {
        if (!eng.binaries.empty()) {
            for (const auto& b : eng.binaries) {
                std::string found = find_binary(b);
                if (!found.empty()) {
                    eng.is_installed = true;
                    eng.installed_binary_path = found;
                    break;
                }
            }
        }

        if (!eng.is_installed && !eng.binary.empty()) {
            std::string found = find_binary(eng.binary);
            if (!found.empty()) {
                eng.is_installed = true;
                eng.installed_binary_path = found;
            }
        }
    } else if (eng.installed_binary_path.empty()) {
        eng.installed_binary_path = eng.binary;
    }

    auto it = engine_index_by_name.find(eng.name);
    if (it != engine_index_by_name.end()) {
        engines[it->second] = eng;
    } else {
        engine_index_by_name[eng.name] = engines.size();
        engines.push_back(eng);
    }
}

const EngineInfo* EngineRegistry::get_engine(const std::string& name) const {
    auto it = engine_index_by_name.find(name);
    if (it != engine_index_by_name.end()) {
        return &engines[it->second];
    }
    return nullptr;
}

EngineInfo* EngineRegistry::get_engine_mut(const std::string& name) {
    auto it = engine_index_by_name.find(name);
    if (it != engine_index_by_name.end()) {
        return &engines[it->second];
    }
    return nullptr;
}

bool EngineRegistry::has_engine(const std::string& name) const {
    return engine_index_by_name.find(name) != engine_index_by_name.end();
}

const std::vector<EngineInfo>& EngineRegistry::get_all_engines() const {
    return engines;
}

void EngineRegistry::detect_installed_engines() {
    for (auto& eng : engines) {
        eng.is_installed = false;
        eng.installed_binary_path.clear();

        if (!eng.binaries.empty()) {
            for (const auto& b : eng.binaries) {
                std::string found = find_binary(b);
                if (!found.empty()) {
                    eng.is_installed = true;
                    eng.installed_binary_path = found;
                    break;
                }
            }
        }

        if (!eng.is_installed && !eng.binary.empty()) {
            std::string found = find_binary(eng.binary);
            if (!found.empty()) {
                eng.is_installed = true;
                eng.installed_binary_path = found;
            }
        }
    }
}

std::vector<EngineInfo> EngineRegistry::find_compatible_engines(
    const std::vector<std::string>& required_capabilities,
    bool only_installed
) const {
    std::vector<EngineInfo> result;
    for (const auto& eng : engines) {
        if (only_installed && !eng.is_installed) {
            continue;
        }
        if (eng.supports_all(required_capabilities)) {
            result.push_back(eng);
        }
    }
    return result;
}

bool EngineRegistry::load_engine_file(const std::string& filepath) {
    toml::table root;
    try {
        root = toml::parse_file(filepath);
    } catch (const toml::parse_error& err) {
        std::cerr << "Error parsing engine TOML file '" << filepath << "': " << err << std::endl;
        return false;
    }

    fs::path p(filepath);

    for (const auto& [key, val] : root) {
        if (auto tbl = val.as_table()) {
            EngineInfo eng;
            eng.name = std::string(key.str());

            if (auto type_node = tbl->get("type")) {
                if (auto s = type_node->as_string()) {
                    if (s->get() != "engine") {
                        continue;
                    }
                }
            }

            if (auto title_node = tbl->get("title")) {
                if (auto s = title_node->as_string()) {
                    eng.title = std::string(s->get());
                }
            }
            if (eng.title.empty()) {
                eng.title = eng.name;
            }

            if (auto bin_node = tbl->get("binary")) {
                if (auto s = bin_node->as_string()) {
                    eng.binary = std::string(s->get());
                }
            }

            if (auto bins_node = tbl->get("binaries")) {
                if (auto arr = bins_node->as_array()) {
                    for (const auto& elem : *arr) {
                        if (auto s = elem.as_string()) {
                            eng.binaries.push_back(std::string(s->get()));
                        }
                    }
                }
            }

            if (auto caps_node = tbl->get("capabilities")) {
                if (auto arr = caps_node->as_array()) {
                    for (const auto& elem : *arr) {
                        if (auto s = elem.as_string()) {
                            eng.capabilities.push_back(std::string(s->get()));
                        }
                    }
                }
            }

            if (auto cmd_node = tbl->get("cmd")) {
                if (auto s = cmd_node->as_string()) {
                    eng.cmd_template = std::string(s->get());
                }
            } else if (auto cmd_upper = tbl->get("CMD")) {
                if (auto s = cmd_upper->as_string()) {
                    eng.cmd_template = std::string(s->get());
                }
            }

            for (const auto& [vk, vv] : *tbl) {
                std::string k_str(vk.str());
                if (k_str == "type" || k_str == "title" || k_str == "binary" ||
                    k_str == "binaries" || k_str == "capabilities" ||
                    k_str == "cmd" || k_str == "CMD") {
                    continue;
                }
                eng.vars[k_str] = parse_toml_node(vv);
            }

            // Fallback for legacy modules: check <NAME>_BIN or BIN in vars
            if (eng.binary.empty() && eng.binaries.empty()) {
                std::string upper_name = eng.name;
                std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
                std::string bin_var = upper_name + "_BIN";
                if (eng.vars.find(bin_var) != eng.vars.end()) {
                    eng.binary = eng.vars[bin_var].to_string();
                } else if (eng.vars.find("BIN") != eng.vars.end()) {
                    eng.binary = eng.vars["BIN"].to_string();
                } else {
                    eng.binary = eng.name;
                }
            }

            // Default capabilities if none specified
            if (eng.capabilities.empty()) {
                eng.capabilities.push_back("vanilla");
            }

            register_engine(eng);
        }
    }

    return true;
}

bool EngineRegistry::load_engine_directory(const std::string& dirpath) {
    fs::path p(dirpath);
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) {
        return false;
    }

    bool success = true;
    for (const auto& entry : fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && entry.path().extension() == ".toml") {
            if (!load_engine_file(entry.path().string())) {
                success = false;
            }
        }
    }
    return success;
}

} // namespace doomlauncher
