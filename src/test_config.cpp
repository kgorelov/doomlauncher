#include "config_module.h"
#include "template_engine.h"
#include <cassert>
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
name = "boom"
title = "Boom Engine Base"
is_menu_item = false

[vars]
BOOM_BIN = "/usr/bin/boom"

[templates]
CMD = "{{BOOM_BIN}} -iwad {{IWAD_PATH}} {% if COMPAT_LEVEL %}-complevel {{COMPAT_LEVEL}}{% endif %} {% if PWADS %}{% for wad in PWADS %}-file {{wad}} {% endfor %}{% endif %}-savedir {{MODULEDIR}}/.saves"
)";
    }

    {
        std::ofstream doom2_toml("test_doom2.toml");
        doom2_toml << R"(
name = "doom2"
title = "Doom II: Hell on Earth"
is_menu_item = true
requires = ["boom"]

[vars]
IWAD_PATH = "$MODULEDIR/DOOM2.WAD"
COMPAT_LEVEL = 2
)";
    }

    {
        std::ofstream plutonia_toml("test_plutonia.toml");
        plutonia_toml << R"(
name = "plutonia"
title = "Final Doom: Plutonia Experiment"
is_menu_item = true
requires = ["doom2"]

[vars]
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

int main() {
    test_template_engine();
    test_module_registry();
    test_real_modules();
    std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
