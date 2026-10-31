#include "ui/assets/fonts/bundled_font_root.h"
#include "ui/localization.h"
#include "ui/runtime/memory_profile.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

int main(int argc, char** argv)
{
    assert(argc == 3 || (argc == 4 && !std::strcmp(argv[3], "--user-override")));
    const bool override = argc == 4;
    namespace fs = std::filesystem;
    char temporary[] = "/tmp/trailmate-bundled-font-XXXXXX";
    assert(mkdtemp(temporary));
    const fs::path base(temporary), user = base / "user", bundled = base / "system";
    const auto font = bundled / "fonts/ja-broadcast-cjk";
    fs::create_directories(font);
    fs::create_directories(user);
    fs::copy_file(argv[1], font / "font.bin");
    for (const auto* name : {"manifest.ini", "ranges.txt"})
        fs::copy_file(fs::path(argv[2]) / name, font / name);
    if (override)
    {
        const auto preferred = user / "fonts/ja-broadcast-cjk";
        fs::create_directories(preferred);
        // A valid, deliberately limited user replacement: keep user precedence
        // even when that choice cannot supply the requested Japanese text.
        std::ofstream(preferred / "manifest.ini") << "kind=font\nid=ja-broadcast-cjk\nusage=content\nsource=builtin\nbuiltin_id=builtin-latin-ui\n";
    }
    setenv("TRAIL_MATE_SETTINGS_ROOT", temporary, 1);
    setenv("TRAIL_MATE_SD_ROOT", (base / "data").c_str(), 1);
    lv_init();
    assert(ui::i18n::configure_external_pack_root(user.string()));
    assert(ui::i18n::configure_bundled_font_root(bundled.string()));
    const auto& profile = ui::runtime::current_memory_profile();
    const bool eligible = profile.max_content_supplement_packs && profile.max_content_supplement_ram_bytes >= 655360;
    const bool covered = ui::i18n::prepare_content_font_for_text("試験放送");
    assert(covered == (eligible && !override));
    if (override)
    {
        fs::remove(user / "fonts/ja-broadcast-cjk/manifest.ini");
        fs::remove(user / "fonts/ja-broadcast-cjk");
        ui::i18n::reload_language();
        assert(ui::i18n::prepare_content_font_for_text("試験放送") == eligible);
    }
    if (!override) assert(fs::is_empty(user)); // Bundling does not copy files into user storage.
    assert(ui::i18n::configure_bundled_font_root(bundled.string()));
    assert(!ui::i18n::configure_bundled_font_root((base / "different").string()));
    assert(!ui::i18n::configure_external_pack_root((base / "different").string()));
    lv_deinit();
    fs::remove_all(base);
    std::cout << "Bundled font root, user precedence and initialization invariants passed\n";
}
