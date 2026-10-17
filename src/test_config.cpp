#include "config_module.h"
#include "menu.h"
#include "template_engine.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace doomlauncher;

void test_template_engine() {
    std::cout << "--- Running TemplateEngine tests ---" << std::endl;

    Scope scope;
    scope["ENGINE_BIN"] = Value("gzdoom");
    scope["IWAD"] = Value("/usr/share/doom/doom2.wad");
    scope["COMPAT"] = Value(2);
    scope["IS_ULTIMATE"] = Value(true);
    scope["HAS_MODS"] = Value(true);
    scope["PWADS"] = Value(Array{Value("brutal.pk3"), Value("metal.wad")});

    // Simple variable interpolation
    std::string result = TemplateEngine::render("{{ENGINE_BIN}} -iwad {{IWAD}}", scope);
    std::cout << "Test Interpolation: " << result << std::endl;
    assert(result == "gzdoom -iwad /usr/share/doom/doom2.wad");

    // Conditionals
    std::string cond_test = TemplateEngine::render(
        "{{ENGINE_BIN}} -iwad {{IWAD}}{% if COMPAT == 2 %} -complevel 2{% endif %}{% if IS_ULTIMATE %} -ultimate{% endif %}",
        scope
    );
    std::cout << "Test Conditionals: " << cond_test << std::endl;
    assert(cond_test == "gzdoom -iwad /usr/share/doom/doom2.wad -complevel 2 -ultimate");

    // Loop test
    std::string loop_test = TemplateEngine::render(
        "{{ENGINE_BIN}} -iwad {{IWAD}}{% if HAS_MODS %}{% for file in PWADS %} -file {{file}}{% endfor %}{% endif %}",
        scope
    );
    std::cout << "Test Loop: " << loop_test << std::endl;
    assert(loop_test == "gzdoom -iwad /usr/share/doom/doom2.wad -file brutal.pk3 -file metal.wad");

    std::cout << "TemplateEngine tests passed!" << std::endl << std::endl;
}

void test_module_registry() {
    std::cout << "--- Running ModuleRegistry & TOML tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path temp_wads = "temp_mod_reg_wads";
    fs::create_directories(temp_wads);
    {
        std::ofstream f(temp_wads / "DOOM2.WAD");
        f << "IWAD";
    }
    {
        std::ofstream f(temp_wads / "PLUTONIA.WAD");
        f << "PWAD";
    }

    // Create temporary test files
    {
        std::ofstream boom_toml("test_boom.toml");
        boom_toml << R"(
[boom]
type = "engine"
title = "Boom"
binary = "sh"
capabilities = ["vanilla", "boom"]
cmd = "{{BIN}} -iwad {{IWAD}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %} -savedir {{SAVEDIR}}"
)";
    }

    {
        std::ofstream doom2_toml("test_doom2.toml");
        doom2_toml << R"(
[doom2]
type = "game"
title = "Doom II: Hell on Earth"
iwad = "DOOM2.WAD"
compat_level = 2
)";
    }

    {
        std::ofstream plutonia_toml("test_plutonia.toml");
        plutonia_toml << R"(
[plutonia]
type = "game"
title = "Final Doom: Plutonia Experiment"
base_game = "doom2"
pwads = ["PLUTONIA.WAD"]
)";
    }

    ModuleRegistry registry;
    assert(registry.load_file("test_boom.toml"));
    assert(registry.load_file("test_doom2.toml"));
    assert(registry.load_file("test_plutonia.toml"));

    // Before WAD discovery, no modules are active
    assert(registry.resolve_menu_items().empty());

    // Discover WADs -> both modules activate
    registry.get_wad_catalog().scan_directory(temp_wads.string());
    std::vector<ResolvedMenuItem> menu_items = registry.resolve_menu_items();

    std::cout << "Resolved Menu Items Count: " << menu_items.size() << std::endl;
    for (const auto& item : menu_items) {
        std::cout << "Menu Item: '" << item.title << "' (" << item.module_name << ")" << std::endl;
        std::cout << "  CMD: " << item.cmd << std::endl;
        assert(item.save_dir == ModuleRegistry::get_module_save_dir(item.module_name));
        assert(item.cmd.find(item.save_dir) != std::string::npos);
    }

    assert(menu_items.size() == 2); // doom2 and plutonia

    // Clean up temporary test files
    fs::remove_all(temp_wads);
    std::remove("test_boom.toml");
    std::remove("test_doom2.toml");
    std::remove("test_plutonia.toml");

    std::cout << "ModuleRegistry tests passed!" << std::endl << std::endl;
}

void test_real_modules() {
    std::cout << "--- Testing modules/main.toml ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path temp_wads = "temp_real_mod_wads";
    fs::create_directories(temp_wads);
    for (const char* w : {"DOOM.WAD", "DOOM2.WAD", "TNT.WAD", "PLUTONIA.WAD", "SIGIL.WAD"}) {
        std::ofstream f(temp_wads / w);
        f << "IWAD";
    }

    ModuleRegistry registry;
    bool loaded = registry.load_file("modules/main.toml");
    if (!loaded) {
        loaded = registry.load_file("../modules/main.toml");
    }
    assert(loaded);

    registry.get_wad_catalog().scan_directory(temp_wads.string());

    std::vector<ResolvedMenuItem> menu_items = registry.resolve_menu_items();
    std::cout << "Loaded real modules count: " << menu_items.size() << std::endl;
    for (const auto& item : menu_items) {
        std::cout << "Menu Item: '" << item.title << "' (" << item.module_name << ")" << std::endl;
        std::cout << "  CMD: " << item.cmd << std::endl;
        assert(!item.cmd.empty());
        assert(item.save_dir == ModuleRegistry::get_module_save_dir(item.module_name));
        assert(item.cmd.find(item.save_dir) != std::string::npos);
    }
    assert(menu_items.size() == 5);

    fs::remove_all(temp_wads);
    std::cout << "Real modules test passed!" << std::endl << std::endl;
}

void test_asset_resolver() {
    std::cout << "--- Running AssetResolver & UserConfig tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path temp_dir = "temp_test_assets";
    fs::create_directories(temp_dir / "sub");

    // Create a mock DOOM2.WAD (uppercase)
    {
        std::ofstream f(temp_dir / "DOOM2.WAD");
        f << "IWAD";
    }

    // Create a mock mod in subfolder
    {
        std::ofstream f(temp_dir / "sub" / "Eviternity.wad");
        f << "PWAD";
    }

    AssetResolver resolver;
    resolver.add_search_dir(temp_dir.string());

    // 1. Case-insensitive lookup in root dir
    std::string found_doom2 = resolver.resolve("doom2.wad");
    std::cout << "Resolved doom2.wad: " << found_doom2 << std::endl;
    assert(!found_doom2.empty());
    assert(fs::path(found_doom2).filename().string() == "DOOM2.WAD");

    // 2. Case-insensitive lookup in subfolder (recursive)
    std::string found_mod = resolver.resolve("EVITERNITY.WAD");
    std::cout << "Resolved EVITERNITY.WAD: " << found_mod << std::endl;
    assert(!found_mod.empty());
    assert(fs::path(found_mod).filename().string() == "Eviternity.wad");

    // 3. Missing file returns empty string
    std::string not_found = resolver.resolve("NONEXISTENT.WAD");
    assert(not_found.empty());

    // 4. Test IWAD aliases
    resolver.set_iwad_alias("doom", (temp_dir / "DOOM2.WAD").string());
    std::string found_alias = resolver.resolve("doom");
    assert(!found_alias.empty());

    // 5. Test UserConfig TOML loading & saving with module_dirs array
    {
        std::ofstream cfg("temp_config.toml");
        cfg << R"(
[paths]
modules = ["/usr/share/doomlauncher/modules", "~/.config/doomlauncher/modules"]
wad_dirs = ["temp_test_assets"]

[preferences]
default_engine = "dsda-doom"

[iwads]
doom2 = "temp_test_assets/DOOM2.WAD"
)";
    }

    UserConfig user_cfg = AssetResolver::load_config("temp_config.toml");
    assert(user_cfg.module_dirs.size() == 2);
    assert(user_cfg.module_dirs[0] == "/usr/share/doomlauncher/modules");
    assert(user_cfg.module_dirs[1] == "~/.config/doomlauncher/modules");
    assert(user_cfg.wad_dirs.size() == 1);
    assert(user_cfg.wad_dirs[0] == "temp_test_assets");
    assert(user_cfg.default_engine == "dsda-doom");
    assert(user_cfg.iwads.find("doom2") != user_cfg.iwads.end());

    // Test saving config and reloading
    user_cfg.iwads["plutonia"] = "temp_test_assets/PLUTONIA.WAD";
    assert(AssetResolver::save_config(user_cfg, "temp_config_saved.toml"));
    UserConfig reloaded_cfg = AssetResolver::load_config("temp_config_saved.toml");
    assert(reloaded_cfg.module_dirs == user_cfg.module_dirs);
    assert(reloaded_cfg.wad_dirs == user_cfg.wad_dirs);
    assert(reloaded_cfg.default_engine == "dsda-doom");
    assert(reloaded_cfg.iwads.at("plutonia") == "temp_test_assets/PLUTONIA.WAD");

    // Clean up
    fs::remove_all(temp_dir);
    fs::remove("temp_config.toml");
    fs::remove("temp_config_saved.toml");

    std::cout << "AssetResolver tests passed!" << std::endl << std::endl;
}

void test_module_registry_asset_resolution() {
    std::cout << "--- Running ModuleRegistry Asset Resolution tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path temp_dir = "temp_reg_assets";
    fs::create_directories(temp_dir);

    {
        std::ofstream f(temp_dir / "DOOM2.WAD");
        f << "IWAD";
    }

    // Engine module
    {
        std::ofstream f("test_eng.toml");
        f << R"(
[eng]
type = "engine"
title = "Test Engine"
binary = "sh"
capabilities = ["vanilla"]
cmd = "{{BIN}} -iwad {{IWAD}} {% if PWADS %}-file {{PWADS[0]}}{% endif %}"
)";
    }

    // Game module using logical filename "doom2.wad" (lowercase, whereas file on disk is DOOM2.WAD)
    {
        std::ofstream f("test_game.toml");
        f << R"(
[game1]
type = "game"
title = "Test Game Found"
iwad = "doom2.wad"
)";
    }

    // Game module with missing asset
    {
        std::ofstream f("test_missing.toml");
        f << R"(
[game2]
type = "game"
title = "Test Game Missing"
iwad = "missing_iwad.wad"
)";
    }

    ModuleRegistry registry;
    registry.get_asset_resolver().add_search_dir(temp_dir.string());

    assert(registry.load_file("test_eng.toml"));
    assert(registry.load_file("test_game.toml"));
    assert(registry.load_file("test_missing.toml"));

    // Default resolve_menu_items() returns ONLY active (satisfied) modules
    std::vector<ResolvedMenuItem> active_items = registry.resolve_menu_items();
    assert(active_items.size() == 1);
    assert(active_items[0].module_name == "game1");

    // resolve_menu_items(false) includes inactive modules for inspection
    std::vector<ResolvedMenuItem> items = registry.resolve_menu_items(false);
    assert(items.size() == 2);

    for (const auto& item : items) {
        if (item.module_name == "game1") {
            assert(item.assets_found == true);
            assert(item.missing_assets.empty());
            assert(!item.resolved_iwad_path.empty());
            // Verify that CMD has the resolved path!
            assert(item.cmd.find(item.resolved_iwad_path) != std::string::npos);
            std::cout << "Game1 resolved CMD: " << item.cmd << std::endl;
        } else if (item.module_name == "game2") {
            assert(item.assets_found == false);
            assert(!item.missing_assets.empty());
            assert(item.missing_assets[0] == "missing_iwad.wad");
            std::cout << "Game2 missing asset correctly flagged: " << item.missing_assets[0] << std::endl;
        }
    }

    // Clean up
    fs::remove_all(temp_dir);
    fs::remove("test_eng.toml");
    fs::remove("test_game.toml");
    fs::remove("test_missing.toml");

    std::cout << "ModuleRegistry Asset Resolution tests passed!" << std::endl << std::endl;
}

void test_engine_registry() {
    std::cout << "--- Running EngineRegistry tests ---" << std::endl;

    // 1. Capability matching
    EngineInfo choco;
    choco.name = "chocolate-doom";
    choco.capabilities = {"vanilla"};

    EngineInfo dsda;
    dsda.name = "dsda-doom";
    dsda.capabilities = {"vanilla", "limit-removing", "boom", "mbf", "mbf21", "complevel"};

    EngineInfo gz;
    gz.name = "gzdoom";
    gz.capabilities = {"vanilla", "boom", "mbf21", "gzdoom", "pk3"};

    assert(choco.supports_all({"vanilla"}));
    assert(!choco.supports_all({"boom"}));
    assert(!choco.supports_all({"gzdoom"}));

    assert(dsda.supports_all({"vanilla"}));
    assert(dsda.supports_all({"boom"}));
    assert(dsda.supports_all({"mbf21"}));
    assert(!dsda.supports_all({"gzdoom"}));

    assert(gz.supports_all({"vanilla"}));
    assert(gz.supports_all({"boom"}));
    assert(gz.supports_all({"gzdoom"}));

    // 2. Binary detection
    assert(EngineRegistry::is_binary_available("sh"));
    assert(!EngineRegistry::is_binary_available("nonexistent_binary_foo_bar_xyz"));

    // 3. TOML loading and detection
    {
        std::ofstream f("test_engine_sample.toml");
        f << R"(
[sample-eng]
type = "engine"
title = "Sample Engine"
binary = "sh"
capabilities = ["vanilla", "boom", "sample_feature"]
cmd = "{{BIN}} -iwad {{IWAD}}"
)";
    }

    EngineRegistry reg;
    assert(reg.load_engine_file("test_engine_sample.toml"));
    assert(reg.has_engine("sample-eng"));

    const EngineInfo* loaded_eng = reg.get_engine("sample-eng");
    assert(loaded_eng != nullptr);
    assert(loaded_eng->is_installed);
    assert(!loaded_eng->installed_binary_path.empty());
    assert(loaded_eng->has_capability("sample_feature"));

    auto matched = reg.find_compatible_engines({"sample_feature"}, true);
    assert(matched.size() == 1);
    assert(matched[0].name == "sample-eng");

    auto none_matched = reg.find_compatible_engines({"nonexistent_cap"}, true);
    assert(none_matched.empty());

    std::remove("test_engine_sample.toml");
    std::cout << "EngineRegistry tests passed!" << std::endl << std::endl;
}

void test_dynamic_engine_fallback() {
    std::cout << "--- Running Dynamic Engine Fallback tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path temp_dir = "temp_dyn_assets";
    fs::create_directories(temp_dir);

    {
        std::ofstream f(temp_dir / "DOOM2.WAD");
        f << "IWAD";
    }

    {
        std::ofstream f("test_dyn_engine.toml");
        f << R"(
[dyn-boom]
type = "engine"
title = "Dynamic Boom"
binary = "sh"
capabilities = ["vanilla", "boom"]
cmd = "{{BIN}} -iwad {{IWAD}} -complevel {{COMPAT_LEVEL}}"
)";
    }

    // Game has NO requires = ["..."], NO CMD template defined!
    {
        std::ofstream f("test_dyn_game.toml");
        f << R"(
[dyn_game]
title = "Dynamic Decoupled Game"
iwad = "doom2.wad"
compat_level = 2
requires_capability = ["vanilla"]
)";
    }

    ModuleRegistry registry;
    registry.get_asset_resolver().add_search_dir(temp_dir.string());

    assert(registry.load_file("test_dyn_engine.toml"));
    assert(registry.load_file("test_dyn_game.toml"));

    std::vector<ResolvedMenuItem> items = registry.resolve_menu_items();
    // Engine module should NOT be a menu item!
    assert(items.size() == 1);

    const auto& item = items[0];
    assert(item.module_name == "dyn_game");
    assert(item.assets_found);
    assert(!item.compatible_engines.empty());
    assert(item.compatible_engines[0].name == "dyn-boom");
    // Verify CMD was dynamically generated from dyn-boom!
    assert(!item.cmd.empty());
    assert(item.cmd.find("-complevel 2") != std::string::npos);
    std::cout << "Dynamically generated command: " << item.cmd << std::endl;

    fs::remove_all(temp_dir);
    std::remove("test_dyn_engine.toml");
    std::remove("test_dyn_game.toml");

    std::cout << "Dynamic Engine Fallback tests passed!" << std::endl << std::endl;
}

void test_game_module_modern_schema() {
    std::cout << "--- Running Game Module Modern Schema tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path temp_dir = "temp_schema_assets";
    fs::create_directories(temp_dir);
    {
        std::ofstream f(temp_dir / "DOOM2.WAD");
        f << "IWAD";
    }
    {
        std::ofstream f(temp_dir / "MOD.WAD");
        f << "PWAD";
    }

    // Engine definition
    {
        std::ofstream f("test_schema_engine.toml");
        f << R"(
[test-eng]
type = "engine"
title = "Test Engine"
binary = "sh"
capabilities = ["vanilla", "boom"]
cmd = "{{BIN}} -iwad {{IWAD}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %}"
)";
    }

    // Base game definition using modern lowercase keys: iwad, compat_level, requires_capability
    {
        std::ofstream f("test_schema_base.toml");
        f << R"(
[base_game]
type = "game"
title = "Base Game"
iwad = "DOOM2.WAD"
compat_level = 2
requires_capability = ["vanilla"]
)";
    }

    // Addon mod definition using base_game, pwads
    {
        std::ofstream f("test_schema_mod.toml");
        f << R"(
[addon_mod]
type = "game"
title = "Addon Mod"
base_game = "base_game"
pwads = ["MOD.WAD"]
)";
    }

    ModuleRegistry registry;
    registry.get_asset_resolver().add_search_dir(temp_dir.string());

    assert(registry.load_file("test_schema_engine.toml"));
    assert(registry.load_file("test_schema_base.toml"));
    assert(registry.load_file("test_schema_mod.toml"));

    std::vector<ResolvedMenuItem> items = registry.resolve_menu_items();
    assert(items.size() == 2); // Base Game and Addon Mod

    for (const auto& item : items) {
        if (item.module_name == "base_game") {
            assert(item.assets_found);
            assert(!item.cmd.empty());
            assert(item.cmd.find("-complevel 2") != std::string::npos);
            assert(item.cmd.find("DOOM2.WAD") != std::string::npos);
        } else if (item.module_name == "addon_mod") {
            assert(item.assets_found);
            assert(!item.cmd.empty());
            assert(item.cmd.find("DOOM2.WAD") != std::string::npos);
            assert(item.cmd.find("MOD.WAD") != std::string::npos);
            // Inherited compat_level 2 and capability vanilla from base_game
            assert(item.cmd.find("-complevel 2") != std::string::npos);
        }
    }

    fs::remove_all(temp_dir);
    std::remove("test_schema_engine.toml");
    std::remove("test_schema_base.toml");
    std::remove("test_schema_mod.toml");

    std::cout << "Game Module Modern Schema tests passed!" << std::endl << std::endl;
}

void test_dynamic_menu_item_binding() {
    std::cout << "--- Running Dynamic Menu Item Binding & Engine Options tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path temp_dir = "temp_p4_assets";
    fs::create_directories(temp_dir);
    {
        std::ofstream f(temp_dir / "DOOM2.WAD");
        f << "IWAD";
    }

    // Two engines
    {
        std::ofstream f("test_p4_eng1.toml");
        f << R"(
[eng1]
type = "engine"
title = "Engine One"
binary = "sh"
capabilities = ["vanilla"]
cmd = "{{BIN}} -iwad {{IWAD}} --engine1"
)";
    }

    {
        std::ofstream f("test_p4_eng2.toml");
        f << R"(
[eng2]
type = "engine"
title = "Engine Two"
binary = "sh"
capabilities = ["vanilla", "advanced"]
cmd = "{{BIN}} -iwad {{IWAD}} --engine2"
)";
    }

    // Three games: Ready, MissingAssets, and NoCompatibleEngine
    {
        std::ofstream f("test_p4_games.toml");
        f << R"(
[ready_game]
title = "Ready Game"
iwad = "DOOM2.WAD"
requires_capability = ["vanilla"]

[missing_game]
title = "Missing Asset Game"
iwad = "NONEXISTENT.WAD"
requires_capability = ["vanilla"]

[incompat_game]
title = "Incompatible Game"
iwad = "DOOM2.WAD"
requires_capability = ["quantum_feature"]
)";
    }

    ModuleRegistry registry;
    registry.get_asset_resolver().add_search_dir(temp_dir.string());

    assert(registry.load_file("test_p4_eng1.toml"));
    assert(registry.load_file("test_p4_eng2.toml"));
    assert(registry.load_file("test_p4_games.toml"));

    // Default resolve_menu_items() omits missing_game
    std::vector<ResolvedMenuItem> active_items = registry.resolve_menu_items();
    assert(active_items.size() == 2);

    std::vector<ResolvedMenuItem> items = registry.resolve_menu_items(false);
    assert(items.size() == 3);

    for (auto& item : items) {
        if (item.module_name == "ready_game") {
            assert(item.status == GameStatus::Ready);
            assert(item.compatible_engines.size() == 2);
            assert(item.selected_engine_index == 0);
            assert(item.cmd.find("--engine1") != std::string::npos);

            // Test cycling to next engine
            assert(item.select_next_engine());
            assert(item.selected_engine_index == 1);
            assert(item.cmd.find("--engine2") != std::string::npos);

            // Test cycling to previous engine
            assert(item.select_prev_engine());
            assert(item.selected_engine_index == 0);
            assert(item.cmd.find("--engine1") != std::string::npos);

            // Test direct engine selection by name
            assert(item.select_engine("eng2"));
            assert(item.selected_engine_index == 1);
            assert(item.cmd.find("--engine2") != std::string::npos);
            assert(!item.select_engine("nonexistent_engine"));
        } else if (item.module_name == "missing_game") {
            assert(item.status == GameStatus::MissingAssets);
            assert(!item.status_details.empty());
        } else if (item.module_name == "incompat_game") {
            assert(item.status == GameStatus::NoCompatibleEngine);
            assert(!item.status_details.empty());
        }
    }

    // Test UserConfig default_engine preference
    UserConfig user_cfg;
    user_cfg.wad_dirs = { temp_dir.string() };
    user_cfg.default_engine = "eng2";
    registry.set_user_config(user_cfg);
    std::vector<ResolvedMenuItem> pref_items = registry.resolve_menu_items();
    for (const auto& item : pref_items) {
        if (item.module_name == "ready_game") {
            assert(item.get_selected_engine() != nullptr);
            assert(item.get_selected_engine()->name == "eng2");
            assert(item.cmd.find("--engine2") != std::string::npos);
        }
    }

    fs::remove_all(temp_dir);
    std::remove("test_p4_eng1.toml");
    std::remove("test_p4_eng2.toml");
    std::remove("test_p4_games.toml");

    std::cout << "Dynamic Menu Item Binding & Engine Options tests passed!" << std::endl << std::endl;
}

void test_phase6_asset_resolver() {
    std::cout << "--- Running Phase 6 AssetResolver Advanced Tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path dir_a = "temp_p6_assets_a";
    fs::path dir_b = "temp_p6_assets_b";
    fs::path dir_c = "temp_p6_assets_c";

    fs::create_directories(dir_a);
    fs::create_directories(dir_b);
    fs::create_directories(dir_c / "sub");

    // 1. Finding files across multiple mock directories
    {
        std::ofstream f(dir_a / "P6_DOOM1.WAD");
        f << "IWAD_DOOM1";
    }
    {
        std::ofstream f(dir_a / "P6_CONFLICT.WAD");
        f << "DIR_A_VERSION";
    }

    {
        std::ofstream f(dir_b / "P6_DOOM2.WAD");
        f << "IWAD_DOOM2";
    }
    {
        std::ofstream f(dir_b / "P6_CONFLICT.WAD");
        f << "DIR_B_VERSION";
    }

    {
        std::ofstream f(dir_c / "P6_TNT.WAD");
        f << "IWAD_TNT";
    }
    {
        std::ofstream f(dir_c / "sub" / "P6_PLUTONIA.WAD");
        f << "IWAD_PLUTONIA";
    }

    // Files specifically for case-insensitivity tests
    {
        std::ofstream f(dir_a / "p6_nerve.wad");
        f << "NERVE";
    }
    {
        std::ofstream f(dir_b / "P6_MASTERLEVELS.WAD");
        f << "MASTER";
    }
    {
        std::ofstream f(dir_c / "P6_Sigil_II.wad");
        f << "SIGIL2";
    }

    AssetResolver resolver;
    // Add multiple mock directories in priority order: dir_a, dir_b, dir_c
    resolver.add_search_dir(dir_a.string());
    resolver.add_search_dir(dir_b.string());
    resolver.add_search_dir(dir_c.string());

    // Verify finding files across multiple directories
    std::string res_doom1 = resolver.resolve("p6_doom1.wad");
    assert(!res_doom1.empty());
    assert(fs::path(res_doom1).filename().string() == "P6_DOOM1.WAD");
    assert(res_doom1.find("temp_p6_assets_a") != std::string::npos);

    std::string res_doom2 = resolver.resolve("p6_doom2.wad");
    assert(!res_doom2.empty());
    assert(fs::path(res_doom2).filename().string() == "P6_DOOM2.WAD");
    assert(res_doom2.find("temp_p6_assets_b") != std::string::npos);

    std::string res_tnt = resolver.resolve("p6_tnt.wad");
    assert(!res_tnt.empty());
    assert(fs::path(res_tnt).filename().string() == "P6_TNT.WAD");
    assert(res_tnt.find("temp_p6_assets_c") != std::string::npos);

    // Verify resolving nested file in subdirectory
    std::string res_plut = resolver.resolve("p6_plutonia.wad");
    assert(!res_plut.empty());
    assert(fs::path(res_plut).filename().string() == "P6_PLUTONIA.WAD");
    assert(res_plut.find("temp_p6_assets_c/sub") != std::string::npos);

    // Verify directory precedence: P6_CONFLICT.WAD in dir_a must be preferred over dir_b
    std::string res_conflict = resolver.resolve("p6_conflict.wad");
    assert(!res_conflict.empty());
    assert(res_conflict.find("temp_p6_assets_a") != std::string::npos);
    {
        std::ifstream f(res_conflict);
        std::string content;
        f >> content;
        assert(content == "DIR_A_VERSION");
    }

    // 2. Case-insensitivity verification
    assert(!resolver.resolve("P6_NERVE.WAD").empty());
    assert(!resolver.resolve("P6_Nerve.Wad").empty());
    assert(!resolver.resolve("p6_nerve.wad").empty());
    assert(fs::path(resolver.resolve("P6_NERVE.WAD")).filename().string() == "p6_nerve.wad");

    assert(!resolver.resolve("p6_masterlevels.wad").empty());
    assert(!resolver.resolve("P6_MasterLevels.wad").empty());
    assert(!resolver.resolve("P6_MASTERLEVELS.WAD").empty());
    assert(fs::path(resolver.resolve("p6_masterlevels.wad")).filename().string() == "P6_MASTERLEVELS.WAD");

    assert(!resolver.resolve("p6_sigil_ii.wad").empty());
    assert(!resolver.resolve("P6_SIGIL_II.WAD").empty());
    assert(!resolver.resolve("p6_sIgIl_Ii.WaD").empty());
    assert(fs::path(resolver.resolve("P6_SIGIL_II.WAD")).filename().string() == "P6_Sigil_II.wad");

    assert(AssetResolver::iequals("DOOM2.WAD", "doom2.wad"));
    assert(AssetResolver::iequals("Doom2.Wad", "DOOM2.WAD"));
    assert(AssetResolver::iequals("nerve.wad", "NERVE.WAD"));
    assert(!AssetResolver::iequals("DOOM2.WAD", "DOOM1.WAD"));

    // 3. Reporting missing files gracefully without throwing
    try {
        std::string missing = resolver.resolve("definitely_nonexistent_99999.wad");
        assert(missing.empty());

        std::string empty_query = resolver.resolve("");
        assert(empty_query.empty());

        std::string non_dir_search = AssetResolver::find_file_in_dir("nonexistent_mock_folder_xyz_987", "file.wad");
        assert(non_dir_search.empty());
    } catch (...) {
        assert(false && "AssetResolver threw an exception on missing or invalid queries!");
    }

    fs::remove_all(dir_a);
    fs::remove_all(dir_b);
    fs::remove_all(dir_c);

    std::cout << "Phase 6 AssetResolver Advanced Tests passed!" << std::endl << std::endl;
}

void test_phase6_engine_capability_matching() {
    std::cout << "--- Running Phase 6 Engine Capability Matching Tests ---" << std::endl;

    EngineRegistry reg;

    // Simulate 4 engines installed on system, plus 1 uninstalled engine
    EngineInfo choco;
    choco.name = "chocolate-doom";
    choco.title = "Chocolate Doom";
    choco.binary = "chocolate-doom";
    choco.capabilities = {"vanilla"};
    choco.is_installed = true;
    choco.installed_binary_path = "/usr/bin/chocolate-doom";
    reg.register_engine(choco);

    EngineInfo boom;
    boom.name = "prboom-plus";
    boom.title = "PrBoom+";
    boom.binary = "prboom-plus";
    boom.capabilities = {"vanilla", "limit-removing", "boom", "complevel"};
    boom.is_installed = true;
    boom.installed_binary_path = "/usr/bin/prboom-plus";
    reg.register_engine(boom);

    EngineInfo dsda;
    dsda.name = "dsda-doom";
    dsda.title = "DSDA-Doom";
    dsda.binary = "dsda-doom";
    dsda.capabilities = {"vanilla", "limit-removing", "boom", "mbf", "mbf21", "complevel"};
    dsda.is_installed = true;
    dsda.installed_binary_path = "/usr/bin/dsda-doom";
    reg.register_engine(dsda);

    EngineInfo gz;
    gz.name = "gzdoom";
    gz.title = "GZDoom";
    gz.binary = "gzdoom";
    gz.capabilities = {"vanilla", "limit-removing", "boom", "mbf", "mbf21", "gzdoom", "pk3", "udmf"};
    gz.is_installed = true;
    gz.installed_binary_path = "/usr/bin/gzdoom";
    reg.register_engine(gz);

    // Uninstalled engine with mbf21 capability
    EngineInfo woof;
    woof.name = "woof";
    woof.title = "Woof!";
    woof.binary = "woof";
    woof.capabilities = {"vanilla", "boom", "mbf", "mbf21"};
    woof.is_installed = false;
    woof.installed_binary_path = "";
    reg.register_engine(woof);

    auto has_engine_named = [](const std::vector<EngineInfo>& list, const std::string& name) {
        return std::any_of(list.begin(), list.end(), [&](const EngineInfo& e) { return e.name == name; });
    };

    // 1. Matching 'vanilla' against simulated engine list (installed only)
    // Ensures Chocolate, Boom, DSDA-Doom, and GZDoom match
    auto vanilla_engines = reg.find_compatible_engines({"vanilla"}, true);
    assert(vanilla_engines.size() == 4);
    assert(has_engine_named(vanilla_engines, "chocolate-doom"));
    assert(has_engine_named(vanilla_engines, "prboom-plus"));
    assert(has_engine_named(vanilla_engines, "dsda-doom"));
    assert(has_engine_named(vanilla_engines, "gzdoom"));
    assert(!has_engine_named(vanilla_engines, "woof")); // uninstalled

    // Matching 'vanilla' including uninstalled
    auto all_vanilla = reg.find_compatible_engines({"vanilla"}, false);
    assert(all_vanilla.size() == 5);
    assert(has_engine_named(all_vanilla, "woof"));

    // 2. Matching 'mbf21' against simulated engine list
    // Ensures Chocolate and Boom are EXCLUDED, DSDA-Doom and GZDoom match
    auto mbf21_engines = reg.find_compatible_engines({"mbf21"}, true);
    assert(mbf21_engines.size() == 2);
    assert(has_engine_named(mbf21_engines, "dsda-doom"));
    assert(has_engine_named(mbf21_engines, "gzdoom"));
    assert(!has_engine_named(mbf21_engines, "chocolate-doom"));
    assert(!has_engine_named(mbf21_engines, "prboom-plus"));

    // 3. Matching 'gzdoom' against simulated engine list
    // Ensures ONLY GZDoom matches
    auto gzdoom_engines = reg.find_compatible_engines({"gzdoom"}, true);
    assert(gzdoom_engines.size() == 1);
    assert(gzdoom_engines[0].name == "gzdoom");
    assert(!has_engine_named(gzdoom_engines, "chocolate-doom"));
    assert(!has_engine_named(gzdoom_engines, "prboom-plus"));
    assert(!has_engine_named(gzdoom_engines, "dsda-doom"));

    // 4. Combined capabilities & edge cases
    auto complevel_engines = reg.find_compatible_engines({"vanilla", "complevel"}, true);
    assert(complevel_engines.size() == 2);
    assert(has_engine_named(complevel_engines, "prboom-plus"));
    assert(has_engine_named(complevel_engines, "dsda-doom"));
    assert(!has_engine_named(complevel_engines, "chocolate-doom"));
    assert(!has_engine_named(complevel_engines, "gzdoom"));

    // Case-insensitive capability matching
    auto upper_mbf21 = reg.find_compatible_engines({"MBF21"}, true);
    assert(upper_mbf21.size() == 2);
    assert(has_engine_named(upper_mbf21, "dsda-doom"));
    assert(has_engine_named(upper_mbf21, "gzdoom"));

    // Unknown capability returns empty list without errors
    auto unknown = reg.find_compatible_engines({"nonexistent_capability_123"}, true);
    assert(unknown.empty());

    std::cout << "Phase 6 Engine Capability Matching Tests passed!" << std::endl << std::endl;
}

void test_phase6_dynamic_template_generation() {
    std::cout << "--- Running Phase 6 Dynamic Template Generation Tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path temp_dir = "temp_p6_dyn_assets";
    fs::create_directories(temp_dir);

    {
        std::ofstream f(temp_dir / "DOOM2.WAD");
        f << "IWAD";
    }
    {
        std::ofstream f(temp_dir / "SIGIL.WAD");
        f << "PWAD";
    }

    // Engine 1: Boom - supports -complevel, -file, -save
    {
        std::ofstream f("test_p6_eng_boom.toml");
        f << R"(
[p6-boom]
type = "engine"
title = "Boom Engine"
binary = "sh"
capabilities = ["vanilla", "boom", "complevel"]
cmd = "{{BIN}} -iwad {{IWAD}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %} -save {{SAVEDIR}}"
)";
    }

    // Engine 2: GZDoom - no complevel support, uses -file and -savedir
    {
        std::ofstream f("test_p6_eng_gzdoom.toml");
        f << R"(
[p6-gzdoom]
type = "engine"
title = "GZDoom Engine"
binary = "sh"
capabilities = ["vanilla", "boom", "gzdoom"]
cmd = "{{BIN}} -iwad {{IWAD}}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %} -savedir {{SAVEDIR}}"
)";
    }

    // Engine 3: Chocolate Doom - no complevel support, uses -merge and -savedir
    {
        std::ofstream f("test_p6_eng_choco.toml");
        f << R"(
[p6-chocolate]
type = "engine"
title = "Chocolate Doom"
binary = "sh"
capabilities = ["vanilla"]
cmd = "{{BIN}} -iwad {{IWAD}}{% if PWADS %}{% for wad in PWADS %} -merge {{wad}}{% endfor %}{% endif %} -savedir {{SAVEDIR}}"
)";
    }

    // Game requiring 'vanilla' capability, compat_level 2, PWAD SIGIL.WAD
    {
        std::ofstream f("test_p6_game.toml");
        f << R"(
[p6_game]
type = "game"
title = "Phase 6 Game"
iwad = "DOOM2.WAD"
pwads = ["SIGIL.WAD"]
compat_level = 2
requires_capability = ["vanilla"]
)";
    }

    setenv("DOOMLAUNCHER_SAVES", "/tmp/doom_saves", 1);

    ModuleRegistry registry;
    registry.get_asset_resolver().add_search_dir(temp_dir.string());

    assert(registry.load_file("test_p6_eng_boom.toml"));
    assert(registry.load_file("test_p6_eng_gzdoom.toml"));
    assert(registry.load_file("test_p6_eng_choco.toml"));
    assert(registry.load_file("test_p6_game.toml"));

    std::vector<ResolvedMenuItem> items = registry.resolve_menu_items();
    assert(items.size() == 1);

    ResolvedMenuItem& item = items[0];
    assert(item.module_name == "p6_game");
    assert(item.status == GameStatus::Ready);
    assert(item.assets_found);
    assert(item.compatible_engines.size() == 3);
    assert(item.save_dir == "/tmp/doom_saves/p6_game");

    // 1. Select Boom engine: verify -complevel 2, -file, and -save are present
    assert(item.select_engine("p6-boom"));
    std::string boom_cmd = item.get_command();
    std::cout << "Boom generated command: " << boom_cmd << std::endl;
    assert(boom_cmd.find("-iwad") != std::string::npos);
    assert(boom_cmd.find("-complevel 2") != std::string::npos);
    assert(boom_cmd.find("-file") != std::string::npos);
    assert(boom_cmd.find("SIGIL.WAD") != std::string::npos);
    assert(boom_cmd.find("-save /tmp/doom_saves/p6_game") != std::string::npos);
    assert(boom_cmd.find("-savedir") == std::string::npos);
    assert(boom_cmd.find("-merge") == std::string::npos);

    // 2. Switch to GZDoom: verify -complevel is removed, -file remains, and -save changes to -savedir
    assert(item.select_engine("p6-gzdoom"));
    std::string gz_cmd = item.get_command();
    std::cout << "GZDoom generated command: " << gz_cmd << std::endl;
    assert(gz_cmd.find("-iwad") != std::string::npos);
    assert(gz_cmd.find("-complevel") == std::string::npos);
    assert(gz_cmd.find("-file") != std::string::npos);
    assert(gz_cmd.find("SIGIL.WAD") != std::string::npos);
    assert(gz_cmd.find("-savedir /tmp/doom_saves/p6_game") != std::string::npos);
    assert(gz_cmd.find(" -save ") == std::string::npos);
    assert(gz_cmd.find("-merge") == std::string::npos);

    // 3. Switch to Chocolate Doom: verify -file changes to -merge, no complevel
    assert(item.select_engine("p6-chocolate"));
    std::string choco_cmd = item.get_command();
    std::cout << "Chocolate generated command: " << choco_cmd << std::endl;
    assert(choco_cmd.find("-iwad") != std::string::npos);
    assert(choco_cmd.find("-complevel") == std::string::npos);
    assert(choco_cmd.find("-merge") != std::string::npos);
    assert(choco_cmd.find("SIGIL.WAD") != std::string::npos);
    assert(choco_cmd.find("-file") == std::string::npos);
    assert(choco_cmd.find("-savedir /tmp/doom_saves/p6_game") != std::string::npos);

    // 4. Switch back to Boom: verify -complevel 2 is restored
    assert(item.select_engine("p6-boom"));
    assert(item.get_command().find("-complevel 2") != std::string::npos);

    // 5. Test cycling through engines with select_next_engine() / select_prev_engine()
    size_t initial_idx = item.selected_engine_index;
    assert(item.select_next_engine());
    assert(item.selected_engine_index == (initial_idx + 1) % 3);
    assert(!item.cmd.empty());
    assert(item.select_prev_engine());
    assert(item.selected_engine_index == initial_idx);

    unsetenv("DOOMLAUNCHER_SAVES");

    // Clean up
    fs::remove_all(temp_dir);
    std::remove("test_p6_eng_boom.toml");
    std::remove("test_p6_eng_gzdoom.toml");
    std::remove("test_p6_eng_choco.toml");
    std::remove("test_p6_game.toml");

    std::cout << "Phase 6 Dynamic Template Generation Tests passed!" << std::endl << std::endl;
}

static void send_test_key(Menu& menu, SDL_Keycode sym) {
    SDL_Event ev;
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = sym;
    menu.handleEvent(ev);
}

void test_menu_paging() {
    std::cout << "--- Running Menu Paging Tests ---" << std::endl;

    Menu menu(nullptr, nullptr, nullptr, 800, 600);

    // Create 15 items -> setMenuItems adds Quit -> 16 total items
    std::vector<ResolvedMenuItem> items;
    for (int i = 1; i <= 15; ++i) {
        ResolvedMenuItem it;
        it.title = "Test Game " + std::to_string(i);
        it.module_name = "game_" + std::to_string(i);
        items.push_back(it);
    }
    menu.setMenuItems(items);

    assert(Menu::ITEMS_PER_PAGE == 6);
    assert(menu.getTotalPages() == 3); // 16 items / 6 = 3 pages (6 + 6 + 4)
    assert(menu.getCurrentPage() == 0);
    assert(menu.getSelectedItem() == 0);

    // 1. Test navigation with Down arrow across pages
    for (int i = 0; i < 5; ++i) {
        send_test_key(menu, SDLK_DOWN);
    }
    assert(menu.getSelectedItem() == 5);
    assert(menu.getCurrentPage() == 0);

    // 6th Down -> moves to index 6 (page 1)
    send_test_key(menu, SDLK_DOWN);
    assert(menu.getSelectedItem() == 6);
    assert(menu.getCurrentPage() == 1);

    // Down to end of page 1 (index 11)
    for (int i = 0; i < 5; ++i) {
        send_test_key(menu, SDLK_DOWN);
    }
    assert(menu.getSelectedItem() == 11);
    assert(menu.getCurrentPage() == 1);

    // Down to page 2 (index 12)
    send_test_key(menu, SDLK_DOWN);
    assert(menu.getSelectedItem() == 12);
    assert(menu.getCurrentPage() == 2);

    // Down to last item (index 15, Quit)
    for (int i = 0; i < 3; ++i) {
        send_test_key(menu, SDLK_DOWN);
    }
    assert(menu.getSelectedItem() == 15);
    assert(menu.getCurrentPage() == 2);

    // Down from last item -> wraps to item 0 (page 0)
    send_test_key(menu, SDLK_DOWN);
    assert(menu.getSelectedItem() == 0);
    assert(menu.getCurrentPage() == 0);

    // 2. Test navigation with Up arrow across pages (wrapping backwards)
    send_test_key(menu, SDLK_UP);
    assert(menu.getSelectedItem() == 15);
    assert(menu.getCurrentPage() == 2);

    // Up from index 6 (start of page 1) -> moves to index 5 (end of page 0)
    menu.setMenuItems(items); // reset to index 0
    for (int i = 0; i < 6; ++i) {
        send_test_key(menu, SDLK_DOWN);
    }
    assert(menu.getSelectedItem() == 6);
    assert(menu.getCurrentPage() == 1);
    send_test_key(menu, SDLK_UP);
    assert(menu.getSelectedItem() == 5);
    assert(menu.getCurrentPage() == 0);

    // 3. Test PageDown navigation
    menu.setMenuItems(items); // cleanly reset to index 0
    assert(menu.getSelectedItem() == 0);

    send_test_key(menu, SDLK_PAGEDOWN); // page 0 -> page 1 (index 6)
    assert(menu.getSelectedItem() == 6);
    assert(menu.getCurrentPage() == 1);

    send_test_key(menu, SDLK_PAGEDOWN); // page 1 -> page 2 (index 12)
    assert(menu.getSelectedItem() == 12);
    assert(menu.getCurrentPage() == 2);

    send_test_key(menu, SDLK_PAGEDOWN); // on last page -> wraps to page 0
    assert(menu.getSelectedItem() == 0);
    assert(menu.getCurrentPage() == 0);

    // 4. Test PageUp navigation
    send_test_key(menu, SDLK_PAGEUP); // on page 0 -> wraps to last page (page 2, index 12)
    assert(menu.getSelectedItem() == 12);
    assert(menu.getCurrentPage() == 2);

    send_test_key(menu, SDLK_PAGEUP); // page 2 -> page 1 (index 6)
    assert(menu.getSelectedItem() == 6);
    assert(menu.getCurrentPage() == 1);

    send_test_key(menu, SDLK_PAGEUP); // page 1 -> page 0 (index 0)
    assert(menu.getSelectedItem() == 0);
    assert(menu.getCurrentPage() == 0);

    std::cout << "Menu Paging Tests passed!" << std::endl << std::endl;
}

void test_menu_osd() {
    std::cout << "--- Running Menu OSD Tests ---" << std::endl;
    Menu menu(nullptr, nullptr, nullptr, 800, 600);

    std::vector<doomlauncher::ResolvedMenuItem> items;
    doomlauncher::ResolvedMenuItem it1;
    it1.title = "Doom II";
    it1.module_name = "doom2";
    it1.status = doomlauncher::GameStatus::Ready;
    it1.assets_found = true;

    doomlauncher::EngineInfo eng1;
    eng1.name = "boom";
    eng1.title = "Boom";
    eng1.cmd_template = "boom -iwad DOOM2.WAD";

    doomlauncher::EngineInfo eng2;
    eng2.name = "dsda";
    eng2.title = "DSDA-Doom";
    eng2.cmd_template = "dsda-doom -iwad DOOM2.WAD";

    it1.compatible_engines = { eng1, eng2 };
    it1.selected_engine_index = 0;
    items.push_back(it1);

    menu.setMenuItems(items);

    assert(menu.getSelectedItem() == 0);
    assert(menu.getMenuItems()[0].get_selected_engine()->name == "boom");

    // Test status message setting and clearing
    menu.setStatusMessage("Test status message");
    assert(menu.getStatusMessage() == "Test status message");

    // Right arrow switches engine and clears status message
    send_test_key(menu, SDLK_RIGHT);
    assert(menu.getMenuItems()[0].get_selected_engine()->name == "dsda");
    assert(menu.getStatusMessage().empty());

    // Left arrow switches back
    send_test_key(menu, SDLK_LEFT);
    assert(menu.getMenuItems()[0].get_selected_engine()->name == "boom");

    // Test font toggling with alternate font (F9)
    TTF_Font* dummyAlt = reinterpret_cast<TTF_Font*>(0x1234);
    Menu menuWithAlt(nullptr, nullptr, nullptr, 800, 600, dummyAlt);
    menuWithAlt.setMenuItems(items);
    assert(!menuWithAlt.isUsingAlternateFont());
    send_test_key(menuWithAlt, SDLK_F9);
    assert(menuWithAlt.isUsingAlternateFont());
    assert(menuWithAlt.getStatusMessage().find("Classic Doom") != std::string::npos);
    send_test_key(menuWithAlt, SDLK_F9);
    assert(!menuWithAlt.isUsingAlternateFont());
    assert(menuWithAlt.getStatusMessage().find("Monospace") != std::string::npos);

    // Ensure safe call to update/render with null fonts doesn't crash
    menu.update();
    menu.render();

    std::cout << "Menu OSD Tests passed!" << std::endl << std::endl;
}

void test_menu_overlays() {
    std::cout << "--- Running Menu Phase 4 Modal Overlays Tests ---" << std::endl;
    Menu menu(nullptr, nullptr, nullptr, 800, 600);

    std::vector<doomlauncher::ResolvedMenuItem> items;
    doomlauncher::ResolvedMenuItem it1;
    it1.title = "Doom II";
    it1.module_name = "doom2";
    it1.status = doomlauncher::GameStatus::Ready;
    it1.assets_found = true;

    doomlauncher::EngineInfo eng1;
    eng1.name = "boom";
    eng1.title = "Boom";
    eng1.binary = "/usr/games/boom";
    eng1.cmd_template = "{{BIN}} -iwad DOOM2.WAD";

    doomlauncher::EngineInfo eng2;
    eng2.name = "dsda";
    eng2.title = "DSDA-Doom";
    eng2.binary = "/usr/games/dsda-doom";
    eng2.cmd_template = "{{BIN}} -iwad DOOM2.WAD";

    it1.compatible_engines = { eng1, eng2 };
    it1.selected_engine_index = 0;
    it1.cmd = it1.get_command();
    items.push_back(it1);

    menu.setMenuItems(items);

    // Initial state
    assert(menu.getOverlayState() == OverlayState::None);

    // 1. F1: Help Overlay
    send_test_key(menu, SDLK_F1);
    assert(menu.getOverlayState() == OverlayState::Help);
    // Dismiss with Escape
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);
    // Reopen with F1, close with F1
    send_test_key(menu, SDLK_F1);
    assert(menu.getOverlayState() == OverlayState::Help);
    send_test_key(menu, SDLK_F1);
    assert(menu.getOverlayState() == OverlayState::None);
    // Reopen with F1, close with Enter
    send_test_key(menu, SDLK_F1);
    assert(menu.getOverlayState() == OverlayState::Help);
    send_test_key(menu, SDLK_RETURN);
    assert(menu.getOverlayState() == OverlayState::None);

    // 2. F3: Selected Module Info
    send_test_key(menu, SDLK_F3);
    assert(menu.getOverlayState() == OverlayState::ModuleInfo);
    // Dismiss with Escape
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);
    // Reopen with F3, close with F3
    send_test_key(menu, SDLK_F3);
    assert(menu.getOverlayState() == OverlayState::ModuleInfo);
    send_test_key(menu, SDLK_F3);
    assert(menu.getOverlayState() == OverlayState::None);

    // 3. F4: Engine Selection Modal
    send_test_key(menu, SDLK_F4);
    assert(menu.getOverlayState() == OverlayState::EngineSelect);
    assert(menu.getModalCursorIndex() == 0);

    // Navigate with Down arrow
    send_test_key(menu, SDLK_DOWN);
    assert(menu.getModalCursorIndex() == 1);
    // Wrap around Down -> 0
    send_test_key(menu, SDLK_DOWN);
    assert(menu.getModalCursorIndex() == 0);
    // Wrap around Up -> 1
    send_test_key(menu, SDLK_UP);
    assert(menu.getModalCursorIndex() == 1);

    // Dismiss with Escape without applying changes
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);
    assert(menu.getMenuItems()[0].selected_engine_index == 0);

    // Reopen F4, navigate to index 1, confirm with Return
    send_test_key(menu, SDLK_F4);
    assert(menu.getOverlayState() == OverlayState::EngineSelect);
    assert(menu.getModalCursorIndex() == 0);
    send_test_key(menu, SDLK_DOWN);
    assert(menu.getModalCursorIndex() == 1);
    send_test_key(menu, SDLK_RETURN);
    assert(menu.getOverlayState() == OverlayState::None);
    assert(menu.getMenuItems()[0].selected_engine_index == 1);
    assert(menu.getMenuItems()[0].get_selected_engine()->name == "dsda");
    assert(menu.getStatusMessage().find("DSDA-Doom") != std::string::npos);

    // 4. F10 / Esc: Fast Quit Confirmation
    assert(!menu.isQuitSelected());
    send_test_key(menu, SDLK_F10);
    assert(menu.getOverlayState() == OverlayState::QuitConfirm);
    // Press 'n' to cancel
    send_test_key(menu, SDLK_n);
    assert(menu.getOverlayState() == OverlayState::None);
    assert(!menu.isQuitSelected());

    // Press Escape on main menu triggers QuitConfirm
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::QuitConfirm);
    // Press Escape again cancels QuitConfirm
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);
    assert(!menu.isQuitSelected());

    // Open with F10, press 'y' to confirm exit
    send_test_key(menu, SDLK_F10);
    assert(menu.getOverlayState() == OverlayState::QuitConfirm);
    send_test_key(menu, SDLK_y);
    assert(menu.getOverlayState() == OverlayState::None);
    assert(menu.isQuitSelected());

    // 5. Headless rendering safety with active overlays
    menu.setOverlayState(OverlayState::Help);
    menu.render();
    menu.setOverlayState(OverlayState::ModuleInfo);
    menu.render();
    menu.setOverlayState(OverlayState::EngineSelect);
    menu.render();
    menu.setOverlayState(OverlayState::QuitConfirm);
    menu.render();
    menu.setOverlayState(OverlayState::None);
    menu.render();

    std::cout << "Menu Phase 4 Modal Overlays Tests passed!" << std::endl << std::endl;
}

void test_phase5_state_persistence() {
    std::cout << "--- Running Phase 5 State Persistence Tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path temp_state_file = "temp_test_state.toml";
    std::error_code ec;
    fs::remove(temp_state_file, ec);

    // 1. Test basic save and load in ModuleRegistry
    {
        ModuleRegistry reg;
        reg.set_saved_engine("doom2", "gzdoom");
        reg.set_saved_engine("sigil", "boom");
        assert(reg.save_state(temp_state_file.string()));
        assert(fs::exists(temp_state_file));
    }

    {
        ModuleRegistry reg2;
        assert(reg2.load_state(temp_state_file.string()));
        const auto& saved = reg2.get_saved_engines();
        assert(saved.size() == 2);
        assert(saved.at("doom2") == "gzdoom");
        assert(saved.at("sigil") == "boom");
    }

    // 2. Test engine restoration during resolve_menu_items
    fs::path temp_asset_dir = "temp_p5_assets";
    fs::create_directories(temp_asset_dir, ec);
    {
        std::ofstream f(temp_asset_dir / "DOOM2.WAD");
        f << "IWAD";
    }

    {
        std::ofstream eng1_file("test_p5_boom.toml");
        eng1_file << R"(
[boom]
type = "engine"
title = "Boom Engine"
binary = "sh"
capabilities = ["vanilla", "boom"]
cmd = "{{BIN}} -iwad {{IWAD}}"
)";
    }

    {
        std::ofstream eng2_file("test_p5_gzdoom.toml");
        eng2_file << R"(
[gzdoom]
type = "engine"
title = "GZDoom"
binary = "sh"
capabilities = ["vanilla", "boom", "gzdoom"]
cmd = "{{BIN}} -iwad {{IWAD}}"
)";
    }

    {
        std::ofstream game_file("test_p5_game.toml");
        game_file << R"(
[doom2]
type = "game"
title = "Doom 2"
iwad = "DOOM2.WAD"
requires_capability = ["vanilla"]
)";
    }

    {
        ModuleRegistry reg;
        reg.get_asset_resolver().add_search_dir(temp_asset_dir.string());
        assert(reg.load_file("test_p5_boom.toml"));
        assert(reg.load_file("test_p5_gzdoom.toml"));
        assert(reg.load_file("test_p5_game.toml"));

        // Default: first compatible engine is selected (boom)
        auto items = reg.resolve_menu_items();
        assert(items.size() == 1);
        assert(items[0].get_selected_engine()->name == "boom");

        // Now load state with doom2 = "gzdoom"
        assert(reg.load_state(temp_state_file.string()));
        auto items_with_state = reg.resolve_menu_items();
        assert(items_with_state.size() == 1);
        assert(items_with_state[0].get_selected_engine()->name == "gzdoom");
    }

    // 3. Test Menu F2 integration
    {
        Menu menu(nullptr, nullptr, nullptr, 800, 600);
        ModuleRegistry reg;
        reg.get_asset_resolver().add_search_dir(temp_asset_dir.string());
        assert(reg.load_file("test_p5_boom.toml"));
        assert(reg.load_file("test_p5_gzdoom.toml"));
        assert(reg.load_file("test_p5_game.toml"));
        assert(reg.load_state(temp_state_file.string()));

        auto items = reg.resolve_menu_items();
        menu.setMenuItems(items);
        menu.setModuleRegistry(&reg);

        // Initially gzdoom is selected
        assert(menu.getMenuItems()[0].get_selected_engine()->name == "gzdoom");

        // Switch to boom with Right arrow
        send_test_key(menu, SDLK_RIGHT);
        assert(menu.getMenuItems()[0].get_selected_engine()->name == "boom");

        // Set custom state path via DOOMLAUNCHER_STATE env var to avoid overwriting real user state
        fs::path f2_state_file = "temp_p5_f2_state.toml";
        fs::remove(f2_state_file, ec);
        setenv("DOOMLAUNCHER_STATE", f2_state_file.c_str(), 1);

        // Press F2 to save settings
        send_test_key(menu, SDLK_F2);
        assert(menu.getStatusMessage().find("Settings saved to") != std::string::npos);
        assert(fs::exists(f2_state_file));

        // Verify the saved content in f2_state_file has doom2 = "boom"
        ModuleRegistry reg_verify;
        assert(reg_verify.load_state(f2_state_file.string()));
        assert(reg_verify.get_saved_engines().at("doom2") == "boom");

        unsetenv("DOOMLAUNCHER_STATE");
        fs::remove(f2_state_file, ec);
    }

    // Clean up
    fs::remove(temp_state_file, ec);
    fs::remove_all(temp_asset_dir, ec);
    std::remove("test_p5_boom.toml");
    std::remove("test_p5_gzdoom.toml");
    std::remove("test_p5_game.toml");

    std::cout << "Phase 5 State Persistence Tests passed!" << std::endl << std::endl;
}

void test_gui_rendering_verification() {
    std::cout << "--- Running Phase 6 Full GUI Rendering Verification Tests ---" << std::endl;
    setenv("SDL_VIDEODRIVER", "dummy", 1);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "Warning: SDL dummy video driver not available, skipping GUI rendering test." << std::endl;
        return;
    }
    if (TTF_Init() == -1) {
        std::cerr << "Warning: TTF_Init failed, skipping GUI rendering test." << std::endl;
        SDL_Quit();
        return;
    }

    SDL_Window* window = SDL_CreateWindow("Test", 0, 0, 800, 600, SDL_WINDOW_HIDDEN);
    assert(window != nullptr);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    assert(renderer != nullptr);

    TTF_Font* titleFont = TTF_OpenFont("fonts/Doom2016Left-RpJDA.ttf", 84);
    TTF_Font* monoFont = TTF_OpenFont("fonts/Hack-Regular.ttf", 20);
    TTF_Font* hudFont = TTF_OpenFont("fonts/DoomClassicHUD.ttf", 20);

    assert(titleFont != nullptr);
    assert(monoFont != nullptr);
    assert(hudFont != nullptr);

    Menu menu(renderer, titleFont, monoFont, 800, 600, hudFont);

    // Create 8 items to test multi-page rendering
    std::vector<ResolvedMenuItem> items;
    for (int i = 1; i <= 8; ++i) {
        ResolvedMenuItem item;
        item.title = "Game " + std::to_string(i);
        item.module_name = "game_" + std::to_string(i);
        item.status = GameStatus::Ready;
        item.assets_found = true;
        item.resolved_iwad_path = "/path/to/DOOM2.WAD";
        item.cmd = "engine -iwad DOOM2.WAD";

        EngineInfo eng1;
        eng1.name = "boom";
        eng1.title = "Boom Engine";
        eng1.binary = "/usr/games/boom";
        eng1.installed_binary_path = "/usr/games/boom";
        eng1.cmd_template = "{{BIN}} -iwad {{IWAD}}";

        EngineInfo eng2;
        eng2.name = "gzdoom";
        eng2.title = "GZDoom";
        eng2.binary = "/usr/games/gzdoom";
        eng2.installed_binary_path = "/usr/games/gzdoom";
        eng2.cmd_template = "{{BIN}} -iwad {{IWAD}}";

        item.compatible_engines = { eng1, eng2 };
        items.push_back(item);
    }
    menu.setMenuItems(items);

    // 1. Render default main menu (page 0)
    menu.render();

    // 2. Render next page (page 1)
    send_test_key(menu, SDLK_PAGEDOWN);
    assert(menu.getCurrentPage() == 1);
    menu.render();

    // 3. Render Help overlay (F1)
    send_test_key(menu, SDLK_F1);
    assert(menu.getOverlayState() == OverlayState::Help);
    menu.render();
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);

    // 4. Render Module Info overlay (F3)
    send_test_key(menu, SDLK_F3);
    assert(menu.getOverlayState() == OverlayState::ModuleInfo);
    menu.render();
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);

    // 5. Render Engine Select overlay (F4)
    send_test_key(menu, SDLK_F4);
    assert(menu.getOverlayState() == OverlayState::EngineSelect);
    menu.render();
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);

    // 6. Render Quit Confirm overlay (F10)
    send_test_key(menu, SDLK_F10);
    assert(menu.getOverlayState() == OverlayState::QuitConfirm);
    menu.render();
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);

    // 7. Render font switch to Classic Doom HUD (F9)
    send_test_key(menu, SDLK_F9);
    assert(menu.isUsingAlternateFont());
    menu.render();

    // 8. Render save status message in OSD
    menu.setStatusMessage("Settings saved to ~/.config/doomlauncher/state.toml");
    menu.render();

    // 9. Render AddWadDialog overlay (F5)
    send_test_key(menu, SDLK_F5);
    assert(menu.getOverlayState() == OverlayState::AddWadDialog);
    menu.render();
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);

    // 10. Render CreateModuleForm overlay
    WadFile dummyWad;
    dummyWad.path = "/path/to/eviternity.wad";
    dummyWad.filename = "eviternity.wad";
    dummyWad.stem_lower = "eviternity";
    dummyWad.sha1 = "1234567890abcdef1234567890abcdef12345678";
    menu.openCreateModuleForm({ dummyWad });
    assert(menu.getOverlayState() == OverlayState::CreateModuleForm);
    menu.render();
    send_test_key(menu, SDLK_ESCAPE);
    assert(menu.getOverlayState() == OverlayState::None);

    // Clean up
    TTF_CloseFont(titleFont);
    TTF_CloseFont(monoFont);
    TTF_CloseFont(hudFont);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    std::cout << "Phase 6 Full GUI Rendering Verification Tests passed!" << std::endl << std::endl;
}

void test_phase1_multi_module_dirs_and_saves() {
    std::cout << "--- Running Phase 1 Multi-Directory Modules & Centralized Saves Tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path sys_mods = "temp_p1_sys_modules";
    fs::path usr_mods = "temp_p1_usr_modules";
    fs::path wad_dir = "temp_p1_wads";
    fs::path cfg_file = "temp_p1_config.toml";

    fs::create_directories(sys_mods / "engines");
    fs::create_directories(sys_mods / "games");
    fs::create_directories(usr_mods / "games");
    fs::create_directories(wad_dir);

    {
        std::ofstream f(wad_dir / "DOOM2.WAD");
        f << "IWAD";
    }
    {
        std::ofstream f(wad_dir / "CUSTOM.WAD");
        f << "PWAD";
    }

    // System engine
    {
        std::ofstream f(sys_mods / "engines" / "dsda.toml");
        f << R"(
[dsda-doom]
type = "engine"
title = "DSDA-Doom"
binary = "sh"
capabilities = ["vanilla", "boom"]
cmd = "{{BIN}} -iwad {{IWAD}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %} -savedir {{SAVEDIR}}"
)";
    }

    // System base game
    {
        std::ofstream f(sys_mods / "games" / "doom2.toml");
        f << R"TOML(
[doom2]
type = "game"
title = "Doom II (System)"
iwad = "DOOM2.WAD"
compat_level = 2
requires_capability = ["vanilla"]
)TOML";
    }

    // User game + user override of doom2
    {
        std::ofstream f(usr_mods / "games" / "custom_mod.toml");
        f << R"TOML(
[custom_mod]
type = "game"
title = "User Custom Mod"
base_game = "doom2"
pwads = ["CUSTOM.WAD"]
compat_level = 9
requires_capability = ["boom"]
)TOML";
    }
    {
        std::ofstream f(usr_mods / "games" / "doom2_override.toml");
        f << R"TOML(
[doom2]
type = "game"
title = "Doom II (User Override)"
iwad = "DOOM2.WAD"
compat_level = 2
requires_capability = ["vanilla"]
)TOML";
    }

    // Save UserConfig with both module dirs
    UserConfig cfg;
    cfg.module_dirs = { sys_mods.string(), usr_mods.string() };
    cfg.wad_dirs = { wad_dir.string() };
    cfg.default_engine = "dsda-doom";
    assert(AssetResolver::save_config(cfg, cfg_file.string()));

    ModuleRegistry reg;
    assert(reg.load_user_config(cfg_file.string()));
    assert(reg.load_configured_modules());

    auto items = reg.resolve_menu_items();
    assert(items.size() == 2);

    bool found_doom2 = false;
    bool found_custom = false;
    for (const auto& item : items) {
        std::string expected_save = ModuleRegistry::get_module_save_dir(item.module_name);
        assert(item.save_dir == expected_save);
        assert(item.cmd.find("-savedir " + expected_save) != std::string::npos);

        if (item.module_name == "doom2") {
            found_doom2 = true;
            // Verify user module overrode system module
            assert(item.title == "Doom II (User Override)");
        } else if (item.module_name == "custom_mod") {
            found_custom = true;
            assert(item.title == "User Custom Mod");
            assert(item.cmd.find("-complevel 9") != std::string::npos);
            assert(item.cmd.find("CUSTOM.WAD") != std::string::npos);
        }
    }
    assert(found_doom2 && found_custom);

    fs::remove_all(sys_mods);
    fs::remove_all(usr_mods);
    fs::remove_all(wad_dir);
    fs::remove(cfg_file);

    std::cout << "Phase 1 Multi-Directory Modules & Centralized Saves Tests passed!" << std::endl << std::endl;
}

void test_phase2_wad_catalog_and_sha1() {
    std::cout << "--- Running Phase 2 WAD Catalog, SHA-1 & Cache Persistence Tests ---" << std::endl;
    namespace fs = std::filesystem;

    // 1. Verify RFC 3174 SHA-1 test vectors
    const char* vec_empty = "";
    assert(WadCatalog::compute_bytes_sha1(reinterpret_cast<const uint8_t*>(vec_empty), 0) ==
           "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    const char* vec_abc = "abc";
    assert(WadCatalog::compute_bytes_sha1(reinterpret_cast<const uint8_t*>(vec_abc), 3) ==
           "a9993e364706816aba3e25717850c26c9cd0d89d");

    const char* vec_long = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    assert(WadCatalog::compute_bytes_sha1(reinterpret_cast<const uint8_t*>(vec_long), std::strlen(vec_long)) ==
           "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    // 2. Create mock WADs and test header inspection + SHA-1 indexing
    fs::path wad_dir = "temp_p2_wads";
    fs::path state_file = "temp_p2_state.toml";
    fs::create_directories(wad_dir / "mods");

    {
        std::ofstream f(wad_dir / "z_custom.wad", std::ios::binary);
        f << "PWAD_CUSTOM_CONTENT_12345";
    }
    {
        std::ofstream f(wad_dir / "DOOM.WAD", std::ios::binary);
        f << "IWAD_DOOM1_CONTENT_ABCDEF";
    }
    {
        std::ofstream f(wad_dir / "DOOM2.WAD", std::ios::binary);
        f << "IWAD_DOOM2_CONTENT_987654";
    }
    {
        std::ofstream f(wad_dir / "mods" / "a_mod.wad", std::ios::binary);
        f << "PWAD_AMOD_CONTENT_XYZ";
    }
    {
        std::ofstream f(wad_dir / "custom_iwad.wad", std::ios::binary);
        f << "IWAD_OTHER_GAME_CONTENT";
    }

    bool is_iwad = false;
    assert(WadCatalog::inspect_wad_header((wad_dir / "DOOM2.WAD").string(), is_iwad) && is_iwad);
    assert(WadCatalog::inspect_wad_header((wad_dir / "z_custom.wad").string(), is_iwad) && !is_iwad);

    UserConfig cfg;
    cfg.wad_dirs = { wad_dir.string() };
    cfg.iwads["my_doom2_alias"] = (wad_dir / "DOOM2.WAD").string();

    WadCatalog catalog;
    catalog.scan_from_config(cfg, false);
    assert(catalog.get_all_wads().size() == 5);

    std::string doom2_sha1 = WadCatalog::compute_file_sha1((wad_dir / "DOOM2.WAD").string());
    assert(doom2_sha1.size() == 40);

    // Lookup by SHA-1 (case-insensitive)
    std::string upper_sha1 = doom2_sha1;
    std::transform(upper_sha1.begin(), upper_sha1.end(), upper_sha1.begin(), ::toupper);
    const WadFile* by_sha1 = catalog.find_by_sha1(upper_sha1);
    assert(by_sha1 != nullptr);
    assert(by_sha1->filename == "DOOM2.WAD");

    // Lookup by alias
    const WadFile* by_alias = catalog.find_by_alias("my_doom2_alias");
    assert(by_alias != nullptr);
    assert(by_alias->sha1 == doom2_sha1);

    // Lookup by filename (case-insensitive) and subdirectory
    const WadFile* by_fn = catalog.find_by_filename("A_MOD.WAD");
    assert(by_fn != nullptr);
    assert(by_fn->filename == "a_mod.wad");

    // resolve_wad prioritizes SHA-1 match even if filename differs
    const WadFile* resolved = catalog.resolve_wad("renamed_or_different.wad", { doom2_sha1 });
    assert(resolved != nullptr);
    assert(resolved->filename == "DOOM2.WAD");

    // 3. Test prioritized dependency list: DOOM2.WAD first, DOOM.WAD second, other IWADs next, PWADs last
    std::vector<WadFile> deps = catalog.get_prioritized_dependency_wads();
    assert(deps.size() == 5);
    assert(deps[0].filename == "DOOM2.WAD");
    assert(deps[1].filename == "DOOM.WAD");
    assert(deps[2].filename == "custom_iwad.wad");
    assert(deps[3].filename == "a_mod.wad");
    assert(deps[4].filename == "z_custom.wad");

    // 4. Test SHA-1 cache persistence in state.toml alongside [engines]
    ModuleRegistry reg;
    reg.set_saved_engine("doom2", "dsda-doom");
    reg.set_wad_catalog(catalog);
    assert(reg.save_state(state_file.string()));

    ModuleRegistry reg_loaded;
    assert(reg_loaded.load_state(state_file.string()));
    assert(reg_loaded.get_saved_engines().at("doom2") == "dsda-doom");
    assert(reg_loaded.get_wad_catalog().get_cache().size() == 5);

    // Verify cache hit on second scan
    reg_loaded.get_wad_catalog().scan_from_config(cfg, false);
    const WadFile* cached_doom2 = reg_loaded.get_wad_catalog().find_by_filename("DOOM2.WAD");
    assert(cached_doom2 != nullptr);
    assert(cached_doom2->sha1 == doom2_sha1);

    // Modify DOOM2.WAD size -> verify cache invalidates and recomputes new SHA-1
    {
        std::ofstream f(wad_dir / "DOOM2.WAD", std::ios::binary | std::ios::trunc);
        f << "IWAD_DOOM2_MODIFIED_CONTENT_LONGER_STRING_99999";
    }
    std::string new_doom2_sha1 = WadCatalog::compute_file_sha1((wad_dir / "DOOM2.WAD").string());
    assert(new_doom2_sha1 != doom2_sha1);

    reg_loaded.get_wad_catalog().clear();
    reg_loaded.get_wad_catalog().scan_from_config(cfg, false);
    const WadFile* updated_doom2 = reg_loaded.get_wad_catalog().find_by_filename("DOOM2.WAD");
    assert(updated_doom2 != nullptr);
    assert(updated_doom2->sha1 == new_doom2_sha1);

    fs::remove_all(wad_dir);
    fs::remove(state_file);

    std::cout << "Phase 2 WAD Catalog, SHA-1 & Cache Persistence Tests passed!" << std::endl << std::endl;
}

void test_phase3_wad_driven_module_activation() {
    std::cout << "--- Running Phase 3 WAD-Driven Module Activation & User Module Creation Tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path wad_dir = "temp_p3_wads";
    fs::path sys_mods = "temp_p3_sys_mods";
    fs::path usr_mods = "temp_p3_usr_mods";
    fs::remove_all(wad_dir);
    fs::remove_all(sys_mods);
    fs::remove_all(usr_mods);
    fs::create_directories(wad_dir);
    fs::create_directories(sys_mods / "engines");
    fs::create_directories(sys_mods / "games");
    fs::create_directories(usr_mods / "games");

    // Create mock WADs with known content -> compute their SHA-1 hashes
    fs::path doom2_path = wad_dir / "renamed_doom2_release.wad";
    fs::path sigil_path = wad_dir / "my_sigil_download.wad";
    fs::path eviternity_path = wad_dir / "Eviternity.wad";

    {
        std::ofstream f(doom2_path, std::ios::binary);
        f << "IWAD_DOOM2_UNIQUE_BYTES_FOR_SHA1";
    }
    {
        std::ofstream f(sigil_path, std::ios::binary);
        f << "PWAD_SIGIL_UNIQUE_BYTES_FOR_SHA1";
    }
    {
        std::ofstream f(eviternity_path, std::ios::binary);
        f << "PWAD_EVITERNITY_UNIQUE_BYTES_FOR_SHA1";
    }

    std::string doom2_sha1 = WadCatalog::compute_file_sha1(doom2_path.string());
    std::string sigil_sha1 = WadCatalog::compute_file_sha1(sigil_path.string());
    std::string eviternity_sha1 = WadCatalog::compute_file_sha1(eviternity_path.string());

    // System engine
    {
        std::ofstream f(sys_mods / "engines" / "dsda.toml");
        f << R"TOML(
[dsda-doom]
type = "engine"
title = "DSDA-Doom"
binary = "sh"
capabilities = ["vanilla", "boom", "mbf", "mbf21"]
cmd = "{{BIN}} -iwad {{IWAD}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %} -savedir {{SAVEDIR}}"
)TOML";
    }

    // Pre-installed modules: doom2 (matched by SHA-1 even though filename on disk is renamed_doom2_release.wad!),
    // sigil (depends on doom2 for this test, matched by SHA-1), and plutonia (missing WAD -> must NOT activate!)
    {
        std::ofstream f(sys_mods / "games" / "doom2.toml");
        f << "[doom2]\n"
          << "type = \"game\"\n"
          << "title = \"Doom II: Hell on Earth\"\n"
          << "iwad = \"DOOM2.WAD\"\n"
          << "iwad_sha1 = [\"" << doom2_sha1 << "\"]\n"
          << "compat_level = 2\n"
          << "requires_capability = [\"vanilla\"]\n";
    }
    {
        std::ofstream f(sys_mods / "games" / "sigil.toml");
        f << "[sigil]\n"
          << "type = \"game\"\n"
          << "title = \"Sigil\"\n"
          << "base_game = \"doom2\"\n"
          << "pwads = [\"SIGIL.WAD\"]\n"
          << "pwad_sha1 = [\"" << sigil_sha1 << "\"]\n"
          << "compat_level = 3\n"
          << "requires_capability = [\"vanilla\"]\n";
    }
    {
        std::ofstream f(sys_mods / "games" / "plutonia.toml");
        f << "[plutonia]\n"
          << "type = \"game\"\n"
          << "title = \"Final Doom: Plutonia\"\n"
          << "base_game = \"doom2\"\n"
          << "pwads = [\"PLUTONIA.WAD\"]\n"
          << "pwad_sha1 = [\"0000000000000000000000000000000000000000\"]\n"
          << "compat_level = 4\n"
          << "requires_capability = [\"vanilla\"]\n";
    }

    UserConfig cfg;
    cfg.module_dirs = { sys_mods.string(), usr_mods.string() };
    cfg.default_engine = "dsda-doom";

    ModuleRegistry reg;
    reg.set_user_config(cfg);
    // Clear system search paths so only our test WADs are used
    reg.get_wad_catalog().clear();
    reg.set_asset_resolver(AssetResolver());
    assert(reg.load_configured_modules());

    // 1. Before adding any WADs, 0 modules are active
    assert(reg.resolve_menu_items().empty());

    // 2. Add renamed_doom2_release.wad -> matched by SHA-1 -> activates 'doom2' only!
    const WadFile* wf_doom2_ptr = reg.register_wad_file(doom2_path.string());
    assert(wf_doom2_ptr != nullptr);
    WadFile wf_doom2 = *wf_doom2_ptr;
    assert(reg.find_modules_for_wad(wf_doom2) == std::vector<std::string>{"doom2"});

    auto items_after_doom2 = reg.resolve_menu_items();
    assert(items_after_doom2.size() == 1);
    assert(items_after_doom2[0].module_name == "doom2");
    assert(items_after_doom2[0].resolved_iwad_path == wf_doom2.path);

    // 3. Add my_sigil_download.wad -> matched by SHA-1 -> activates 'sigil'!
    const WadFile* wf_sigil_ptr = reg.register_wad_file(sigil_path.string());
    assert(wf_sigil_ptr != nullptr);
    WadFile wf_sigil = *wf_sigil_ptr;
    assert(reg.find_modules_for_wad(wf_sigil) == std::vector<std::string>{"sigil"});

    auto items_after_sigil = reg.resolve_menu_items();
    assert(items_after_sigil.size() == 2); // doom2 + sigil (plutonia remains inactive!)

    // 4. Add Eviternity.wad -> no matching module exists yet -> find_modules_for_wad returns empty
    const WadFile* wf_evit_ptr = reg.register_wad_file(eviternity_path.string());
    assert(wf_evit_ptr != nullptr);
    WadFile wf_evit = *wf_evit_ptr;
    assert(reg.find_modules_for_wad(wf_evit).empty());

    // 5. Create user module for Eviternity.wad -> writes to usr_mods/games/eviternity.toml and activates it!
    UserModuleSpec spec;
    spec.title = "Eviternity";
    spec.base_game = "doom2";
    spec.pwads = { wf_evit.filename };
    spec.pwad_sha1 = { eviternity_sha1 };
    spec.compat_level = "11";
    spec.requires_capability = { "mbf" };

    std::string created_toml_path;
    assert(reg.create_user_module(spec, created_toml_path, usr_mods.string()));
    assert(fs::exists(created_toml_path));
    assert(reg.find_modules_for_wad(wf_evit) == std::vector<std::string>{"eviternity"});

    auto items_after_create = reg.resolve_menu_items();
    assert(items_after_create.size() == 3); // doom2, sigil, eviternity

    bool found_evit = false;
    for (const auto& it : items_after_create) {
        if (it.module_name == "eviternity") {
            found_evit = true;
            assert(it.title == "Eviternity");
            assert(it.cmd.find("-complevel 11") != std::string::npos);
            assert(it.cmd.find(wf_evit.path) != std::string::npos);
            assert(it.cmd.find(wf_doom2.path) != std::string::npos);
        }
    }
    assert(found_evit);

    fs::remove_all(wad_dir);
    fs::remove_all(sys_mods);
    fs::remove_all(usr_mods);

    std::cout << "Phase 3 WAD-Driven Module Activation & User Module Creation Tests passed!" << std::endl << std::endl;
}

void test_phase4_add_wad_and_create_module_dialogs() {
    std::cout << "--- Running Phase 4 Add WAD & Create Module UI Dialog Tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path test_root = "temp_p4_ui_test";
    fs::remove_all(test_root);
    fs::path wad_dir = test_root / "wads";
    fs::path sys_mods = test_root / "sys_mods";
    fs::path usr_mods = test_root / "usr_mods";
    fs::path cfg_file = test_root / "config.toml";
    fs::create_directories(wad_dir / "subdir");
    fs::create_directories(sys_mods / "engines");
    fs::create_directories(sys_mods / "games");
    fs::create_directories(usr_mods / "games");

    fs::path doom2_path = wad_dir / "DOOM2.WAD";
    fs::path evit_path = wad_dir / "eviternity.wad";
    {
        std::ofstream f(doom2_path, std::ios::binary);
        f << "IWAD_DOOM2_PHASE4_TEST_CONTENT";
    }
    {
        std::ofstream f(evit_path, std::ios::binary);
        f << "PWAD_EVITERNITY_PHASE4_TEST_CONTENT";
    }

    std::string doom2_sha1 = WadCatalog::compute_file_sha1(doom2_path.string());

    {
        std::ofstream f(sys_mods / "engines" / "dsda.toml");
        f << R"TOML(
[dsda-doom]
type = "engine"
title = "DSDA-Doom"
binary = "sh"
capabilities = ["vanilla", "boom", "mbf", "mbf21"]
cmd = "{{BIN}} -iwad {{IWAD}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %} -savedir {{SAVEDIR}}"
)TOML";
    }
    {
        std::ofstream f(sys_mods / "games" / "doom2.toml");
        f << "[doom2]\n"
          << "type = \"game\"\n"
          << "title = \"Doom II: Hell on Earth\"\n"
          << "iwad = \"DOOM2.WAD\"\n"
          << "iwad_sha1 = [\"" << doom2_sha1 << "\"]\n"
          << "compat_level = 2\n"
          << "requires_capability = [\"vanilla\"]\n";
    }

    UserConfig cfg;
    cfg.module_dirs = { sys_mods.string(), usr_mods.string() };
    cfg.default_engine = "dsda-doom";

    ModuleRegistry reg;
    reg.set_user_config(cfg);
    reg.get_wad_catalog().clear();
    reg.set_asset_resolver(AssetResolver());
    assert(reg.load_configured_modules());

    Menu menu(nullptr, nullptr, nullptr, 1024, 768);
    menu.setModuleRegistry(&reg);
    menu.setConfigSavePath(cfg_file.string());
    menu.setUserModulesDir(usr_mods.string());
    menu.setMenuItems(reg.resolve_menu_items());

    // Initially only "Quit" is in the menu (0 active games)
    assert(menu.getMenuItems().size() == 1);
    assert(menu.getMenuItems()[0].module_name == "quit");

    auto sendKey = [&](SDL_Keycode sym) {
        SDL_Event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = sym;
        menu.handleEvent(ev);
    };

    auto sendText = [&](const char* txt) {
        SDL_Event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.type = SDL_TEXTINPUT;
        std::strncpy(ev.text.text, txt, sizeof(ev.text.text) - 1);
        menu.handleEvent(ev);
    };

    // 1. Open AddWadDialog via F5 and load wad_dir
    sendKey(SDLK_F5);
    assert(menu.getOverlayState() == OverlayState::AddWadDialog);
    menu.loadBrowserDirectory(wad_dir.string());

    const auto& entries = menu.getBrowserEntries();
    // Should contain [..], subdir/, DOOM2.WAD, eviternity.wad
    assert(entries.size() == 4);
    assert(entries[0].is_parent);
    assert(entries[1].is_dir && entries[1].name == "subdir/");
    assert(!entries[2].is_dir && entries[2].name == "DOOM2.WAD");
    assert(!entries[3].is_dir && entries[3].name == "eviternity.wad");

    // 2. Navigate down to DOOM2.WAD (index 2) and press Enter -> activates existing module 'doom2'!
    sendKey(SDLK_DOWN); // index 1 (subdir/)
    sendKey(SDLK_DOWN); // index 2 (DOOM2.WAD)
    assert(menu.getBrowserCursorIndex() == 2);
    sendKey(SDLK_RETURN);

    assert(menu.getOverlayState() == OverlayState::None);
    assert(menu.getMenuItems().size() == 2); // doom2 + quit
    assert(menu.getMenuItems()[menu.getSelectedItem()].module_name == "doom2");
    assert(menu.getStatusMessage().find("Activated module: Doom II: Hell on Earth") != std::string::npos);
    assert(fs::exists(cfg_file));

    // 3. Press F5 again, select eviternity.wad (index 3), press Enter -> opens CreateModuleForm!
    sendKey(SDLK_F5);
    assert(menu.getOverlayState() == OverlayState::AddWadDialog);
    menu.setBrowserCursorIndex(3); // eviternity.wad
    sendKey(SDLK_RETURN);

    assert(menu.getOverlayState() == OverlayState::CreateModuleForm);
    assert(menu.getFormTitle() == "Eviternity");
    assert(!menu.getFormBaseOptions().empty());
    // DOOM2.WAD should be prioritized at index 0
    assert(menu.getFormBaseOptions()[0].wad.filename == "DOOM2.WAD");
    assert(menu.getFormBaseOptions()[0].base_game_module == "doom2");

    // Edit title via Backspace + TextInput ("Eviternity" -> "Eviternity RC1")
    sendText(" RC1");
    assert(menu.getFormTitle() == "Eviternity RC1");

    // Move to Complevel field (field 3) and cycle Right to '11 (MBF)' (index 5)
    menu.setFormFieldIndex(3);
    for (int i = 0; i < 5; ++i) {
        sendKey(SDLK_RIGHT);
    }
    assert(menu.getFormComplevelIndex() == 5); // 11 (MBF)
    assert(menu.getFormCapabilityIndex() == 3); // mbf

    // Move to Save button (field 5) and press Enter -> creates usr_mods/games/eviternity_rc1.toml & activates!
    menu.setFormFieldIndex(5);
    sendKey(SDLK_RETURN);

    assert(menu.getOverlayState() == OverlayState::None);
    assert(fs::exists(usr_mods / "games" / "eviternity_rc1.toml"));
    assert(menu.getMenuItems().size() == 3); // doom2, eviternity_rc1, quit
    assert(menu.getMenuItems()[menu.getSelectedItem()].module_name == "eviternity_rc1");
    assert(menu.getMenuItems()[menu.getSelectedItem()].title == "Eviternity RC1");
    assert(menu.getStatusMessage().find("Created & activated module: Eviternity RC1") != std::string::npos);

    fs::remove_all(test_root);

    std::cout << "Phase 4 Add WAD & Create Module UI Dialog Tests passed!" << std::endl << std::endl;
}

void test_phase5_end_to_end_verification() {
    std::cout << "--- Running Phase 5 End-to-End WAD & Module Lifecycle Verification Tests ---" << std::endl;
    namespace fs = std::filesystem;

    fs::path test_root = "temp_p5_e2e_test";
    fs::remove_all(test_root);
    fs::path wad_dir = test_root / "wads";
    fs::path sys_mods = test_root / "sys_mods";
    fs::path usr_mods = test_root / "usr_mods";
    fs::path cfg_file = test_root / "config.toml";
    fs::path state_file = test_root / "state.toml";

    fs::create_directories(wad_dir);
    fs::create_directories(sys_mods / "engines");
    fs::create_directories(sys_mods / "games");
    fs::create_directories(usr_mods / "games");

    // Create DOOM2.WAD, DOOM.WAD, a custom standalone IWAD (FREEDOOM2.WAD), and a multi-PWAD combo (sunlust.wad + sunlust_music.wad)
    fs::path doom2_path = wad_dir / "DOOM2.WAD";
    fs::path doom1_path = wad_dir / "DOOM.WAD";
    fs::path freedoom_path = wad_dir / "freedoom2.wad";
    fs::path sunlust_path = wad_dir / "sunlust.wad";
    fs::path sunlust_mus_path = wad_dir / "sunlust_music.wad";

    {
        std::ofstream f(doom2_path, std::ios::binary);
        f << "IWAD_DOOM2_E2E_BYTES";
    }
    {
        std::ofstream f(doom1_path, std::ios::binary);
        f << "IWAD_DOOM1_E2E_BYTES";
    }
    {
        std::ofstream f(freedoom_path, std::ios::binary);
        f << "IWAD_FREEDOOM2_E2E_BYTES";
    }
    {
        std::ofstream f(sunlust_path, std::ios::binary);
        f << "PWAD_SUNLUST_MAIN_E2E_BYTES";
    }
    {
        std::ofstream f(sunlust_mus_path, std::ios::binary);
        f << "PWAD_SUNLUST_MUSIC_E2E_BYTES";
    }

    std::string doom2_sha1 = WadCatalog::compute_file_sha1(doom2_path.string());
    std::string doom1_sha1 = WadCatalog::compute_file_sha1(doom1_path.string());

    {
        std::ofstream f(sys_mods / "engines" / "dsda.toml");
        f << R"TOML(
[dsda-doom]
type = "engine"
title = "DSDA-Doom"
binary = "sh"
capabilities = ["vanilla", "limit-removing", "boom", "mbf", "mbf21", "complevel"]
cmd = "{{BIN}} -iwad {{IWAD}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %} -save {{SAVEDIR}}"
)TOML";
    }
    {
        std::ofstream f(sys_mods / "games" / "doom2.toml");
        f << "[doom2]\n"
          << "type = \"game\"\n"
          << "title = \"Doom II: Hell on Earth\"\n"
          << "iwad = \"DOOM2.WAD\"\n"
          << "iwad_sha1 = [\"" << doom2_sha1 << "\"]\n"
          << "compat_level = 2\n"
          << "requires_capability = [\"vanilla\"]\n";
    }
    {
        std::ofstream f(sys_mods / "games" / "doom1.toml");
        f << "[doom]\n"
          << "type = \"game\"\n"
          << "title = \"The Ultimate Doom\"\n"
          << "iwad = \"DOOM.WAD\"\n"
          << "iwad_sha1 = [\"" << doom1_sha1 << "\"]\n"
          << "compat_level = 3\n"
          << "requires_capability = [\"vanilla\"]\n";
    }

    // Write initial config.toml
    UserConfig initial_cfg;
    initial_cfg.module_dirs = { sys_mods.string(), usr_mods.string() };
    initial_cfg.default_engine = "dsda-doom";
    assert(AssetResolver::save_config(initial_cfg, cfg_file.string()));

    // Session 1: Load config, add DOOM.WAD + DOOM2.WAD, then multi-select sunlust.wad + sunlust_music.wad, then add standalone freedoom2.wad
    {
        ModuleRegistry reg;
        reg.load_state(state_file.string());
        assert(reg.load_user_config(cfg_file.string()));
        reg.get_wad_catalog().clear();
        reg.set_asset_resolver(AssetResolver());
        assert(reg.load_configured_modules());

        Menu menu(nullptr, nullptr, nullptr, 1024, 768);
        menu.setModuleRegistry(&reg);
        menu.setConfigSavePath(cfg_file.string());
        menu.setUserModulesDir(usr_mods.string());
        menu.setMenuItems(reg.resolve_menu_items());

        auto sendKey = [&](SDL_Keycode sym) {
            SDL_Event ev;
            std::memset(&ev, 0, sizeof(ev));
            ev.type = SDL_KEYDOWN;
            ev.key.keysym.sym = sym;
            menu.handleEvent(ev);
        };

        // 1. Multi-select DOOM.WAD and DOOM2.WAD in AddWadDialog using Space + A
        menu.openAddWadDialog(wad_dir.string());
        const auto& entries = menu.getBrowserEntries();
        // Entries: [..], DOOM.WAD, DOOM2.WAD, freedoom2.wad, sunlust.wad, sunlust_music.wad
        assert(entries.size() == 6);
        menu.setBrowserCursorIndex(1); // DOOM.WAD
        sendKey(SDLK_SPACE);
        menu.setBrowserCursorIndex(2); // DOOM2.WAD
        sendKey(SDLK_SPACE);
        sendKey(SDLK_a);

        assert(menu.getOverlayState() == OverlayState::None);
        assert(menu.getMenuItems().size() == 3); // doom, doom2, quit

        // 2. Multi-select sunlust.wad (index 4) and sunlust_music.wad (index 5) using Space + A -> opens CreateModuleForm
        menu.openAddWadDialog(wad_dir.string());
        menu.setBrowserCursorIndex(4); // sunlust.wad
        sendKey(SDLK_SPACE);
        menu.setBrowserCursorIndex(5); // sunlust_music.wad
        sendKey(SDLK_SPACE);
        sendKey(SDLK_a);

        assert(menu.getOverlayState() == OverlayState::CreateModuleForm);
        assert(menu.getFormTitle() == "Sunlust");
        // Verify prioritized base WADs: DOOM2.WAD is first, DOOM.WAD is second
        assert(menu.getFormBaseOptions().size() >= 2);
        assert(menu.getFormBaseOptions()[0].wad.filename == "DOOM2.WAD");
        assert(menu.getFormBaseOptions()[1].wad.filename == "DOOM.WAD");

        // Set Complevel to 9 (Boom) (index 4) -> auto-sets capability to boom (index 2)
        menu.setFormFieldIndex(3);
        for (int i = 0; i < 4; ++i) sendKey(SDLK_RIGHT);
        assert(menu.getFormComplevelIndex() == 4);
        assert(menu.getFormCapabilityIndex() == 2);

        // Save module with F2 shortcut
        sendKey(SDLK_F2);
        assert(menu.getOverlayState() == OverlayState::None);
        assert(fs::exists(usr_mods / "games" / "sunlust.toml"));

        // 3. Add standalone IWAD freedoom2.wad (index 3) -> CreateModuleForm offers (Standalone IWAD) at index 0!
        menu.openAddWadDialog(wad_dir.string());
        menu.setBrowserCursorIndex(3); // freedoom2.wad
        sendKey(SDLK_RETURN);

        assert(menu.getOverlayState() == OverlayState::CreateModuleForm);
        assert(!menu.getFormBaseOptions().empty());
        assert(menu.getFormBaseOptions()[0].is_standalone);
        sendKey(SDLK_F2);
        assert(menu.getOverlayState() == OverlayState::None);
        assert(fs::exists(usr_mods / "games" / "freedoom2.toml"));

        // Save state (including WAD SHA-1 cache and engine selections)
        std::string savedStatePath;
        assert(reg.save_state(menu.getMenuItems(), savedStatePath, state_file.string()));
    }

    // Session 2: Simulate fresh restart of Doom Launcher loading config.toml, state.toml, and usr_mods/games/*.toml
    {
        ModuleRegistry reg2;
        assert(reg2.load_state(state_file.string()));
        assert(reg2.load_user_config(cfg_file.string()));
        assert(reg2.load_configured_modules());

        auto items = reg2.resolve_menu_items();
        // Should have 4 active games: doom, doom2, sunlust, freedoom2
        assert(items.size() == 4);

        bool found_sunlust = false;
        bool found_freedoom = false;
        for (const auto& item : items) {
            if (item.module_name == "sunlust") {
                found_sunlust = true;
                assert(item.resolved_pwad_paths.size() == 2);
                assert(item.cmd.find("-complevel 9") != std::string::npos);
                assert(item.cmd.find("sunlust.wad") != std::string::npos);
                assert(item.cmd.find("sunlust_music.wad") != std::string::npos);
                assert(item.cmd.find("-save " + ModuleRegistry::get_module_save_dir("sunlust")) != std::string::npos);
            } else if (item.module_name == "freedoom2") {
                found_freedoom = true;
                assert(item.resolved_iwad_path.find("freedoom2.wad") != std::string::npos);
                assert(item.resolved_pwad_paths.empty());
            }
        }
        assert(found_sunlust);
        assert(found_freedoom);
    }

    fs::remove_all(test_root);

    std::cout << "Phase 5 End-to-End WAD & Module Lifecycle Verification Tests passed!" << std::endl << std::endl;
}

int main() {
    test_template_engine();
    test_module_registry();
    test_real_modules();
    test_asset_resolver();
    test_module_registry_asset_resolution();
    test_engine_registry();
    test_dynamic_engine_fallback();
    test_game_module_modern_schema();
    test_dynamic_menu_item_binding();
    test_phase6_asset_resolver();
    test_phase6_engine_capability_matching();
    test_phase6_dynamic_template_generation();
    test_menu_paging();
    test_menu_osd();
    test_menu_overlays();
    test_phase5_state_persistence();
    test_gui_rendering_verification();
    test_phase1_multi_module_dirs_and_saves();
    test_phase2_wad_catalog_and_sha1();
    test_phase3_wad_driven_module_activation();
    test_phase4_add_wad_and_create_module_dialogs();
    test_phase5_end_to_end_verification();
    std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
