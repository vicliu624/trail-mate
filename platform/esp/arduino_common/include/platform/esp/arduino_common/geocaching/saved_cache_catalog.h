#pragma once
#include "platform/esp/arduino_common/geocaching/sd_gpx_hash.h"
#include "platform/esp/arduino_common/geocaching/sd_request_store.h"
#include "ui_presentation/geocaching/geocaching_source.h"
#include <cstdio>
#include <memory>
#include <new>

namespace platform::esp::arduino_common::geocaching
{
// A projection, not another copy of the cache database. The owner serializes
// this with state mutations and resets it after installs or media handoff.
template <class Digest>
class SavedCacheCatalog
{
  public:
    SavedCacheCatalog(::geocaching::storage::LogicalState& state, ::geocaching::protocol::RecordCrypto& crypto)
        : state_(state), crypto_(crypto) {}
    void reset()
    {
        reader_.reset();
        digest_.reset();
        cursor_ = 0;
        valid_ = invalid_ = 0;
        checking_ = true;
        ++generation_;
    }
    bool pending() const { return checking_; }
    bool contains(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash) const
    {
        for (size_t i = 0; i < 64; ++i)
        {
            if (!(valid_ & (uint64_t(1) << i))) continue;
            Candidate entry;
            if (candidate(i, entry) == Lookup::Ready && entry.id.bytes == id && entry.hash.bytes == hash) return true;
        }
        return false;
    }
    void advance()
    {
        if (!checking_) return;
        if (reader_)
        {
            if (reader_->step() == GpxHashStep::Reading) return;
            std::array<uint8_t, 32> hash;
            finish(reader_->result(hash) && hash == expected_hash_);
            return;
        }
        if (cursor_ == 64)
        {
            checking_ = false;
            ++generation_;
            return;
        }
        Candidate entry;
        const auto result = candidate(cursor_, entry);
        if (result == Lookup::End)
        {
            checking_ = false;
            ++generation_;
            return;
        }
        if (result == Lookup::Pending)
        {
            ++cursor_;
            return;
        }
        if (result != Lookup::Ready)
        {
            finish(false);
            return;
        }
        auto verified = ::geocaching::protocol::VerificationResult::WorkspaceTooSmall;
        ::geocaching::protocol::VerifiedRecordView record;
        state_.withScratch([&](uint8_t* bytes, size_t capacity)
                           { verified = ::geocaching::protocol::verifyGeocache(entry.signed_cache, crypto_, bytes, capacity, record, &entry.id, &entry.hash); });
        if (verified != ::geocaching::protocol::VerificationResult::Valid)
        {
            finish(false);
            return;
        }
        expected_hash_ = entry.file_hash;
        char path[112]{};
        std::snprintf(path, sizeof(path), "/trailmate/geocaching/caches/");
        size_t offset = std::strlen(path);
        constexpr char hex[] = "0123456789abcdef";
        for (auto byte : entry.id.bytes)
        {
            path[offset++] = hex[byte >> 4];
            path[offset++] = hex[byte & 15];
        }
        std::memcpy(path + offset, ".gpx", 5);
        digest_.reset(new (std::nothrow) Digest);
        if (digest_) reader_.reset(new (std::nothrow) SdGpxHash<Digest>(*digest_));
        if (!reader_ || !reader_->open(path)) finish(false);
    }
    void snapshot(::ui::geocaching::Snapshot& out) const
    {
        out = {};
        out.generation = generation_;
        out.can_refresh = !checking_;
        for (uint64_t bits = valid_; bits; bits &= bits - 1) ++out.count;
        std::snprintf(out.status.data(), out.status.size(), "%s", checking_ ? "Checking saved GPX files..." : invalid_ ? "Some GPX files changed or are unavailable"
                                                                                                          : out.count  ? "Saved GPX - available offline"
                                                                                                                       : "No downloaded caches");
    }
    bool item(size_t visible_index, uint64_t generation, ::ui::geocaching::Item& out) const
    {
        out = {};
        if (generation != generation_) return false;
        for (size_t i = 0; i < 64; ++i)
        {
            if (!(valid_ & (uint64_t(1) << i))) continue;
            if (visible_index--) continue;
            Candidate entry;
            if (candidate(i, entry) != Lookup::Ready) return false;
            out.id = entry.id.bytes;
            out.revision_hash = entry.hash.bytes;
            out.latitude_e7 = entry.record.latitude_e7;
            out.longitude_e7 = entry.record.longitude_e7;
            out.downloaded = true;
            std::memcpy(out.name.data(), entry.record.name.data(), entry.record.name.size());
            const auto lat = entry.record.latitude_e7 < 0 ? -int64_t(entry.record.latitude_e7) : int64_t(entry.record.latitude_e7);
            const auto lon = entry.record.longitude_e7 < 0 ? -int64_t(entry.record.longitude_e7) : int64_t(entry.record.longitude_e7);
            std::snprintf(out.detail.data(), out.detail.size(), "%s%ld.%07ld, %s%ld.%07ld\nSaved GPX on SD card\nRevision %lu",
                          entry.record.latitude_e7 < 0 ? "-" : "", long(lat / 10000000), long(lat % 10000000),
                          entry.record.longitude_e7 < 0 ? "-" : "", long(lon / 10000000), long(lon % 10000000), static_cast<unsigned long>(entry.record.revision));
            return true;
        }
        return false;
    }

  private:
    enum class Lookup : uint8_t
    {
        End,
        Pending,
        Invalid,
        Ready
    };
    struct Candidate
    {
        ::geocaching::GeocacheId id;
        ::geocaching::RevisionHash hash;
        ::geocaching::ByteView signed_cache;
        ::geocaching::RecordView record;
        std::array<uint8_t, 32> file_hash{};
    };
    Lookup candidate(size_t ordinal, Candidate& out) const
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        out = {};
        const auto view = state_.view();
        size_t cursor = 0;
        MutationView head_row;
        while (view.next(cursor, head_row))
        {
            if (head_row.table != 2) continue;
            if (ordinal--) continue;
            CacheHeadView head;
            if (!decodeCacheHead(head_row.key, head_row.value, head)) return Lookup::Invalid;
            if (!head.current_hash.size) return Lookup::Pending;
            std::memcpy(out.id.bytes.data(), head_row.key.data, 32);
            std::memcpy(out.hash.bytes.data(), head.current_hash.data, 32);
            size_t scan = 0;
            MutationView row;
            uint64_t latest = 0;
            while (view.next(scan, row))
            {
                if (row.table != 12) continue;
                InstallRecordView install;
                if (!decodeInstallRecord(row.key, row.value, install)) return Lookup::Invalid;
                if (install.phase == InstallPhase::Installed && install.generation <= head.install_generation && install.generation > latest &&
                    !std::memcmp(install.cache_id.data, out.id.bytes.data(), 32) && !std::memcmp(install.revision_hash.data, out.hash.bytes.data(), 32))
                {
                    latest = install.generation;
                    std::memcpy(out.file_hash.data(), install.new_file_hash.data, 32);
                }
            }
            if (!latest) return Lookup::Invalid;
            scan = 0;
            while (view.next(scan, row))
            {
                if (row.table != 5) continue;
                OutgoingView outgoing;
                ByteView value;
                TaskView task;
                if (!decodeOutgoing(row.key, row.value, outgoing) || outgoing.state != 4 ||
                    !view.find(10, outgoing.task_id, value) || !decodeTask(outgoing.task_id, value, task) || task.kind != 2 || task.state != 3 ||
                    task.cache_id.size != 32 || task.revision_hash.size != 32 ||
                    std::memcmp(task.cache_id.data, out.id.bytes.data(), 32) || std::memcmp(task.revision_hash.data, out.hash.bytes.data(), 32)) continue;
                RequestId id;
                std::memcpy(id.bytes.data(), row.key.data + 32, 16);
                protocol::GetResponseView response;
                if (!protocol::decodeGetResponse(outgoing.terminal_data, id, 8192, response) || response.has_conflict) return Lookup::Invalid;
                protocol::CmpReader reader(response.signed_cache);
                size_t fields = 0;
                ByteView encoded, signature;
                if (!reader.array(fields, 2) || !reader.binary(encoded, kMaxRecordBytes) || !reader.binary(signature, 64) ||
                    !protocol::decodeGeocacheRecord(encoded, out.record)) return Lookup::Invalid;
                out.signed_cache = response.signed_cache;
                return Lookup::Ready;
            }
            return Lookup::Invalid;
        }
        return Lookup::End;
    }
    void finish(bool valid)
    {
        (valid ? valid_ : invalid_) |= uint64_t(1) << cursor_;
        ++cursor_;
        ++generation_;
        reader_.reset();
        digest_.reset();
    }
    ::geocaching::storage::LogicalState& state_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    std::unique_ptr<Digest> digest_;
    std::unique_ptr<SdGpxHash<Digest>> reader_;
    std::array<uint8_t, 32> expected_hash_{};
    uint64_t valid_ = 0, invalid_ = 0, generation_ = 1;
    size_t cursor_ = 0;
    bool checking_ = true;
};
} // namespace platform::esp::arduino_common::geocaching
