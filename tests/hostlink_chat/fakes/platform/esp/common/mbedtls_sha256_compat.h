#pragma once
#include <cstddef>
#include <cstdint>
// Deterministic test double, never used as a cryptographic implementation.
struct mbedtls_sha256_context
{
    uint32_t state = 2166136261U;
};
inline void mbedtls_sha256_init(mbedtls_sha256_context* c) { c->state = 2166136261U; }
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
namespace platform::esp::common::crypto
{
inline int sha256_starts(mbedtls_sha256_context*, int) { return 0; }
inline int sha256_update(mbedtls_sha256_context* c, const uint8_t* bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i) c->state = (c->state ^ bytes[i]) * 16777619U;
    return 0;
}
inline int sha256_finish(mbedtls_sha256_context* c, uint8_t* out)
{
    for (unsigned i = 0; i < 32; ++i) out[i] = uint8_t(c->state >> ((i % 4) * 8));
    return 0;
}
} // namespace platform::esp::common::crypto
