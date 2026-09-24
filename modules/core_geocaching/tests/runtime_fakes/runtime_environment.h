#pragma once
// Hardware seams only. The test links the production browse runtime, stores,
// clients and dispatcher without conditional compilation in those sources.
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/ports/i_geocaching_transport.h"
#include "esp_random.h"
#include "geocaching/protocol/sign_record.h"
#include "geocaching/protocol/verify_record.h"
#include "ui_presentation/geocaching/geocaching_source.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace runtime_test
{
inline uint64_t clock_ms = 0;
inline bool memory_available = true, card_ready = true, external_owner = false;
inline bool fail_read = false, in_ui = false;
inline std::string fail_read_path;
inline size_t io_bytes = 0, ui_io = 0, blocked_io = 0;
// Optional host-only profiling, reset at each acceptance wait boundary.
inline bool profile_reads = false;
inline std::map<std::string, uint64_t> read_bytes_by_path;
inline std::map<std::string, std::vector<uint8_t>> files;
inline std::set<std::string> directories{"/", "/trailmate"};
struct Allocation
{
    size_t size;
    std::string owner;
};
inline std::map<void*, Allocation> allocations;
inline ::ui::geocaching::Source* source = nullptr;
inline size_t open_files = 0, open_dirs = 0;
inline bool access()
{
    if (in_ui) ++ui_io;
    if (!card_ready || external_owner)
    {
        ++blocked_io;
        return false;
    }
    return true;
}
inline bool allocated(const char* name)
{
    for (const auto& allocation : allocations)
        if (allocation.second.owner == name) return true;
    return false;
}
} // namespace runtime_test

using SemaphoreHandle_t = bool*;
constexpr int pdTRUE = 1;
inline SemaphoreHandle_t xSemaphoreCreateMutex()
{
    static bool taken = false;
    return &taken;
}
inline int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned)
{
    if (*semaphore) return 0;
    *semaphore = true;
    return pdTRUE;
}
inline void xSemaphoreGive(SemaphoreHandle_t semaphore) { *semaphore = false; }
inline uint32_t millis() { return static_cast<uint32_t>(runtime_test::clock_ms); }
inline int64_t esp_timer_get_time() { return static_cast<int64_t>(runtime_test::clock_ms * 1000); }
struct RuntimeTestSerial
{
    template <class... Args>
    void printf(const char*, Args...) {}
};
inline RuntimeTestSerial Serial;
inline void heap_caps_free(void* bytes)
{
    if (!bytes) return;
    const auto found = runtime_test::allocations.find(bytes);
    assert(found != runtime_test::allocations.end());
    std::memset(bytes, 0xa5, found->second.size);
    runtime_test::allocations.erase(found);
    std::free(bytes);
}
class LoraBoard
{
};
namespace platform::esp::common::memory
{
inline bool admit(const char*, size_t, size_t, size_t, size_t, size_t, size_t = 0) { return runtime_test::memory_available; }
inline void* allocatePreferred(const char* owner, size_t size, bool = true)
{
    if (!runtime_test::memory_available) return nullptr;
    auto* bytes = std::malloc(size);
    if (bytes) runtime_test::allocations.emplace(bytes, runtime_test::Allocation{size, owner});
    return bytes;
}
} // namespace platform::esp::common::memory
namespace platform::esp::common
{
class EspGeocachingCrypto final : public ::geocaching::protocol::RecordCrypto
{
  public:
    bool sha256(::geocaching::ByteView bytes, uint8_t out[32]) override
    {
        chat::reticulum::fullHash(bytes.data, bytes.size, out);
        return true;
    }
    ::geocaching::protocol::VerificationResult verifyEd25519(::geocaching::ByteView key, ::geocaching::ByteView signature, ::geocaching::ByteView message) override
    {
        return ed25519_verify(signature.data, message.data, message.size, key.data) ? ::geocaching::protocol::VerificationResult::Valid : ::geocaching::protocol::VerificationResult::InvalidSignature;
    }
};
namespace meshcore_runtime
{
class Sha256Digest
{
    std::vector<uint8_t> bytes_;

  public:
    void update(const uint8_t* bytes, size_t size) { bytes_.insert(bytes_.end(), bytes, bytes + size); }
    bool finalize(uint8_t* hash, size_t size)
    {
        if (size != 32) return false;
        chat::reticulum::fullHash(bytes_.data(), bytes_.size(), hash);
        return true;
    }
};
} // namespace meshcore_runtime
} // namespace platform::esp::common
namespace app
{
struct Config
{
    int reticulumConfig() const { return 0; }
};
struct AppContext
{
    static AppContext& getInstance()
    {
        static AppContext instance;
        return instance;
    }
    Config readConfig() const { return {}; }
};
} // namespace app
namespace geocaching::ui::shell
{
inline void bind(::ui::geocaching::Source* source) { runtime_test::source = source; }
} // namespace geocaching::ui::shell
namespace chat
{
class IMeshAdapter
{
  public:
    virtual ~IMeshAdapter() = default;
};
namespace reticulum
{
enum class ReticulumUsage
{
    BackgroundIpService
};
class ReticulumAdapter : public IMeshAdapter
{
  public:
    ReticulumAdapter(LoraBoard&, void*, ReticulumUsage) {}
    void applyConfig(int) {}
};
} // namespace reticulum
namespace lxmf
{
class LxmfAdapter
{
};
} // namespace lxmf
class MeshAdapterRouter
{
  public:
    struct Identity
    {
        std::array<uint8_t, 64> public_key{}, private_key{};
        unsigned signs = 0;
        Identity()
        {
            std::array<uint8_t, 32> seed{};
            for (size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<uint8_t>(i);
            ed25519_create_keypair(public_key.data() + 32, private_key.data(), seed.data());
        }
        bool isReady() const { return true; }
        void combinedPublicKey(uint8_t* out) const { std::memcpy(out, public_key.data(), public_key.size()); }
        bool sign(const uint8_t* data, size_t size, uint8_t* signature)
        {
            ++signs;
            ed25519_sign(signature, data, size, public_key.data() + 32, private_key.data());
            return true;
        }
    } identity;
    bool ready = true;
    unsigned sends = 0;
    MeshProtocol selected_chat = MeshProtocol::Meshtastic;
    std::array<uint8_t, 16> local{};
    std::vector<uint8_t> sent_bytes;
    std::unique_ptr<IMeshAdapter> service;
    lxmf::GeocachingAnnouncementHandler announcement = nullptr;
    lxmf::CustomDeliveryHandler delivery = nullptr;
    void* context = nullptr;
    bool bindGeocachingHandlers(lxmf::GeocachingAnnouncementHandler announce, lxmf::CustomDeliveryHandler receive, void* value)
    {
        announcement = announce;
        delivery = receive;
        context = value;
        return true;
    }
    std::unique_ptr<IMeshAdapter> takeInactiveReticulumCache() { return {}; }
    IMeshAdapter* backendForProtocol(MeshProtocol protocol) { return protocol == MeshProtocol::Reticulum ? service.get() : nullptr; }
    MeshProtocol backendProtocol() const { return selected_chat; }
    bool installServiceBackend(MeshProtocol, std::unique_ptr<IMeshAdapter> backend)
    {
        service = std::move(backend);
        return true;
    }
    std::unique_ptr<IMeshAdapter> takeServiceBackend(MeshProtocol, const IMeshAdapter*) { return std::move(service); }
    bool getGeocachingDispatchDestination(uint8_t out[16])
    {
        if (!ready) return false;
        std::memcpy(out, local.data(), 16);
        return true;
    }
    bool getGeocachingAuthorKey(uint8_t out[64])
    {
        identity.combinedPublicKey(out);
        return ready;
    }
    bool signGeocachingRecord(lxmf::ByteSpan record, uint8_t* scratch, size_t capacity, uint8_t* out, size_t output_capacity, size_t& written)
    {
        return ::geocaching::protocol::signGeocacheRecord({record.data, record.size}, identity, scratch, capacity, out, output_capacity, written);
    }
    MeshSendResult sendGeocachingData(const uint8_t[16], lxmf::ByteSpan bytes, bool, std::array<uint8_t, 32>* hash, const uint8_t expected[16])
    {
        if (!ready || std::memcmp(expected, local.data(), 16)) return MeshSendResult::fail(MeshOperationFailure::NotReady);
        ++sends;
        sent_bytes.assign(bytes.data, bytes.data + bytes.size);
        hash->fill(0x42);
        return MeshSendResult::success(1);
    }
};
} // namespace chat
