#include "platform/linux/resource_pack_store.h"
#include "platform/linux/runtime_paths.h"
#include "ui/localization.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

int main()
{
    namespace fs = std::filesystem;
    char temporary[] = "/tmp/trailmate-resource-root-XXXXXX";
    assert(mkdtemp(temporary));
    const fs::path base(temporary);
    setenv("TRAIL_MATE_SETTINGS_ROOT", temporary, 1);
    setenv("TRAIL_MATE_SD_ROOT", (base / "data").c_str(), 1);
    unsetenv("TRAIL_MATE_RESOURCE_PACK_ROOT");
    assert(platform::linux_runtime::resource_pack_root() == base / "data/trailmate/packs");
    const auto root = base / "custom resources";
    setenv("TRAIL_MATE_RESOURCE_PACK_ROOT", root.c_str(), 1);
    assert(platform::linux_runtime::resource_pack_root() == root);
    fs::create_directories(root / "locales/test-locale");
    std::ofstream(root / "locales/test-locale/manifest.ini")
        << "kind=locale\nid=test-locale\ndisplay_name=Test locale\ntranslation_status=release\nstrings=strings.tsv\n";
    std::ofstream(root / "locales/test-locale/strings.tsv") << "Settings\tCustom settings\n";
    lv_init();
    assert(ui::i18n::configure_external_pack_root(root.string()));
    assert(ui::i18n::locale_count() == 2);
    bool found = false;
    for (size_t i = 0; i < ui::i18n::locale_count(); ++i)
        found = found || !std::strcmp(ui::i18n::locale_at(i)->id, "test-locale");
    assert(found);
    assert(ui::i18n::set_locale("test-locale", false));
    assert(!std::strcmp(ui::i18n::tr("Settings"), "Custom settings"));
    // Exercise the actual store-to-registry path, including manual-resource preservation.
    const auto bundle = base / "catalog/bundle";
    fs::create_directories(bundle / "locales/installed-locale");
    std::ofstream(bundle / "package.ini") << "kind=package\nid=bundle\nversion=1.0\n";
    std::ofstream(bundle / "locales/installed-locale/manifest.ini")
        << "kind=locale\nid=installed-locale\ntranslation_status=release\nstrings=strings.tsv\n";
    std::ofstream(bundle / "locales/installed-locale/strings.tsv") << "Settings\tInstalled settings\n";
    ui::runtime::packs::PackageRecord package;
    package.id = "bundle";
    package.archive_path = bundle;
    platform::linux_runtime::ResourcePackStore store(root);
    std::string error;
    assert(store.install(package, error));
    ui::i18n::reload_language();
    assert(ui::i18n::locale_count() == 3);
    assert(ui::i18n::set_locale("installed-locale", false));
    assert(!std::strcmp(ui::i18n::tr("Settings"), "Installed settings"));
    assert(store.uninstall(package.id, error));
    ui::i18n::reload_language();
    assert(ui::i18n::locale_count() == 2);
    assert(!ui::i18n::set_locale("installed-locale", false));
    // Changing roots after resource pointers have escaped must be rejected.
    assert(ui::i18n::configure_external_pack_root(root.string()));
    assert(!ui::i18n::configure_external_pack_root((base / "other").string()));
    lv_deinit();
    fs::remove_all(base);
}
