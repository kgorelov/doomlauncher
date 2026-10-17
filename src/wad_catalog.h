#ifndef WAD_CATALOG_H
#define WAD_CATALOG_H

#include "asset_resolver.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace doomlauncher {

struct WadFile {
    std::string path;
    std::string filename;
    std::string stem_lower;
    std::string sha1;
    uintmax_t file_size = 0;
    int64_t mtime = 0;
    bool is_iwad_header = false;
};

struct WadCacheEntry {
    uintmax_t file_size = 0;
    int64_t mtime = 0;
    std::string sha1;
};

class WadCatalog {
public:
    WadCatalog() = default;

    void clear();

    // Scans directories for .wad, .pk3, .zip files
    void scan_directory(const std::string& dirpath, bool recursive_one_level = true);
    void scan_directories(const std::vector<std::string>& dirs, bool recursive_one_level = true);
    void scan_from_config(const UserConfig& config, bool include_system_defaults = true);

    // Indexes a single WAD file, computing SHA-1 (or using cached SHA-1 if size+mtime match)
    const WadFile* add_wad_file(const std::string& filepath, const std::string& alias = "");

    // Lookups
    const WadFile* find_by_sha1(const std::string& sha1) const;
    const WadFile* find_by_filename(const std::string& filename) const;
    const WadFile* find_by_alias(const std::string& alias) const;
    const WadFile* find_by_path(const std::string& filepath) const;

    // Resolves by SHA-1 list first, then alias, then filename
    const WadFile* resolve_wad(const std::string& name_or_path, const std::vector<std::string>& sha1_list = {}) const;

    const std::vector<WadFile>& get_all_wads() const;

    // Returns known WADs sorted with doom2.wad and doom.wad prioritized at the top
    std::vector<WadFile> get_prioritized_dependency_wads() const;

    // SHA-1 cache persistence in state.toml ([wad_cache])
    bool load_cache(const std::string& state_filepath = "");
    bool save_cache(const std::string& state_filepath = "") const;
    const std::unordered_map<std::string, WadCacheEntry>& get_cache() const;

    // Static utilities
    static std::string compute_bytes_sha1(const uint8_t* data, size_t len);
    static std::string compute_file_sha1(const std::string& filepath);
    static bool inspect_wad_header(const std::string& filepath, bool& out_is_iwad);
    static bool is_supported_wad_extension(const std::string& filename);

private:
    std::vector<WadFile> wads;
    std::unordered_map<std::string, size_t> index_by_path;
    std::unordered_map<std::string, size_t> index_by_sha1;
    std::unordered_map<std::string, size_t> index_by_filename_lower;
    std::unordered_map<std::string, size_t> index_by_stem_lower;
    std::unordered_map<std::string, size_t> index_by_alias_lower;
    std::unordered_map<std::string, WadCacheEntry> sha1_cache;
};

} // namespace doomlauncher

#endif // WAD_CATALOG_H
