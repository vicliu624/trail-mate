#include "platform/linux/resource_pack_store.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/pack_repository_runtime.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using platform::linux_runtime::ResourcePackStore;
using ui::runtime::packs::PackageRecord;

void write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path);
    file << text;
    file.close();
    assert(file);
}
std::string read(const fs::path& path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}
PackageRecord package(const fs::path& catalog, const std::string& id, const std::string& locale)
{
    const auto directory = catalog / id;
    write(directory / "package.ini", "kind=package\nid=" + id + "\nversion=1.0\n");
    write(directory / "locales" / locale / "manifest.ini", "kind=locale\nid=" + locale + "\nstrings=strings.tsv\n");
    write(directory / "locales" / locale / "strings.tsv", "Settings\tCustom settings\n");
    PackageRecord result;
    result.id = id;
    result.version = "1.0";
    result.archive_path = directory;
    return result;
}
int main()
{
    char directory[] = "/tmp/trailmate-pack-store-XXXXXX";
    assert(mkdtemp(directory));
    const fs::path base(directory), root = base / "installed resources", catalog = base / "catalog";
    setenv("TRAIL_MATE_RESOURCE_PACK_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_PACK_ROOT", catalog.c_str(), 1);
    setenv("TRAIL_MATE_SETTINGS_ROOT", (base / "state").c_str(), 1);
    ResourcePackStore store(root);
    auto first = package(catalog, "first", "one");
    std::string error;
    assert(store.install(first, error));
    assert(read(root / "locales/one/strings.tsv") == "Settings\tCustom settings\n");
    std::vector<ui::runtime::packs::InstalledPackageRecord> installed;
    assert(store.installed(installed, error) && installed.size() == 1 && installed[0].version == "1.0");
    // Records persist independently of in-process state and of the source catalog.
    auto child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        std::vector<ui::runtime::packs::InstalledPackageRecord> reopened;
        std::string failure;
        _exit(ResourcePackStore(root).installed(reopened, failure) && reopened.size() == 1 ? 0 : 1);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    auto second = package(catalog, "second", "two");
    // A byte-identical resource can be shared; removing one owner keeps it alive.
    fs::copy(fs::path(first.archive_path) / "locales/one", fs::path(second.archive_path) / "locales/one", fs::copy_options::recursive);
    assert(store.install(second, error));
    assert(store.uninstall(first.id, error));
    assert(fs::exists(root / "locales/one/strings.tsv"));
    assert(store.uninstall(second.id, error));
    assert(!fs::exists(root / "locales/one") && !fs::exists(root / "locales/two"));
    assert(store.install(first, error));
    write(fs::path(second.archive_path) / "locales/one/strings.tsv", "Settings\tConflicting text\n");
    assert(!store.install(second, error));
    assert(read(root / "locales/one/strings.tsv") == "Settings\tCustom settings\n");
    assert(store.installed(installed, error) && installed.size() == 1);
    // A failed publication leaves both previous content and installed metadata intact.
    auto third = package(catalog, "third", "three");
    ResourcePackStore failing(root, []
                              { throw std::runtime_error("injected publication failure"); });
    assert(!failing.install(third, error));
    assert(!fs::exists(root / "locales/three") && store.installed(installed, error) && installed.size() == 1);
    child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        ResourcePackStore interrupted(root, []
                                      { _exit(42); });
        interrupted.install(third, error);
        _exit(1);
    }
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 42);
    assert(!fs::exists(root / "locales/three") && store.installed(installed, error) && installed.size() == 1);
    // Failed dependencies, missing payloads and links never create installation records.
    write(fs::path(third.archive_path) / "locales/three/manifest.ini", "kind=locale\nid=three\nstrings=missing.tsv\n");
    assert(!store.install(third, error));
    write(fs::path(third.archive_path) / "locales/three/manifest.ini", "kind=locale\nid=three\nstrings=strings.tsv\nui_font_pack=absent\n");
    assert(!store.install(third, error));
    fs::create_symlink(base / "outside", fs::path(third.archive_path) / "link");
    assert(!store.install(third, error));
    assert(!store.uninstall("../first", error));
    // Missing source catalog does not hide installed records or uninstall support.
    fs::rename(catalog, base / "offline");
    std::vector<PackageRecord> records;
    assert(ui::runtime::packs::is_supported());
    assert(ui::runtime::packs::fetch_catalog(records, error) && records.size() == 1 && records[0].installed);
    assert(ui::runtime::packs::uninstall_package(records[0], error));
    assert(store.installed(installed, error) && installed.empty());
    // The UI-facing start operation completes on its owner worker.
    fs::rename(base / "offline", catalog);
    assert(ui::runtime::packs::start_install_package(first, error));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (ui::runtime::packs::install_status().busy && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(ui::runtime::packs::install_status().phase == ui::runtime::packs::PackageInstallPhase::Succeeded);
    assert(store.installed(installed, error) && installed.size() == 1);
    assert(store.uninstall(first.id, error));
    // Pre-existing manually deployed resources are retained, never attributed to a package.
    // Caller flags cannot bypass requirements in the captured package manifest.
    write(fs::path(first.archive_path) / "package.ini", "kind=package\nid=first\nversion=1.1\nmin_firmware_version=999999.0.0\n");
    assert(!store.install(first, error));
    assert(store.installed(installed, error) && installed.empty());
    write(fs::path(first.archive_path) / "package.ini", "kind=package\nid=first\nversion=1.1\nsupported_memory_profiles=unavailable-profile\n");
    assert(!store.install(first, error));
    write(fs::path(first.archive_path) / "package.ini", "kind=package\nid=first\nversion=1.0\n");
    const auto manual = base / "manual";
    write(manual / "locales/manual/manifest.ini", "kind=locale\nid=manual\nstrings=strings.tsv\n");
    write(manual / "locales/manual/strings.tsv", "Settings\tManual text\n");
    ResourcePackStore imported(manual);
    assert(imported.install(first, error) && imported.uninstall(first.id, error));
    assert(read(manual / "locales/manual/strings.tsv") == "Settings\tManual text\n");
    fs::remove_all(base);
}
