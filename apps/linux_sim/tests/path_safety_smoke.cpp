#include "platform/linux/capability_status.h"
#include "platform/linux/runtime_paths.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{

void set_env_var(const char* name, const std::string& value)
{
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void test_resolve_paths()
{
    namespace lnx = platform::linux_runtime;

    const auto paths = lnx::resolve_paths();
    assert(!paths.settings_root.empty());
    assert(!paths.sd_root.empty());
    assert(!paths.cache_root.empty());

    // sd_root should be a child of or equal to settings_root, or an explicit
    // override.  We only check that both are non-empty paths.
    assert(!paths.sd_root.string().empty());
    assert(!paths.settings_root.string().empty());

    std::printf("[path-safety] resolve_paths OK\n");
}

void test_settings_file()
{
    namespace lnx = platform::linux_runtime;

    const auto path = lnx::settings_file("test_ns");
    assert(!path.empty());
    assert(path.extension() == ".kv");

    std::printf("[path-safety] settings_file OK\n");
}

void test_child_under_root_allow()
{
    namespace lnx = platform::linux_runtime;

    const std::filesystem::path temp =
        std::filesystem::temp_directory_path() / "trailmate_path_test";
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);
    std::filesystem::create_directories(temp / "sub", ec);
    assert(!ec);

    // Write a marker file so resolution succeeds.
    {
        std::ofstream f(temp / "sub" / "ok.txt");
        f << "ok\n";
    }

    std::filesystem::path out;
    assert(lnx::resolve_child_under_root(temp, "sub/ok.txt", out));
    assert(out.filename() == "ok.txt");

    std::filesystem::remove_all(temp, ec);
    std::printf("[path-safety] child_under_root_allow OK\n");
}

void test_child_under_root_reject_absolute()
{
    namespace lnx = platform::linux_runtime;

    const auto temp = std::filesystem::temp_directory_path() / "trailmate_path_test2";
    std::error_code ec;
    std::filesystem::create_directories(temp, ec);

    std::filesystem::path out;
    assert(!lnx::resolve_child_under_root(temp, "/etc/passwd", out));
    assert(out.empty());

    std::filesystem::remove_all(temp, ec);
    std::printf("[path-safety] child_under_root_reject_absolute OK\n");
}

void test_child_under_root_reject_dotdot()
{
    namespace lnx = platform::linux_runtime;

    const auto temp = std::filesystem::temp_directory_path() / "trailmate_path_test3";
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);
    std::filesystem::create_directories(temp / "a" / "b", ec);

    std::filesystem::path out;
    assert(!lnx::resolve_child_under_root(temp, "a/../secret", out));
    assert(out.empty());

    // Also reject ".." as the sole component.
    assert(!lnx::resolve_child_under_root(temp, "..", out));
    assert(out.empty());

    std::filesystem::remove_all(temp, ec);
    std::printf("[path-safety] child_under_root_reject_dotdot OK\n");
}

void test_child_under_root_reject_empty()
{
    namespace lnx = platform::linux_runtime;

    const auto temp = std::filesystem::temp_directory_path();
    std::filesystem::path out;
    assert(!lnx::resolve_child_under_root(temp, "", out));
    assert(out.empty());

    std::printf("[path-safety] child_under_root_reject_empty OK\n");
}

void test_safe_remove()
{
    namespace lnx = platform::linux_runtime;

    const auto temp = std::filesystem::temp_directory_path() / "trailmate_path_test_rm";
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);
    std::filesystem::create_directories(temp / "sub", ec);

    // Create a file.
    {
        std::ofstream f(temp / "sub" / "victim.txt");
        f << "data\n";
    }
    assert(std::filesystem::exists(temp / "sub" / "victim.txt"));

    // Safe remove.
    assert(lnx::safe_remove_under_root(temp, "sub/victim.txt"));
    assert(!std::filesystem::exists(temp / "sub" / "victim.txt"));

    // Removing a non-existent file should return true (already gone).
    assert(lnx::safe_remove_under_root(temp, "sub/victim.txt"));

    // Reject dotdot removal.
    assert(!lnx::safe_remove_under_root(temp, "../secret"));

    std::filesystem::remove_all(temp, ec);
    std::printf("[path-safety] safe_remove OK\n");
}

void test_safe_write()
{
    namespace lnx = platform::linux_runtime;

    const auto temp = std::filesystem::temp_directory_path() / "trailmate_path_test_write";
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);

    assert(lnx::safe_write_under_root(temp, "data/config.txt", "hello world"));
    assert(std::filesystem::exists(temp / "data" / "config.txt"));

    // Read back.
    std::ifstream in(temp / "data" / "config.txt");
    std::string content;
    std::getline(in, content, '\0');
    assert(content == "hello world");

    // Reject traversal.
    assert(!lnx::safe_write_under_root(temp, "../escape.txt", "bad"));

    std::filesystem::remove_all(temp, ec);
    std::printf("[path-safety] safe_write OK\n");
}

void test_resolve_paths_with_env_override()
{
    namespace lnx = platform::linux_runtime;

    const std::string override_root =
        (std::filesystem::temp_directory_path() / "trailmate_override_test").string();

    set_env_var("TRAIL_MATE_SETTINGS_ROOT", override_root);
    set_env_var("TRAIL_MATE_SD_ROOT", override_root + "/my_sd");
    set_env_var("TRAIL_MATE_CACHE_ROOT", override_root + "/my_cache");

    const auto paths = lnx::resolve_paths();
    assert(paths.settings_root.string() == override_root);
    assert(paths.sd_root.string() == override_root + "/my_sd");
    assert(paths.cache_root.string() == override_root + "/my_cache");

    set_env_var("TRAIL_MATE_SETTINGS_ROOT", "");
    set_env_var("TRAIL_MATE_SD_ROOT", "");
    set_env_var("TRAIL_MATE_CACHE_ROOT", "");

    std::printf("[path-safety] resolve_paths_with_env_override OK\n");
}

void test_capability_state_labels()
{
    namespace lnx = platform::linux_runtime;

    // Verify every state has a non-null label.
    assert(lnx::capability_state_label(lnx::CapabilityState::Unsupported) != nullptr);
    assert(lnx::capability_state_label(lnx::CapabilityState::Simulated) != nullptr);
    assert(lnx::capability_state_label(lnx::CapabilityState::Available) != nullptr);
    assert(lnx::capability_state_label(lnx::CapabilityState::Degraded) != nullptr);
    assert(lnx::capability_state_label(lnx::CapabilityState::Error) != nullptr);

    std::printf("[path-safety] capability_state_labels OK\n");
}

} // namespace

int main()
{
    test_resolve_paths();
    test_settings_file();
    test_child_under_root_allow();
    test_child_under_root_reject_absolute();
    test_child_under_root_reject_dotdot();
    test_child_under_root_reject_empty();
    test_safe_remove();
    test_safe_write();
    test_resolve_paths_with_env_override();
    test_capability_state_labels();

    std::printf("[path-safety] All tests passed.\n");
    return 0;
}
