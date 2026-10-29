#include "platform/linux/remote_pack_repository.h"
#include "platform/linux/resource_pack_policy.h"
#include "platform/linux/resource_pack_store.h"
#include "platform/linux/resource_pack_tasks.h"
#include "platform/ui/http_client_runtime.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

namespace native = platform::linux_runtime;
namespace packs = ui::runtime::packs;
std::string get(const std::string& url)
{
    platform::ui::http_client::Request request;
    request.url = url.c_str();
    request.max_bytes = 2 * 1024 * 1024;
    std::string output, error;
    assert(platform::ui::http_client::get_text(request, output, error));
    return output;
}
int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 3);
    const std::string base = argv[1];
    assert(native::configure_resource_pack_policy({"1.0.0", "balanced"}));
    std::vector<packs::PackageRecord> catalog;
    std::string error;
    if (argc == 3)
    {
        // A new process must recover catalog metadata from disk when HTTP fails.
        packs::request_catalog_refresh();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (packs::catalog_status().busy && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(packs::fetch_catalog(catalog, error) && catalog.size() == 1 && catalog[0].display_name == "Remote test" && !catalog[0].download_url.empty());
        native::stop_resource_pack_tasks();
        return 0;
    }
    const auto text = get(base + "/catalog.json");
    assert(native::parse_pack_catalog(text, base, catalog, error) && catalog.size() == 1);
    const auto package = catalog[0];
    assert(package.id == "remote.test" && package.version == "1.0.0" && package.provided_locale_ids == std::vector<std::string>{"remote-test"});
    assert(!native::parse_pack_catalog(text + "junk", base, catalog, error));
    assert(!native::parse_pack_catalog("{\"packages\":[]}", base, catalog, error));
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-remote-pack-" + std::to_string(getpid()));
    const auto installed = root / "installed";
    std::filesystem::create_directories(root);
    {
        auto staged = native::stage_remote_pack(package, root / "cache", installed, error);
        assert(staged && std::filesystem::is_regular_file(staged->directory() / "package.ini"));
        auto ready = package;
        ready.archive_path = staged->directory();
        assert(native::ResourcePackStore(installed).install(ready, error));
    }
    assert(std::filesystem::is_regular_file(installed / "locales/remote-test/strings.tsv"));
    std::vector<packs::InstalledPackageRecord> records;
    assert(native::ResourcePackStore(installed).installed(records, error) && records.size() == 1 && records[0].archive_sha256 == package.archive_sha256);
    {
        auto staged = native::stage_remote_pack(package, root / "cache", installed, error);
        assert(staged);
        auto ready = package;
        ready.archive_path = staged->directory();
        ready.version = "different";
        assert(!native::ResourcePackStore(installed).install(ready, error));
        ready.version = package.version;
        std::atomic_bool cancel_publish{false};
        assert(!native::ResourcePackStore(installed, [&] { cancel_publish = true; }, [&] { return cancel_publish.load(); }).install(ready, error));
        assert(native::ResourcePackStore(installed).installed(records, error) && records[0].archive_sha256 == package.archive_sha256);
    }
    auto wrong = package;
    wrong.archive_sha256 = std::string(64, '0');
    assert(!native::stage_remote_pack(wrong, root / "cache", installed, error));
    wrong = package;
    ++wrong.archive_size_bytes;
    assert(!native::stage_remote_pack(wrong, root / "cache", installed, error));
    wrong = package;
    wrong.min_firmware_version = "99.0.0";
    assert(!native::stage_remote_pack(wrong, root / "cache", installed, error));
    std::atomic_bool cancelled{true};
    assert(!native::stage_remote_pack(package, root / "cache", installed, error, &cancelled));
    assert(!native::stage_remote_pack(package, installed / "cache", installed, error));
    assert(native::parse_pack_catalog(get(base + "/cases.json"), base, catalog, error));
    for (size_t index = 1; index < catalog.size(); ++index)
        assert(!native::stage_remote_pack(catalog[index], root / "cache", installed, error));
    assert(!std::filesystem::exists(root / "outside"));
    for (const auto& entry : std::filesystem::directory_iterator(root / "cache/pack-downloads"))
        assert(false && "Staging directory leaked");
    setenv("TRAIL_MATE_RESOURCE_PACK_ROOT", installed.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "state-cache").c_str(), 1);
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "state").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_PACK_CATALOG_URL", (base + "/catalog.json").c_str(), 1);
    setenv("TRAIL_MATE_PACK_BASE_URL", base.c_str(), 1);
    unsetenv("TRAIL_MATE_PACK_ROOT");
    const auto started = std::chrono::steady_clock::now();
    packs::request_catalog_refresh();
    (void)packs::fetch_catalog(catalog, error);
    assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
    const auto deadline = started + std::chrono::seconds(5);
    while (packs::catalog_status().busy && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!packs::catalog_status().busy && packs::fetch_catalog(catalog, error) && catalog.size() == 1);
    assert(catalog[0].installed && !catalog[0].update_available && !catalog[0].download_url.empty());
    assert(packs::start_install_package(catalog[0], error));
    while (packs::install_status().busy && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(packs::install_status().phase == packs::PackageInstallPhase::Succeeded && packs::install_status().progress_percent == 100);
    auto slow = package;
    slow.download_url = base + "/slow.zip";
    assert(packs::start_install_package(slow, error));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto stopped = std::chrono::steady_clock::now();
    native::stop_resource_pack_tasks();
    assert(std::chrono::steady_clock::now() - stopped < std::chrono::seconds(3));
    assert(!packs::install_status().busy && packs::install_status().phase == packs::PackageInstallPhase::Failed);
    assert(native::ResourcePackStore(installed).installed(records, error) && records[0].archive_sha256 == package.archive_sha256);
    (void)get(base + "/catalog-offline");
    const auto child = fork();
    assert(child >= 0);
    if (child == 0) { execl(argv[0], argv[0], base.c_str(), root.c_str(), nullptr); _exit(127); }
    int child_status = 0;
    assert(waitpid(child, &child_status, 0) == child && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
    setenv("TRAIL_MATE_PACK_CATALOG_URL", (base + "/slow-catalog.json").c_str(), 1);
    packs::request_catalog_refresh();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto catalog_stopped = std::chrono::steady_clock::now();
    native::stop_resource_pack_tasks();
    assert(std::chrono::steady_clock::now() - catalog_stopped < std::chrono::seconds(3));
    assert(!packs::catalog_status().busy);
    std::filesystem::remove_all(root);
}
