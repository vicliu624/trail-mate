#pragma once
#include "geocaching/protocol/verify_record.h"
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/geocaching/query_browse_port.h"

namespace platform::esp::arduino_common::geocaching
{
// A bounded, disposable browse session. No SD state, journal or recovery.
// The runtime serializes UI reads, authenticated responses and transport sends.
class LiveQueryPort final : public QueryBrowsePort
{
  public:
    using RandomId = bool (*)(void*, uint8_t[16]);
    using Result = ::geocaching::QueryPersistence;
    LiveQueryPort(::geocaching::protocol::RecordCrypto& crypto, RandomId random)
        : crypto_(crypto), random_(random) {}
    bool newRequestId(::geocaching::RequestId& out) override { return random_(nullptr, out.bytes.data()); }
    Result submit(const ::geocaching::DirectoryEntry& directory, const ::geocaching::RequestId& id,
                  ::geocaching::ByteView request) override
    {
        if (!request.data || !request.size || request.size > request_.size()) return Result::Rejected;
        destination_ = directory.delivery;
        request_id_ = id;
        std::memcpy(request_.data(), request.data, request.size);
        request_size_ = request.size;
        next_send_ = 0;
        return Result::Committed;
    }
    // A successful transport submission is not a directory response. Retry the
    // same id sparingly until QueryClient accepts a response or times out.
    chat::MeshSendResult dispatch(chat::MeshAdapterRouter& router, const ::geocaching::Destination& local, uint64_t now)
    {
        if (!request_size_ || now < next_send_) return {};
        std::array<uint8_t, 32> hash{};
        const auto sent = router.sendGeocachingData(destination_.bytes.data(), {request_.data(), request_size_}, false, &hash, local.bytes.data());
        next_send_ = now + (sent.ok ? 15000 : 1000);
        return sent;
    }
    Result commitCapabilities(const ::geocaching::Destination& source, const ::geocaching::RequestId& id,
                              ::geocaching::ByteView response) override
    {
        ::geocaching::protocol::DirectoryCapabilities capabilities;
        if (!matches(source, id) || !::geocaching::protocol::decodeDirectoryCapabilities(response, id, capabilities) || !remember(source, id, response)) return Result::Rejected;
        request_size_ = 0;
        return Result::Committed;
    }
    Result commitPage(const ::geocaching::Destination& source, const ::geocaching::RequestId& id,
                      ::geocaching::ByteView response, const ::geocaching::protocol::QueryPageView&) override
    {
        if (!matches(source, id) || response.size > page_.size() || !remember(source, id, response)) return Result::Rejected;
        std::memcpy(page_.data(), response.data, response.size);
        page_size_ = response.size;
        page_id_ = id;
        page_source_ = source;
        request_size_ = 0;
        ++generation_;
        return Result::Committed;
    }
    Result pollPersistence() override { return Result::Rejected; }
    Result cancel(const ::geocaching::Destination&, const ::geocaching::RequestId&) override
    {
        request_size_ = 0;
        return Result::Committed;
    }
    uint64_t generation() const override { return generation_; }
    bool pageSource(::geocaching::Destination& out) const override
    {
        if (!page_size_) return false;
        out = page_source_;
        return true;
    }
    bool page(::geocaching::protocol::QueryPageView& out) const override
    {
        out = {};
        return page_size_ && ::geocaching::protocol::decodeQueryPage({page_.data(), page_size_}, page_id_, page_.size(), 20, out);
    }
    bool summary(size_t index, ::geocaching::protocol::SummaryView& out) const override
    {
        ::geocaching::protocol::QueryPageView current;
        if (!page(current) || index >= current.count) return false;
        ::geocaching::protocol::CmpReader rows(current.encoded_items);
        for (size_t i = 0; i <= index; ++i)
            if (!::geocaching::protocol::decodeSummary(rows, out)) return false;
        return true;
    }
    bool accepted(const ::geocaching::Destination& source, const ::geocaching::RequestId& id, ::geocaching::ByteView response) const override
    {
        if (!response.data || !response.size || response.size > page_.size()) return false;
        for (const auto& receipt : receipts_)
        {
            if (!receipt.valid || receipt.source.bytes != source.bytes || receipt.id.bytes != id.bytes) continue;
            std::array<uint8_t, 32> hash{};
            return crypto_.sha256(response, hash.data()) && hash == receipt.hash;
        }
        return false;
    }

  private:
    bool matches(const ::geocaching::Destination& source, const ::geocaching::RequestId& id) const
    {
        return request_size_ && source.bytes == destination_.bytes && id.bytes == request_id_.bytes;
    }
    bool remember(const ::geocaching::Destination& source, const ::geocaching::RequestId& id, ::geocaching::ByteView response)
    {
        auto& receipt = receipts_[next_receipt_];
        receipt.valid = false;
        if (!crypto_.sha256(response, receipt.hash.data())) return false;
        receipt.source = source;
        receipt.id = id;
        receipt.valid = true;
        next_receipt_ ^= 1;
        return true;
    }
    struct Receipt
    {
        ::geocaching::Destination source;
        ::geocaching::RequestId id;
        std::array<uint8_t, 32> hash{};
        bool valid = false;
    };
    ::geocaching::protocol::RecordCrypto& crypto_;
    RandomId random_;
    ::geocaching::Destination destination_, page_source_;
    ::geocaching::RequestId request_id_, page_id_;
    std::array<uint8_t, 256> request_{};
    std::array<uint8_t, 2048> page_{};
    std::array<Receipt, 2> receipts_{};
    size_t request_size_ = 0, page_size_ = 0;
    uint64_t generation_ = 0, next_send_ = 0;
    unsigned next_receipt_ = 0;
};
static_assert(sizeof(LiveQueryPort) < 3072, "Browsing has a bounded memory budget");
} // namespace platform::esp::arduino_common::geocaching
