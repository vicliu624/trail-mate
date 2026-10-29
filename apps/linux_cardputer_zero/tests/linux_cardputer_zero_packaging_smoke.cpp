#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream.is_open());
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

bool not_contains(const std::string& haystack, const char* needle)
{
    return !contains(haystack, needle);
}

std::size_t position_of(const std::string& haystack, const char* needle)
{
    const auto pos = haystack.find(needle);
    assert(pos != std::string::npos);
    return pos;
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];

    const std::string cmake = read_file(
        repo_root / "apps/linux_cardputer_zero/CMakeLists.txt");
    assert(contains(cmake, "TRAIL_MATE_CARDPUTER_ZERO_BUILD_DEVICE"));
    assert(contains(cmake, "TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION \"${TRAIL_MATE_DEBIAN_PROJECT_VERSION}\""));
    assert(contains(cmake, "src/cardputer_zero_wayland_main.cpp"));
    assert(contains(cmake, "src/platform/wayland/wayland_presenter.cpp"));
    assert(contains(cmake, "pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)"));
    assert(contains(cmake, "pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)"));
    assert(contains(cmake, "pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)"));
    assert(contains(cmake, "find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)"));
    assert(contains(cmake, "xdg-shell.xml"));
    assert(contains(cmake, "xdg-decoration-unstable-v1.xml"));
    assert(contains(cmake, "add_executable(trailmate_cardputer_zero_wayland"));
    assert(contains(cmake, "OUTPUT_NAME \"trailmate-cardputer-zero\""));
    assert(contains(cmake, "add_executable(trailmate_cardputer_zero_fbdev"));
    assert(contains(cmake, "src/cardputer_zero_main.cpp"));
    assert(contains(cmake, "linux_framebuffer_platform.cpp"));
    assert(contains(cmake, "evdev_input.cpp"));
    assert(contains(cmake, "OUTPUT_NAME \"trailmate-cardputer-zero-fbdev\""));
    assert(contains(cmake, "FetchContent_Declare(lvgl"));
    assert(contains(cmake, "GIT_TAG v9.4.0"));
    assert(contains(cmake, "EXCLUDE_FROM_ALL"));
    assert(contains(cmake, "TRAIL_MATE_CARDPUTER_ZERO_PRIVATE_LIBDIR"));
    assert(contains(cmake, "lib/trailmate-cardputer-zero"));
    assert(contains(cmake, "install(TARGETS trailmate_cardputer_zero_wayland trailmate_cardputer_zero_fbdev"));
    assert(contains(cmake, "RUNTIME DESTINATION \"${TRAIL_MATE_CARDPUTER_ZERO_PRIVATE_LIBDIR}\""));
    assert(contains(cmake, "trailmate-cardputer-zero-applaunch"));
    assert(contains(cmake, "APPLaunch/applications"));
    assert(contains(cmake, "APPLaunch/share/images"));
    assert(contains(cmake, "RENAME trailmate.desktop"));
    assert(contains(cmake, "CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS"));
    assert(contains(cmake, "PERMISSIONS"));
    assert(contains(cmake, "WORLD_READ WORLD_EXECUTE"));
    assert(contains(cmake, "OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ"));
    assert(contains(cmake, "CPACK_GENERATOR \"DEB\""));
    assert(contains(cmake, "CPACK_PACKAGE_NAME \"trailmate-cardputer-zero\""));
    assert(contains(cmake, "CPACK_DEBIAN_PACKAGE_ARCHITECTURE \"arm64\""));
    assert(contains(cmake, "libsqlite3-0"));
    assert(contains(cmake, "libcurl4"));
    assert(contains(cmake, "libssl3"));
    assert(contains(cmake, "libwayland-client0"));
    assert(contains(cmake, "libxkbcommon0"));
    assert(contains(cmake, "libnotify-bin"));
    assert(contains(cmake, "gdal-bin"));
    assert(contains(cmake, "unzip"));
    assert(contains(cmake, "ca-certificates"));
    assert(contains(cmake, "network-manager"));
    assert(contains(cmake, "CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF"));
    assert(not_contains(cmake, "NO_LEGACY_PRESENTATION"));
    assert(contains(cmake, "CMAKE_SYSTEM_PROCESSOR MATCHES \"^(aarch64|arm64|ARM64)$\""));
    assert(contains(cmake, "cardputer_zero_arm64_asset_sources"));
    assert(contains(cmake, "foreach(source_path IN LISTS TRAIL_MATE_LINUX_UI_SHELL_SOURCES)"));
    assert(contains(cmake, "source_path MATCHES \"/modules/ui_shared/src/ui/assets/.*\\\\.c$\""));
    assert(contains(cmake, "PROPERTIES COMPILE_OPTIONS \"-O0\""));

    const std::size_t common_shell = position_of(
        cmake,
        "trailmate_add_linux_ui_shell(");
    const std::size_t wayland_target = position_of(
        cmake,
        "add_executable(trailmate_cardputer_zero_wayland");
    const std::size_t fbdev_target = position_of(
        cmake,
        "add_executable(trailmate_cardputer_zero_fbdev");
    assert(common_shell < wayland_target);
    assert(wayland_target < fbdev_target);

    const std::string wayland_main = read_file(
        repo_root /
        "apps/linux_cardputer_zero/src/cardputer_zero_wayland_main.cpp");
    assert(contains(wayland_main, "WaylandPresenter"));
    assert(contains(wayland_main, "runShellUi"));
    assert(not_contains(wayland_main, "TRAIL_MATE_FRAMEBUFFER"));
    assert(not_contains(wayland_main, "LinuxFramebufferPlatform"));

    const std::string wayland_presenter = read_file(
        repo_root /
        "apps/linux_cardputer_zero/src/platform/wayland/wayland_presenter.cpp");
    assert(contains(wayland_presenter, "wl_display_connect"));
    assert(contains(wayland_presenter, "xdg_toplevel_set_app_id"));
    assert(contains(wayland_presenter, "io.github.vicliu624.trailmate"));
    assert(contains(wayland_presenter, "xkb_state_key_get_utf8"));
    assert(not_contains(wayland_presenter, "LinuxFramebufferPlatform"));

    const std::string fbdev_main = read_file(
        repo_root / "apps/linux_cardputer_zero/src/cardputer_zero_main.cpp");
    assert(contains(fbdev_main, "TRAIL_MATE_FRAMEBUFFER"));
    assert(contains(fbdev_main, "/dev/fb0"));
    assert(contains(fbdev_main, "LinuxFramebufferPlatform"));
    assert(contains(fbdev_main, "runShellUi"));
    assert(not_contains(fbdev_main, "screenshot_capture"));

    const std::string desktop = read_file(
        repo_root /
        "apps/linux_cardputer_zero/packaging/trailmate-cardputer-zero.desktop");
    assert(contains(desktop, "Exec=/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-applaunch"));
    assert(contains(desktop, "Icon=share/images/trailmate-cardputer-zero.png"));
    assert(contains(desktop, "X-Zero-AppId=io.github.vicliu624.trailmate"));
    assert(contains(desktop, "X-Zero-Display=wayland"));
    assert(not_contains(desktop, "X-Zero-Display=fbdev"));

    const std::string launcher = read_file(
        repo_root /
        "apps/linux_cardputer_zero/packaging/trailmate-cardputer-zero-applaunch");
    assert(contains(launcher, "backend=${TRAIL_MATE_DISPLAY_BACKEND:-auto}"));
    assert(contains(launcher, "TRAIL_MATE_RUNTIME_MODE:=mesh"));
    assert(contains(launcher, "TRAIL_MATE_LORA_SPI:=/dev/spidev0.1"));
    assert(contains(launcher, "TRAIL_MATE_LORA_GPIOCHIP:=/dev/gpiochip0"));
    assert(contains(launcher, "TRAIL_MATE_LORA_POWER_GPIO:=-1"));
    assert(contains(launcher, "TRAIL_MATE_LORA_RESET_GPIO:=26"));
    assert(contains(launcher, "TRAIL_MATE_LORA_BUSY_GPIO:=22"));
    assert(contains(launcher, "TRAIL_MATE_LORA_IRQ_GPIO:=23"));
    assert(contains(launcher, "TRAIL_MATE_LORA_SPI_HZ:=500000"));
    assert(contains(launcher, "TRAIL_MATE_LORA_DIO2_RF_SWITCH:=1"));
    assert(contains(launcher, "TRAIL_MATE_LORA_DIO3_TCXO_1V8:=1"));
    assert(contains(launcher, "TRAIL_MATE_GPS_BAUD:=115200"));
    assert(contains(launcher, "TRAIL_MATE_GPS_AUTO_SERIAL:=1"));
    assert(contains(launcher, "TRAIL_MATE_GPS_DEVICE_CANDIDATES:=/dev/serial0:/dev/ttyAMA1:/dev/ttyAMA0:/dev/ttyS0:/dev/ttyS1"));
    assert(not_contains(launcher, "TRAIL_MATE_GPS_DEVICE=/dev/serial0"));
    assert(contains(launcher, "settings_root=${TRAIL_MATE_SETTINGS_ROOT:-${HOME:?HOME or TRAIL_MATE_SETTINGS_ROOT is required}/.trailmate_cardputer_zero}"));
    assert(contains(launcher, "load_env_file /etc/trailmate-cardputer-zero.env"));
    assert(contains(launcher, "load_env_file \"$settings_root/trailmate.env\""));
    assert(contains(launcher, "TRAIL_MATE_EARTHDATA_TOKEN"));
    assert(contains(launcher, "TRAIL_MATE_DESKTOP_NOTIFICATIONS:=freedesktop"));
    assert(not_contains(launcher, "7890"));
    assert(not_contains(launcher, "/home/pi"));
    assert(contains(launcher, "export TRAIL_MATE_RUNTIME_MODE"));
    assert(contains(launcher, "export TRAIL_MATE_LORA_SPI TRAIL_MATE_LORA_GPIOCHIP"));
    assert(contains(launcher, "export TRAIL_MATE_GPS_BAUD TRAIL_MATE_GPS_AUTO_SERIAL"));
    assert(contains(launcher, "export TRAIL_MATE_GPS_DEVICE TRAIL_MATE_GPS_DEVICE_CANDIDATES"));
    assert(contains(launcher, "export TRAIL_MATE_DESKTOP_NOTIFICATIONS"));
    assert(contains(launcher, "export ALL_PROXY all_proxy HTTPS_PROXY https_proxy HTTP_PROXY http_proxy"));
    assert(contains(launcher, "wayland|labwc)"));
    assert(contains(launcher, "if [ -n \"${WAYLAND_DISPLAY:-}\" ]; then"));
    assert(contains(launcher, "Wayland session is required"));
    assert(contains(launcher, "TRAIL_MATE_DISPLAY_BACKEND=fbdev"));
    assert(contains(launcher, "fb|fbdev|framebuffer|device)"));
    assert(contains(launcher, "exec \"$script_dir/trailmate-cardputer-zero\" \"$@\""));
    assert(contains(launcher, "exec \"$script_dir/trailmate-cardputer-zero-fbdev\" \"$@\""));
    assert(contains(launcher, "APPLAUNCH_LINUX_FBDEV_DEVICE"));
    assert(contains(launcher, "APPLAUNCH_LINUX_KEYBOARD_DEVICE"));
    assert(contains(launcher, "fb_st7789v"));
    assert(contains(launcher, "TRAIL_MATE_FRAMEBUFFER=/dev/fb1"));
    assert(contains(launcher, "TRAIL_MATE_INPUT_DEVICE"));

    const std::string presets = read_file(
        repo_root / "builds/linux_cmake/CMakePresets.json");
    assert(contains(presets, "linux-cardputer-zero-release"));
    assert(contains(presets, "linux-cardputer-zero-deb"));
    assert(contains(presets, "TRAIL_MATE_CARDPUTER_ZERO_ENABLE_DEB_PACKAGE"));

    const std::string workflow = read_file(
        repo_root / ".github/workflows/cardputer-zero-linux.yml");
    assert(contains(workflow, "linux-cardputer-zero-debug"));
    assert(contains(workflow, "libwayland-dev"));
    assert(contains(workflow, "libwayland-bin"));
    assert(contains(workflow, "libxkbcommon-dev"));
    assert(contains(workflow, "wayland-protocols"));
    assert(contains(workflow, "bash apps/linux_cardputer_zero/tools/build_cardputer_zero_deb.sh"));
    assert(contains(workflow, "trailmate-cardputer-zero-deb"));
    assert(contains(workflow, "build/cardputer-zero-deb/*.deb"));

    const std::string package_script = read_file(
        repo_root /
        "apps/linux_cardputer_zero/tools/build_cardputer_zero_deb.sh");
    assert(contains(package_script, "/tmp/trailmate-cardputer-zero-package"));
    assert(contains(package_script, "scripts/project_version.py\" --debian"));
    assert(contains(package_script, "package_build_type=\"${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_BUILD_TYPE:-Release}\""));
    assert(contains(package_script, "docker_platform=\"${TRAIL_MATE_CARDPUTER_ZERO_DOCKER_PLATFORM:-linux/arm64}\""));
    assert(contains(package_script, "builder_image=\"${TRAIL_MATE_CARDPUTER_ZERO_BUILDER_IMAGE:-trailmate-cardputer-zero-builder:bookworm-arm64}\""));
    assert(contains(package_script, "out_dir=\"${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_OUT:-${repo_root}/build/cardputer-zero-deb}\""));
    assert(contains(package_script, "compose_file=\"${script_dir}/compose.cardputer-zero-builder.yml\""));
    assert(contains(package_script, "TRAIL_MATE_CARDPUTER_ZERO_COMPOSE_PROJECT"));
    assert(contains(package_script, "docker.exe"));
    assert(contains(package_script, "compose_cmd"));
    assert(contains(package_script, "compose version"));
    assert(contains(package_script, "docker-compose version"));
    assert(contains(package_script, "container_id"));
    assert(contains(package_script, "cleanup_container"));
    assert(contains(package_script, "\"${compose_cmd[@]}\" -f \"${compose_file}\" -p \"${compose_project}\" build deb-builder"));
    assert(contains(package_script, "run"));
    assert(contains(package_script, "-d"));
    assert(contains(package_script, "--no-deps"));
    assert(contains(package_script, "\"${docker_cmd[@]}\" exec"));
    assert(contains(package_script, "tar -C \"${source_dir}\" -cf - ."));
    assert(contains(package_script, "tar -C /work/source -xf -"));
    assert(contains(package_script, "tar -C /work/out -cf -"));
    assert(contains(package_script, "tar -C \"${out_dir}\" -xf -"));
    assert(contains(package_script, "--exclude='./*.o'"));
    assert(contains(package_script, "--exclude='./*.obj'"));
    assert(contains(package_script, "--exclude='./*.d'"));
    assert(contains(package_script, "--exclude='./CMakeCache.txt'"));
    assert(contains(package_script, "--exclude='./CMakeFiles'"));
    assert(contains(package_script, "find \"${source_dir}\" -type d -exec chmod 0755"));
    assert(contains(package_script, "find \"${source_dir}\" -type f -exec chmod 0644"));
    assert(contains(package_script, "TRAIL_MATE_CARDPUTER_ZERO_APT_MIRROR"));
    assert(contains(package_script, "TRAIL_MATE_CARDPUTER_ZERO_BUILDER_IMAGE"));
    assert(contains(package_script, "/opt/trailmate/cardputer_zero_deb_package_entrypoint.sh"));
    assert(not_contains(package_script, "apt_get()"));
    assert(not_contains(package_script, "apt-get install"));
    assert(not_contains(package_script, "/var/cache/apt/archives"));

    const std::string builder_compose = read_file(
        repo_root /
        "apps/linux_cardputer_zero/tools/compose.cardputer-zero-builder.yml");
    assert(contains(builder_compose, "name: trailmate-cardputer-zero-package"));
    assert(contains(builder_compose, "deb-builder:"));
    assert(contains(builder_compose, "TRAIL_MATE_CARDPUTER_ZERO_BUILDER_IMAGE:-trailmate-cardputer-zero-builder:bookworm-arm64"));
    assert(contains(builder_compose, "platform: ${TRAIL_MATE_CARDPUTER_ZERO_DOCKER_PLATFORM:-linux/arm64}"));
    assert(contains(builder_compose, "dockerfile: Dockerfile.cardputer-zero-builder"));
    assert(contains(builder_compose, "TRAIL_MATE_CARDPUTER_ZERO_APT_MIRROR"));
    assert(contains(builder_compose, "TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION:-}"));
    assert(contains(builder_compose, "TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_BUILD_TYPE:-Release"));
    assert(contains(builder_compose, "cardputer-zero-cmake-build:/work/build"));
    assert(contains(builder_compose, "command: [\"/bin/sleep\", \"infinity\"]"));
    assert(not_contains(builder_compose, "build/cardputer-zero-deb"));
    assert(not_contains(builder_compose, "/mnt/"));

    const std::string builder_dockerfile = read_file(
        repo_root /
        "apps/linux_cardputer_zero/tools/Dockerfile.cardputer-zero-builder");
    assert(contains(builder_dockerfile, "FROM debian:bookworm"));
    assert(contains(builder_dockerfile, "TRAIL_MATE_CARDPUTER_ZERO_APT_MIRROR"));
    assert(contains(builder_dockerfile, "Acquire::Retries=8"));
    assert(contains(builder_dockerfile, "Acquire::Queue-Mode=access"));
    assert(contains(builder_dockerfile, "Acquire::http::Pipeline-Depth=0"));
    assert(contains(builder_dockerfile, "Acquire::http::No-Cache=true"));
    assert(contains(builder_dockerfile, "Acquire::BrokenProxy=true"));
    assert(contains(builder_dockerfile, "apt_get()"));
    assert(contains(builder_dockerfile, "for attempt in 1 2 3 4 5"));
    assert(contains(builder_dockerfile, "libwayland-bin"));
    assert(contains(builder_dockerfile, "libwayland-dev"));
    assert(contains(builder_dockerfile, "libxkbcommon-dev"));
    assert(contains(builder_dockerfile, "wayland-protocols"));
    assert(contains(builder_dockerfile, "COPY cardputer_zero_deb_package_entrypoint.sh"));
    assert(contains(builder_dockerfile, "CMD [\"/bin/sleep\", \"infinity\"]"));

    const std::string package_entrypoint = read_file(
        repo_root /
        "apps/linux_cardputer_zero/tools/cardputer_zero_deb_package_entrypoint.sh");
    assert(not_contains(package_entrypoint, "apt-get install"));
    assert(contains(package_entrypoint, "dpkg-deb -c"));
    assert(contains(package_entrypoint, "bad package permission"));
    assert(contains(package_entrypoint, "Architecture)"));
    assert(contains(package_entrypoint, "Expected arm64 package"));
    assert(contains(package_entrypoint, "Version)"));
    assert(contains(package_entrypoint, "Expected package version"));
    assert(contains(package_entrypoint, "-DCMAKE_BUILD_TYPE=\"${package_build_type}\""));
    assert(contains(package_entrypoint, "-DCMAKE_C_FLAGS_RELEASE=\"-O2 -DNDEBUG\""));
    assert(contains(package_entrypoint, "-DCMAKE_CXX_FLAGS_RELEASE=\"-O2 -DNDEBUG\""));
    assert(contains(package_entrypoint, "-DTRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION=\"${package_version}\""));
    assert(contains(package_entrypoint, "Missing required package dependency"));
    assert(contains(package_entrypoint, "tr ', ' '\\n\\n'"));
    assert(contains(package_entrypoint, "grep -Fxq"));
    assert(contains(package_entrypoint, "apt-cache show"));
    assert(contains(package_entrypoint, "usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero"));
    assert(contains(package_entrypoint, "usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-fbdev"));
    assert(contains(package_entrypoint, "usr/share/APPLaunch/applications/trailmate.desktop"));
    assert(contains(package_entrypoint, "usr/share/APPLaunch/share/images/trailmate-cardputer-zero.png"));
    assert(contains(package_entrypoint, "Unexpected /usr/bin device entry"));
    assert(contains(package_entrypoint, "X-Zero-Display=wayland"));
    assert(contains(package_entrypoint, "TRAIL_MATE_DISPLAY_BACKEND"));
    assert(contains(package_entrypoint, "WAYLAND_DISPLAY"));
    assert(contains(package_entrypoint, "TRAIL_MATE_LORA_SPI"));
    assert(contains(package_entrypoint, "TRAIL_MATE_GPS_BAUD"));
    assert(contains(package_entrypoint, "TRAIL_MATE_DESKTOP_NOTIFICATIONS"));
    assert(contains(package_entrypoint, "libwayland-client0"));
    assert(contains(package_entrypoint, "libxkbcommon0"));
    assert(contains(package_entrypoint, "libnotify-bin"));
    assert(contains(package_entrypoint, "gdal-bin"));
    assert(contains(package_entrypoint, "unzip"));
    assert(contains(package_entrypoint, "readelf -h"));
    assert(contains(package_entrypoint, "Machine:[[:space:]]*AArch64"));
    assert(contains(package_entrypoint, "ldd"));
    assert(contains(package_entrypoint, "not found"));

    return 0;
}
