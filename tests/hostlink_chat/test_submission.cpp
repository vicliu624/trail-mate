#include "hostlink/chat_submission_owner.h"
#include <array>
#include <cassert>
#include <cstring>

namespace hm = hostlink::management;
namespace cm = hostlink::chat_management;

struct Port final : cm::SubmissionPort
{
    cm::SubmissionRecord saved{};
    bool present = false;
    unsigned sends = 0, reservations = 0, commits = 0, polls = 0;
    bool reserve_pending = false, commit_pending = false, reservation_ambiguous = false;
    bool no_space = false, commit_failure = false;
    hm::Status admission = hm::Status::Ok;
    cm::SubmissionState outcome = cm::SubmissionState::Submitted;
    std::array<uint8_t, 4> expected_text{1, 2, 3, 4};
    hm::Status validate(const cm::SubmissionRecord&, const uint8_t*, const uint8_t*, std::size_t) override { return admission; }
    cm::LookupResult lookup(const cm::SubmissionRecord&, cm::SubmissionRecord& out) override
    {
        if (!present) return cm::LookupResult::Missing;
        out = saved;
        return cm::LookupResult::Found;
    }
    cm::ReservationResult reserve(const cm::SubmissionRecord& record) override
    {
        ++reservations;
        if (no_space) return cm::ReservationResult::Rejected;
        assert(!present);
        saved = record;
        present = true;
        if (reservation_ambiguous) return cm::ReservationResult::Ambiguous;
        return reserve_pending ? cm::ReservationResult::Pending : cm::ReservationResult::Created;
    }
    cm::ReservationResult pollReservation(const cm::SubmissionRecord&) override
    {
        ++polls;
        return reserve_pending ? cm::ReservationResult::Pending : cm::ReservationResult::Created;
    }
    void submit(cm::SubmissionRecord& record, const uint8_t* text, std::size_t count) override
    {
        assert(present && saved.state == cm::SubmissionState::Reserved);
        assert(count == expected_text.size() && !std::memcmp(text, expected_text.data(), count));
        ++sends;
        record.state = outcome;
        record.message[0] = 77;
        record.delivery_state = 1; // Queued; never invent Delivered.
        record.revision = 1;
    }
    cm::CommitResult commit(cm::SubmissionRecord& record) override
    {
        ++commits;
        if (commit_failure) return cm::CommitResult::Unavailable;
        saved = record;
        return commit_pending ? cm::CommitResult::Pending : cm::CommitResult::Durable;
    }
    cm::CommitResult pollCommit(cm::SubmissionRecord&) override
    {
        ++polls;
        return commit_pending ? cm::CommitResult::Pending : cm::CommitResult::Durable;
    }
};

cm::SubmissionRecord binding()
{
    cm::SubmissionRecord record;
    record.store[0] = 1;
    record.owner[0] = 2;
    record.operation[0] = 3;
    record.content_hash[0] = 4;
    record.conversation[0] = 0x91;
    record.conversation_size = 1;
    record.text_size = 4;
    return record;
}
const uint8_t context[16]{9}, request[16]{8};
void start(cm::ChatSubmissionOwner& owner, const cm::SubmissionRecord& record)
{
    const uint8_t text[]{1, 2, 3, 4};
    assert(owner.begin(record, context, request, 42, text, sizeof(text)) == hm::Status::Ok);
}
void finish(cm::ChatSubmissionOwner& owner)
{
    for (unsigned step = 0; step < 20 && !owner.ready(); ++step) owner.advance();
    assert(owner.ready());
}
int main()
{
    {
        Port port;
        cm::ChatSubmissionOwner owner(port);
        auto record = binding();
        uint8_t text[]{1, 2, 3, 4}, ids[16]{8};
        assert(owner.begin(record, context, ids, 42, text, sizeof(text)) == hm::Status::Ok);
        std::memset(text, 0xff, sizeof(text));
        std::memset(ids, 0xff, sizeof(ids));
        finish(owner);
        assert(port.sends == 1 && port.reservations == 1 && port.commits == 1);
        assert(owner.status() == hm::Status::Ok && owner.sequence() == 42 && owner.requestId()[0] == 8);
        assert(owner.record().state == cm::SubmissionState::Submitted && owner.record().message[0] == 77);
        owner.releaseReply(); // A dropped serial response does not undo commit.
        start(owner, record);
        finish(owner);
        assert(port.sends == 1 && owner.record().state == cm::SubmissionState::Submitted);
        owner.releaseReply();
        record.content_hash[0] ^= 1;
        start(owner, record);
        finish(owner);
        assert(owner.status() == hm::Status::Conflict && port.sends == 1);
    }
    for (unsigned crash_after : {2U, 3U})
    {
        Port port;
        {
            cm::ChatSubmissionOwner owner(port);
            start(owner, binding());
            for (unsigned i = 0; i < crash_after; ++i) owner.advance();
            assert(port.present && port.saved.state == cm::SubmissionState::Reserved);
        }
        const auto before = port.sends;
        cm::ChatSubmissionOwner recovered(port);
        start(recovered, binding());
        finish(recovered);
        assert(port.sends == before && recovered.record().state == cm::SubmissionState::Uncertain);
    }
    {
        Port port;
        port.reserve_pending = true;
        cm::ChatSubmissionOwner owner(port);
        start(owner, binding());
        for (unsigned i = 0; i < 8; ++i) owner.advance();
        assert(!port.sends && port.reservations == 1 && port.polls > 0);
        port.reserve_pending = false;
        port.commit_pending = true;
        for (unsigned i = 0; i < 8; ++i) owner.advance();
        assert(port.sends == 1 && port.commits == 1 && !owner.ready());
        port.commit_pending = false;
        finish(owner);
        assert(port.sends == 1 && owner.status() == hm::Status::Ok);
    }
    {
        Port port;
        port.no_space = true;
        cm::ChatSubmissionOwner owner(port);
        start(owner, binding());
        finish(owner);
        assert(owner.status() == hm::Status::Busy && !port.sends);
    }
    {
        Port port;
        port.reservation_ambiguous = true;
        cm::ChatSubmissionOwner owner(port);
        start(owner, binding());
        finish(owner);
        assert(owner.status() == hm::Status::StorageUnavailable && !port.sends);
        owner.releaseReply();
        start(owner, binding());
        finish(owner);
        assert(owner.record().state == cm::SubmissionState::Uncertain && !port.sends);
    }
    {
        Port port;
        port.commit_failure = true;
        cm::ChatSubmissionOwner owner(port);
        start(owner, binding());
        finish(owner);
        assert(owner.status() == hm::Status::StorageUnavailable && port.sends == 1);
        owner.releaseReply();
        start(owner, binding());
        finish(owner);
        assert(owner.record().state == cm::SubmissionState::Uncertain && port.sends == 1);
    }
    {
        Port port;
        port.admission = hm::Status::Conflict;
        cm::ChatSubmissionOwner owner(port);
        const uint8_t text[]{1, 2, 3, 4};
        assert(owner.begin(binding(), context, request, 1, text, sizeof(text)) == hm::Status::Conflict);
        assert(!owner.active() && !port.sends && !port.reservations);
    }
}
