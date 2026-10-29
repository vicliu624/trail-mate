#pragma once
#include "chat/ports/device_identity_restore.h"
#include "hostlink/management_envelope.h"
#include <array>
#include <filesystem>

namespace platform::linux_runtime
{
enum class IdentityRestoreTarget : uint8_t
{
    DeviceAuthor,
    Radio
};
struct IdentityRestoreCommand
{
    IdentityRestoreTarget target = IdentityRestoreTarget::DeviceAuthor;
    std::array<uint8_t, 448> body{};
    size_t size = 0;
    ~IdentityRestoreCommand()
    {
        volatile uint8_t* bytes = body.data();
        for (size_t i = 0; i < body.size(); ++i) bytes[i] = 0;
    }
};
hostlink::management::Status validate_identity_restore(const uint8_t*, size_t);
hostlink::management::Status validate_radio_identity_restore(const uint8_t*, size_t);
void bind_identity_restore(void* owner, const std::filesystem::path& database);
void unbind_identity_restore(void* owner);
bool identity_restore_available(const std::filesystem::path& database);
bool identity_restore_busy();
// One owned request crosses to the frontend. A request that has not started
// expires without applying; once applying, the caller waits for a definite result.
chat::DeviceIdentityRestoreResult request_identity_restore(const IdentityRestoreCommand&, const std::filesystem::path& database);
void service_identity_restore(void* owner, chat::DeviceIdentityRestoreResult (*apply)(void*, const IdentityRestoreCommand&));
} // namespace platform::linux_runtime
