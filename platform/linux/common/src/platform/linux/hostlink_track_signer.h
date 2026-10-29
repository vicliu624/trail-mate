#pragma once
#include "chat/ports/device_author.h"
#include "hostlink/management_envelope.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "track_sharing/sign_manifest.h"
#include <algorithm>

namespace platform::linux_runtime
{
// The admitted job keeps the author identity's shared ownership lease. Private
// keys never leave that identity; the generic signing primitive receives a port.
struct HostlinkTrackSigner
{
    explicit HostlinkTrackSigner(const LinuxReticulumIdentity& identity) : identity(identity) {}
    bool isReady() const { return identity.ready(); }
    uint32_t authorGeneration() const { return identity.generation(); }
    void combinedPublicKey(uint8_t* output) const { std::copy_n(identity.publicKey().data(), 64, output); }
    bool sign(const uint8_t* message, size_t bytes, uint8_t* signature) { return identity.sign(message, bytes, signature); }
    ::hostlink::management::Status readAuthor(uint8_t* output, uint8_t* hash, uint32_t& generation)
    {
        LinuxGeocachingCrypto crypto;
        if (!isReady() || !output || !hash || !crypto.sha256({identity.publicKey().data(), 64}, hash)) return ::hostlink::management::Status::StorageUnavailable;
        combinedPublicKey(output);
        generation = authorGeneration();
        return ::hostlink::management::Status::Ok;
    }
    chat::DeviceAuthorResult signTrackManifest(const uint8_t* raw, size_t bytes, uint32_t generation, track_sharing::ManifestView& view,
                                               uint8_t* workspace, size_t capacity, uint8_t* signature, size_t signature_capacity)
    {
        if (!generation || generation != authorGeneration()) return chat::DeviceAuthorResult::StaleIdentity;
        return track_sharing::signManifestForGeneration({raw, bytes}, *this, generation, view, workspace, capacity, signature, signature_capacity)
                   ? chat::DeviceAuthorResult::Ok
                   : chat::DeviceAuthorResult::InvalidInput;
    }
    const LinuxReticulumIdentity& identity;
};
} // namespace platform::linux_runtime
