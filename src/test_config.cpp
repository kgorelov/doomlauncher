#include "config_module.h"
#include "template_engine.h"
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
    }
    assert(menu_items.size() > 0);
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

int main() {
    test_template_engine();
    test_module_registry();
    test_real_modules();
    test_asset_resolver();
    test_module_registry_asset_resolution();
    std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
