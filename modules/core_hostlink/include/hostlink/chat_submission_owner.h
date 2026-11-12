#pragma once

#include "hostlink/management_envelope.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hostlink::chat_management
{

constexpr std::size_t kIdSize = 16;
constexpr std::size_t kDigestSize = 32;
constexpr std::size_t kConversationBytes = 96;
constexpr std::size_t kTextBytes = 256;

enum class SubmissionState : uint8_t
{
    Reserved,
    Submitted,
    Rejected,
    Uncertain,
};

// The persistence port stores metadata only; the existing chat store owns
// message bodies. All byte fields have one canonical wire representation.
struct SubmissionRecord
{
    uint8_t store[kIdSize]{};
    uint8_t owner[kIdSize]{};
    uint8_t operation[kIdSize]{};
    uint8_t content_hash[kDigestSize]{};
    uint8_t conversation[kConversationBytes]{};
    uint8_t conversation_size = 0;
    uint16_t text_size = 0;
    SubmissionState state = SubmissionState::Reserved;
    uint8_t message[kIdSize]{};
    uint8_t delivery_state = 0;
    uint32_t revision = 0;
    uint16_t failure = 0;
};

enum class LookupResult : uint8_t
{
    Found,
    Missing,
    Pending,
    Unavailable,
};

enum class ReservationResult : uint8_t
{
    Created,
    Existing,
    Pending,
    Rejected,
    Ambiguous,
};

enum class CommitResult : uint8_t
{
    Durable,
    Pending,
    Unavailable,
};

class SubmissionPort
{
  public:
    virtual ~SubmissionPort() = default;
    virtual management::Status validate(const SubmissionRecord& binding, const uint8_t* context,
                                        const uint8_t* text, std::size_t text_size) = 0;
    virtual LookupResult lookup(const SubmissionRecord& key, SubmissionRecord& result) = 0;
    // Created means this owner obtained a new, durably committed reservation.
    // A lost physical completion must be Ambiguous, never Created on retry.
    virtual ReservationResult reserve(const SubmissionRecord& record) = 0;
    virtual ReservationResult pollReservation(const SubmissionRecord& record) = 0;
    // Called once, after Created. Populate the existing business message ref,
    // actual delivery state/failure, and Submitted/Rejected/Uncertain state.
    virtual void submit(SubmissionRecord& record, const uint8_t* text, std::size_t text_size) = 0;
    virtual CommitResult commit(SubmissionRecord& record) = 0;
    virtual CommitResult pollCommit(SubmissionRecord& record) = 0;
};

// One accepted management command, advanced by the existing service pump.
// Allocate this owner in adapter-owned storage, never on an ESP task stack.
// Disconnect does not cancel an accepted operation or permit a second send.
class ChatSubmissionOwner final
{
  public:
    explicit ChatSubmissionOwner(SubmissionPort& port) : port_(port) {}

    management::Status begin(const SubmissionRecord& binding, const uint8_t* context,
                             const uint8_t* request_id, uint16_t sequence,
                             const uint8_t* text, std::size_t text_size)
    {
        if (active()) return management::Status::Busy;
        if (!context || !request_id || !text || text_size == 0 || text_size > kTextBytes ||
            binding.conversation_size == 0 || binding.conversation_size > kConversationBytes ||
            binding.text_size != text_size || !nonzero(binding.store) || !nonzero(binding.owner) ||
            !nonzero(binding.operation)) return management::Status::InvalidField;
        const auto status = port_.validate(binding, context, text, text_size);
        if (status != management::Status::Ok) return status;
        binding_ = binding;
        binding_.state = SubmissionState::Reserved;
        binding_.revision = 0;
        binding_.failure = 0;
        binding_.delivery_state = 0;
        std::memset(binding_.message, 0, sizeof(binding_.message));
        std::memcpy(request_, request_id, sizeof(request_));
        std::memcpy(context_, context, sizeof(context_));
        std::memcpy(text_, text, text_size);
        sequence_ = sequence;
        status_ = management::Status::Ok;
        stage_ = Stage::Lookup;
        return management::Status::Ok;
    }

    void advance()
    {
        switch (stage_)
        {
        case Stage::Lookup:
        {
            const auto result = port_.lookup(binding_, record_);
            if (result == LookupResult::Pending) return;
            if (result == LookupResult::Unavailable) return finish(management::Status::StorageUnavailable);
            if (result == LookupResult::Found)
            {
                if (!sameBinding(binding_, record_)) return finish(management::Status::Conflict);
                // An old reservation cannot prove whether the business call
                // happened before interruption. Never execute it again.
                if (record_.state == SubmissionState::Reserved) record_.state = SubmissionState::Uncertain;
                return finish(management::Status::Ok);
            }
            record_ = binding_;
            stage_ = Stage::Reserve;
            return;
        }
        case Stage::Reserve:
            return reserved(port_.reserve(record_));
        case Stage::ReservationPending:
            return reserved(port_.pollReservation(record_));
        case Stage::Submit:
            // Move first: even a subsequent persistence failure must not take
            // this owner back through the business call.
            stage_ = Stage::Commit;
            if (const auto admission = port_.validate(binding_, context_, text_, binding_.text_size); admission != management::Status::Ok)
            {
                record_.state = SubmissionState::Rejected;
                record_.failure = uint16_t(0x8000U | static_cast<uint16_t>(admission));
                return;
            }
            port_.submit(record_, text_, binding_.text_size);
            if (record_.state == SubmissionState::Reserved) record_.state = SubmissionState::Uncertain;
            return;
        case Stage::Commit:
            return committed(port_.commit(record_));
        case Stage::CommitPending:
            return committed(port_.pollCommit(record_));
        default:
            return;
        }
    }

    bool active() const { return stage_ != Stage::Idle; }
    bool ready() const { return stage_ == Stage::Ready; }
    management::Status status() const { return status_; }
    const SubmissionRecord& record() const { return record_; }
    const uint8_t* requestId() const { return request_; }
    uint16_t sequence() const { return sequence_; }

    void releaseReply()
    {
        if (!ready()) return;
        std::memset(text_, 0, sizeof(text_));
        std::memset(request_, 0, sizeof(request_));
        // Metadata has no message body or secret keys. Avoid large aggregate
        // temporaries on ESP stacks; the next begin/lookup overwrites it.
        stage_ = Stage::Idle;
    }

  private:
    enum class Stage : uint8_t
    {
        Idle,
        Lookup,
        Reserve,
        ReservationPending,
        Submit,
        Commit,
        CommitPending,
        Ready,
    };
    static bool nonzero(const uint8_t* id)
    {
        for (std::size_t i = 0; i < kIdSize; ++i)
            if (id[i]) return true;
        return false;
    }
    static bool sameBinding(const SubmissionRecord& a, const SubmissionRecord& b)
    {
        return std::memcmp(a.store, b.store, kIdSize) == 0 &&
               std::memcmp(a.owner, b.owner, kIdSize) == 0 &&
               std::memcmp(a.operation, b.operation, kIdSize) == 0 &&
               std::memcmp(a.content_hash, b.content_hash, kDigestSize) == 0 &&
               a.text_size == b.text_size && a.conversation_size == b.conversation_size &&
               std::memcmp(a.conversation, b.conversation, a.conversation_size) == 0;
    }
    void reserved(ReservationResult result)
    {
        switch (result)
        {
        case ReservationResult::Created:
            stage_ = Stage::Submit;
            return;
        case ReservationResult::Pending:
            stage_ = Stage::ReservationPending;
            return;
        case ReservationResult::Existing:
            stage_ = Stage::Lookup;
            return;
        case ReservationResult::Rejected:
            return finish(management::Status::Busy);
        case ReservationResult::Ambiguous:
            return finish(management::Status::StorageUnavailable);
        }
    }
    void committed(CommitResult result)
    {
        if (result == CommitResult::Pending) stage_ = Stage::CommitPending;
        else finish(result == CommitResult::Durable ? management::Status::Ok : management::Status::StorageUnavailable);
    }
    void finish(management::Status status)
    {
        status_ = status;
        stage_ = Stage::Ready;
    }

    SubmissionPort& port_;
    SubmissionRecord binding_{};
    SubmissionRecord record_{};
    uint8_t text_[kTextBytes]{};
    uint8_t request_[kIdSize]{};
    uint8_t context_[kIdSize]{};
    uint16_t sequence_ = 0;
    Stage stage_ = Stage::Idle;
    management::Status status_ = management::Status::Ok;
};

static_assert(sizeof(ChatSubmissionOwner) <= 768, "Keep the lazy adapter-owned submission state bounded");

} // namespace hostlink::chat_management
