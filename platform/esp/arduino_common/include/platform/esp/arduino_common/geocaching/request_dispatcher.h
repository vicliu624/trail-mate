#pragma once
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/geocaching/request_dispatch_store.h"

namespace platform::esp::arduino_common::geocaching
{
enum class DispatchStatus : uint8_t
{
    Idle,
    Submitted,
    Deferred,
    StorageBlocked,
    Corrupt
};
struct DispatchResult
{
    DispatchStatus status = DispatchStatus::Idle;
    chat::MeshOperationFailure failure = chat::MeshOperationFailure::None;
};

// Serialized worker only, off task stack. The owner supplies its configured
// retry delay and a current time observation. Submitted is not Delivered.
class RequestDispatcher
{
  public:
    RequestDispatcher(chat::MeshAdapterRouter& router, RequestDispatchStore& store,
                      uint32_t retry_delay_ms, uint64_t attempt_timeout_ms);
    // Optional key of a newly committed foreground request. It bypasses only
    // history selection; beginAttempt/readForSend still validate durable state.
    DispatchResult dispatchOne(const ::geocaching::storage::StoredTime& now, ::geocaching::ByteView preferred = {});

  private:
    enum class Phase : uint8_t
    {
        Select,
        SelectRequest,
        ExpireCommit,
        BeginCommit,
        Send,
        SuccessCommit,
        FailureCommit
    };
    Phase phase_ = Phase::Select;
    std::array<uint8_t, 16> attempt_id_{};
    chat::MeshOperationFailure send_failure_ = chat::MeshOperationFailure::None;
    chat::MeshAdapterRouter& router_;
    RequestDispatchStore& store_;
    uint32_t retry_delay_ms_;
    uint64_t attempt_timeout_ms_;
    uint64_t recovery_started_ms_ = 0;
    bool clock_initialized_ = false;
    uint64_t not_before_ = 0;
    std::array<uint8_t, 16> boot_{};
    std::array<uint8_t, 48> cursor_{};
    bool has_cursor_ = false;
    std::array<uint8_t, 48> preferred_{};
    bool has_preferred_ = false;
};
static_assert(sizeof(RequestDispatcher) <= 256, "Dispatcher retains metadata only, not a request-sized buffer");
} // namespace platform::esp::arduino_common::geocaching
