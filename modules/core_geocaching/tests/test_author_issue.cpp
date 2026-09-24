#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/usecase/author_issue.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
struct Port : geocaching::AuthorIssuePort
{
    std::array<uint8_t, 64> author{};
    bool reserved = false, reject = false;
    unsigned signs = 0;
    unsigned pending_steps = 0, cancellations = 0;
    bool getAuthorKey(uint8_t out[64]) override
    {
        std::memcpy(out, author.data(), 64);
        return true;
    }
    geocaching::AuthorReservationResult reserve(geocaching::ByteView, uint8_t*, size_t) override
    {
        if (pending_steps)
        {
            --pending_steps;
            return geocaching::AuthorReservationResult::Pending;
        }
        reserved = !reject;
        return reserved ? geocaching::AuthorReservationResult::Reserved : geocaching::AuthorReservationResult::Failed;
    }
    void cancelReservation() override { ++cancellations; }
    bool sign(geocaching::ByteView record, uint8_t*, size_t, uint8_t* out, size_t capacity, size_t& written) override
    {
        ++signs;
        if (!reserved) return false;
        std::array<uint8_t, 64> signature{};
        geocaching::protocol::CmpWriter writer(out, capacity);
        if (!writer.array(2) || !writer.binary(record) || !writer.binary({signature.data(), 64})) return false;
        written = writer.size();
        return true;
    }
};
int main(int argc, char** argv)
{
    using namespace geocaching;
    if (argc != 2) return 1;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    protocol::CmpReader reader({bytes.data(), bytes.size()});
    size_t count = 0;
    ByteView record;
    if (!reader.array(count, 2) || !reader.binary(record, 4096)) return 2;
    RecordView decoded;
    if (!protocol::decodeGeocacheRecord(record, decoded)) return 3;
    std::vector<uint8_t> workspace(record.size + 70);
    Port port;
    std::memcpy(port.author.data(), decoded.author_public_key.data, 64);
    auto issue = std::make_unique<AuthorIssue>(port);
    if (!issue->begin(record, workspace.data(), workspace.size())) return 4;
    issue->advance();
    issue->advance();
    if (!port.reserved || port.signs || issue->phase() != AuthorIssuePhase::Sign) return 5;
    issue->advance();
    if (issue->phase() != AuthorIssuePhase::Signed || !issue->signedRecord().size || port.signs != 1) return 6;
    port.signs = 0;
    port.reject = true;
    auto rejected = std::make_unique<AuthorIssue>(port);
    if (!rejected->begin(record, workspace.data(), workspace.size())) return 7;
    rejected->advance();
    rejected->advance();
    rejected->advance();
    if (rejected->phase() != AuthorIssuePhase::Failed || port.signs) return 8;
    port.reject = false;
    auto cancelled = std::make_unique<AuthorIssue>(port);
    if (!cancelled->begin(record, workspace.data(), workspace.size())) return 9;
    cancelled->advance();
    cancelled->advance();
    cancelled->cancel();
    cancelled->advance();
    if (cancelled->phase() != AuthorIssuePhase::Cancelled || port.signs || cancelled->signedRecord().size) return 10;
    port.reserved = false;
    port.pending_steps = 2;
    auto delayed = std::make_unique<AuthorIssue>(port);
    if (!delayed->begin(record, workspace.data(), workspace.size())) return 11;
    delayed->advance();
    delayed->advance();
    delayed->advance();
    if (delayed->phase() != AuthorIssuePhase::Reserve || port.signs || port.reserved) return 12;
    delayed->advance();
    if (delayed->phase() != AuthorIssuePhase::Sign || port.signs || !port.reserved) return 13;
    delayed->advance();
    if (delayed->phase() != AuthorIssuePhase::Signed || port.signs != 1) return 14;
    port.pending_steps = 2;
    port.signs = 0;
    auto pending_cancel = std::make_unique<AuthorIssue>(port);
    if (!pending_cancel->begin(record, workspace.data(), workspace.size())) return 15;
    pending_cancel->advance();
    pending_cancel->advance();
    pending_cancel->cancel();
    pending_cancel->advance();
    if (pending_cancel->phase() != AuthorIssuePhase::Cancelled || port.cancellations != 1 || port.signs) return 16;
    AuthorIssue too_small(port), overlapping(port);
    if (too_small.begin(record, workspace.data(), workspace.size() - 1) ||
        too_small.phase() != AuthorIssuePhase::Idle) return 17;
    std::vector<uint8_t> aliased(record.size + 70);
    std::memcpy(aliased.data(), record.data, record.size);
    if (overlapping.begin({aliased.data(), record.size}, aliased.data(), aliased.size()) ||
        overlapping.phase() != AuthorIssuePhase::Idle) return 18;
    return 0;
}
