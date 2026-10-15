#include "config_module.h"
#include "menu.h"
#include "template_engine.h"
#include <algorithm>
#include <cassert>
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

    // Create temporary test files
    {
        std::ofstream boom_toml("test_boom.toml");
        boom_toml << R"(
[boom]
BOOM_BIN = "/usr/bin/boom"
CMD = "{{BOOM_BIN}} -iwad {{IWAD_PATH}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %} {% if PWADS %}{% for wad in PWADS %}-file {{wad}} {% endfor %}{% endif %}-savedir {{MODULE_DIR}}/.saves"
)";
    }

    {
        std::ofstream doom2_toml("test_doom2.toml");
        doom2_toml << R"(
[doom2]
title = "Doom II: Hell on Earth"
requires = ["boom"]
IWAD_PATH = "{{MODULE_DIR}}/DOOM2.WAD"
COMPAT_LEVEL = 2
)";
    }

    {
        std::ofstream plutonia_toml("test_plutonia.toml");
        plutonia_toml << R"(
[plutonia]
title = "Final Doom: Plutonia Experiment"
requires = ["doom2"]
PWADS = ["PLUTONIA.WAD"]
)";
    }

    ModuleRegistry registry;
    assert(registry.load_file("test_boom.toml"));
    assert(registry.load_file("test_doom2.toml"));
    assert(registry.load_file("test_plutonia.toml"));

    std::vector<ResolvedMenuItem> menu_items = registry.resolve_menu_items();

    std::cout << "Resolved Menu Items Count: " << menu_items.size() << std::endl;
    for (const auto& item : menu_items) {
        std::cout << "Menu Item: '" << item.title << "' (" << item.module_name << ")" << std::endl;
        std::cout << "  CMD: " << item.cmd << std::endl;
    }

    assert(menu_items.size() == 2); // doom2 and plutonia

    // Clean up temporary test files
    std::remove("test_boom.toml");
    std::remove("test_doom2.toml");
    std::remove("test_plutonia.toml");

    std::cout << "ModuleRegistry tests passed!" << std::endl << std::endl;
}

void test_real_modules() {
    std::cout << "--- Testing modules/main.toml ---" << std::endl;
    ModuleRegistry registry;
    bool loaded = registry.load_file("modules/main.toml");
    if (!loaded) {
        loaded = registry.load_file("../modules/main.toml");
    }
    assert(loaded);

    std::vector<ResolvedMenuItem> menu_items = registry.resolve_menu_items();
    std::cout << "Loaded real modules count: " << menu_items.size() << std::endl;
    for (const auto& item : menu_items) {
        std::cout << "Menu Item: '" << item.title << "' (" << item.module_name << ")" << std::endl;
        std::cout << "  CMD: " << item.cmd << std::endl;
        assert(!item.cmd.empty());
    }
    assert(menu_items.size() == 5);
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

    // 5. Test UserConfig TOML loading
    {
        std::ofstream cfg("temp_config.toml");
        cfg << R"(
[paths]
wad_dirs = ["temp_test_assets"]

[preferences]
default_engine = "dsda-doom"

[iwads]
doom2 = "temp_test_assets/DOOM2.WAD"
)";
    }

    UserConfig user_cfg = AssetResolver::load_config("temp_config.toml");
    assert(user_cfg.wad_dirs.size() == 1);
    assert(user_cfg.wad_dirs[0] == "temp_test_assets");
    assert(user_cfg.default_engine == "dsda-doom");
    assert(user_cfg.iwads.find("doom2") != user_cfg.iwads.end());

    // Clean up
    fs::remove_all(temp_dir);
    fs::remove("temp_config.toml");

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
CMD = "engine -iwad {{IWAD_PATH}} {% if PWADS %}-file {{PWADS[0]}}{% endif %}"
)";
    }

    // Game module using logical filename "doom2.wad" (lowercase, whereas file on disk is DOOM2.WAD)
    {
        std::ofstream f("test_game.toml");
        f << R"(
[game1]
title = "Test Game Found"
requires = ["eng"]
IWAD_PATH = "doom2.wad"
)";
    }

    // Game module with missing asset
    {
        std::ofstream f("test_missing.toml");
        f << R"(
[game2]
title = "Test Game Missing"
requires = ["eng"]
IWAD_PATH = "missing_iwad.wad"
)";
    }

    ModuleRegistry registry;
    registry.get_asset_resolver().add_search_dir(temp_dir.string());

    assert(registry.load_file("test_eng.toml"));
    assert(registry.load_file("test_game.toml"));
    assert(registry.load_file("test_missing.toml"));

    std::vector<ResolvedMenuItem> items = registry.resolve_menu_items();
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
cmd = "{{BIN}} -iwad {{IWAD_PATH}}"
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
cmd = "{{BIN}} -iwad {{IWAD_PATH}} -complevel {{COMPAT_LEVEL}}"
)";
    }

    // Game has NO requires = ["..."], NO CMD template defined!
    {
        std::ofstream f("test_dyn_game.toml");
        f << R"(
[dyn_game]
title = "Dynamic Decoupled Game"
IWAD_PATH = "doom2.wad"
COMPAT_LEVEL = 2
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
cmd = "{{BIN}} -iwad {{IWAD_PATH}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %}"
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
cmd = "{{BIN}} -iwad {{IWAD_PATH}} --engine1"
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
cmd = "{{BIN}} -iwad {{IWAD_PATH}} --engine2"
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

    std::vector<ResolvedMenuItem> items = registry.resolve_menu_items();
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
cmd = "{{BIN}} -iwad {{IWAD_PATH}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %}{% if SAVEDIR %} -save {{SAVEDIR}}{% endif %}"
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
cmd = "{{BIN}} -iwad {{IWAD_PATH}}{% if PWADS %}{% for wad in PWADS %} -file {{wad}}{% endfor %}{% endif %}{% if SAVEDIR %} -savedir {{SAVEDIR}}{% endif %}"
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
cmd = "{{BIN}} -iwad {{IWAD_PATH}}{% if PWADS %}{% for wad in PWADS %} -merge {{wad}}{% endfor %}{% endif %}{% if SAVEDIR %} -savedir {{SAVEDIR}}{% endif %}"
)";
    }

    // Game requiring 'vanilla' capability, compat_level 2, PWAD SIGIL.WAD, and savedir
    {
        std::ofstream f("test_p6_game.toml");
        f << R"(
[p6_game]
type = "game"
title = "Phase 6 Game"
iwad = "DOOM2.WAD"
pwads = ["SIGIL.WAD"]
compat_level = 2
savedir = "/tmp/doom_saves"
requires_capability = ["vanilla"]
)";
    }

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

    // 1. Select Boom engine: verify -complevel 2, -file, and -save are present
    assert(item.select_engine("p6-boom"));
    std::string boom_cmd = item.get_command();
    std::cout << "Boom generated command: " << boom_cmd << std::endl;
    assert(boom_cmd.find("-iwad") != std::string::npos);
    assert(boom_cmd.find("-complevel 2") != std::string::npos);
    assert(boom_cmd.find("-file") != std::string::npos);
    assert(boom_cmd.find("SIGIL.WAD") != std::string::npos);
    assert(boom_cmd.find("-save /tmp/doom_saves") != std::string::npos);
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
    assert(gz_cmd.find("-savedir /tmp/doom_saves") != std::string::npos);
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
    assert(choco_cmd.find("-savedir /tmp/doom_saves") != std::string::npos);

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
cmd_template = "{{BIN}} -iwad {{IWAD}}"
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
cmd_template = "{{BIN}} -iwad {{IWAD}}"
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
    std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
