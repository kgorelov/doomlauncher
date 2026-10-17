#include "config_module.h"
#include "toml.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glob.h>
#include <iostream>
#include <stdexcept>

namespace doomlauncher {

namespace {

namespace fs = std::filesystem;

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

std::vector<std::string> expand_glob_pattern(const std::string& pattern) {
    std::vector<std::string> filenames;
    glob_t glob_result;
    std::memset(&glob_result, 0, sizeof(glob_result));

    int return_value = glob(pattern.c_str(), GLOB_TILDE, nullptr, &glob_result);
    if (return_value == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            filenames.push_back(std::string(glob_result.gl_pathv[i]));
        }
    }
    globfree(&glob_result);
    return filenames;
}

void parse_single_module_table(const toml::table& tbl, Module& mod, const std::string& module_name) {
    mod.name = module_name;

    if (auto title_node = tbl.get("title")) {
        if (auto s = title_node->as_string()) {
            mod.title = std::string(s->get());
        }
    }

    if (auto type_node = tbl.get("type")) {
        if (auto s = type_node->as_string()) {
            mod.type = std::string(s->get());
        }
    }

    if (auto req_node = tbl.get("requires")) {
        if (auto arr = req_node->as_array()) {
            for (const auto& elem : *arr) {
                if (auto s = elem.as_string()) {
                    mod.requires.push_back(std::string(s->get()));
                }
            }
        }
    }

    if (auto base_node = tbl.get("base_game")) {
        if (auto s = base_node->as_string()) {
            mod.base_game = std::string(s->get());
            if (!mod.base_game.empty() &&
                std::find(mod.requires.begin(), mod.requires.end(), mod.base_game) == mod.requires.end()) {
                mod.requires.push_back(mod.base_game);
            }
        }
    }

    if (auto inc_node = tbl.get("includes")) {
        if (auto arr = inc_node->as_array()) {
            for (const auto& elem : *arr) {
                if (auto s = elem.as_string()) {
                    mod.includes.push_back(std::string(s->get()));
                }
            }
        }
    }

    if (auto iwad_node = tbl.get("iwad")) {
        if (auto s = iwad_node->as_string()) {
            mod.iwad = std::string(s->get());
        }
    }

    if (auto pwads_node = tbl.get("pwads")) {
        if (auto arr = pwads_node->as_array()) {
            for (const auto& elem : *arr) {
                if (auto s = elem.as_string()) {
                    mod.pwads.push_back(std::string(s->get()));
                }
            }
        } else if (auto s = pwads_node->as_string()) {
            mod.pwads.push_back(std::string(s->get()));
        }
    }

    if (auto cl_node = tbl.get("compat_level")) {
        mod.compat_level = parse_toml_node(*cl_node).to_string();
    }

    if (auto req_cap = tbl.get("requires_capability")) {
        if (auto arr = req_cap->as_array()) {
            for (const auto& elem : *arr) {
                if (auto s = elem.as_string()) {
                    mod.requires_capability.push_back(std::string(s->get()));
                }
            }
        } else if (auto s = req_cap->as_string()) {
            mod.requires_capability.push_back(std::string(s->get()));
        }
    }

    for (const auto& [key, val] : tbl) {
        std::string k_str(key.str());
        if (k_str == "title" || k_str == "type" || k_str == "requires" || k_str == "base_game" ||
            k_str == "includes" || k_str == "iwad" || k_str == "pwads" || k_str == "compat_level" ||
            k_str == "requires_capability") {
            continue;
        }
        mod.vars[k_str] = parse_toml_node(val);
    }
}

} // namespace

const EngineInfo* ResolvedMenuItem::get_selected_engine() const {
    if (compatible_engines.empty()) {
        return nullptr;
    }
    if (selected_engine_index >= compatible_engines.size()) {
        return &compatible_engines[0];
    }
    return &compatible_engines[selected_engine_index];
}

std::string ResolvedMenuItem::get_command_for_engine(size_t engine_idx) const {
    if (compatible_engines.empty()) {
        return cmd;
    }
    if (engine_idx >= compatible_engines.size()) {
        engine_idx = 0;
    }
    const auto& eng = compatible_engines[engine_idx];
    if (eng.cmd_template.empty()) {
        return cmd;
    }

    Scope eng_scope = vars;
    std::string bin_path = eng.installed_binary_path.empty() ? eng.binary : eng.installed_binary_path;
    eng_scope["BIN"] = Value(bin_path);

    if (!resolved_iwad_path.empty()) {
        eng_scope["IWAD"] = Value(resolved_iwad_path);
    }
    if (!resolved_pwad_paths.empty()) {
        Array pwads_arr;
        for (const auto& p : resolved_pwad_paths) {
            pwads_arr.push_back(Value(p));
        }
        eng_scope["PWADS"] = Value(pwads_arr);
    }
    if (!save_dir.empty()) {
        eng_scope["SAVEDIR"] = Value(save_dir);
    }

    for (const auto& [k, v] : eng.vars) {
        if (k == "BIN") continue;
        if (eng_scope.find(k) == eng_scope.end()) {
            eng_scope[k] = v;
        }
    }

    std::string rendered = TemplateEngine::render(eng.cmd_template, eng_scope);
    for (int p = 0; p < 2; ++p) {
        std::string next = TemplateEngine::render(rendered, eng_scope);
        if (next == rendered) break;
        rendered = next;
    }
    return rendered;
}

std::string ResolvedMenuItem::get_command() const {
    if (!compatible_engines.empty()) {
        return get_command_for_engine(selected_engine_index);
    }
    return cmd;
}

bool ResolvedMenuItem::select_next_engine() {
    if (compatible_engines.size() <= 1) {
        return false;
    }
    selected_engine_index = (selected_engine_index + 1) % compatible_engines.size();
    cmd = get_command();
    return true;
}

bool ResolvedMenuItem::select_prev_engine() {
    if (compatible_engines.size() <= 1) {
        return false;
    }
    selected_engine_index = (selected_engine_index + compatible_engines.size() - 1) % compatible_engines.size();
    cmd = get_command();
    return true;
}

bool ResolvedMenuItem::select_engine(const std::string& engine_name) {
    for (size_t i = 0; i < compatible_engines.size(); ++i) {
        if (compatible_engines[i].name == engine_name) {
            selected_engine_index = i;
            cmd = get_command();
            return true;
        }
    }
    return false;
}

void ModuleRegistry::set_asset_resolver(const AssetResolver& resolver) {
    asset_resolver = resolver;
}

AssetResolver& ModuleRegistry::get_asset_resolver() {
    return asset_resolver;
}

const AssetResolver& ModuleRegistry::get_asset_resolver() const {
    return asset_resolver;
}

bool ModuleRegistry::load_user_config(const std::string& config_path) {
    user_config = AssetResolver::load_config(config_path);
    asset_resolver = AssetResolver(user_config);
    return true;
}

void ModuleRegistry::set_user_config(const UserConfig& config) {
    user_config = config;
    asset_resolver = AssetResolver(user_config);
}

const UserConfig& ModuleRegistry::get_user_config() const {
    return user_config;
}

std::string ModuleRegistry::get_default_state_path() {
    const char* env_state = std::getenv("DOOMLAUNCHER_STATE");
    if (env_state && *env_state) {
        return env_state;
    }
    const char* xdg_cfg = std::getenv("XDG_CONFIG_HOME");
    if (xdg_cfg && *xdg_cfg) {
        return std::string(xdg_cfg) + "/doomlauncher/state.toml";
    }
    return AssetResolver::expand_path("~/.config/doomlauncher/state.toml");
}

std::string ModuleRegistry::get_default_saves_root() {
    const char* env_saves = std::getenv("DOOMLAUNCHER_SAVES");
    if (env_saves && *env_saves) {
        return AssetResolver::expand_path(env_saves);
    }
    const char* xdg_cfg = std::getenv("XDG_CONFIG_HOME");
    if (xdg_cfg && *xdg_cfg) {
        return std::string(xdg_cfg) + "/doomlauncher/saves";
    }
    return AssetResolver::expand_path("~/.config/doomlauncher/saves");
}

std::string ModuleRegistry::get_module_save_dir(const std::string& module_name) {
    return (fs::path(get_default_saves_root()) / module_name).lexically_normal().string();
}

bool ModuleRegistry::load_state(const std::string& filepath) {
    std::string path_str = filepath.empty() ? get_default_state_path() : filepath;
    std::string resolved_path = AssetResolver::expand_path(path_str);

    std::error_code ec;
    if (!fs::exists(resolved_path, ec)) {
        return false;
    }

    try {
        toml::table tbl = toml::parse_file(resolved_path);
        if (auto engines = tbl.get_as<toml::table>("engines")) {
            for (const auto& [key, val] : *engines) {
                if (auto str = val.as_string()) {
                    saved_engines[std::string(key.str())] = std::string(str->get());
                }
            }
        }
        return true;
    } catch (const toml::parse_error& err) {
        std::cerr << "Warning: Error parsing state file '" << resolved_path << "': " << err << std::endl;
        return false;
    }
}

bool ModuleRegistry::save_state(const std::string& filepath) const {
    std::string path_str = filepath.empty() ? get_default_state_path() : filepath;
    std::string resolved_path = AssetResolver::expand_path(path_str);

    try {
        fs::path p(resolved_path);
        if (p.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }

        toml::table tbl;
        std::error_code ec;
        if (fs::exists(p, ec)) {
            try {
                tbl = toml::parse_file(resolved_path);
            } catch (...) {
                tbl = toml::table();
            }
        }

        toml::table engines_tbl;
        for (const auto& [mod_name, eng_name] : saved_engines) {
            engines_tbl.insert_or_assign(mod_name, eng_name);
        }
        tbl.insert_or_assign("engines", engines_tbl);

        std::ofstream out(resolved_path);
        if (!out.is_open()) {
            return false;
        }
        out << tbl << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to save state to '" << resolved_path << "': " << e.what() << std::endl;
        return false;
    }
}

bool ModuleRegistry::save_state(const std::vector<ResolvedMenuItem>& items, std::string& out_saved_path, const std::string& filepath) {
    std::string path_str = filepath.empty() ? get_default_state_path() : filepath;
    out_saved_path = AssetResolver::expand_path(path_str);

    for (const auto& item : items) {
        if (item.module_name.empty() || item.module_name == "quit") continue;
        const auto* eng = item.get_selected_engine();
        if (eng) {
            saved_engines[item.module_name] = eng->name;
        }
    }

    return save_state(out_saved_path);
}

bool ModuleRegistry::save_state(const std::vector<ResolvedMenuItem>& items, const std::string& filepath) {
    std::string dummy;
    return save_state(items, dummy, filepath);
}

void ModuleRegistry::set_saved_engine(const std::string& module_name, const std::string& engine_name) {
    saved_engines[module_name] = engine_name;
}

const std::unordered_map<std::string, std::string>& ModuleRegistry::get_saved_engines() const {
    return saved_engines;
}

void ModuleRegistry::apply_saved_state(std::vector<ResolvedMenuItem>& items) const {
    for (auto& item : items) {
        if (item.module_name == "quit" || item.compatible_engines.empty()) continue;
        auto it = saved_engines.find(item.module_name);
        if (it != saved_engines.end()) {
            item.select_engine(it->second);
        }
    }
}

bool ModuleRegistry::save_state_from_items(const std::vector<ResolvedMenuItem>& items, std::string& out_saved_path, const std::string& filepath) {
    ModuleRegistry reg;
    return reg.save_state(items, out_saved_path, filepath);
}

void ModuleRegistry::set_engine_registry(const EngineRegistry& registry) {
    engine_registry = registry;
}

EngineRegistry& ModuleRegistry::get_engine_registry() {
    return engine_registry;
}

const EngineRegistry& ModuleRegistry::get_engine_registry() const {
    return engine_registry;
}

void ModuleRegistry::add_module(const Module& module) {
    modules[module.name] = module;
}

const Module* ModuleRegistry::get_module(const std::string& name) const {
    auto it = modules.find(name);
    if (it != modules.end()) {
        return &it->second;
    }
    return nullptr;
}

bool ModuleRegistry::has_module(const std::string& name) const {
    return modules.find(name) != modules.end();
}

bool ModuleRegistry::load_file(const std::string& filepath) {
    toml::table root;
    try {
        root = toml::parse_file(filepath);
    } catch (const toml::parse_error& err) {
        std::cerr << "Error parsing TOML file '" << filepath << "': " << err << std::endl;
        return false;
    }

    fs::path p(filepath);
    fs::path abs_p = fs::absolute(p);
    std::string dir_path = abs_p.parent_path().string();

    std::vector<Module> loaded_modules_in_file;

    auto maybe_register_engine = [&](const Module& mod) {
        if (mod.type == "engine" || dir_path.find("engines") != std::string::npos) {
            EngineInfo eng;
            eng.name = mod.name;
            eng.title = mod.title.empty() ? mod.name : mod.title;
            if (mod.vars.find("binary") != mod.vars.end()) eng.binary = mod.vars.at("binary").to_string();
            if (mod.vars.find("binaries") != mod.vars.end() && mod.vars.at("binaries").type == Value::Type::Array) {
                for (const auto& b : mod.vars.at("binaries").arr_val) {
                    eng.binaries.push_back(b.to_string());
                }
            }
            if (mod.vars.find("capabilities") != mod.vars.end() && mod.vars.at("capabilities").type == Value::Type::Array) {
                for (const auto& c : mod.vars.at("capabilities").arr_val) {
                    eng.capabilities.push_back(c.to_string());
                }
            }
            if (mod.vars.find("cmd") != mod.vars.end()) eng.cmd_template = mod.vars.at("cmd").to_string();
            eng.vars = mod.vars;

            if (eng.binary.empty() && eng.binaries.empty()) {
                eng.binary = eng.name;
            }
            if (eng.capabilities.empty()) {
                eng.capabilities.push_back("vanilla");
            }
            engine_registry.register_engine(eng);
        }
    };

    for (const auto& [key, val] : root) {
        if (auto mod_tbl = val.as_table()) {
            Module mod;
            parse_single_module_table(*mod_tbl, mod, std::string(key.str()));
            add_module(mod);
            loaded_modules_in_file.push_back(mod);
            maybe_register_engine(mod);
        }
    }

    if (loaded_modules_in_file.empty()) {
        Module mod;
        parse_single_module_table(root, mod, abs_p.stem().string());
        add_module(mod);
        loaded_modules_in_file.push_back(mod);
        maybe_register_engine(mod);
    }

    for (const auto& mod : loaded_modules_in_file) {
        for (const auto& inc_pattern : mod.includes) {
            fs::path inc_path(inc_pattern);
            std::string full_pattern;
            if (inc_path.is_relative()) {
                full_pattern = (fs::path(dir_path) / inc_path).string();
            } else {
                full_pattern = inc_pattern;
            }

            std::vector<std::string> matched_files = expand_glob_pattern(full_pattern);
            for (const auto& matched_file : matched_files) {
                if (fs::canonical(matched_file) != fs::canonical(abs_p)) {
                    load_file(matched_file);
                }
            }
        }
    }

    return true;
}

bool ModuleRegistry::load_directory(const std::string& dirpath) {
    fs::path p(AssetResolver::expand_path(dirpath));
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) {
        return false;
    }

    bool any_loaded = false;
    for (const auto& sub : {"engines", "games"}) {
        fs::path sub_dir = p / sub;
        if (fs::exists(sub_dir, ec) && fs::is_directory(sub_dir, ec)) {
            std::vector<std::string> sub_files = expand_glob_pattern((sub_dir / "*.toml").string());
            for (const auto& file : sub_files) {
                if (load_file(file)) {
                    any_loaded = true;
                }
            }
        }
    }

    std::vector<std::string> files = expand_glob_pattern((p / "*.toml").string());
    for (const auto& file : files) {
        if (load_file(file)) {
            any_loaded = true;
        }
    }
    return any_loaded;
}

bool ModuleRegistry::load_configured_modules() {
    std::vector<std::string> dirs = user_config.module_dirs;
    if (dirs.empty()) {
        dirs = AssetResolver::get_default_module_dirs();
    }

    bool any_loaded = false;
    for (const auto& dir : dirs) {
        std::string expanded = AssetResolver::expand_path(dir);
        std::error_code ec;
        if (fs::is_directory(expanded, ec)) {
            if (load_directory(expanded)) {
                any_loaded = true;
            }
        } else if (fs::exists(expanded, ec)) {
            if (load_file(expanded)) {
                any_loaded = true;
            }
        }
    }
    return any_loaded;
}

void ModuleRegistry::resolve_dependencies_recursive(
    const std::string& module_name,
    std::vector<std::string>& ordered_deps,
    std::unordered_map<std::string, bool>& visited,
    std::unordered_map<std::string, bool>& in_stack
) {
    if (in_stack[module_name]) {
        throw std::runtime_error("Circular dependency detected involving module: " + module_name);
    }

    if (visited[module_name]) {
        return;
    }

    const Module* mod = get_module(module_name);
    if (!mod) {
        throw std::runtime_error("Required module not found: " + module_name);
    }

    visited[module_name] = true;
    in_stack[module_name] = true;

    for (const auto& req : mod->requires) {
        resolve_dependencies_recursive(req, ordered_deps, visited, in_stack);
    }

    in_stack[module_name] = false;
    ordered_deps.push_back(module_name);
}

std::vector<ResolvedMenuItem> ModuleRegistry::resolve_menu_items() {
    std::vector<ResolvedMenuItem> items;

    for (const auto& [name, mod] : modules) {
        if (mod.title.empty() || mod.type == "engine") {
            continue;
        }

        std::vector<std::string> ordered_deps;
        std::unordered_map<std::string, bool> visited;
        std::unordered_map<std::string, bool> in_stack;

        try {
            resolve_dependencies_recursive(name, ordered_deps, visited, in_stack);
        } catch (const std::exception& e) {
            std::cerr << "Error resolving module '" << name << "': " << e.what() << std::endl;
            continue;
        }

        Scope merged_scope;
        std::string effective_iwad;
        std::vector<std::string> effective_pwads;
        std::string effective_compat_level;

        for (const auto& dep_name : ordered_deps) {
            const Module* dep_mod = get_module(dep_name);
            if (!dep_mod) continue;

            if (!dep_mod->iwad.empty()) {
                effective_iwad = dep_mod->iwad;
            }
            for (const auto& p : dep_mod->pwads) {
                effective_pwads.push_back(p);
            }
            if (!dep_mod->compat_level.empty()) {
                effective_compat_level = dep_mod->compat_level;
            }
            for (const auto& [k, v] : dep_mod->vars) {
                merged_scope[k] = v;
            }
        }

        ResolvedMenuItem item;
        item.module_name = mod.name;
        item.title = mod.title;
        item.save_dir = get_module_save_dir(mod.name);

        merged_scope["SAVEDIR"] = Value(item.save_dir);
        if (!effective_compat_level.empty()) {
            merged_scope["COMPAT_LEVEL"] = Value(effective_compat_level);
        }

        // Resolve IWAD
        if (!effective_iwad.empty()) {
            std::string raw_iwad = TemplateEngine::render(effective_iwad, merged_scope);
            std::string resolved = asset_resolver.resolve(raw_iwad);
            if (!resolved.empty()) {
                item.resolved_iwad_path = resolved;
                merged_scope["IWAD"] = Value(resolved);
            } else {
                item.assets_found = false;
                item.missing_assets.push_back(raw_iwad);
                merged_scope["IWAD"] = Value(raw_iwad);
            }
        }

        // Resolve PWADs
        if (!effective_pwads.empty()) {
            Array resolved_arr;
            for (const auto& p : effective_pwads) {
                std::string raw_pwad = TemplateEngine::render(p, merged_scope);
                std::string resolved = asset_resolver.resolve(raw_pwad);
                if (!resolved.empty()) {
                    resolved_arr.push_back(Value(resolved));
                    item.resolved_pwad_paths.push_back(resolved);
                } else {
                    resolved_arr.push_back(Value(raw_pwad));
                    item.assets_found = false;
                    item.missing_assets.push_back(raw_pwad);
                }
            }
            merged_scope["PWADS"] = Value(resolved_arr);
        }

        // Evaluate custom template variables (such as custom CMD if defined)
        for (int pass = 0; pass < 3; ++pass) {
            bool any_changed = false;
            Scope current_scope = merged_scope;
            for (const auto& [k, v] : current_scope) {
                if (v.type == Value::Type::String) {
                    std::string rendered = TemplateEngine::render(v.str_val, merged_scope);
                    if (rendered != v.str_val) {
                        merged_scope[k] = Value(rendered);
                        any_changed = true;
                    }
                }
            }
            if (!any_changed) break;
        }

        if (merged_scope.find("CMD") != merged_scope.end()) {
            item.cmd = merged_scope["CMD"].to_string();
        }

        // Engine matching and dynamic CMD generation
        std::vector<std::string> req_caps = mod.requires_capability;
        if (req_caps.empty()) {
            for (const auto& dep_name : ordered_deps) {
                const Module* dep_mod = get_module(dep_name);
                if (dep_mod && !dep_mod->requires_capability.empty()) {
                    req_caps = dep_mod->requires_capability;
                    break;
                }
            }
        }
        if (req_caps.empty()) {
            req_caps.push_back("vanilla");
        }
        item.compatible_engines = engine_registry.find_compatible_engines(req_caps, true);

        // Check if there is a saved engine preference for this module, or fallback to default engine
        bool engine_selected = false;
        auto saved_it = saved_engines.find(item.module_name);
        if (saved_it != saved_engines.end() && !item.compatible_engines.empty()) {
            engine_selected = item.select_engine(saved_it->second);
        }
        if (!engine_selected && !user_config.default_engine.empty() && !item.compatible_engines.empty()) {
            item.select_engine(user_config.default_engine);
        }

        // Determine GameStatus
        if (!item.assets_found) {
            item.status = GameStatus::MissingAssets;
            if (!item.missing_assets.empty()) {
                item.status_details = "Missing asset: " + item.missing_assets[0];
            } else {
                item.status_details = "Missing required game assets";
            }
        } else if (item.compatible_engines.empty() && item.cmd.empty()) {
            item.status = GameStatus::NoCompatibleEngine;
            item.status_details = "No compatible engine installed";
        } else {
            item.status = GameStatus::Ready;
            item.status_details.clear();
        }

        item.vars = merged_scope;

        if (!item.compatible_engines.empty()) {
            item.cmd = item.get_command();
            item.vars["CMD"] = Value(item.cmd);
        }

        items.push_back(item);
    }

    return items;
}

} // namespace doomlauncher
