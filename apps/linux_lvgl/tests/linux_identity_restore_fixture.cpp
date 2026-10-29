#include "app/linux_app_services.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/voice/vmp_private_crypto.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/linux/application_lifecycle.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/reticulum_call_runtime.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

namespace native = platform::linux_runtime;
struct NoRadio final : native::IPacketRadio
{
    const char* name() const override { return "identity restore fixture"; }
    bool hardwareCandidatePresent() const override { return false; }
    bool acquire() override { std::abort(); }
    void release() override { std::abort(); }
    bool configureLoRa(const native::RadioSettings&) override { std::abort(); }
    bool startReceive() override { std::abort(); }
    bool transmit(const uint8_t*, size_t) override { std::abort(); }
    bool pollReceive(native::RadioPacket*) override { std::abort(); }
    float readRssi() override { std::abort(); }
    bool isOnline() const override { return false; }
    const char* lastError() const override { return "physical radio forbidden"; }
    native::RadioSettings appliedLoRaConfig() const override { return {}; }
    native::RadioStatistics stats() const override { return {}; }
};
void expandedFixture(const std::filesystem::path& path)
{
    for (unsigned round = 0; round < 2; ++round)
    {
        native::LinuxReticulumIdentity identity(path);
        assert(identity.begin());
        struct Secret
        {
            uint8_t input[96]{}, expanded[96]{};
            ~Secret()
            {
                volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(this);
                for (size_t i = 0; i < sizeof(*this); ++i) p[i] = 0;
            }
        } secret;
        size_t size = 0;
        assert(identity.copyPrivateKey(identity.generation(), secret.input, sizeof(secret.input), size));
        if (identity.privateFormat() == 1)
        {
            std::copy_n(secret.input, 32, secret.expanded);
            secret.expanded[0] &= 0xf8;
            secret.expanded[31] = (secret.expanded[31] & 0x7f) | 0x40;
            uint8_t public_key[32];
            ed25519_create_keypair(public_key, secret.expanded + 32, secret.input + 32);
        }
        else std::copy_n(secret.input, 96, secret.expanded);
        assert(identity.replaceKeyPair(identity.generation(), identity.publicKey().data(), 0, secret.expanded, 96, identity.publicKey().data()) == chat::DeviceIdentityRestoreResult::CommittedRebootRequired);
    }
}
int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::filesystem::path root(argv[1]);
    const std::string mode(argv[2]);
    std::filesystem::create_directories(root);
    for (const char* name : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_CACHE_ROOT", "TRAIL_MATE_SD_ROOT", "TRAIL_MATE_STATE_ROOT"}) setenv(name, root.c_str(), 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-no-test-gps", 1);
    setenv("TRAIL_MATE_DESKTOP_NOTIFICATIONS", "0", 1);
    setenv("TRAIL_MATE_HOSTLINK_BIND", "127.0.0.1", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    unsigned boot = 0;
    {
        std::ifstream file(root / "boot-count");
        file >> boot;
    }
    if (!boot)
    {
        assert(native::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}));
        if (mode == "expanded") expandedFixture(native::sqlite_database_path());
        if (mode.rfind("radio", 0) == 0)
        {
            struct Keys
            {
                chat::voice::vmp::EphemeralKeyPair pair;
                mesh::LocalIdentity local;
                ~Keys()
                {
                    volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(this);
                    for (size_t i = 0; i < sizeof(*this); ++i) p[i] = 0;
                }
            } keys;
            assert(chat::voice::vmp::generateEphemeralKeyPair(&keys.pair));
            std::copy_n(keys.pair.public_key, 32, keys.local.public_key);
            std::copy_n(keys.pair.private_key, 32, keys.local.private_key);
            keys.local.valid = true;
            assert(native::mesh::LinuxSqliteLocalIdentityStore().save(keys.local).ok);
            if (mode == "radio-fail-mc" || mode == "radio-fail-mt")
            {
                {
                    native::LinuxMeshCoreIdentity identity;
                    assert(identity.begin());
                }
                sqlite3* db = nullptr;
                assert(sqlite3_open(native::sqlite_database_path().c_str(), &db) == SQLITE_OK);
                const char* trigger = mode == "radio-fail-mc"
                                          ? "CREATE TRIGGER reject_radio BEFORE UPDATE ON linux_protocol_identity BEGIN SELECT RAISE(ABORT,'injected'); END;"
                                          : "CREATE TRIGGER reject_radio BEFORE INSERT ON settings WHEN NEW.namespace='chat' AND NEW.key='pki_priv' BEGIN SELECT RAISE(ABORT,'injected'); END;";
                assert(sqlite3_exec(db, trigger, nullptr, nullptr, nullptr) == SQLITE_OK);
                sqlite3_close(db);
            }
        }
        if (mode == "write-failure")
        {
            {
                native::LinuxReticulumIdentity identity(native::sqlite_database_path());
                assert(identity.begin());
            }
            sqlite3* db = nullptr;
            assert(sqlite3_open(native::sqlite_database_path().c_str(), &db) == SQLITE_OK);
            assert(sqlite3_exec(db, "CREATE TRIGGER reject_identity_restore BEFORE UPDATE ON linux_reticulum_identity BEGIN SELECT RAISE(ABORT,'injected'); END;", nullptr, nullptr, nullptr) == SQLITE_OK);
            sqlite3_close(db);
        }
    }
    {
        std::ofstream file(root / "boot-count");
        file << boot + 1;
    }
    return native::run_application(argv, [&]
                                   {
        NoRadio radio;
        trailmate::linux_app::LinuxAppServicesOptions options;
        options.packet_radio = &radio;
        options.runtime_mode = native::LinuxRuntimeMode::DeviceRealMesh;
        trailmate::linux_app::LinuxAppServices services(options);
        assert(services.initialize());
        std::string endpoint;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (endpoint.empty())
        {
            std::ifstream input(root / "hostlink/endpoint.txt");
            std::getline(input, endpoint);
            assert(std::chrono::steady_clock::now() < deadline);
            if (endpoint.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::cout << "ENDPOINT " << endpoint << std::endl;
        std::string commands;
        while (!native::application_restart_requested())
        {
            services.tick(128);
            pollfd input{STDIN_FILENO, POLLIN, 0};
            if (::poll(&input, 1, 0) > 0)
            {
                char bytes[64]; const auto count = ::read(STDIN_FILENO, bytes, sizeof(bytes));
                if (count <= 0) { native::request_application_exit(); break; }
                commands.append(bytes, size_t(count));
                for (size_t end; (end = commands.find('\n')) != std::string::npos;)
                {
                    const auto command = commands.substr(0, end); commands.erase(0, end + 1);
                    if (command == "quit") { native::request_application_exit(); services.shutdown(); return 0; }
                    if (command == "gate-on")
                    {
                        assert(platform::ui::reticulum_call::try_begin_configuration_change());
                        std::cout << "GATE active" << std::endl;
                    }
                    if (command == "gate-off")
                    {
                        platform::ui::reticulum_call::end_configuration_change();
                        std::cout << "GATE idle" << std::endl;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        services.shutdown();
        return 0; });
}
