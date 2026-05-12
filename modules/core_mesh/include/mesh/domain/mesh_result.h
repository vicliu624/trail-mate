#pragma once

#include <stdint.h>

namespace mesh
{

enum class StoreFailure : uint8_t
{
    None = 0,
    NotFound,
    Corrupt,
    VersionUnsupported,
    IoError,
    NoSpace,
    PermissionDenied,
    InvalidArgument,
};

struct StoreResult
{
    bool ok = false;
    StoreFailure failure = StoreFailure::None;

    StoreResult() = default;

    StoreResult(bool result_ok, StoreFailure result_failure)
        : ok(result_ok),
          failure(result_failure)
    {
    }

    static StoreResult success()
    {
        return StoreResult(true, StoreFailure::None);
    }

    static StoreResult fail(StoreFailure failure)
    {
        return StoreResult(false, failure);
    }
};

enum class ProtocolFailure : uint8_t
{
    None = 0,
    Unsupported,
    MissingPeerKey,
    CryptoFailed,
    EncodeFailed,
    DecodeFailed,
    InvalidInput,
};

struct ProtocolResult
{
    bool ok = false;
    ProtocolFailure failure = ProtocolFailure::None;

    ProtocolResult() = default;

    ProtocolResult(bool result_ok, ProtocolFailure result_failure)
        : ok(result_ok),
          failure(result_failure)
    {
    }

    static ProtocolResult success()
    {
        return ProtocolResult(true, ProtocolFailure::None);
    }

    static ProtocolResult fail(ProtocolFailure failure)
    {
        return ProtocolResult(false, failure);
    }
};

} // namespace mesh
