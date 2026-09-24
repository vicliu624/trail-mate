#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "geocaching/usecase/download_client.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
struct Crypto : geocaching::protocol::RecordCrypto
{
    bool sha256(geocaching::ByteView in, std::uint8_t out[32]) override
    {
        chat::reticulum::fullHash(in.data, in.size, out);
        return true;
    }
    geocaching::protocol::VerificationResult verifyEd25519(geocaching::ByteView k, geocaching::ByteView s, geocaching::ByteView m) override
    {
        using R = geocaching::protocol::VerificationResult;
        return ed25519_verify(s.data, m.data, m.size, k.data) ? R::Valid : R::InvalidSignature;
    }
};
struct Port : geocaching::DownloadPort
{
    using Result = geocaching::DownloadOperationResult;
    unsigned commits = 0;
    Result submission = Result::Complete, installation = Result::Rejected, polled = Result::Pending;
    Result submit(const geocaching::Destination&, const geocaching::RequestId&, geocaching::ByteView r) override
    {
        return r.size == 95 ? submission : Result::Rejected;
    }
    Result commit(const geocaching::Destination&, const geocaching::RequestId&, uint64_t generation, geocaching::ByteView,
                  const geocaching::protocol::VerifiedRecordView& value) override
    {
        if (generation != 1 || value.record.name != "Test") return Result::Rejected;
        if (installation != Result::Rejected) ++commits;
        return installation;
    }
    Result poll() override { return polled; }
    Result cancel(const geocaching::Destination&, const geocaching::RequestId&, uint64_t) override { return Result::Pending; }
};
int main(int argc, char** argv)
{
    using namespace geocaching;
    if (argc != 3) return 1;
    std::vector<uint8_t> data[2];
    for (int i = 0; i < 2; ++i)
    {
        std::ifstream file(argv[i + 1], std::ios::binary);
        if (!file.good()) return 2;
        data[i].assign(std::istreambuf_iterator<char>(file), {});
    }
    protocol::SummaryView summary;
    protocol::QueryPageView page;
    RequestId id;
    id.bytes.fill(3);
    if (!protocol::decodeQueryResponse({data[0].data(), data[0].size()}, id, 2048, &summary, 1, page)) return 3;
    id.bytes.fill(4);
    Destination source;
    Crypto crypto;
    Port port;
    std::vector<uint8_t> scratch(summary.signed_bytes + 64);
    auto client = std::make_unique<DownloadClient>(port, crypto);
    const ByteView response{data[1].data(), data[1].size()};
    if (!client->begin(source, id, summary, 1)) return 4;
    if (client->accept(source, response, scratch.data(), scratch.size()) || client->phase() != DownloadPhase::Waiting || port.commits) return 5;
    port.installation = DownloadOperationResult::Pending;
    if (client->accept(source, response, data[1].data(), data[1].size()) || port.commits) return 6;
    if (client->accept(source, response, scratch.data(), 1) || port.commits) return 7;
    if (client->accept(source, response, scratch.data(), scratch.size()) || client->phase() != DownloadPhase::Installing || port.commits != 1) return 8;
    std::fill(scratch.begin(), scratch.end(), 0xa5);
    client->advance();
    if (client->phase() != DownloadPhase::Installing) return 9;
    port.polled = DownloadOperationResult::Complete;
    client->advance();
    if (client->phase() != DownloadPhase::Stored || client->cancel()) return 10;

    Port delayed;
    delayed.submission = DownloadOperationResult::Pending;
    DownloadClient pending(delayed, crypto);
    if (!pending.begin(source, id, summary, 1) || pending.phase() != DownloadPhase::Submitting ||
        pending.accept(source, response, scratch.data(), scratch.size())) return 11;
    delayed.polled = DownloadOperationResult::Complete;
    pending.advance();
    if (pending.phase() != DownloadPhase::Waiting || !pending.cancel() || pending.phase() != DownloadPhase::Cancelling) return 12;
    if (pending.accept(source, response, scratch.data(), scratch.size())) return 13;
    delayed.polled = DownloadOperationResult::Pending;
    pending.advance();
    if (pending.phase() != DownloadPhase::Cancelling) return 14;
    delayed.polled = DownloadOperationResult::Complete;
    pending.advance();
    if (pending.phase() != DownloadPhase::Cancelled) return 15;

    // Recovery borrows its preview for this call only. A late read must not
    // expose Waiting or accept a response until the persistent port is ready.
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        Port restored_port;
        DownloadClient restored(restored_port, crypto);
        auto preview = summary;
        std::string transient_name(summary.name);
        preview.name = transient_name;
        if (restored.resume(source, id, preview, 1, DownloadOperationResult::Rejected) ||
            restored.phase() != DownloadPhase::Idle ||
            !restored.resume(source, id, preview, 1, DownloadOperationResult::Pending)) return 16;
        transient_name.assign(transient_name.size(), 'x');
        restored.advance();
        if (restored.phase() != DownloadPhase::Submitting ||
            restored.accept(source, response, scratch.data(), scratch.size()) || restored_port.commits) return 17;
        if (scenario == 1)
        {
            restored_port.polled = DownloadOperationResult::Rejected;
            restored.advance();
            if (restored.phase() != DownloadPhase::Failed) return 18;
        }
        else if (scenario == 2)
        {
            if (!restored.cancel() || restored.phase() != DownloadPhase::Cancelling) return 19;
            restored_port.polled = DownloadOperationResult::Complete;
            restored.advance();
            if (restored.phase() != DownloadPhase::Cancelled) return 20;
        }
        else
        {
            restored_port.polled = restored_port.installation = DownloadOperationResult::Complete;
            restored.advance();
            if (restored.phase() != DownloadPhase::Waiting ||
                !restored.accept(source, response, scratch.data(), scratch.size()) ||
                restored.phase() != DownloadPhase::Stored || restored_port.commits != 1) return 21;
        }
    }
    return 0;
}
