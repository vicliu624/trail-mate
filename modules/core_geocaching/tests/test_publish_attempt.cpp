#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "geocaching/usecase/publish_attempt.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
struct Crypto : geocaching::protocol::RecordCrypto
{
    bool sha256(geocaching::ByteView in, uint8_t out[32]) override
    {
        chat::reticulum::fullHash(in.data, in.size, out);
        return true;
    }
    geocaching::protocol::VerificationResult verifyEd25519(geocaching::ByteView key, geocaching::ByteView sig, geocaching::ByteView message) override
    {
        return ed25519_verify(sig.data, message.data, message.size, key.data) ? geocaching::protocol::VerificationResult::Valid : geocaching::protocol::VerificationResult::InvalidSignature;
    }
};
struct Port : geocaching::PublishAttemptPort
{
    using Result = geocaching::PublishPersistence;
    Result submission = Result::Committed, saving = Result::Committed, polled = Result::Pending;
    bool commit_ok = false;
    unsigned commits = 0;
    Result submit(const geocaching::Destination&, const geocaching::RequestId&, geocaching::ByteView) override { return submission; }
    Result commitResult(const geocaching::Destination&, const geocaching::RequestId&, geocaching::ByteView) override
    {
        ++commits;
        return commit_ok ? saving : Result::Rejected;
    }
    Result cancel(const geocaching::Destination&, const geocaching::RequestId&) override { return Result::Committed; }
    Result poll() override { return polled; }
};
int main(int argc, char** argv)
{
    using namespace geocaching;
    if (argc != 3) return 1;
    std::ifstream a(argv[1], std::ios::binary), b(argv[2], std::ios::binary);
    std::vector<uint8_t> record((std::istreambuf_iterator<char>(a)), {}), response((std::istreambuf_iterator<char>(b)), {});
    std::vector<uint8_t> workspace(record.size() + 26);
    Crypto crypto;
    Port port;
    RequestId request;
    request.bytes.fill(2);
    Destination source;
    auto attempt = std::make_unique<PublishAttempt>(port, crypto);
    if (!attempt->begin(source, request, {record.data(), record.size()}, workspace.data(), workspace.size())) return 2;
    std::fill(workspace.begin(), workspace.end(), 0xa5);
    if (attempt->accept(source, {response.data(), response.size()}) || attempt->phase() != PublishAttemptPhase::Waiting) return 3;
    port.commit_ok = true;
    if (!attempt->accept(source, {response.data(), response.size()}) || attempt->phase() != PublishAttemptPhase::Confirmed) return 4;
    const auto commits = port.commits;
    if (!attempt->accept(source, {response.data(), response.size()}) || port.commits != commits) return 5;
    source.bytes[0] = 1;
    if (attempt->accept(source, {response.data(), response.size()})) return 6;
    auto cancelled = std::make_unique<PublishAttempt>(port, crypto);
    if (!cancelled->begin(source, request, {record.data(), record.size()}, workspace.data(), workspace.size()) || !cancelled->cancel() ||
        cancelled->accept(source, {response.data(), response.size()})) return 7;
    PublishAttempt rejected(port, crypto);
    if (rejected.begin(source, request, {record.data(), record.size()}, workspace.data(), workspace.size() - 1)) return 8;
    std::vector<uint8_t> aliased(record.size() + 26);
    std::memcpy(aliased.data(), record.data(), record.size());
    if (rejected.begin(source, request, {aliased.data(), record.size()}, aliased.data(), aliased.size())) return 9;
    source.bytes[0] = 0;
    auto trailing = response;
    trailing.push_back(0);
    if (attempt->accept(source, {trailing.data(), trailing.size()})) return 10;
    protocol::CmpReader reader({response.data(), response.size()});
    size_t fields = 0;
    uint64_t value = 0;
    ByteView bytes;
    if (!reader.array(fields, 6) || !reader.unsignedInteger(value) || !reader.unsignedInteger(value) ||
        !reader.unsignedInteger(value) || !reader.binary(bytes, 16) || !reader.unsignedInteger(value) ||
        !reader.array(fields, 7) || !reader.binary(bytes, 32) || !reader.unsignedInteger(value) ||
        !reader.binary(bytes, 32)) return 11;
    const auto disposition_offset = reader.position();
    auto changed = response;
    changed[disposition_offset] ^= 1;
    if (attempt->accept(source, {changed.data(), changed.size()}) || port.commits != commits) return 12;
    auto noncanonical = response;
    noncanonical.insert(noncanonical.begin() + disposition_offset, 0xcc);
    if (attempt->accept(source, {noncanonical.data(), noncanonical.size()})) return 13;
    Port delayed;
    delayed.submission = PublishPersistence::Pending;
    delayed.saving = PublishPersistence::Pending;
    delayed.commit_ok = true;
    PublishAttempt asynchronous(delayed, crypto);
    if (!asynchronous.begin(source, request, {record.data(), record.size()}, workspace.data(), workspace.size()) ||
        asynchronous.phase() != PublishAttemptPhase::Submitting || asynchronous.accept(source, {response.data(), response.size()})) return 14;
    asynchronous.advance();
    if (asynchronous.phase() != PublishAttemptPhase::Submitting) return 15;
    delayed.polled = PublishPersistence::Committed;
    asynchronous.advance();
    if (asynchronous.phase() != PublishAttemptPhase::Waiting || asynchronous.accept(source, {response.data(), response.size()}) ||
        asynchronous.phase() != PublishAttemptPhase::Committing || asynchronous.cancel()) return 16;
    delayed.polled = PublishPersistence::Pending;
    asynchronous.advance();
    if (asynchronous.phase() != PublishAttemptPhase::Committing) return 17;
    delayed.polled = PublishPersistence::Rejected;
    asynchronous.advance();
    if (asynchronous.phase() != PublishAttemptPhase::Failed || asynchronous.accept(source, {response.data(), response.size()})) return 18;
    PublishAttempt durable(delayed, crypto);
    if (!durable.begin(source, request, {record.data(), record.size()}, workspace.data(), workspace.size())) return 19;
    delayed.polled = PublishPersistence::Committed;
    durable.advance();
    if (durable.accept(source, {response.data(), response.size()})) return 20;
    durable.advance();
    if (durable.phase() != PublishAttemptPhase::Confirmed) return 21;
    protocol::VerifiedRecordView verified;
    if (protocol::verifyGeocache({record.data(), record.size()}, crypto, workspace.data(), workspace.size(), verified) !=
        protocol::VerificationResult::Valid) return 22;
    std::vector<uint8_t> original_request(record.size() + 26);
    size_t request_size = 0;
    if (!protocol::encodePublishRequest(request, verified.record.encoded, verified.signature, 512,
                                        original_request.data(), original_request.size(), request_size)) return 23;
    original_request.resize(request_size);
    Port restored_port;
    restored_port.submission = PublishPersistence::Rejected;
    restored_port.commit_ok = true;
    PublishAttempt restored(restored_port, crypto);
    if (!restored.resume(source, request, {original_request.data(), original_request.size()}, workspace.data(), workspace.size(), verified.id, verified.hash) ||
        restored.phase() != PublishAttemptPhase::Waiting || restored_port.commits) return 24;
    if (!restored.accept(source, {response.data(), response.size()}) || restored_port.commits != 1) return 25;
    PublishAttempt confirmed(restored_port, crypto);
    if (!confirmed.resume(source, request, {original_request.data(), original_request.size()}, workspace.data(), workspace.size(), verified.id, verified.hash,
                          {response.data(), response.size()}) ||
        confirmed.phase() != PublishAttemptPhase::Confirmed ||
        !confirmed.accept(source, {response.data(), response.size()}) || restored_port.commits != 1) return 26;
    PublishAttempt invalid(restored_port, crypto);
    const auto untouched = original_request;
    if (invalid.resume(source, request, {original_request.data(), original_request.size()}, original_request.data(), original_request.size(), verified.id, verified.hash) ||
        original_request != untouched) return 27;
    original_request.back() ^= 1;
    if (invalid.resume(source, request, {original_request.data(), original_request.size()}, workspace.data(), workspace.size(), verified.id, verified.hash) ||
        invalid.phase() != PublishAttemptPhase::Idle) return 28;
    return 0;
}
