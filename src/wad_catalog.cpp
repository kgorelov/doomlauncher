#include "wad_catalog.h"
#include "config_module.h"
#include "toml.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

inline uint32_t rotl32(uint32_t value, unsigned int count) {
    return (value << count) | (value >> (32 - count));
}

class Sha1Context {
public:
    Sha1Context() {
        reset();
    }

    void reset() {
        h0 = 0x67452301;
        h1 = 0xEFCDAB89;
        h2 = 0x98BADCFE;
        h3 = 0x10325476;
        h4 = 0xC3D2E1F0;
        total_bits = 0;
        buffer_len = 0;
    }

    void update(const uint8_t* data, size_t len) {
        total_bits += static_cast<uint64_t>(len) * 8;
        size_t offset = 0;

        if (buffer_len > 0) {
            size_t copy_len = std::min(len, 64 - buffer_len);
            std::memcpy(buffer + buffer_len, data, copy_len);
            buffer_len += copy_len;
            offset += copy_len;
            if (buffer_len == 64) {
                process_block(buffer);
                buffer_len = 0;
            }
        }

        while (offset + 64 <= len) {
            process_block(data + offset);
            offset += 64;
        }

        if (offset < len) {
            buffer_len = len - offset;
            std::memcpy(buffer, data + offset, buffer_len);
        }
    }

    std::string finalize_hex() {
        uint8_t pad[64];
        std::memset(pad, 0, sizeof(pad));
        pad[0] = 0x80;

        uint64_t bits_Snapshot = total_bits;
        size_t pad_len = (buffer_len < 56) ? (56 - buffer_len) : (120 - buffer_len);
        update(pad, pad_len);

        uint8_t length_bytes[8];
        for (int i = 0; i < 8; ++i) {
            length_bytes[7 - i] = static_cast<uint8_t>((bits_Snapshot >> (i * 8)) & 0xFF);
        }
        update(length_bytes, 8);

        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::nouppercase
            << std::setw(8) << h0
            << std::setw(8) << h1
            << std::setw(8) << h2
            << std::setw(8) << h3
            << std::setw(8) << h4;
        return oss.str();
    }

private:
    uint32_t h0, h1, h2, h3, h4;
    uint64_t total_bits;
    uint8_t buffer[64];
    size_t buffer_len;

    void process_block(const uint8_t* block) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
};

int64_t get_file_mtime_seconds(const fs::path& p) {
    std::error_code ec;
    auto ftime = fs::last_write_time(p, ec);
    if (ec) return 0;
    auto dur = ftime.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(dur).count();
}

} // namespace

std::string WadCatalog::compute_bytes_sha1(const uint8_t* data, size_t len) {
    Sha1Context ctx;
    ctx.update(data, len);
    return ctx.finalize_hex();
}

std::string WadCatalog::compute_file_sha1(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    Sha1Context ctx;
    uint8_t buf[65536];
    while (file.good()) {
        file.read(reinterpret_cast<char*>(buf), sizeof(buf));
        std::streamsize bytes_read = file.gcount();
        if (bytes_read > 0) {
            ctx.update(buf, static_cast<size_t>(bytes_read));
        }
    }
    return ctx.finalize_hex();
}

bool WadCatalog::inspect_wad_header(const std::string& filepath, bool& out_is_iwad) {
    out_is_iwad = false;
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    char magic[4] = {0, 0, 0, 0};
    file.read(magic, 4);
    if (file.gcount() < 4) {
        return false;
    }

    if (std::memcmp(magic, "IWAD", 4) == 0) {
        out_is_iwad = true;
        return true;
    }
    if (std::memcmp(magic, "PWAD", 4) == 0) {
        out_is_iwad = false;
        return true;
    }
    return false;
}

bool WadCatalog::is_supported_wad_extension(const std::string& filename) {
    std::string ext = to_lower(fs::path(filename).extension().string());
    return ext == ".wad" || ext == ".pk3" || ext == ".zip";
}

void WadCatalog::clear() {
    wads.clear();
    index_by_path.clear();
    index_by_sha1.clear();
    index_by_filename_lower.clear();
    index_by_stem_lower.clear();
    index_by_alias_lower.clear();
}

const WadFile* WadCatalog::add_wad_file(const std::string& filepath, const std::string& alias) {
    if (filepath.empty()) return nullptr;

    std::string expanded = AssetResolver::expand_path(filepath);
    std::error_code ec;
    fs::path p(expanded);
    if (!fs::exists(p, ec) || fs::is_directory(p, ec)) {
        return nullptr;
    }

    fs::path norm_path = fs::absolute(p, ec).lexically_normal();
    std::string path_str = norm_path.string();

    auto existing_it = index_by_path.find(path_str);
    if (existing_it != index_by_path.end()) {
        if (!alias.empty()) {
            index_by_alias_lower[to_lower(alias)] = existing_it->second;
        }
        return &wads[existing_it->second];
    }

    WadFile wf;
    wf.path = path_str;
    wf.filename = norm_path.filename().string();
    wf.stem_lower = to_lower(norm_path.stem().string());
    wf.file_size = fs::file_size(norm_path, ec);
    if (ec) wf.file_size = 0;
    wf.mtime = get_file_mtime_seconds(norm_path);
    inspect_wad_header(path_str, wf.is_iwad_header);

    // Check SHA-1 cache first
    auto cache_it = sha1_cache.find(path_str);
    if (cache_it != sha1_cache.end() &&
        cache_it->second.file_size == wf.file_size &&
        cache_it->second.mtime == wf.mtime &&
        cache_it->second.sha1.size() == 40) {
        wf.sha1 = cache_it->second.sha1;
    } else {
        wf.sha1 = compute_file_sha1(path_str);
        if (!wf.sha1.empty()) {
            WadCacheEntry entry;
            entry.file_size = wf.file_size;
            entry.mtime = wf.mtime;
            entry.sha1 = wf.sha1;
            sha1_cache[path_str] = entry;
        }
    }

    size_t idx = wads.size();
    wads.push_back(wf);

    index_by_path[wf.path] = idx;
    if (!wf.sha1.empty()) {
        // Keep first discovered path for a given SHA-1 if duplicates exist
        if (index_by_sha1.find(wf.sha1) == index_by_sha1.end()) {
            index_by_sha1[wf.sha1] = idx;
        }
    }

    std::string lower_fn = to_lower(wf.filename);
    if (index_by_filename_lower.find(lower_fn) == index_by_filename_lower.end()) {
        index_by_filename_lower[lower_fn] = idx;
    }
    if (index_by_stem_lower.find(wf.stem_lower) == index_by_stem_lower.end()) {
        index_by_stem_lower[wf.stem_lower] = idx;
    }
    if (!alias.empty()) {
        index_by_alias_lower[to_lower(alias)] = idx;
    }

    return &wads[idx];
}

void WadCatalog::scan_directory(const std::string& dirpath, bool recursive_one_level) {
    if (dirpath.empty()) return;
    std::string expanded = AssetResolver::expand_path(dirpath);
    std::error_code ec;
    fs::path dir_path(expanded);

    if (!fs::exists(dir_path, ec) || !fs::is_directory(dir_path, ec)) {
        return;
    }

    std::vector<fs::path> regular_files;
    std::vector<fs::path> subdirs;

    for (const auto& entry : fs::directory_iterator(dir_path, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec)) {
            if (is_supported_wad_extension(entry.path().filename().string())) {
                regular_files.push_back(entry.path());
            }
        } else if (recursive_one_level && entry.is_directory(ec)) {
            subdirs.push_back(entry.path());
        }
    }

    std::sort(regular_files.begin(), regular_files.end());
    for (const auto& f : regular_files) {
        add_wad_file(f.string());
    }

    if (recursive_one_level) {
        std::sort(subdirs.begin(), subdirs.end());
        for (const auto& sub : subdirs) {
            scan_directory(sub.string(), false);
        }
    }
}

void WadCatalog::scan_directories(const std::vector<std::string>& dirs, bool recursive_one_level) {
    for (const auto& dir : dirs) {
        scan_directory(dir, recursive_one_level);
    }
}

void WadCatalog::scan_from_config(const UserConfig& config, bool include_system_defaults) {
    // 1. Explicit IWADs from [iwads] take highest precedence
    for (const auto& [alias, path] : config.iwads) {
        add_wad_file(path, alias);
    }

    // 2. User configured wad_dirs
    scan_directories(config.wad_dirs, true);

    // 3. Default system search paths
    if (include_system_defaults) {
        scan_directories(AssetResolver::get_default_system_search_paths(), true);
    }
}

const WadFile* WadCatalog::find_by_sha1(const std::string& sha1) const {
    if (sha1.empty()) return nullptr;
    auto it = index_by_sha1.find(to_lower(sha1));
    if (it != index_by_sha1.end()) {
        return &wads[it->second];
    }
    return nullptr;
}

const WadFile* WadCatalog::find_by_filename(const std::string& filename) const {
    if (filename.empty()) return nullptr;
    fs::path p(filename);
    std::string lower_fn = to_lower(p.filename().string());

    auto it_fn = index_by_filename_lower.find(lower_fn);
    if (it_fn != index_by_filename_lower.end()) {
        return &wads[it_fn->second];
    }

    std::string lower_stem = to_lower(p.stem().string());
    auto it_stem = index_by_stem_lower.find(lower_stem);
    if (it_stem != index_by_stem_lower.end()) {
        return &wads[it_stem->second];
    }

    return nullptr;
}

const WadFile* WadCatalog::find_by_alias(const std::string& alias) const {
    if (alias.empty()) return nullptr;
    std::string lower = to_lower(alias);
    auto it = index_by_alias_lower.find(lower);
    if (it != index_by_alias_lower.end()) {
        return &wads[it->second];
    }

    std::string stem = to_lower(fs::path(alias).stem().string());
    auto it_stem = index_by_alias_lower.find(stem);
    if (it_stem != index_by_alias_lower.end()) {
        return &wads[it_stem->second];
    }

    return nullptr;
}

const WadFile* WadCatalog::find_by_path(const std::string& filepath) const {
    if (filepath.empty()) return nullptr;
    std::error_code ec;
    fs::path p(AssetResolver::expand_path(filepath));
    std::string norm = fs::absolute(p, ec).lexically_normal().string();
    auto it = index_by_path.find(norm);
    if (it != index_by_path.end()) {
        return &wads[it->second];
    }
    return nullptr;
}

const WadFile* WadCatalog::resolve_wad(const std::string& name_or_path, const std::vector<std::string>& sha1_list) const {
    for (const auto& h : sha1_list) {
        if (const WadFile* found = find_by_sha1(h)) {
            return found;
        }
    }

    if (name_or_path.empty()) return nullptr;

    if (const WadFile* by_path = find_by_path(name_or_path)) {
        return by_path;
    }
    if (const WadFile* by_alias = find_by_alias(name_or_path)) {
        return by_alias;
    }
    if (const WadFile* by_fn = find_by_filename(name_or_path)) {
        return by_fn;
    }
    return nullptr;
}

const std::vector<WadFile>& WadCatalog::get_all_wads() const {
    return wads;
}

std::vector<WadFile> WadCatalog::get_prioritized_dependency_wads() const {
    std::vector<WadFile> sorted = wads;

    auto get_priority = [](const WadFile& w) -> int {
        if (w.stem_lower == "doom2") return 0;
        if (w.stem_lower == "doom" || w.stem_lower == "doom1") return 1;
        if (w.stem_lower == "plutonia" || w.stem_lower == "tnt" ||
            w.stem_lower == "freedoom2" || w.stem_lower == "freedoom1" ||
            w.stem_lower == "heretic" || w.stem_lower == "hexen") {
            return 2;
        }
        if (w.is_iwad_header) return 3;
        return 4;
    };

    std::sort(sorted.begin(), sorted.end(), [&](const WadFile& a, const WadFile& b) {
        int pa = get_priority(a);
        int pb = get_priority(b);
        if (pa != pb) return pa < pb;
        return to_lower(a.filename) < to_lower(b.filename);
    });

    return sorted;
}

bool WadCatalog::load_cache(const std::string& state_filepath) {
    std::string path_str = state_filepath.empty() ? ModuleRegistry::get_default_state_path() : state_filepath;
    std::string resolved_path = AssetResolver::expand_path(path_str);

    std::error_code ec;
    if (!fs::exists(resolved_path, ec)) {
        return false;
    }

    try {
        toml::table tbl = toml::parse_file(resolved_path);
        if (auto cache_tbl = tbl.get_as<toml::table>("wad_cache")) {
            for (const auto& [key, val] : *cache_tbl) {
                if (auto entry_tbl = val.as_table()) {
                    WadCacheEntry entry;
                    if (auto sz = entry_tbl->get_as<int64_t>("size")) {
                        entry.file_size = static_cast<uintmax_t>(sz->get());
                    }
                    if (auto mt = entry_tbl->get_as<int64_t>("mtime")) {
                        entry.mtime = mt->get();
                    }
                    if (auto sh = entry_tbl->get_as<std::string>("sha1")) {
                        entry.sha1 = to_lower(std::string(sh->get()));
                    }
                    if (entry.sha1.size() == 40) {
                        sha1_cache[std::string(key.str())] = entry;
                    }
                }
            }
        }
        return true;
    } catch (const toml::parse_error& err) {
        std::cerr << "Warning: Error parsing wad_cache from state file '" << resolved_path << "': " << err << std::endl;
        return false;
    }
}

bool WadCatalog::save_cache(const std::string& state_filepath) const {
    std::string path_str = state_filepath.empty() ? ModuleRegistry::get_default_state_path() : state_filepath;
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

        toml::table cache_tbl;
        for (const auto& [path_key, entry] : sha1_cache) {
            if (entry.sha1.size() != 40) continue;
            toml::table entry_tbl;
            entry_tbl.insert_or_assign("size", static_cast<int64_t>(entry.file_size));
            entry_tbl.insert_or_assign("mtime", entry.mtime);
            entry_tbl.insert_or_assign("sha1", entry.sha1);
            cache_tbl.insert_or_assign(path_key, entry_tbl);
        }
        tbl.insert_or_assign("wad_cache", cache_tbl);

        std::ofstream out(resolved_path);
        if (!out.is_open()) {
            return false;
        }
        out << tbl << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to save wad_cache to '" << resolved_path << "': " << e.what() << std::endl;
        return false;
    }
}

const std::unordered_map<std::string, WadCacheEntry>& WadCatalog::get_cache() const {
    return sha1_cache;
}

} // namespace doomlauncher
