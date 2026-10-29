#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv)
{
    assert(argc == 2);

    const std::string path =
        std::string(argv[1]) +
        "/platform/esp/arduino_common/src/platform_ui_reticulum_network_config_runtime.cpp";
    std::ifstream file(path, std::ios::binary);
    assert(file.is_open());
    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    const std::string tms_path =
        std::string(argv[1]) +
        "/platform/esp/arduino_common/src/app_config_tms_settings_extension.cpp";
    std::ifstream tms_file(tms_path, std::ios::binary);
    assert(tms_file.is_open());
    const std::string tms_source((std::istreambuf_iterator<char>(tms_file)),
                                 std::istreambuf_iterator<char>());
    std::ifstream network_records_file(
        std::string(argv[1]) + "/modules/core_sys/include/app/reticulum_network_records.h",
        std::ios::binary);
    assert(network_records_file.is_open());
    const std::string network_records((std::istreambuf_iterator<char>(network_records_file)),
                                      std::istreambuf_iterator<char>());

    // A single PSRAM-resident runtime snapshot is populated from TMS before
    // the LXMF adapter starts. JSON is permitted only as a one-time migration
    // input and is never a normal runtime or NVS-backed configuration source.
    assert(source.find("NetworkConfig* s_active = nullptr;") != std::string::npos);
    assert(source.find("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") != std::string::npos);
    assert(source.find("bool setFromTms(const NetworkConfig& config)") !=
           std::string::npos);
    assert(source.find("bool snapshotForTms(") != std::string::npos);
    assert(source.find("LegacyImportResult importLegacy(") != std::string::npos);
    assert(source.find("kLegacyConfigPath") != std::string::npos);
    assert(source.find("rt_net_cfg") == std::string::npos);
    assert(source.find("Preferences") == std::string::npos);
    assert(source.find("write_sd_file_atomic") == std::string::npos);
    assert(source.find("void poll(const chat::MeshConfig& legacy_config)") !=
           std::string::npos);
    // The pre-release TMSET6 document already used this public Reticulum
    // namespace. Keep it stable in TMSET7 so migration is value-preserving;
    // `rt_net` was an unshipped internal spelling and must never reappear.
    assert(tms_source.find("ReticulumNetworkRecords") != std::string::npos);
    assert(network_records.find("rt.net.version") != std::string::npos);
    assert(network_records.find("rt.net.interface.%u.id") != std::string::npos);
    assert(tms_source.find("rt_net.") == std::string::npos);
    return 0;
}
