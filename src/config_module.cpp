#include "config_module.h"
#include "toml.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
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

void parse_single_module_table(const toml::table& tbl, Module& mod, const std::string& module_name, const std::string& dir_path) {
    mod.name = module_name;
    mod.module_dir = dir_path;

    if (auto title_node = tbl.get("title")) {
        if (auto s = title_node->as_string()) {
            mod.title = std::string(s->get());
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

    if (auto inc_node = tbl.get("includes")) {
        if (auto arr = inc_node->as_array()) {
            for (const auto& elem : *arr) {
                if (auto s = elem.as_string()) {
                    mod.includes.push_back(std::string(s->get()));
                }
            }
        }
    }

    if (auto vars_tbl = tbl.get_as<toml::table>("vars")) {
        for (const auto& [key, val] : *vars_tbl) {
            mod.vars[std::string(key.str())] = parse_toml_node(val);
        }
    }

    if (auto tmpl_tbl = tbl.get_as<toml::table>("templates")) {
        for (const auto& [key, val] : *tmpl_tbl) {
            mod.vars[std::string(key.str())] = parse_toml_node(val);
        }
    }

    for (const auto& [key, val] : tbl) {
        std::string k_str(key.str());
        if (k_str == "name" || k_str == "title" || k_str == "is_menu_item" || k_str == "menu_item" ||
            k_str == "requires" || k_str == "includes" || k_str == "vars" || k_str == "templates" || k_str == "modules") {
            continue;
        }

        mod.vars[k_str] = parse_toml_node(val);
    }
}

std::string expand_module_dir(const std::string& text, const std::string& module_dir) {
    std::string result = text;
    std::string keys[2] = {"{{MODULE_DIR}}", "{{MODULEDIR}}"};
    for (const auto& k : keys) {
        size_t pos = 0;
        while ((pos = result.find(k, pos)) != std::string::npos) {
            result.replace(pos, k.length(), module_dir);
            pos += module_dir.length();
        }
    }
    return result;
}

Value expand_value_module_dir(const Value& val, const std::string& module_dir) {
    if (val.type == Value::Type::String) {
        return Value(expand_module_dir(val.str_val, module_dir));
    } else if (val.type == Value::Type::Array) {
        Array res_arr;
        for (const auto& elem : val.arr_val) {
            res_arr.push_back(expand_value_module_dir(elem, module_dir));
        }
        return Value(res_arr);
    }
    return val;
}

} // namespace

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

    for (const auto& [key, val] : root) {
        if (auto mod_tbl = val.as_table()) {
            Module mod;
            parse_single_module_table(*mod_tbl, mod, std::string(key.str()), dir_path);
            add_module(mod);
            loaded_modules_in_file.push_back(mod);
        }
    }

    if (loaded_modules_in_file.empty()) {
        Module mod;
        parse_single_module_table(root, mod, abs_p.stem().string(), dir_path);
        add_module(mod);
        loaded_modules_in_file.push_back(mod);
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
    fs::path p(dirpath);
    if (!fs::exists(p) || !fs::is_directory(p)) {
        return false;
    }

    std::string pattern = (p / "*.toml").string();
    std::vector<std::string> files = expand_glob_pattern(pattern);
    bool success = true;
    for (const auto& file : files) {
        if (!load_file(file)) {
            success = false;
        }
    }
    return success;
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
        if (mod.title.empty()) {
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

        for (const auto& dep_name : ordered_deps) {
            const Module* dep_mod = get_module(dep_name);
            if (!dep_mod) continue;

            Scope dir_scope;
            dir_scope["MODULE_DIR"] = Value(dep_mod->module_dir);
            dir_scope["MODULEDIR"] = Value(dep_mod->module_dir);
            dir_scope["MODULE_NAME"] = Value(dep_mod->name);
            dir_scope["MODULENAME"] = Value(dep_mod->name);
            dir_scope[dep_mod->name + ".MODULE_DIR"] = Value(dep_mod->module_dir);
            dir_scope[dep_mod->name + ".MODULEDIR"] = Value(dep_mod->module_dir);

            for (const auto& [k, v] : dep_mod->vars) {
                merged_scope[k] = expand_value_module_dir(v, dep_mod->module_dir);
            }
        }

        // Target module overrides
        merged_scope["MODULE_DIR"] = Value(mod.module_dir);
        merged_scope["MODULEDIR"] = Value(mod.module_dir);
        merged_scope["MODULE_NAME"] = Value(mod.name);
        merged_scope["MODULENAME"] = Value(mod.name);

        ResolvedMenuItem item;
        item.module_name = mod.name;
        item.title = mod.title;

        for (int pass = 0; pass < 5; ++pass) {
            bool any_changed = false;
            Scope current_scope = merged_scope;
            for (const auto& [k, v] : current_scope) {
                if (v.type == Value::Type::String) {
                    std::string rendered = TemplateEngine::render(v.str_val, merged_scope);
                    if (rendered != v.str_val) {
                        merged_scope[k] = Value(rendered);
                        any_changed = true;
                    }
                } else if (v.type == Value::Type::Array) {
                    Array rendered_arr;
                    bool arr_changed = false;
                    for (const auto& elem : v.arr_val) {
                        if (elem.type == Value::Type::String) {
                            std::string rendered = TemplateEngine::render(elem.str_val, merged_scope);
                            if (rendered != elem.str_val) arr_changed = true;
                            rendered_arr.push_back(Value(rendered));
                        } else {
                            rendered_arr.push_back(elem);
                        }
                    }
                    if (arr_changed) {
                        merged_scope[k] = Value(rendered_arr);
                        any_changed = true;
                    }
                }
            }
            if (!any_changed) break;
        }

        if (merged_scope.find("CMD") != merged_scope.end()) {
            item.cmd = merged_scope["CMD"].to_string();
        }

        item.vars = merged_scope;
        items.push_back(item);
    }

    return items;
}

} // namespace doomlauncher
