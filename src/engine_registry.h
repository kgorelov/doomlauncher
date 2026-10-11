#ifndef ENGINE_REGISTRY_H
#define ENGINE_REGISTRY_H

#include "template_engine.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace doomlauncher {

struct EngineInfo {
    std::string name;
    std::string title;
    std::string binary;
    std::vector<std::string> binaries;
    std::vector<std::string> capabilities;
    std::string cmd_template;
    Scope vars;

    bool is_installed = false;
    std::string installed_binary_path;

    bool has_capability(const std::string& cap) const;
    bool supports_all(const std::vector<std::string>& required_caps) const;
};

class EngineRegistry {
public:
    EngineRegistry() = default;

    void register_engine(const EngineInfo& engine);
    const EngineInfo* get_engine(const std::string& name) const;
    EngineInfo* get_engine_mut(const std::string& name);
    bool has_engine(const std::string& name) const;

    const std::vector<EngineInfo>& get_all_engines() const;

    // Checks PATH and filesystem to see which engines are installed
    void detect_installed_engines();

    // Query engines matching required capabilities
    std::vector<EngineInfo> find_compatible_engines(
        const std::vector<std::string>& required_capabilities,
        bool only_installed = true
    ) const;

    // Static utilities for binary detection
    static std::string find_binary(const std::string& binary_name);
    static bool is_binary_available(const std::string& binary_name);

    // Load engine modules from TOML
    bool load_engine_file(const std::string& filepath);
    bool load_engine_directory(const std::string& dirpath);

private:
    std::vector<EngineInfo> engines;
    std::unordered_map<std::string, size_t> engine_index_by_name;
};

} // namespace doomlauncher

#endif // ENGINE_REGISTRY_H
