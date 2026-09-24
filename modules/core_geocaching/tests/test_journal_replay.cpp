#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "geocaching/protocol/query_request.h"
#include "geocaching/protocol/record_encoder.h"
#include "geocaching/storage/draft_record.h"
#include "geocaching/storage/queued_request.h"
#include "platform/esp/arduino_common/geocaching/indexed_dispatch_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_download_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_publication_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_query_store_port.h"
#include "platform/esp/arduino_common/geocaching/request_dispatcher.h"
#include "platform/esp/arduino_common/geocaching/saved_cache_catalog.h"
#include "platform/esp/arduino_common/geocaching/sd_author_issue_port.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_index_import.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_references.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_writer.h"
#include "platform/esp/arduino_common/geocaching/sd_download_port.h"
#include "platform/esp/arduino_common/geocaching/sd_index_append.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include "platform/esp/arduino_common/geocaching/sd_index_head_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_index_head_writer.h"
#include "platform/esp/arduino_common/geocaching/sd_index_initialize.h"
#include "platform/esp/arduino_common/geocaching/sd_index_lookup.h"
#include "platform/esp/arduino_common/geocaching/sd_index_references.h"
#include "platform/esp/arduino_common/geocaching/sd_index_replay.h"
#include "platform/esp/arduino_common/geocaching/sd_index_root_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_index_root_writer.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include "platform/esp/arduino_common/geocaching/sd_index_transaction.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_attempt_update.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_begin_attempt.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_directory_reply.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_download_context.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_download_recovery.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_download_reply.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_draft_save.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_install.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_new_task.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_pending_request.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_recovery.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_stop_task.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_value_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_replay.h"
#include "platform/esp/arduino_common/geocaching/sd_publish_port.h"
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

std::map<std::string, std::vector<uint8_t>> files;
std::set<std::string> index_directories;
std::map<std::string, size_t> read_bytes;
bool fail_read_once = false;
bool fail_nothrow_once = false;
unsigned nothrow_allocations_before_failure = 0;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (fail_nothrow_once && !nothrow_allocations_before_failure)
    {
        fail_nothrow_once = false;
        return nullptr;
    }
    if (fail_nothrow_once) --nothrow_allocations_before_failure;
    try
    {
        return ::operator new(size);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}
size_t step_bytes = 0;
bool exceeded_budget = false;
bool flush_ok = true;
size_t write_limit = SIZE_MAX;
unsigned flush_calls = 0;
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return true; }
bool sd_external_block_owner_active() { return false; }
bool sd_is_directory(const char* path)
{
    if (std::string(path).find("/trailmate/geocaching/.state/index") == 0)
        return index_directories.count(path) != 0;
    return true;
}
bool sd_mkdir(const char* path)
{
    const std::string directory(path);
    const auto parent = directory.substr(0, directory.find_last_of('/'));
    if (!sd_is_directory(parent.c_str()) || files.count(directory)) return false;
    index_directories.insert(directory);
    return true;
}
bool sd_exists(const char* path) { return files.count(path) != 0; }
bool sd_rename(const char* from, const char* to)
{
    if (!files.count(from) || files.count(to)) return false;
    files[to] = std::move(files.at(from));
    files.erase(from);
    return true;
}
class SdRuntimeDir::Impl
{
  public:
    bool open = false;
    std::vector<std::string> names;
    size_t position = 0;
};
SdRuntimeDir::SdRuntimeDir() : impl_(new Impl) {}
SdRuntimeDir::~SdRuntimeDir() { delete impl_; }
bool SdRuntimeDir::open(const char* path)
{
    impl_->names.clear();
    impl_->position = 0;
    const std::string prefix = std::string(path) + '/';
    for (const auto& file : files)
        if (file.first.compare(0, prefix.size(), prefix) == 0 && file.first.find('/', prefix.size()) == std::string::npos)
            impl_->names.push_back(file.first.substr(prefix.size()));
    return impl_->open = true;
}
void SdRuntimeDir::close() { impl_->open = false; }
bool SdRuntimeDir::is_open() const { return impl_->open; }
SdDirReadStatus SdRuntimeDir::read_next_status(char* name, size_t capacity, bool* is_dir)
{
    if (!impl_->open) return SdDirReadStatus::IoError;
    if (impl_->position == impl_->names.size()) return SdDirReadStatus::End;
    const auto& next = impl_->names[impl_->position++];
    if (next.size() >= capacity) return SdDirReadStatus::IoError;
    std::memcpy(name, next.c_str(), next.size() + 1);
    if (is_dir) *is_dir = false;
    return SdDirReadStatus::Entry;
}
class SdRuntimeFile::Impl
{
  public:
    std::string path;
    size_t offset = 0;
    bool open = false;
    bool append = false;
    bool writable = false;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char* path, const char* mode)
{
    impl_->path = path;
    impl_->offset = 0;
    impl_->append = mode[0] == 'a';
    impl_->writable = impl_->append || mode[0] == 'w';
    if (mode[0] == 'w') files[path].clear();
    if (impl_->append) files.try_emplace(path);
    return impl_->open = files.count(path) != 0;
}
void SdRuntimeFile::close() { impl_->open = false; }
bool SdRuntimeFile::is_open() const { return impl_->open; }
size_t SdRuntimeFile::write(const void* bytes, size_t count)
{
    if (!impl_->open || !impl_->writable) return 0;
    auto& data = files.at(impl_->path);
    const auto written = std::min(count, write_limit);
    const auto* input = static_cast<const uint8_t*>(bytes);
    data.insert(data.end(), input, input + written);
    impl_->offset = data.size();
    step_bytes += written;
    return written;
}
bool SdRuntimeFile::flush()
{
    ++flush_calls;
    return flush_ok;
}
uint64_t SdRuntimeFile::size() const { return files.at(impl_->path).size(); }
bool SdRuntimeFile::seek(uint64_t offset)
{
    if (!impl_->open || offset > files.at(impl_->path).size()) return false;
    impl_->offset = static_cast<size_t>(offset);
    return true;
}
int SdRuntimeFile::read(void* output, size_t count)
{
    if (fail_read_once)
    {
        fail_read_once = false;
        return -1;
    }
    const auto& data = files.at(impl_->path);
    const auto n = std::min(count, data.size() - impl_->offset);
    step_bytes += n;
    ::read_bytes[impl_->path] += n;
    std::memcpy(output, data.data() + impl_->offset, n);
    impl_->offset += n;
    return static_cast<int>(n);
}
SdFileReadResult sd_read_file(const char* path, uint8_t* buffer, size_t capacity)
{
    SdFileReadResult result;
    auto entry = files.find(path);
    if (entry == files.end())
    {
        result.status = SdFileReadStatus::Missing;
        return result;
    }
    result.file_size = entry->second.size();
    result.bytes_read = std::min(capacity, entry->second.size());
    step_bytes += result.bytes_read;
    std::memcpy(buffer, entry->second.data(), result.bytes_read);
    result.status = SdFileReadStatus::Ready;
    return result;
}
} // namespace platform::esp::arduino_common::storage

using namespace platform::esp::arduino_common::geocaching;
// Test driver only; production owners yield between every operation.
ReplayStep nextReady(SdJournalReplay& replay, ::geocaching::storage::TransactionView& transaction)
{
    for (unsigned step = 0; step < 512; ++step)
    {
        step_bytes = 0;
        const auto result = replay.next(transaction);
        if (step_bytes > 512)
        {
            exceeded_budget = true;
            return ReplayStep::Corrupt;
        }
        if (result != ReplayStep::Advancing) return result;
    }
    return ReplayStep::Corrupt;
}
template <class Validate>
ReplayStep applyReady(SdJournalReplay& replay, ::geocaching::storage::LogicalState& state, Validate validate)
{
    for (unsigned step = 0; step < 512; ++step)
    {
        step_bytes = 0;
        const auto result = replay.applyNext(state, validate);
        if (step_bytes > 512)
        {
            exceeded_budget = true;
            return ReplayStep::Corrupt;
        }
        if (result != ReplayStep::Advancing) return result;
    }
    return ReplayStep::Corrupt;
}
int checkIndexTransactions()
{
    using namespace ::geocaching;
    using namespace ::geocaching::storage;
    files.clear();
    VolumeInstance volume{};
    const auto format = encodeVolumeHeader(volume);
    files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
    std::array<uint8_t, 16> keys[2]{};
    keys[0][0] = 1;
    keys[1][0] = 2;
    const auto bucket = static_cast<uint8_t>(::sys::crc32(keys[0].data(), 16));
    bool collision = false;
    for (unsigned i = 0; i < 256; ++i)
    {
        keys[1][15] = static_cast<uint8_t>(i);
        if (static_cast<uint8_t>(::sys::crc32(keys[1].data(), 16)) == bucket)
        {
            collision = true;
            break;
        }
    }
    if (!collision) return 80;
    const auto frameFor = [&](uint64_t sequence)
    {
        uint8_t values[2][64], payload[256];
        size_t sizes[2]{}, size = 0;
        MutationView changes[2];
        for (unsigned i = 0; i < 2; ++i)
        {
            DraftView draft;
            draft.generation = sequence;
            draft.name = sequence == 1 ? (i ? "Beta" : "Alpha") : (i ? "Beta updated" : "Alpha updated");
            if (!encodeDraft({keys[i].data(), 16}, draft, values[i], sizeof(values[i]), sizes[i])) return std::vector<uint8_t>{};
            changes[i] = {4, {keys[i].data(), 16}, {values[i], sizes[i]}, false};
        }
        RecordHeader header;
        if (!encodeTransaction(sequence - 1, changes, 2, payload, sizeof(payload), size) ||
            !makeRecordHeader(RecordKind::Transaction, sequence, {payload, size}, header)) return std::vector<uint8_t>{};
        std::vector<uint8_t> frame(header.begin(), header.end());
        frame.insert(frame.end(), payload, payload + size);
        return frame;
    };
    auto frame = frameFor(1);
    if (frame.empty()) return 81;
    files["/trailmate/geocaching/.state/journal/0000000000000001.gcj"] = frame;
    std::array<uint8_t, kIndexShardBitmapSize> bitmap{};
    IndexRootBytes base, candidate;
    IndexRootView initial{23, 0, 1, 'a', {bitmap.data(), bitmap.size()}}, parent;
    if (!encodeIndexRoot(volume, initial, base) || !decodeIndexRoot({base.data(), base.size()}, volume, parent)) return 82;
    for (unsigned copy = 0; copy < 2; ++copy)
    {
        SdIndexRootWriter writer(volume);
        if (!writer.begin(copy, base)) return 83;
        auto status = IndexRootWriteStep::Working;
        for (unsigned i = 0; i < 64 && status == IndexRootWriteStep::Working; ++i) status = writer.step();
        if (status != IndexRootWriteStep::Verified) return 84;
    }
    const auto readDraftName = [&](const IndexRootView& root, unsigned key, std::string_view expected)
    {
        uint8_t buffer[512];
        SdIndexGet reader(volume);
        if (!reader.begin(root, 4, {keys[key].data(), 16}, buffer, sizeof(buffer))) return false;
        auto rs = IndexGetStep::Working;
        for (unsigned i = 0; i < 128 && rs == IndexGetStep::Working; ++i)
        {
            if (reader.value().data) return false;
            step_bytes = 0;
            rs = reader.step();
            if (step_bytes > 512) return false;
        }
        DraftView draft;
        return rs == IndexGetStep::Ready && decodeDraft({keys[key].data(), 16}, reader.value(), draft) && draft.name == expected;
    };
    auto transaction = std::make_unique<SdIndexTransaction>(volume);
    if (!transaction->begin(parent, 0, {frame.data(), frame.size()}, 1, 0, candidate)) return 85;
    auto result = IndexTransactionStep::Working;
    IndexRootView committed;
    for (unsigned i = 0; i < 1024 && result == IndexTransactionStep::Working; ++i)
    {
        if (transaction->committed(committed)) return 86;
        step_bytes = 0;
        result = transaction->step();
        if (step_bytes > 512) return 87;
    }
    if (result != IndexTransactionStep::Verified || transaction->completedEntries() != 2 || !transaction->committed(committed) ||
        committed.sequence != 1 || !readDraftName(committed, 0, "Alpha") || !readDraftName(committed, 1, "Beta")) return 88;
    uint8_t tiny[24];
    SdIndexGet missing(volume), limited(volume), overlap(volume);
    if (!missing.begin(committed, 7, {keys[0].data(), 16}, tiny, sizeof(tiny)) || missing.step() != IndexGetStep::NotFound || missing.value().data ||
        overlap.begin(committed, 4, {keys[0].data(), 16}, candidate.data(), candidate.size()) ||
        !limited.begin(committed, 4, {keys[0].data(), 16}, tiny, sizeof(tiny))) return 95;
    auto limited_result = IndexGetStep::Working;
    for (unsigned step = 0; step < 128 && limited_result == IndexGetStep::Working; ++step) limited_result = limited.step();
    if (limited_result != IndexGetStep::WorkspaceTooSmall || limited.value().data) return 96;
    auto next_frame = frameFor(2);
    files["/trailmate/geocaching/.state/journal/0000000000000002.gcj"] = next_frame;
    transaction = std::make_unique<SdIndexTransaction>(volume);
    if (!transaction->begin(committed, 1, {next_frame.data(), next_frame.size()}, 2, 0, base)) return 89;
    result = IndexTransactionStep::Working;
    std::map<std::string, std::vector<uint8_t>> after_first;
    for (unsigned i = 0; i < 1024 && result == IndexTransactionStep::Working; ++i)
    {
        step_bytes = 0;
        result = transaction->step();
        if (step_bytes > 512) return 90;
        if (transaction->completedEntries() == 1 && after_first.empty())
        {
            after_first = files;
            write_limit = 7;
        }
    }
    write_limit = SIZE_MAX;
    if (result != IndexTransactionStep::IoError || after_first.empty() ||
        !readDraftName(committed, 0, "Alpha") || !readDraftName(committed, 1, "Beta")) return 91;
    // Restart from a crash after the first complete shard entry, before the
    // injected short write. Replaying the transaction must not duplicate it.
    transaction.reset();
    files = after_first;
    transaction = std::make_unique<SdIndexTransaction>(volume);
    if (!transaction->begin(committed, 1, {next_frame.data(), next_frame.size()}, 2, 0, base)) return 92;
    result = IndexTransactionStep::Working;
    for (unsigned i = 0; i < 1024 && result == IndexTransactionStep::Working; ++i) result = transaction->step();
    IndexRootView updated;
    if (result != IndexTransactionStep::Verified || !transaction->committed(updated) || updated.sequence != 2 ||
        !readDraftName(updated, 0, "Alpha updated") || !readDraftName(updated, 1, "Beta updated")) return 93;
    char path[80];
    if (!indexShardPath('a', 4, {keys[0].data(), 16}, path, sizeof(path)) || files[path].size() != 4 * kIndexEntrySize) return 94;
    uint8_t erase_payload[64];
    size_t erase_size = 0;
    MutationView erase{4, {keys[0].data(), 16}, {}, true};
    RecordHeader erase_header;
    if (!encodeTransaction(2, &erase, 1, erase_payload, sizeof(erase_payload), erase_size) ||
        !makeRecordHeader(RecordKind::Transaction, 3, {erase_payload, erase_size}, erase_header)) return 97;
    std::vector<uint8_t> erase_frame(erase_header.begin(), erase_header.end());
    erase_frame.insert(erase_frame.end(), erase_payload, erase_payload + erase_size);
    files["/trailmate/geocaching/.state/journal/0000000000000003.gcj"] = erase_frame;
    transaction = std::make_unique<SdIndexTransaction>(volume);
    if (!transaction->begin(updated, 0, {erase_frame.data(), erase_frame.size()}, 3, 0, candidate)) return 98;
    result = IndexTransactionStep::Working;
    for (unsigned i = 0; i < 1024 && result == IndexTransactionStep::Working; ++i) result = transaction->step();
    IndexRootView deleted;
    if (result != IndexTransactionStep::Verified || !transaction->committed(deleted) ||
        !readDraftName(updated, 0, "Alpha updated") || !readDraftName(deleted, 1, "Beta updated")) return 99;
    uint8_t read_buffer[512];
    SdIndexGet removed(volume);
    if (!removed.begin(deleted, 4, {keys[0].data(), 16}, read_buffer, sizeof(read_buffer))) return 100;
    auto removed_result = IndexGetStep::Working;
    for (unsigned i = 0; i < 128 && removed_result == IndexGetStep::Working; ++i) removed_result = removed.step();
    if (removed_result != IndexGetStep::NotFound || removed.value().data) return 101;
    SdIndexScan live_rows(volume);
    if (!live_rows.begin(deleted, 4, read_buffer, sizeof(read_buffer))) return 144;
    auto scan_result = IndexScanStep::Working;
    unsigned live_count = 0;
    for (unsigned step = 0; step < 1024; ++step)
    {
        step_bytes = 0;
        scan_result = live_rows.step();
        if (step_bytes > 512) return 145;
        if (scan_result == IndexScanStep::Item)
        {
            MutationView row;
            DraftView draft;
            if (!live_rows.item(row) || !decodeDraft(row.key, row.value, draft) || draft.name != "Beta updated" || !live_rows.advance()) return 146;
            ++live_count;
        }
        else if (scan_result != IndexScanStep::Working) break;
    }
    if (scan_result != IndexScanStep::End || live_count != 1) return 147;
    auto& damaged = files["/trailmate/geocaching/.state/journal/0000000000000002.gcj"];
    damaged.back() ^= 1;
    SdIndexGet corrupt(volume);
    if (!corrupt.begin(deleted, 4, {keys[1].data(), 16}, read_buffer, sizeof(read_buffer))) return 102;
    auto corrupt_result = IndexGetStep::Working;
    for (unsigned i = 0; i < 128 && corrupt_result == IndexGetStep::Working; ++i) corrupt_result = corrupt.step();
    if (corrupt_result != IndexGetStep::Invalid || corrupt.value().data) return 103;
    transaction.reset();
    files.clear();
    return 0;
}

int checkIndexedCommitCapacity()
{
    using namespace ::geocaching;
    using namespace ::geocaching::storage;
    files.clear();
    read_bytes.clear();
    VolumeInstance volume{};
    const auto format = encodeVolumeHeader(volume);
    files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
    index_directories.clear();
    IndexRootBytes roots[2];
    IndexRootView current;
    for (const auto* checkpoint : {"/trailmate/geocaching/.state/checkpoint/a.gcs", "/trailmate/geocaching/.state/checkpoint/b.gcs"})
    {
        files[checkpoint] = {1, 2, 3};
        const auto before = files;
        SdIndexInitialize blocked(volume);
        if (!blocked.begin(roots[0])) return 233;
        auto status = IndexRootWriteStep::Working;
        for (unsigned i = 0; i < 16 && status == IndexRootWriteStep::Working; ++i) status = blocked.step();
        if (status != IndexRootWriteStep::Invalid || files != before || !index_directories.empty()) return 234;
        files.erase(checkpoint);
    }
    SdIndexInitialize initialize(volume);
    if (!initialize.begin(roots[0])) return 104;
    auto initialized = IndexRootWriteStep::Working;
    for (unsigned step = 0; step < 128 && initialized == IndexRootWriteStep::Working; ++step)
    {
        step_bytes = 0;
        initialized = initialize.step();
        if (step_bytes > 512) return 169;
    }
    if (initialized != IndexRootWriteStep::Verified ||
        files["/trailmate/geocaching/.state/index/root.h0"] != std::vector<uint8_t>(roots[0].begin(), roots[0].end()) ||
        files["/trailmate/geocaching/.state/index/root.h1"] != files["/trailmate/geocaching/.state/index/root.h0"]) return 170;
    roots[1] = roots[0];
    SdIndexInitialize duplicate(volume);
    if (!duplicate.begin(roots[1])) return 171;
    initialized = IndexRootWriteStep::Working;
    for (unsigned step = 0; step < 128 && initialized == IndexRootWriteStep::Working; ++step) initialized = duplicate.step();
    if (initialized != IndexRootWriteStep::Invalid) return 172;
    if (!decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, current)) return 105;
    uint8_t frame[512], encoded[256];
    unsigned copy = 0;
    size_t logical_bytes = 0;
    const std::string description(96, 'x');
    for (unsigned row = 1; row <= 40; ++row)
    {
        std::array<uint8_t, 16> key{};
        key[0] = static_cast<uint8_t>(row);
        const auto name = std::string("Draft ") + std::to_string(row);
        DraftView draft;
        draft.name = name;
        draft.description = description;
        size_t size = 0;
        if (!encodeDraft({key.data(), key.size()}, draft, encoded, sizeof(encoded), size)) return 106;
        logical_bytes += key.size() + size + 4;
        const auto staged_reads = read_bytes["/trailmate/geocaching/.state/journal.pending"];
        auto commit = std::make_unique<SdIndexedDraftSave>(volume);
        if (!commit->begin(current, copy, {key.data(), key.size()}, {encoded, size}, 0, frame, sizeof(frame), roots[1 - copy])) return 107;
        auto result = IndexedCommitStep::Working;
        bool released = false;
        for (unsigned step = 0; step < 512 && result == IndexedCommitStep::Working; ++step)
        {
            step_bytes = 0;
            result = commit->step();
            if (step_bytes > 512) return 108;
            if (commit->inputConsumed() && !released)
            {
                std::memset(encoded, 0xa5, sizeof(encoded));
                key.fill(0xa5);
                released = true;
            }
        }
        if (result != IndexedCommitStep::Verified || !commit->committed(current) || current.sequence != row || !released) return 109;
        char journal_path[96];
        std::snprintf(journal_path, sizeof(journal_path), "/trailmate/geocaching/.state/journal/%016llx.gcj", static_cast<unsigned long long>(row));
        // The single readback is now performed before publishing the filename.
        if (read_bytes[journal_path] != 0 ||
            read_bytes["/trailmate/geocaching/.state/journal.pending"] - staged_reads != files[journal_path].size()) return 118;
        copy = 1 - copy;
    }
    if (logical_bytes <= 4096) return 110;
    {
        std::array<uint8_t, 16> key{};
        key[0] = 1;
        DraftView next;
        next.name = "Stale editor overwrite";
        size_t size = 0;
        if (!encodeDraft({key.data(), key.size()}, next, encoded, sizeof(encoded), size)) return 193;
        const auto before = files;
        auto stale = std::make_unique<SdIndexedDraftSave>(volume);
        if (!stale->begin(current, copy, {key.data(), key.size()}, {encoded, size}, 0, frame, sizeof(frame), roots[1 - copy])) return 194;
        auto result = IndexedCommitStep::Working;
        for (unsigned step = 0; step < 512 && result == IndexedCommitStep::Working; ++step) result = stale->step();
        if (result != IndexedCommitStep::Invalid || files != before) return 195;
    }
    for (unsigned row : {1U, 20U, 40U})
    {
        std::array<uint8_t, 16> key{};
        key[0] = static_cast<uint8_t>(row);
        SdIndexGet get(volume);
        if (!get.begin(current, 4, {key.data(), key.size()}, frame, sizeof(frame))) return 111;
        auto result = IndexGetStep::Working;
        for (unsigned step = 0; step < 256 && result == IndexGetStep::Working; ++step) result = get.step();
        DraftView draft;
        if (result != IndexGetStep::Ready || !decodeDraft({key.data(), key.size()}, get.value(), draft) ||
            draft.name != std::string("Draft ") + std::to_string(row) || draft.description != description) return 112;
    }
    std::array<uint8_t, 16> extra_key{};
    extra_key[0] = 41;
    DraftView extra;
    extra.name = "Extra";
    extra.description = description;
    size_t extra_size = 0;
    if (!encodeDraft({extra_key.data(), extra_key.size()}, extra, encoded, sizeof(encoded), extra_size)) return 113;
    MutationView extra_mutation{4, {extra_key.data(), extra_key.size()}, {encoded, extra_size}, false};
    const auto before = files;
    const uint8_t malformed_value = 0xc0;
    auto malformed = extra_mutation;
    malformed.value = {&malformed_value, 1};
    SdIndexedCommit bad_row(volume);
    if (bad_row.begin(current, copy, &malformed, 1, frame, sizeof(frame), roots[1 - copy]) || files != before) return 140;
    SdIndexedCommit too_small(volume);
    if (too_small.begin(current, copy, &extra_mutation, 1, frame, 24, roots[1 - copy]) || files != before) return 114;
    IndexRootView stale;
    if (!decodeIndexRoot({roots[1 - copy].data(), roots[1 - copy].size()}, volume, stale)) return 115;
    SdIndexedCommit conflict(volume);
    if (!conflict.begin(stale, 1 - copy, &extra_mutation, 1, frame, sizeof(frame), roots[copy])) return 116;
    auto conflict_result = IndexedCommitStep::Working;
    for (unsigned step = 0; step < 128 && conflict_result == IndexedCommitStep::Working; ++step) conflict_result = conflict.step();
    if (conflict_result != IndexedCommitStep::RecoveryRequired || files != before) return 117;
    if (!encodeDraft({extra_key.data(), extra_key.size()}, extra, frame, sizeof(frame), extra_size)) return 119;
    extra_mutation.value = {frame, extra_size};
    SdIndexedCommit aliased(volume);
    const auto alias_staged_reads = read_bytes["/trailmate/geocaching/.state/journal.pending"];
    if (!aliased.begin(current, copy, &extra_mutation, 1, frame, sizeof(frame), roots[1 - copy])) return 120;
    auto aliased_result = IndexedCommitStep::Working;
    bool released_alias = false;
    for (unsigned step = 0; step < 512 && aliased_result == IndexedCommitStep::Working; ++step)
    {
        aliased_result = aliased.step();
        if (aliased.inputConsumed() && !released_alias)
        {
            std::memset(frame, 0xa5, sizeof(frame));
            released_alias = true;
        }
    }
    const std::string alias_path = "/trailmate/geocaching/.state/journal/0000000000000029.gcj";
    if (aliased_result != IndexedCommitStep::Verified || !released_alias || read_bytes[alias_path] != files[alias_path].size() ||
        read_bytes["/trailmate/geocaching/.state/journal.pending"] - alias_staged_reads != files[alias_path].size()) return 121;
    if (!aliased.committed(current)) return 122;
    copy = 1 - copy;
    extra_key[0] = 42;
    if (!encodeDraft({extra_key.data(), extra_key.size()}, extra, encoded, sizeof(encoded), extra_size)) return 123;
    extra_mutation.value = {encoded, extra_size};
    const auto old_root0 = files["/trailmate/geocaching/.state/index/root.h0"];
    const auto old_root1 = files["/trailmate/geocaching/.state/index/root.h1"];
    SdIndexedCommit interrupted(volume);
    if (!interrupted.begin(current, copy, &extra_mutation, 1, frame, sizeof(frame), roots[1 - copy])) return 124;
    auto interrupted_result = IndexedCommitStep::Working;
    for (unsigned step = 0; step < 512 && interrupted_result == IndexedCommitStep::Working; ++step)
    {
        interrupted_result = interrupted.step();
        if (interrupted.inputConsumed()) flush_ok = false;
    }
    flush_ok = true;
    IndexRootView unpublished;
    if (interrupted_result != IndexedCommitStep::RecoveryRequired || interrupted.committed(unpublished) ||
        files["/trailmate/geocaching/.state/index/root.h0"] != old_root0 || files["/trailmate/geocaching/.state/index/root.h1"] != old_root1) return 125;
    // Discard transient input and reconstruct the already durable transaction.
    std::memset(frame, 0, sizeof(frame));
    SdIndexedCommit rejected(volume);
    if (!rejected.resume(current, copy, frame, sizeof(frame), roots[1 - copy])) return 126;
    auto rejected_state = IndexedCommitStep::Working;
    for (unsigned step = 0; step < 64 && rejected_state == IndexedCommitStep::Working; ++step) rejected_state = rejected.step();
    if (rejected_state != IndexedCommitStep::NeedsValidation || rejected.step() != IndexedCommitStep::NeedsValidation ||
        !rejected.validateRecovered(false) || rejected.step() != IndexedCommitStep::Invalid || rejected.committed(unpublished) ||
        files["/trailmate/geocaching/.state/index/root.h0"] != old_root0 || files["/trailmate/geocaching/.state/index/root.h1"] != old_root1) return 127;
    for (unsigned after_validation = 0; after_validation < 2; ++after_validation)
    {
        SdIndexedCommit changed(volume);
        if (!changed.resume(current, copy, frame, sizeof(frame), roots[1 - copy])) return 134;
        auto status = IndexedCommitStep::Working;
        for (unsigned step = 0; step < 64 && status == IndexedCommitStep::Working; ++step) status = changed.step();
        RecordFrameView original;
        MutationView mutation;
        TransactionView transaction;
        const auto saved_frame = changed.recoveredFrame();
        if (status != IndexedCommitStep::NeedsValidation || !decodeRecordFrame(saved_frame, original) ||
            !decodeTransaction(original.payload, current.sequence, &mutation, 1, transaction)) return 135;
        if (after_validation && !changed.validateRecovered(true)) return 136;
        frame[mutation.key.data - frame] ^= 1;
        RecordHeader replacement;
        if (!makeRecordHeader(RecordKind::Transaction, original.sequence, original.payload, replacement)) return 137;
        std::memcpy(frame, replacement.data(), replacement.size());
        if (!after_validation && !changed.validateRecovered(true)) return 138;
        if (changed.step() != IndexedCommitStep::Invalid || changed.committed(unpublished) ||
            files["/trailmate/geocaching/.state/index/root.h0"] != old_root0 || files["/trailmate/geocaching/.state/index/root.h1"] != old_root1) return 139;
    }
    SdIndexedCommit recovery(volume);
    if (!recovery.resume(current, copy, frame, sizeof(frame), roots[1 - copy])) return 128;
    auto restored = IndexedCommitStep::Working;
    for (unsigned step = 0; step < 64 && restored == IndexedCommitStep::Working; ++step)
    {
        step_bytes = 0;
        restored = recovery.step();
        if (step_bytes > 512) return 132;
    }
    RecordFrameView recovered;
    TransactionView restored_transaction;
    MutationView restored_mutation;
    DraftView validated_draft;
    if (restored != IndexedCommitStep::NeedsValidation || !decodeRecordFrame(recovery.recoveredFrame(), recovered) ||
        !decodeTransaction(recovered.payload, current.sequence, &restored_mutation, 1, restored_transaction) || restored_mutation.table != 4 ||
        !decodeDraft(restored_mutation.key, restored_mutation.value, validated_draft) || validated_draft.name != "Extra" ||
        !recovery.validateRecovered(true)) return 133;
    restored = IndexedCommitStep::Working;
    for (unsigned step = 0; step < 512 && restored == IndexedCommitStep::Working; ++step) restored = recovery.step();
    if (restored != IndexedCommitStep::Verified || !recovery.committed(current) || current.sequence != 42) return 129;
    SdIndexGet recovered_value(volume);
    if (!recovered_value.begin(current, 4, {extra_key.data(), extra_key.size()}, frame, sizeof(frame))) return 130;
    auto fetched = IndexGetStep::Working;
    for (unsigned step = 0; step < 128 && fetched == IndexGetStep::Working; ++step) fetched = recovered_value.step();
    DraftView recovered_draft;
    if (fetched != IndexGetStep::Ready || !decodeDraft({extra_key.data(), extra_key.size()}, recovered_value.value(), recovered_draft) || recovered_draft.name != "Extra") return 131;
    copy = 1 - copy;
    const auto current_root0 = files["/trailmate/geocaching/.state/index/root.h0"];
    const auto current_root1 = files["/trailmate/geocaching/.state/index/root.h1"];
    uint8_t invalid_payload[128];
    size_t invalid_size = 0;
    extra_key[0] = 43;
    malformed.key = {extra_key.data(), extra_key.size()};
    RecordHeader invalid_header;
    if (!encodeTransaction(current.sequence, &malformed, 1, invalid_payload, sizeof(invalid_payload), invalid_size) ||
        !makeRecordHeader(RecordKind::Transaction, 43, {invalid_payload, invalid_size}, invalid_header)) return 141;
    auto& invalid_file = files["/trailmate/geocaching/.state/journal/000000000000002b.gcj"];
    invalid_file.assign(invalid_header.begin(), invalid_header.end());
    invalid_file.insert(invalid_file.end(), invalid_payload, invalid_payload + invalid_size);
    SdIndexedCommit bad_recovery(volume);
    if (!bad_recovery.resume(current, copy, frame, sizeof(frame), roots[1 - copy])) return 142;
    auto bad_result = IndexedCommitStep::Working;
    for (unsigned step = 0; step < 64 && bad_result == IndexedCommitStep::Working; ++step) bad_result = bad_recovery.step();
    if (bad_result != IndexedCommitStep::RecoveryRequired || bad_recovery.recoveredFrame().data ||
        files["/trailmate/geocaching/.state/index/root.h0"] != current_root0 || files["/trailmate/geocaching/.state/index/root.h1"] != current_root1) return 143;
    auto scan = std::make_unique<SdIndexScan>(volume);
    if (!scan->begin(current, 4, frame, sizeof(frame))) return 148;
    std::array<bool, 43> seen{};
    unsigned count = 0;
    auto scanned = IndexScanStep::Working;
    for (unsigned step = 0; step < 8192; ++step)
    {
        step_bytes = 0;
        scanned = scan->step();
        if (step_bytes > 512) return 149;
        if (scanned == IndexScanStep::Item)
        {
            MutationView row;
            DraftView draft;
            if (!scan->item(row) || row.key.size != 16 || !row.key.data[0] || row.key.data[0] > 42 || seen[row.key.data[0]] ||
                !decodeDraft(row.key, row.value, draft) || scan->step() != IndexScanStep::Item) return 150;
            seen[row.key.data[0]] = true;
            ++count;
            if (!scan->advance()) return 151;
        }
        else if (scanned != IndexScanStep::Working) break;
    }
    if (scanned != IndexScanStep::End || count != 42) return 152;
    if (!index_directories.count("/trailmate/geocaching/.state/index") ||
        !index_directories.count("/trailmate/geocaching/.state/index/a") ||
        !index_directories.count("/trailmate/geocaching/.state/index/a/04")) return 168;
    scan.reset();
    files.erase("/trailmate/geocaching/.state/journal/000000000000002b.gcj");
    uint8_t request[64], values[512], transaction_bytes[512];
    size_t request_size = 0, transaction_size = 0;
    ::geocaching::RequestId request_id;
    if (!::geocaching::protocol::encodeCapabilitiesRequest(request_id, request, sizeof(request), request_size)) return 153;
    QueuedRequestWorkspace queued(values, sizeof(values));
    if (!encodeNewRequestTask(current.sequence, {}, {}, request_id, {}, 3, {request, request_size}, {}, queued,
                              transaction_bytes, sizeof(transaction_bytes), transaction_size)) return 154;
    MutationView linked[2];
    TransactionView linked_transaction;
    if (!decodeTransaction({transaction_bytes, transaction_size}, current.sequence, linked, 2, linked_transaction)) return 155;
    for (unsigned round = 0; round < 2; ++round)
    {
        MutationView remove_task{10, linked[1].key, {}, true};
        // Validate the candidate before writing any journal bytes: creation
        // resolves both rows from the overlay; parent-only deletion is rejected.
        auto preflight = std::make_unique<SdIndexReferences>(volume);
        if (!preflight->begin(current, frame, sizeof(frame), round ? &remove_task : linked, round ? 1 : 2)) return 162;
        auto checked = IndexScanStep::Working;
        const auto unchanged_files = files;
        for (unsigned step = 0; step < 8192 && checked == IndexScanStep::Working; ++step)
        {
            step_bytes = 0;
            checked = preflight->step();
            if (step_bytes > 512) return 163;
        }
        if (checked != (round ? IndexScanStep::Invalid : IndexScanStep::End) || files != unchanged_files) return 164;
        if (round)
        {
            MutationView remove_both[] = {remove_task, {5, linked[0].key, {}, true}};
            preflight = std::make_unique<SdIndexReferences>(volume);
            if (!preflight->begin(current, frame, sizeof(frame), remove_both, 2)) return 165;
            checked = IndexScanStep::Working;
            for (unsigned step = 0; step < 8192 && checked == IndexScanStep::Working; ++step) checked = preflight->step();
            if (checked != IndexScanStep::End || files != unchanged_files) return 166;
        }
        SdIndexedCommit commit(volume);
        if (!commit.begin(current, copy, round ? &remove_task : linked, round ? 1 : 2, frame, sizeof(frame), roots[1 - copy], false, round ? 0 : 2)) return 156;
        auto committed = IndexedCommitStep::Working;
        for (unsigned step = 0; step < 8192 && committed == IndexedCommitStep::Working; ++step)
        {
            step_bytes = 0;
            committed = commit.step();
            if (step_bytes > 512) return 157;
        }
        if (round)
        {
            if (committed != IndexedCommitStep::Invalid || files != unchanged_files) return 167;
            continue;
        }
        if (committed != IndexedCommitStep::Verified || !commit.committed(current)) return 158;
        copy = 1 - copy;
        const auto existing_task = files;
        SdIndexedCommit duplicate_task(volume);
        if (!duplicate_task.begin(current, copy, linked, 2, frame, sizeof(frame), roots[1 - copy], false, 2)) return 196;
        auto duplicate_status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && duplicate_status == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            duplicate_status = duplicate_task.step();
            if (step_bytes > 512) return 197;
        }
        if (duplicate_status != IndexedCommitStep::Invalid || files != existing_task) return 198;
        auto validation = std::make_unique<SdIndexReferences>(volume);
        if (!validation->begin(current, frame, sizeof(frame))) return 159;
        auto validated = IndexScanStep::Working;
        for (unsigned step = 0; step < 8192 && validated == IndexScanStep::Working; ++step)
        {
            step_bytes = 0;
            validated = validation->step();
            if (step_bytes > 512) return 160;
        }
        if (validated != (round ? IndexScanStep::Invalid : IndexScanStep::End)) return 161;
    }
    std::array<uint8_t, 36> issuance_key{};
    issuance_key.back() = 1;
    std::array<uint8_t, 64> author{};
    uint8_t issuance[160];
    size_t issuance_size = 0;
    RevisionHash issued_hash;
    if (!encodeAuthorIssued(issued_hash, {author.data(), author.size()}, {}, issuance, sizeof(issuance), issuance_size)) return 173;
    for (unsigned round = 0; round < 4; ++round)
    {
        // Creation and byte-identical retry succeed; rewriting or deleting an
        // issued revision must fail before touching journal or index files.
        if (round == 2) issuance[3] ^= 1;
        MutationView mutation{3, {issuance_key.data(), issuance_key.size()}, round == 3 ? ByteView{} : ByteView{issuance, issuance_size}, round == 3};
        const auto before = files;
        SdIndexedCommit validation(volume);
        if (!validation.begin(current, copy, &mutation, 1, frame, sizeof(frame), roots[1 - copy], true)) return 186;
        auto validation_result = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && validation_result == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            validation_result = validation.step();
            if (step_bytes > 512) return 187;
        }
        IndexRootView unpublished;
        if (validation_result != (round < 2 ? IndexedCommitStep::Validated : IndexedCommitStep::Invalid) ||
            files != before || validation.committed(unpublished)) return 188;
        SdIndexedCommit commit(volume);
        if (!commit.begin(current, copy, &mutation, 1, frame, sizeof(frame), roots[1 - copy])) return 174;
        auto result = IndexedCommitStep::Working;
        for (unsigned step = 0; step < 8192 && result == IndexedCommitStep::Working; ++step)
        {
            step_bytes = 0;
            result = commit.step();
            if (step_bytes > 512) return 175;
        }
        if (round < 2)
        {
            if (result != IndexedCommitStep::Verified || !commit.committed(current)) return 176;
            copy = 1 - copy;
        }
        else if (result != IndexedCommitStep::Invalid || files != before) return 177;
    }
    // Simulate loss of power after a valid journal is durable but before its
    // index publication. Recovery must await business acceptance and then
    // advance the root only after the entire index transaction is verified.
    std::array<uint8_t, 16> resumed_key{};
    resumed_key[0] = 90;
    DraftView resumed_draft;
    resumed_draft.generation = 1;
    resumed_draft.name = "Recovered draft";
    size_t resumed_size = 0;
    if (!encodeDraft({resumed_key.data(), resumed_key.size()}, resumed_draft, encoded, sizeof(encoded), resumed_size)) return 178;
    MutationView resume_row{4, {resumed_key.data(), resumed_key.size()}, {encoded, resumed_size}, false};
    uint8_t original_issuance[160];
    std::memcpy(original_issuance, issuance, issuance_size);
    original_issuance[3] ^= 1;
    MutationView resume_rows[] = {resume_row, {3, {issuance_key.data(), issuance_key.size()}, {original_issuance, issuance_size}, false}, linked[0]};
    SdGeocachingJournal journal(volume);
    if (journal.begin(current.sequence, resume_rows, 3) != JournalWriteResult::InProgress) return 179;
    auto journal_status = JournalWriteResult::InProgress;
    for (unsigned i = 0; i < 256 && journal_status == JournalWriteResult::InProgress; ++i) journal_status = journal.step();
    if (journal_status != JournalWriteResult::Verified) return 180;
    const auto resumed_sequence = current.sequence + 1;
    MutationView pending_rows[3];
    uint8_t validation_frame[512];
    const auto before_replay = files;
    for (unsigned allocation = 0; allocation < 2; ++allocation)
    {
        auto replay = std::make_unique<SdIndexReplay>(volume, current, copy, roots[0], roots[1],
                                                      JournalSegmentRange{true, resumed_sequence, resumed_sequence},
                                                      frame, sizeof(frame), pending_rows, 3);
        auto status = IndexReplayStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexReplayStep::Working; ++i) status = replay->step();
        if (status != IndexReplayStep::NeedsValidation) return 453;
        nothrow_allocations_before_failure = allocation;
        fail_nothrow_once = true;
        if (!replay->accept(true, validation_frame, sizeof(validation_frame)) || fail_nothrow_once ||
            replay->step() != IndexReplayStep::OutOfMemory || files != before_replay) return 454;
        if (replay->step() != IndexReplayStep::OutOfMemory) return 455;
    }
    // A read failure during business validation must not trigger index repair.
    // Restarting recovery after the media becomes readable retains the journal.
    {
        auto replay = std::make_unique<SdIndexReplay>(volume, current, copy, roots[0], roots[1],
                                                      JournalSegmentRange{true, resumed_sequence, resumed_sequence},
                                                      frame, sizeof(frame), pending_rows, 3);
        auto status = IndexReplayStep::Working;
        bool injected = false;
        for (unsigned i = 0; i < 8192; ++i)
        {
            status = replay->step();
            if (status == IndexReplayStep::NeedsValidation)
            {
                if (injected || !replay->accept(true, validation_frame, sizeof(validation_frame))) return 450;
                injected = fail_read_once = true;
            }
            else if (status != IndexReplayStep::Working) break;
        }
        if (!injected || fail_read_once || status != IndexReplayStep::IoError || files != before_replay) return 451;
        if (replay->step() != IndexReplayStep::IoError) return 452;
    }
    for (unsigned accepted = 0; accepted < 2; ++accepted)
    {
        auto replay = std::make_unique<SdIndexReplay>(volume, current, copy, roots[0], roots[1],
                                                      JournalSegmentRange{true, resumed_sequence, resumed_sequence},
                                                      frame, sizeof(frame), pending_rows, 3);
        auto status = IndexReplayStep::Working;
        bool asked = false;
        for (unsigned i = 0; i < 8192; ++i)
        {
            step_bytes = 0;
            status = replay->step();
            if (step_bytes > 512) return 181;
            if (status == IndexReplayStep::NeedsValidation)
            {
                TransactionView pending;
                if (asked || files != before_replay || !replay->pending(pending) || pending.count != 3 ||
                    !replay->accept(accepted != 0, validation_frame, sizeof(validation_frame))) return 182;
                asked = true;
            }
            else if (status != IndexReplayStep::Working) break;
        }
        if (!asked || status != (accepted ? IndexReplayStep::Complete : IndexReplayStep::Invalid)) return 183;
        if (accepted && (!replay->selected(current, copy) || current.sequence != resumed_sequence)) return 184;
        if (!accepted && files != before_replay) return 185;
    }
    // Recovery cannot bypass immutable issuance checks, even when its caller
    // accepts the transaction's operation-specific business rules.
    MutationView rewrite{3, {issuance_key.data(), issuance_key.size()}, {issuance, issuance_size}, false};
    SdGeocachingJournal bad_journal(volume);
    if (bad_journal.begin(current.sequence, &rewrite, 1) != JournalWriteResult::InProgress) return 189;
    journal_status = JournalWriteResult::InProgress;
    for (unsigned i = 0; i < 256 && journal_status == JournalWriteResult::InProgress; ++i) journal_status = bad_journal.step();
    if (journal_status != JournalWriteResult::Verified) return 190;
    const auto before_bad_replay = files;
    auto bad_replay = std::make_unique<SdIndexReplay>(volume, current, copy, roots[0], roots[1],
                                                      JournalSegmentRange{true, current.sequence + 1, current.sequence + 1},
                                                      frame, sizeof(frame), pending_rows, 3);
    auto bad_status = IndexReplayStep::Working;
    for (unsigned i = 0; i < 8192; ++i)
    {
        bad_status = bad_replay->step();
        if (bad_status == IndexReplayStep::NeedsValidation)
        {
            if (!bad_replay->accept(true, validation_frame, sizeof(validation_frame))) return 191;
        }
        else if (bad_status != IndexReplayStep::Working) break;
    }
    if (bad_status != IndexReplayStep::Invalid || files != before_bad_replay) return 192;
    files.clear();
    return 0;
}

int checkIndexedDraftPublication()
{
    using namespace ::geocaching;
    using namespace ::geocaching::storage;
    files.clear();
    index_directories.clear();
    VolumeInstance volume{};
    const auto format = encodeVolumeHeader(volume);
    files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
    IndexRootBytes roots[2];
    SdIndexInitialize initialize(volume);
    if (!initialize.begin(roots[0])) return 201;
    auto initialized = IndexRootWriteStep::Working;
    for (unsigned i = 0; i < 128 && initialized == IndexRootWriteStep::Working; ++i) initialized = initialize.step();
    if (initialized != IndexRootWriteStep::Verified) return 202;
    roots[1] = roots[0];
    IndexRootView current;
    if (!decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, current)) return 203;
    unsigned copy = 0;
    uint8_t frame[1024], encoded[256], record_bytes[256], request_bytes[512], outgoing_bytes[768];
    std::array<uint8_t, 16> draft_id{}, task_id{};
    draft_id.fill(3);
    task_id.fill(4);
    std::array<uint8_t, 64> author{}, signature{};
    std::array<uint8_t, 32> hash{}, cache_id{};
    hash.fill(5);
    DraftView draft;
    draft.author = {author.data(), author.size()};
    draft.base_hash = {hash.data(), hash.size()};
    draft.name = "Frozen draft";
    size_t size = 0;
    if (!encodeDraft({draft_id.data(), draft_id.size()}, draft, encoded, sizeof(encoded), size)) return 204;
    auto commit = [&](const MutationView* rows, size_t count)
    {
        auto operation = std::make_unique<SdIndexedCommit>(volume);
        if (!operation->begin(current, copy, rows, count, frame, sizeof(frame), roots[1 - copy])) return false;
        auto result = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && result == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            result = operation->step();
            if (step_bytes > 512) return false;
        }
        if (result != IndexedCommitStep::Verified || !operation->committed(current)) return false;
        copy = 1 - copy;
        return true;
    };
    MutationView frozen{4, {draft_id.data(), draft_id.size()}, {encoded, size}, false};
    if (!commit(&frozen, 1)) return 205;
    auto save_draft = [&](uint64_t expected)
    {
        if (!encodeDraft({draft_id.data(), draft_id.size()}, draft, encoded, sizeof(encoded), size)) return IndexedCommitStep::Invalid;
        auto operation = std::make_unique<SdIndexedDraftSave>(volume);
        if (!operation->begin(current, copy, {draft_id.data(), draft_id.size()}, {encoded, size}, expected,
                              frame, sizeof(frame), roots[1 - copy])) return IndexedCommitStep::Invalid;
        auto result = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && result == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            result = operation->step();
            if (step_bytes > 512) return IndexedCommitStep::IoError;
        }
        if (result == IndexedCommitStep::Verified)
        {
            if (!operation->committed(current)) return IndexedCommitStep::Invalid;
            copy = 1 - copy;
        }
        return result;
    };
    draft.generation = 2;
    draft.name = "Next edit";
    const auto frozen_files = files;
    if (save_draft(1) != IndexedCommitStep::Invalid || files != frozen_files) return 206;
    RecordView record;
    record.author_public_key = {author.data(), author.size()};
    record.creation_nonce = {draft_id.data(), draft_id.size()};
    record.revision = 1;
    record.name = "Frozen draft";
    record.difficulty_x2 = record.terrain_x2 = 2;
    size_t record_size = 0, request_size = 0;
    RequestId request;
    if (!protocol::encodeGeocacheRecord(record, record_bytes, sizeof(record_bytes), record_size) ||
        !protocol::encodePublishRequest(request, {record_bytes, record_size}, {signature.data(), signature.size()},
                                        512, request_bytes, sizeof(request_bytes), request_size)) return 207;
    QueuedRequestWorkspace workspace(outgoing_bytes, sizeof(outgoing_bytes));
    auto publish = std::make_unique<SdIndexedNewTask>(volume);
    if (!publish->begin(current, copy, {}, {}, request, task_id, 1, {request_bytes, request_size}, {},
                        {{cache_id.data(), cache_id.size()}, {hash.data(), hash.size()}, 0},
                        workspace, frame, sizeof(frame), roots[1 - copy])) return 208;
    auto publish_status = IndexedCommitStep::Working;
    for (unsigned i = 0; i < 8192 && publish_status == IndexedCommitStep::Working; ++i)
    {
        step_bytes = 0;
        publish_status = publish->step();
        if (step_bytes > 512) return 212;
    }
    if (publish_status != IndexedCommitStep::Verified || !publish->committed(current)) return 213;
    copy = 1 - copy;
    if (save_draft(1) != IndexedCommitStep::Verified) return 209;
    const auto saved_files = files;
    draft.generation = 3;
    author[0] ^= 1;
    if (save_draft(2) != IndexedCommitStep::Invalid || files != saved_files) return 210;
    author[0] ^= 1;
    hash[0] ^= 1;
    if (save_draft(2) != IndexedCommitStep::Invalid || files != saved_files) return 211;
    hash[0] ^= 1;
    GeocacheId download_id;
    RevisionHash download_hash;
    download_id.bytes = cache_id;
    download_hash.bytes = hash;
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        request.bytes.fill(static_cast<uint8_t>(8 + attempt));
        task_id.fill(static_cast<uint8_t>(12 + attempt));
        if (!protocol::encodeGetRequest(request, download_id, &download_hash, nullptr, 8192,
                                        request_bytes, sizeof(request_bytes), request_size)) return 214;
        const uint64_t generation = attempt == 2 ? 2 : 1;
        auto download = std::make_unique<SdIndexedNewTask>(volume);
        if (!download->begin(current, copy, {}, {}, request, task_id, 2, {request_bytes, request_size}, {},
                             {{cache_id.data(), cache_id.size()}, {hash.data(), hash.size()}, generation},
                             workspace, frame, sizeof(frame), roots[1 - copy])) return 215;
        const auto before_download = files;
        auto status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            status = download->step();
            if (step_bytes > 512) return 216;
        }
        if (attempt == 1)
        {
            if (status != IndexedCommitStep::Invalid || files != before_download) return 217;
            continue;
        }
        if (status != IndexedCommitStep::Verified || !download->committed(current)) return 218;
        copy = 1 - copy;
        SdIndexGet head(volume);
        if (!head.begin(current, 2, {cache_id.data(), cache_id.size()}, frame, sizeof(frame))) return 219;
        auto read = IndexGetStep::Working;
        for (unsigned i = 0; i < 512 && read == IndexGetStep::Working; ++i) read = head.step();
        CacheHeadView decoded;
        if (read != IndexGetStep::Ready || !decodeCacheHead({cache_id.data(), cache_id.size()}, head.value(), decoded) ||
            decoded.install_generation != generation) return 220;
    }
    std::array<uint8_t, 48> stopped_request{};
    std::copy(request.bytes.begin(), request.bytes.end(), stopped_request.begin() + 32);
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        auto context_key = stopped_request;
        if (scenario == 1) std::fill(context_key.begin() + 32, context_key.end(), 8);
        auto context = std::make_unique<SdIndexedDownloadContext>(volume);
        if (!context->begin(current, {context_key.data(), context_key.size()}, scenario == 0 ? 2 : scenario == 1 ? 1
                                                                                                                 : 99,
                            frame, sizeof(frame))) return 133;
        auto status = IndexGetStep::Working;
        for (unsigned i = 0; i < 4096 && status == IndexGetStep::Working; ++i)
        {
            step_bytes = 0;
            status = context->step();
            if (step_bytes > 512) return 134;
        }
        OutgoingView outgoing;
        TaskView task;
        CacheHeadView head;
        if (!scenario)
        {
            if (status != IndexGetStep::Ready || !context->intentActive() || !context->view(outgoing, task, head) ||
                head.install_generation != 2 || outgoing.request.size != request_size ||
                std::memcmp(outgoing.request.data, request_bytes, request_size)) return 135;
        }
        else if (status != IndexGetStep::Invalid || context->view(outgoing, task, head) || context->intentActive()) return 136;
    }
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
        const auto before_stop = files;
        const auto sequence = current.sequence;
        auto stop = std::make_unique<SdIndexedStopTask>(volume);
        const ByteView stop_key = repeat ? ByteView{task_id.data(), task_id.size()} : ByteView{stopped_request.data(), stopped_request.size()};
        if (!stop->begin(current, copy, stop_key, !repeat, frame, sizeof(frame), roots[1 - copy])) return 78;
        auto status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            status = stop->step();
            if (step_bytes > 512) return 79;
        }
        if (status != IndexedCommitStep::Verified || !stop->committed(current)) return 80;
        if (repeat)
        {
            if (current.sequence != sequence || files != before_stop) return 81;
        }
        else
        {
            if (current.sequence != sequence + 1) return 82;
            copy = 1 - copy;
        }
    }
    SdIndexGet stopped_task(volume);
    if (!stopped_task.begin(current, 10, {task_id.data(), task_id.size()}, frame, sizeof(frame))) return 83;
    auto stopped_status = IndexGetStep::Working;
    for (unsigned i = 0; i < 512 && stopped_status == IndexGetStep::Working; ++i) stopped_status = stopped_task.step();
    TaskView stopped;
    if (stopped_status != IndexGetStep::Ready || !decodeTask({task_id.data(), task_id.size()}, stopped_task.value(), stopped) ||
        stopped.state != 5 || stopped.continue_intent) return 84;
    auto stopped_context = std::make_unique<SdIndexedDownloadContext>(volume);
    if (!stopped_context->begin(current, {stopped_request.data(), stopped_request.size()}, 2, frame, sizeof(frame))) return 137;
    auto context_status = IndexGetStep::Working;
    for (unsigned i = 0; i < 4096 && context_status == IndexGetStep::Working; ++i) context_status = stopped_context->step();
    if (context_status != IndexGetStep::Ready || stopped_context->intentActive()) return 138;
    stopped_context.reset();
    std::array<uint8_t, 16> attempt_id{};
    attempt_id.back() = 1;
    for (uint8_t blocked_request : {uint8_t(8), uint8_t(10)})
    {
        std::array<uint8_t, 48> blocked_key{};
        std::fill(blocked_key.begin() + 32, blocked_key.end(), blocked_request);
        const auto before = files;
        auto send = std::make_unique<SdIndexedBeginAttempt>(volume);
        if (!send->begin(current, copy, {}, {blocked_key.data(), blocked_key.size()}, attempt_id, {},
                         workspace, frame, sizeof(frame), roots[1 - copy])) return 97;
        auto status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedCommitStep::Working; ++i) status = send->step();
        if (status != IndexedCommitStep::Invalid || files != before) return 98;
    }
    request.bytes.fill(30);
    task_id.fill(31);
    if (!protocol::encodeQueryRequest(request, {-900000000, -1800000000, 900000000, 1800000000},
                                      7, {}, 4, {}, 512, request_bytes, sizeof(request_bytes), request_size)) return 221;
    auto query = std::make_unique<SdIndexedNewTask>(volume);
    if (!query->begin(current, copy, {}, {}, request, task_id, 3, {request_bytes, request_size}, {}, {},
                      workspace, frame, sizeof(frame), roots[1 - copy])) return 222;
    auto query_status = IndexedCommitStep::Working;
    for (unsigned i = 0; i < 8192 && query_status == IndexedCommitStep::Working; ++i) query_status = query->step();
    if (query_status != IndexedCommitStep::Verified || !query->committed(current)) return 223;
    copy = 1 - copy;
    std::array<uint8_t, 48> reply_key{};
    std::copy(request.bytes.begin(), request.bytes.end(), reply_key.begin() + 32);
    std::array<uint8_t, 64> attempt_key{};
    std::copy(reply_key.begin(), reply_key.end(), attempt_key.begin());
    std::copy(attempt_id.begin(), attempt_id.end(), attempt_key.begin() + 48);
    std::array<uint8_t, 48> publication_key{};
    for (unsigned selection = 0; selection < 4; ++selection)
    {
        Destination local;
        if (selection == 3) local.bytes.fill(99);
        const ByteView after = selection == 0 ? ByteView{} : selection == 2 ? ByteView{reply_key.data(), reply_key.size()}
                                                                            : ByteView{publication_key.data(), publication_key.size()};
        auto pending = std::make_unique<SdIndexedPendingRequest>(volume);
        if (!pending->begin(current, local, after, frame, sizeof(frame))) return 103;
        auto status = IndexedPendingStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedPendingStep::Working; ++i)
        {
            step_bytes = 0;
            status = pending->step();
            if (step_bytes > 512) return 104;
        }
        PendingRequestView selected_request;
        if (selection < 2)
        {
            if (status != IndexedPendingStep::Ready || !pending->selected(selected_request) ||
                selected_request.key != (selection ? reply_key : publication_key)) return 105;
            if (selection && (selected_request.request.size != request_size ||
                              std::memcmp(selected_request.request.data, request_bytes, request_size))) return 106;
        }
        else if (status != IndexedPendingStep::None || pending->selected(selected_request)) return 107;
    }
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
        const auto before = files;
        auto send = std::make_unique<SdIndexedBeginAttempt>(volume);
        if (!send->begin(current, copy, {}, {reply_key.data(), reply_key.size()}, attempt_id, {},
                         workspace, frame, sizeof(frame), roots[1 - copy])) return 99;
        auto status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            status = send->step();
            if (step_bytes > 512) return 100;
        }
        if (repeat)
        {
            if (status != IndexedCommitStep::Invalid || files != before) return 101;
        }
        else
        {
            if (status != IndexedCommitStep::Verified || !send->committed(current)) return 102;
            copy = 1 - copy;
        }
    }
    auto unresolved = std::make_unique<SdIndexedPendingRequest>(volume);
    if (!unresolved->begin(current, {}, {publication_key.data(), publication_key.size()}, frame, sizeof(frame))) return 108;
    auto unresolved_status = IndexedPendingStep::Working;
    for (unsigned i = 0; i < 8192 && unresolved_status == IndexedPendingStep::Working; ++i) unresolved_status = unresolved->step();
    if (unresolved_status != IndexedPendingStep::None) return 109;
    unresolved.reset();
    uint8_t response[128];
    protocol::CmpWriter writer(response, sizeof(response));
    if (!writer.array(6) || !writer.unsignedInteger(1) || !writer.unsignedInteger(1) || !writer.unsignedInteger(2) ||
        !writer.binary({request.bytes.data(), request.bytes.size()}) || !writer.unsignedInteger(200) ||
        !writer.array(4) || !writer.binary({draft_id.data(), draft_id.size()}) || !writer.array(0) ||
        !writer.nil() || !writer.unsignedInteger(60)) return 224;
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        if (attempt == 2) response[writer.size() - 1] = 61;
        const auto before_reply = files;
        const auto sequence = current.sequence;
        auto reply = std::make_unique<SdIndexedDirectoryReply>(volume);
        if (!reply->begin(current, copy, {reply_key.data(), reply_key.size()}, 2, {response, writer.size()},
                          workspace, frame, sizeof(frame), roots[1 - copy])) return 225;
        auto status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            status = reply->step();
            if (step_bytes > 512) return 226;
        }
        if (attempt == 2)
        {
            if (status != IndexedCommitStep::Invalid || files != before_reply) return 227;
        }
        else
        {
            if (status != IndexedCommitStep::Verified || !reply->committed(current)) return 228;
            if (attempt == 0)
            {
                if (current.sequence != sequence + 1) return 229;
                copy = 1 - copy;
            }
            else if (current.sequence != sequence || files != before_reply) return 230;
        }
    }
    SdIndexGet task_read(volume);
    if (!task_read.begin(current, 10, {task_id.data(), task_id.size()}, frame, sizeof(frame))) return 231;
    auto task_status = IndexGetStep::Working;
    for (unsigned i = 0; i < 512 && task_status == IndexGetStep::Working; ++i) task_status = task_read.step();
    TaskView completed_task;
    if (task_status != IndexGetStep::Ready || !decodeTask({task_id.data(), task_id.size()}, task_read.value(), completed_task) ||
        completed_task.state != 3) return 232;
    std::array<uint8_t, 32> transport_hash{};
    transport_hash.fill(0x44);
    for (unsigned event = 0; event < 7; ++event)
    {
        if (event == 2) transport_hash[0] ^= 1;
        const auto before_event = files;
        const auto sequence = current.sequence;
        const auto terminal = event == 3 ? TxAttemptState::CancelledBeforeSend : event == 6 ? TxAttemptState::Failed
                                                                                            : TxAttemptState::Delivered;
        auto update = std::make_unique<SdIndexedAttemptUpdate>(volume);
        if (!update->begin(current, copy, {attempt_key.data(), attempt_key.size()},
                           event < 3 ? ByteView{transport_hash.data(), transport_hash.size()} : ByteView{}, terminal, {},
                           outgoing_bytes, sizeof(outgoing_bytes), frame, sizeof(frame), roots[1 - copy])) return 89;
        auto status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            status = update->step();
            if (step_bytes > 512) return 90;
        }
        const bool rejected = event == 2 || event == 3 || event == 6;
        if (rejected)
        {
            if (status != IndexedCommitStep::Invalid || files != before_event) return 91;
        }
        else
        {
            if (status != IndexedCommitStep::Verified || !update->committed(current)) return 92;
            if (event == 0 || event == 4)
            {
                if (current.sequence != sequence + 1) return 93;
                copy = 1 - copy;
            }
            else if (current.sequence != sequence || files != before_event) return 94;
        }
    }
    SdIndexGet retained_response(volume);
    if (!retained_response.begin(current, 5, {reply_key.data(), reply_key.size()}, frame, sizeof(frame))) return 95;
    auto retained_status = IndexGetStep::Working;
    for (unsigned i = 0; i < 512 && retained_status == IndexGetStep::Working; ++i) retained_status = retained_response.step();
    OutgoingView retained;
    if (retained_status != IndexGetStep::Ready || !decodeOutgoing({reply_key.data(), reply_key.size()}, retained_response.value(), retained) ||
        retained.state != 4 || retained.terminal_data.size != writer.size() || retained.terminal_data.data[writer.size() - 1] != 60) return 96;
    const auto before_completed_stop = files;
    auto completed_stop = std::make_unique<SdIndexedStopTask>(volume);
    if (!completed_stop->begin(current, copy, {task_id.data(), task_id.size()}, false, frame, sizeof(frame), roots[1 - copy])) return 85;
    auto completed_status = IndexedCommitStep::Working;
    for (unsigned i = 0; i < 512 && completed_status == IndexedCommitStep::Working; ++i) completed_status = completed_stop->step();
    if (completed_status != IndexedCommitStep::Invalid || files != before_completed_stop) return 86;
    // Exercise the production dispatcher against the disk-indexed adapter,
    // including an asynchronous empty expiry scan and timeout-driven retry.
    SdIndexGet expected_publication(volume);
    if (!expected_publication.begin(current, 5, {publication_key.data(), publication_key.size()}, frame, sizeof(frame))) return 110;
    auto expected_status = IndexGetStep::Working;
    for (unsigned i = 0; i < 512 && expected_status == IndexGetStep::Working; ++i) expected_status = expected_publication.step();
    OutgoingView publication;
    if (expected_status != IndexGetStep::Ready || !decodeOutgoing({publication_key.data(), publication_key.size()}, expected_publication.value(), publication)) return 111;
    const std::vector<uint8_t> expected_request(publication.request.data, publication.request.data + publication.request.size);
    const auto sequence_before_dispatch = current.sequence;
    IndexWorkspaceOwner workspace_owner;
    // Match the device session: adapter objects outlive transient buffers.
    // Poison and release only after the caller consumed the whole slice.
    struct TransientWorkspace
    {
        IndexWorkspaceOwner& owner;
        QueuedRequestWorkspace& workspace;
        IndexedDispatchStore* dispatch = nullptr;
        IndexedQueryStorePort* query = nullptr;
        std::unique_ptr<uint8_t[]> frame, encoded;
        unsigned prepares = 0, releases = 0;
        bool fail_next = false;
        static bool prepare(void* context, const void*)
        {
            auto& self = *static_cast<TransientWorkspace*>(context);
            ++self.prepares;
            if (self.fail_next)
            {
                self.fail_next = false;
                return false;
            }
            if (!self.frame) self.frame.reset(new uint8_t[1024]);
            if (!self.encoded) self.encoded.reset(new uint8_t[768]);
            self.workspace.outgoing = self.encoded.get();
            if (self.dispatch) self.dispatch->bindWorkspace(self.frame.get());
            if (self.query) self.query->bindWorkspace(self.frame.get());
            return true;
        }
        void trim()
        {
            if (owner.holder() || !frame) return;
            std::memset(frame.get(), 0xa5, 1024);
            std::memset(encoded.get(), 0x5a, 768);
            frame.reset();
            encoded.reset();
            workspace.outgoing = nullptr;
            ++releases;
        }
    } transient{workspace_owner, workspace};
    auto dispatch_store = std::make_unique<IndexedDispatchStore>(volume, current, copy, roots[0], roots[1], workspace_owner, workspace, frame, sizeof(frame));
    transient.dispatch = dispatch_store.get();
    if (!workspace_owner.setPrepare(TransientWorkspace::prepare, &transient)) return 401;
    chat::MeshAdapterRouter router;
    RequestDispatcher dispatcher(router, *dispatch_store, 100, 1000);
    DispatchResult dispatched;
    transient.fail_next = true;
    if (dispatcher.dispatchOne({}).status != DispatchStatus::Deferred || workspace_owner.holder() ||
        dispatch_store->needsRecovery() || dispatch_store->busy() || transient.prepares != 1) return 402;
    for (unsigned i = 0; i < 32768; ++i)
    {
        step_bytes = 0;
        dispatched = dispatcher.dispatchOne({});
        transient.trim();
        if (step_bytes > 512 || dispatch_store->needsRecovery()) return 112;
        if (dispatched.status == DispatchStatus::Submitted) break;
    }
    if (dispatched.status != DispatchStatus::Submitted || router.sends != 1 || router.sent_bytes != expected_request ||
        current.sequence != sequence_before_dispatch + 2) return 113;
    StoredTime timeout;
    timeout.monotonic_ms = 2000;
    bool expired = false;
    for (unsigned i = 0; i < 32768; ++i)
    {
        step_bytes = 0;
        dispatcher.dispatchOne(timeout);
        transient.trim();
        if (step_bytes > 512 || dispatch_store->needsRecovery() || router.sends != 1) return 114;
        if (dispatch_store->expirationChanged() && !dispatch_store->busy())
        {
            expired = true;
            break;
        }
    }
    if (!expired || current.sequence != sequence_before_dispatch + 3) return 115;
    timeout.monotonic_ms = 2200;
    for (unsigned i = 0; i < 32768; ++i)
    {
        step_bytes = 0;
        dispatched = dispatcher.dispatchOne(timeout);
        transient.trim();
        if (step_bytes > 512 || dispatch_store->needsRecovery()) return 116;
        if (dispatched.status == DispatchStatus::Submitted) break;
    }
    if (dispatched.status != DispatchStatus::Submitted || router.sends != 2 || router.sent_bytes != expected_request ||
        current.sequence != sequence_before_dispatch + 5) return 117;
    struct ReceiptCrypto : protocol::RecordCrypto
    {
        bool sha256(ByteView input, uint8_t output[32]) override
        {
            chat::reticulum::fullHash(input.data, input.size, output);
            return true;
        }
        protocol::VerificationResult verifyEd25519(ByteView, ByteView, ByteView) override { return protocol::VerificationResult::CryptoUnavailable; }
    } receipt_crypto;
    uint8_t page_cache[2048];
    uint8_t random_counter = 60;
    // Construction validates real buffers before the first dynamic lease.
    workspace.outgoing = outgoing_bytes;
    IndexedQueryStorePort port(
        volume, current, copy, roots[0], roots[1], {}, workspace_owner, workspace, frame, sizeof(frame), page_cache, sizeof(page_cache), receipt_crypto,
        [](void* context, uint8_t out[16])
        { const auto next = ++*static_cast<uint8_t*>(context); std::memset(out, next, 16); return true; },
        [](void*)
        { return StoredTime{}; },
        &random_counter);
    transient.query = &port;
    auto persist = [&](QueryPersistence result)
    {
        for (unsigned i = 0; i < 8192 && result == QueryPersistence::Pending; ++i)
        {
            step_bytes = 0;
            result = port.pollPersistence();
            if (workspace_owner.heldBy(&port))
            {
                const auto before_wait = step_bytes;
                const auto waiting = dispatcher.dispatchOne(timeout);
                if (waiting.status != DispatchStatus::Deferred || dispatch_store->needsRecovery() || step_bytes != before_wait)
                    return QueryPersistence::Rejected;
            }
            transient.trim();
            if (step_bytes > 512) return QueryPersistence::Rejected;
        }
        return result;
    };
    RequestId first_page_id;
    std::vector<uint8_t> first_page_response;
    for (unsigned page_number = 0; page_number < 2; ++page_number)
    {
        if (!port.newRequestId(request) || !protocol::encodeQueryRequest(request, {-900000000, -1800000000, 900000000, 1800000000},
                                                                         7, {}, 4, {}, 512, request_bytes, sizeof(request_bytes), request_size)) return 118;
        DirectoryEntry directory;
        const int other_owner = 0;
        if (!workspace_owner.acquire(&other_owner)) return 131;
        const auto prepares = transient.prepares;
        const std::vector<uint8_t> prior_frame(frame, frame + sizeof(frame)), prior_encoding(outgoing_bytes, outgoing_bytes + sizeof(outgoing_bytes));
        const auto submitted = port.submit(directory, request, {request_bytes, request_size});
        step_bytes = 0;
        if (submitted != QueryPersistence::Pending || port.pollPersistence() != QueryPersistence::Pending || step_bytes ||
            transient.prepares != prepares || std::memcmp(frame, prior_frame.data(), sizeof(frame)) ||
            std::memcmp(outgoing_bytes, prior_encoding.data(), sizeof(outgoing_bytes))) return 132;
        workspace_owner.release(&other_owner);
        if (persist(submitted) != QueryPersistence::Committed || workspace_owner.holder()) return 119;
        protocol::CmpWriter page_writer(response, sizeof(response));
        if (!page_writer.array(6) || !page_writer.unsignedInteger(1) || !page_writer.unsignedInteger(1) || !page_writer.unsignedInteger(2) ||
            !page_writer.binary({request.bytes.data(), request.bytes.size()}) || !page_writer.unsignedInteger(200) ||
            !page_writer.array(4) || !page_writer.binary({draft_id.data(), draft_id.size()}) || !page_writer.array(0) ||
            !page_writer.nil() || !page_writer.unsignedInteger(60)) return 120;
        protocol::QueryPageView page;
        if (!protocol::decodeQueryPage({response, page_writer.size()}, request, 512, 4, page)) return 121;
        const auto pending = port.commitPage({}, request, {response, page_writer.size()}, page);
        step_bytes = 0;
        if (pending != QueryPersistence::Pending || port.page(page) || port.accepted({}, request, {response, page_writer.size()}) || step_bytes) return 122;
        if (persist(pending) != QueryPersistence::Committed) return 123;
        step_bytes = 0;
        if (!port.page(page) || page.count || !port.accepted({}, request, {response, page_writer.size()}) || step_bytes) return 124;
        for (unsigned i = 0; i < 8192 && port.maintenancePending(); ++i)
        {
            step_bytes = 0;
            port.maintenanceStep();
            transient.trim();
            if (step_bytes > 512) return 125;
        }
        if (port.maintenancePending()) return 126;
        if (!page_number)
        {
            first_page_id = request;
            first_page_response.assign(response, response + page_writer.size());
        }
    }
    step_bytes = 0;
    if (port.accepted({}, first_page_id, {first_page_response.data(), first_page_response.size()}) || step_bytes || !port.maintenancePending()) return 127;
    for (unsigned i = 0; i < 8192 && port.maintenancePending(); ++i)
    {
        port.maintenanceStep();
        transient.trim();
    }
    step_bytes = 0;
    if (!port.accepted({}, first_page_id, {first_page_response.data(), first_page_response.size()}) || step_bytes) return 128;
    first_page_response.back() = 61;
    if (port.accepted({}, first_page_id, {first_page_response.data(), first_page_response.size()})) return 129;
    for (unsigned i = 0; i < 8192 && port.maintenancePending(); ++i)
    {
        port.maintenanceStep();
        transient.trim();
    }
    if (port.accepted({}, first_page_id, {first_page_response.data(), first_page_response.size()})) return 130;
    protocol::QueryPageView visible_page;
    if (transient.releases < 8 || transient.frame || transient.encoded || workspace_owner.holder() || !port.page(visible_page)) return 403;
    // Losing the pinned root during a write must require session recovery;
    // rejecting just this request would allow later writes on uncertain state.
    DirectoryEntry directory;
    if (!port.newRequestId(request) || !protocol::encodeQueryRequest(request, {-900000000, -1800000000, 900000000, 1800000000}, 7, {}, 4, {}, 512, request_bytes, sizeof(request_bytes), request_size) ||
        port.submit(directory, request, {request_bytes, request_size}) != QueryPersistence::Pending ||
        port.pollPersistence() != QueryPersistence::Pending || !workspace_owner.heldBy(&port)) return 404;
    const auto unchanged = files;
    ++current.revision;
    if (port.pollPersistence() != QueryPersistence::Rejected || !port.needsRecovery() || workspace_owner.holder() ||
        port.submit(directory, request, {request_bytes, request_size}) != QueryPersistence::Rejected || files != unchanged) return 405;
    transient.trim();
    --current.revision;
    for (bool proof : {false, true})
    {
        workspace.outgoing = outgoing_bytes;
        IndexedQueryStorePort failed_port(
            volume, current, copy, roots[0], roots[1], {}, workspace_owner, workspace,
            frame, sizeof(frame), page_cache, sizeof(page_cache), receipt_crypto,
            [](void*, uint8_t out[16])
            { std::memset(out, 0x79, 16); return true; },
            [](void*)
            { return StoredTime{}; },
            nullptr);
        transient.query = &failed_port;
        if (proof)
        {
            if (failed_port.accepted({}, first_page_id, {first_page_response.data(), first_page_response.size()})) return 408;
            failed_port.maintenanceStep();
        }
        else if (failed_port.submit(directory, request, {request_bytes, request_size}) != QueryPersistence::Pending ||
                 failed_port.pollPersistence() != QueryPersistence::Pending) return 409;
        fail_read_once = true;
        auto result = QueryPersistence::Pending;
        for (unsigned i = 0; i < 8192 && !failed_port.needsRecovery() && result == QueryPersistence::Pending; ++i)
        {
            if (proof) failed_port.maintenanceStep();
            else result = failed_port.pollPersistence();
            transient.trim();
        }
        if (fail_read_once || !failed_port.needsRecovery() || workspace_owner.holder() || files != unchanged ||
            failed_port.submit(directory, request, {request_bytes, request_size}) != QueryPersistence::Rejected) return 410;
    }
    transient.query = &port;
    files.clear();
    index_directories.clear();
    return 0;
}

int checkCheckpointIndexedRead()
{
    using namespace ::geocaching;
    using namespace ::geocaching::storage;
    files.clear();
    VolumeInstance volume{};
    const auto format = encodeVolumeHeader(volume);
    files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
    std::array<uint8_t, 16> key{};
    uint8_t value[128], payload[256], frame[512];
    DraftView draft;
    draft.name = "Checkpoint cache";
    size_t value_size = 0;
    if (!encodeDraft({key.data(), key.size()}, draft, value, sizeof(value), value_size)) return 235;
    protocol::CmpWriter writer(payload, sizeof(payload));
    if (!writer.array(2) || !writer.unsignedInteger(0) || !writer.array(1) || !writer.array(3) ||
        !writer.unsignedInteger(4) || !writer.binary({key.data(), key.size()}) || !writer.binary({value, value_size})) return 236;
    RecordHeader header;
    if (!makeRecordHeader(RecordKind::CheckpointPage, 12, {payload, writer.size()}, header)) return 237;
    std::vector<uint8_t> encoded(header.begin(), header.end());
    encoded.insert(encoded.end(), payload, payload + writer.size());
    for (char slot : {'a', 'b'})
    {
        CheckpointIndexCursor cursor;
        IndexedMutation hint;
        if (!cursor.open({encoded.data(), encoded.size()}, 12, slot, 17) || !cursor.next(hint) || !cursor.complete()) return 238;
        IndexEntryBytes index;
        IndexedMutation decoded;
        if (!encodeIndexEntry(volume, hint, index) || !decodeIndexEntry({index.data(), index.size()}, volume, decoded) ||
            decoded.location.source != (slot == 'a' ? IndexedValueSource::CheckpointA : IndexedValueSource::CheckpointB)) return 239;
        const std::string path = std::string("/trailmate/geocaching/.state/checkpoint/") + slot + ".gcs";
        auto& file = files[path];
        file.assign(17, 0);
        file.insert(file.end(), encoded.begin(), encoded.end());
        for (unsigned changed = 0; changed < 2; ++changed)
        {
            if (changed)
            {
                RecordHeader replacement;
                if (!makeRecordHeader(RecordKind::CheckpointPage, 13, {payload, writer.size()}, replacement)) return 240;
                std::copy(replacement.begin(), replacement.end(), file.begin() + 17);
            }
            SdIndexedValueReader read(volume);
            if (!read.begin(decoded, frame, sizeof(frame))) return 241;
            auto status = IndexedReadStep::Working;
            for (unsigned i = 0; i < 128 && status == IndexedReadStep::Working; ++i)
            {
                step_bytes = 0;
                status = read.step();
                if (step_bytes > 512) return 242;
            }
            if (status != (changed ? IndexedReadStep::Invalid : IndexedReadStep::Ready)) return 243;
            if (!changed && (read.value().size != value_size || std::memcmp(read.value().data, value, value_size))) return 244;
            if (changed && read.value().data) return 245;
        }
    }
    files.clear();
    files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
    CheckpointCandidate selected;
    selected.state = CheckpointCandidateState::Verified;
    selected.sequence = 12;
    chat::reticulum::fullHash(encoded.data(), encoded.size(), selected.digest.data());
    uint8_t tail[64];
    protocol::CmpWriter tail_writer(tail, sizeof(tail));
    if (!tail_writer.array(3) || !tail_writer.unsignedInteger(1) || !tail_writer.unsignedInteger(1) ||
        !tail_writer.binary({selected.digest.data(), selected.digest.size()}) ||
        !makeRecordHeader(RecordKind::CheckpointTail, 12, {tail, tail_writer.size()}, header)) return 246;
    auto& checkpoint = files["/trailmate/geocaching/.state/checkpoint/a.gcs"];
    checkpoint = encoded;
    checkpoint.insert(checkpoint.end(), header.begin(), header.end());
    checkpoint.insert(checkpoint.end(), tail, tail + tail_writer.size());
    struct TestDigest
    {
        std::vector<uint8_t> bytes;
        void update(const uint8_t* data, size_t size) { bytes.insert(bytes.end(), data, data + size); }
        bool finalize(uint8_t* out, size_t)
        {
            chat::reticulum::fullHash(bytes.data(), bytes.size(), out);
            return true;
        }
    };
    // Compare the actual staged writer against the independently encoded fixture,
    // then use its durable output for index import and checkpoint+journal restart.
    {
        const auto expected_checkpoint = checkpoint;
        TestDigest digest;
        auto staged = std::make_unique<SdCheckpointWriter<TestDigest>>(volume, digest);
        const auto advance = [&]()
        {
            auto state = CheckpointWriteStep::Working;
            for (unsigned i = 0; i < 4096 && state == CheckpointWriteStep::Working; ++i)
            {
                step_bytes = 0;
                state = staged->step();
                if (step_bytes > 512) return CheckpointWriteStep::Invalid;
            }
            return state;
        };
        MutationView row{4, {key.data(), key.size()}, {value, value_size}, false};
        if (!staged->begin(12) || advance() != CheckpointWriteStep::Ready ||
            !staged->page(&row, 1) || advance() != CheckpointWriteStep::Ready ||
            !staged->finish() || advance() != CheckpointWriteStep::Verified) return 456;
        if (files.at(SdCheckpointWriter<TestDigest>::path) != expected_checkpoint || checkpoint != expected_checkpoint) return 457;
        // Test-only publication into an otherwise fresh store. Production must
        // establish slot/reference safety before performing this transition.
        files.erase("/trailmate/geocaching/.state/checkpoint/a.gcs");
        if (!platform::esp::arduino_common::storage::sd_rename(SdCheckpointWriter<TestDigest>::path,
                                                               "/trailmate/geocaching/.state/checkpoint/a.gcs")) return 458;
    }
    const auto source_files = files;
    for (unsigned correct = 0; correct < 2; ++correct)
    {
        files = source_files;
        index_directories.clear();
        auto expected = selected;
        if (!correct) expected.digest[0] ^= 1;
        TestDigest digest;
        IndexRootBytes root_bytes;
        auto import = std::make_unique<SdCheckpointIndexImport<TestDigest>>(volume, digest);
        if (!import->begin('a', expected, frame, sizeof(frame), root_bytes)) return 247;
        auto status = IndexRootWriteStep::Working;
        for (unsigned i = 0; i < 4096 && status == IndexRootWriteStep::Working; ++i)
        {
            step_bytes = 0;
            status = import->step();
            if (step_bytes > 512) return 248;
        }
        IndexRootView root;
        if (!correct)
        {
            if (status != IndexRootWriteStep::Invalid || import->selected(root) ||
                files.count("/trailmate/geocaching/.state/index/root.h0") || files.count("/trailmate/geocaching/.state/index/root.h1")) return 249;
            continue;
        }
        if (status != IndexRootWriteStep::Verified || !import->selected(root) || root.sequence != 12 ||
            files["/trailmate/geocaching/.state/index/root.h0"] != files["/trailmate/geocaching/.state/index/root.h1"] ||
            files["/trailmate/geocaching/.state/checkpoint/a.gcs"] != source_files.at("/trailmate/geocaching/.state/checkpoint/a.gcs")) return 250;
        SdIndexGet read(volume);
        if (!read.begin(root, 4, {key.data(), key.size()}, frame, sizeof(frame))) return 251;
        auto read_status = IndexGetStep::Working;
        for (unsigned i = 0; i < 512 && read_status == IndexGetStep::Working; ++i) read_status = read.step();
        if (read_status != IndexGetStep::Ready || read.value().size != value_size || std::memcmp(read.value().data, value, value_size)) return 252;
        auto references = std::make_unique<SdCheckpointReferences>(volume);
        IndexRootBytes other_bytes = root_bytes;
        IndexRootView other_view;
        if (!decodeIndexRoot({other_bytes.data(), other_bytes.size()}, volume, other_view)) return 467;
        for (unsigned alias = 0; alias < 2; ++alias)
        {
            auto rejected = std::make_unique<SdCheckpointReferences>(volume);
            const auto before = files;
            const auto before_root = root_bytes;
            const auto before_other = other_bytes;
            auto* overlapping = alias ? other_bytes.data() + 48 : root_bytes.data() + 48;
            if (rejected->begin(root, other_view, overlapping, 64) || rejected->step() != IndexScanStep::Invalid ||
                !rejected->referenced('a') || !rejected->referenced('b') || files != before ||
                root_bytes != before_root || other_bytes != before_other) return 468;
        }
        if (!references->referenced('a') || !references->referenced('b') ||
            !references->begin(root, root, frame, sizeof(frame))) return 459;
        auto reference_status = IndexScanStep::Working;
        for (unsigned i = 0; i < 8192 && reference_status == IndexScanStep::Working; ++i)
        {
            step_bytes = 0;
            reference_status = references->step();
            if (step_bytes > 512) return 460;
        }
        if (reference_status != IndexScanStep::End || !references->referenced('a') || references->referenced('b')) return 461;
        const auto active_files = files;
        const auto active_directories = index_directories;
        for (unsigned valid = 0; valid < 5; ++valid)
        {
            files = active_files;
            index_directories = active_directories;
            TestDigest replacement_digest;
            auto replacement = std::make_unique<SdCheckpointIndexImport<TestDigest>>(volume, replacement_digest);
            IndexRootBytes replacement_bytes;
            uint8_t comparison_frame[512];
            auto checkpoint_candidate = selected;
            if (!valid) checkpoint_candidate.digest[0] ^= 1;
            char checkpoint_slot = 'a';
            if (valid >= 2)
            {
                // Valid CRC/SHA and sequence alone cannot authorize altered,
                // missing or added business rows. Keep the parent's slot intact.
                uint8_t changed_value[128];
                size_t changed_size = 0;
                auto changed_draft = draft;
                changed_draft.name = "Changed checkpoint";
                auto added_key = key;
                added_key.back() = 1;
                if (!encodeDraft({key.data(), key.size()}, changed_draft, changed_value, sizeof(changed_value), changed_size)) return 477;
                MutationView rows[] = {{4, {key.data(), key.size()}, {valid == 2 ? changed_value : value, valid == 2 ? changed_size : value_size}, false},
                                       {4, {added_key.data(), added_key.size()}, {value, value_size}, false}};
                TestDigest alternate_digest;
                auto alternate = std::make_unique<SdCheckpointWriter<TestDigest>>(volume, alternate_digest);
                const auto advance_alternate = [&]()
                {
                    auto state = CheckpointWriteStep::Working;
                    for (unsigned i = 0; i < 4096 && state == CheckpointWriteStep::Working; ++i) state = alternate->step();
                    return state;
                };
                if (!alternate->begin(12) || advance_alternate() != CheckpointWriteStep::Ready ||
                    !alternate->page(rows, valid == 3 ? 0 : valid == 4 ? 2
                                                                       : 1) ||
                    advance_alternate() != CheckpointWriteStep::Ready ||
                    !alternate->finish() || advance_alternate() != CheckpointWriteStep::Verified) return 478;
                chat::reticulum::fullHash(alternate_digest.bytes.data(), alternate_digest.bytes.size(), checkpoint_candidate.digest.data());
                if (!platform::esp::arduino_common::storage::sd_rename(SdCheckpointWriter<TestDigest>::path,
                                                                       "/trailmate/geocaching/.state/checkpoint/b.gcs")) return 479;
                checkpoint_slot = 'b';
            }
            if (!replacement->beginReplacement(checkpoint_slot, checkpoint_candidate, root, 0, frame, sizeof(frame), replacement_bytes,
                                               comparison_frame, sizeof(comparison_frame))) return 469;
            auto replaced = IndexRootWriteStep::Working;
            for (unsigned i = 0; i < 8192 && replaced == IndexRootWriteStep::Working; ++i)
            {
                step_bytes = 0;
                replaced = replacement->step();
                if (step_bytes > 512) return 470;
                // Every construction/publication boundary retains the active
                // root and every byte of its original index/checkpoint files.
                for (const auto& file : active_files)
                    if (file.first != "/trailmate/geocaching/.state/index/root.h1" &&
                        files.at(file.first) != file.second) return 471;
            }
            if (valid != 1)
            {
                if (replaced != IndexRootWriteStep::Invalid ||
                    files.at("/trailmate/geocaching/.state/index/root.h1") != active_files.at("/trailmate/geocaching/.state/index/root.h1")) return 472;
            }
            else
            {
                IndexRootView next, chosen;
                if (replaced != IndexRootWriteStep::Verified || !replacement->selected(next) || next.slot != 'b' ||
                    next.epoch != root.epoch + 1 || next.revision != root.revision + 1 || next.sequence != root.sequence ||
                    !selectIndexRoot(root, next, chosen) || chosen.slot != 'b') return 473;
                SdIndexGet next_read(volume);
                if (!next_read.begin(next, 4, {key.data(), key.size()}, frame, sizeof(frame))) return 474;
                auto next_status = IndexGetStep::Working;
                for (unsigned i = 0; i < 512 && next_status == IndexGetStep::Working; ++i) next_status = next_read.step();
                if (next_status != IndexGetStep::Ready || next_read.value().size != value_size ||
                    std::memcmp(next_read.value().data, value, value_size)) return 475;
            }
            // A restart selects the old root after failed preparation and the
            // new root after publication. No preparatory tree is made current.
            IndexRootBytes restarted_roots[2];
            uint8_t restart_validation[512];
            MutationView restart_rows[3];
            auto restarted = std::make_unique<SdIndexedRecovery<TestDigest>>(volume, restarted_roots[0], restarted_roots[1],
                                                                             frame, sizeof(frame), restart_validation, sizeof(restart_validation), restart_rows, 3);
            auto restarted_status = IndexedRecoveryStep::Working;
            for (unsigned i = 0; i < 8192 && restarted_status == IndexedRecoveryStep::Working; ++i)
                restarted_status = restarted->step();
            IndexRootView restarted_root;
            unsigned restarted_copy = 0;
            if (restarted_status != IndexedRecoveryStep::Restored || !restarted->selected(restarted_root, restarted_copy) ||
                restarted_root.slot != (valid == 1 ? 'b' : 'a') || restarted_root.sequence != 12) return 476;
        }
        files = active_files;
        index_directories = active_directories;
    }
    files = source_files;
    index_directories.clear();
    draft.generation = 2;
    draft.name = "After checkpoint";
    if (!encodeDraft({key.data(), key.size()}, draft, value, sizeof(value), value_size)) return 70;
    MutationView updated{4, {key.data(), key.size()}, {value, value_size}, false};
    SdGeocachingJournal suffix(volume);
    if (suffix.begin(12, &updated, 1) != JournalWriteResult::InProgress) return 71;
    auto suffix_status = JournalWriteResult::InProgress;
    for (unsigned i = 0; i < 256 && suffix_status == JournalWriteResult::InProgress; ++i) suffix_status = suffix.step();
    if (suffix_status != JournalWriteResult::Verified) return 72;
    uint8_t validation_frame[512];
    MutationView mutations[3];
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        if (scenario == 2)
        {
            files.clear();
            index_directories.clear();
            files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
        }
        const auto before = files;
        IndexRootBytes roots[2];
        auto recovery = std::make_unique<SdIndexedRecovery<TestDigest>>(volume, roots[0], roots[1], frame, sizeof(frame),
                                                                        validation_frame, sizeof(validation_frame), mutations, 3);
        auto status = IndexedRecoveryStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedRecoveryStep::Working; ++i)
        {
            step_bytes = 0;
            status = recovery->step();
            if (step_bytes > 512) return 73;
        }
        IndexRootView root;
        unsigned copy = 0;
        if (status != IndexedRecoveryStep::Restored || !recovery->selected(root, copy) || root.sequence != (scenario == 2 ? 0 : 13)) return 74;
        if (scenario == 1 && files != before) return 75;
        if (scenario != 2)
        {
            SdIndexGet read(volume);
            if (!read.begin(root, 4, {key.data(), key.size()}, frame, sizeof(frame))) return 76;
            auto read_status = IndexGetStep::Working;
            for (unsigned i = 0; i < 512 && read_status == IndexGetStep::Working; ++i) read_status = read.step();
            if (read_status != IndexGetStep::Ready || read.value().size != value_size || std::memcmp(read.value().data, value, value_size)) return 77;
            IndexRootView other_root;
            if (!decodeIndexRoot({roots[1 - copy].data(), roots[1 - copy].size()}, volume, other_root)) return 462;
            // Newest root reads the updated draft from the journal. The older
            // recoverable root still pins the original checkpoint slot.
            for (unsigned audit = 0; audit < 3; ++audit)
            {
                auto references = std::make_unique<SdCheckpointReferences>(volume);
                if (!references->begin(root, audit == 1 ? root : other_root, frame, sizeof(frame))) return 463;
                if (audit == 2) fail_read_once = true;
                auto reference_status = IndexScanStep::Working;
                for (unsigned i = 0; i < 8192 && reference_status == IndexScanStep::Working; ++i)
                {
                    step_bytes = 0;
                    reference_status = references->step();
                    if (step_bytes > 512) return 464;
                }
                if (audit == 2)
                {
                    if (fail_read_once || reference_status != IndexScanStep::IoError ||
                        !references->referenced('a') || !references->referenced('b')) return 465;
                }
                else if (reference_status != IndexScanStep::End || references->referenced('a') != (audit == 0) ||
                         references->referenced('b')) return 466;
            }
        }
    }
    files.clear();
    index_directories.clear();
    return 0;
}

int checkIndexedDownloadReceipt(const char* path)
{
    using namespace ::geocaching;
    using namespace ::geocaching::storage;
    struct Crypto : protocol::RecordCrypto
    {
        bool sha256(ByteView bytes, uint8_t out[32]) override
        {
            chat::reticulum::fullHash(bytes.data, bytes.size, out);
            return true;
        }
        protocol::VerificationResult verifyEd25519(ByteView key, ByteView signature, ByteView message) override
        {
            return ed25519_verify(signature.data, message.data, message.size, key.data) ? protocol::VerificationResult::Valid : protocol::VerificationResult::InvalidSignature;
        }
    } crypto;
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> response((std::istreambuf_iterator<char>(input)), {});
    protocol::CmpReader envelope({response.data(), response.size()});
    size_t fields = 0;
    uint64_t number = 0;
    ByteView id_bytes;
    if (!envelope.array(fields, 6) || !envelope.unsignedInteger(number) || !envelope.unsignedInteger(number) ||
        !envelope.unsignedInteger(number) || !envelope.binary(id_bytes, 16) || id_bytes.size != 16) return 139;
    RequestId id;
    std::memcpy(id.bytes.data(), id_bytes.data, 16);
    protocol::GetResponseView reply;
    uint8_t verification[512], frame[1024], request[256], outgoing[1024];
    protocol::VerifiedRecordView record;
    if (!protocol::decodeGetResponse({response.data(), response.size()}, id, 8192, reply) ||
        protocol::verifyGeocache(reply.signed_cache, crypto, verification, sizeof(verification), record) != protocol::VerificationResult::Valid) return 140;
    protocol::CmpReader signed_record(reply.signed_cache);
    ByteView encoded, signature;
    if (!signed_record.array(fields, 2) || !signed_record.binary(encoded, 4096) || !signed_record.binary(signature, 64)) return 141;
    const auto signature_offset = static_cast<size_t>(signature.data - response.data());
    files.clear();
    index_directories.clear();
    VolumeInstance volume{};
    const auto format = encodeVolumeHeader(volume);
    files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
    IndexRootBytes roots[2];
    SdIndexInitialize initialize(volume);
    if (!initialize.begin(roots[0])) return 142;
    auto initialized = IndexRootWriteStep::Working;
    for (unsigned i = 0; i < 128 && initialized == IndexRootWriteStep::Working; ++i) initialized = initialize.step();
    if (initialized != IndexRootWriteStep::Verified) return 143;
    roots[1] = roots[0];
    IndexRootView root;
    if (!decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, root)) return 144;
    unsigned copy = 0;
    const auto initial_files = files;
    const auto initial_roots = std::array<IndexRootBytes, 2>{roots[0], roots[1]};
    size_t request_size = 0;
    if (!protocol::encodeGetRequest(id, record.id, &record.hash, nullptr, 8192, request, sizeof(request), request_size)) return 145;
    QueuedRequestWorkspace workspace(outgoing, sizeof(outgoing));
    std::array<uint8_t, 16> task{};
    task.fill(0x42);
    auto create = std::make_unique<SdIndexedNewTask>(volume);
    if (!create->begin(root, copy, {}, {}, id, task, 2, {request, request_size}, {},
                       {{record.id.bytes.data(), 32}, {record.hash.bytes.data(), 32}, 1}, workspace,
                       frame, sizeof(frame), roots[1 - copy])) return 146;
    auto created = IndexedCommitStep::Working;
    for (unsigned i = 0; i < 8192 && created == IndexedCommitStep::Working; ++i) created = create->step();
    if (created != IndexedCommitStep::Verified || !create->committed(root)) return 147;
    copy = 1 - copy;
    std::array<uint8_t, 48> key{};
    std::copy(id.bytes.begin(), id.bytes.end(), key.begin() + 32);
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
        auto received = response;
        if (scenario == 1) received[signature_offset] ^= 1;
        const auto before = files;
        const auto sequence = root.sequence;
        auto receipt = std::make_unique<SdIndexedDownloadReply>(volume, crypto);
        if (!receipt->begin(root, copy, {key.data(), key.size()}, scenario == 0 ? 2 : 1, {received.data(), received.size()},
                            workspace, frame, sizeof(frame), verification, sizeof(verification), roots[1 - copy])) return 148;
        auto status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            status = receipt->step();
            if (step_bytes > 512) return 149;
        }
        if (scenario < 2)
        {
            if (status != IndexedCommitStep::Invalid || files != before) return 150;
        }
        else
        {
            if (status != IndexedCommitStep::Verified || !receipt->committed(root)) return 151;
            if (scenario == 2)
            {
                if (root.sequence != sequence + 1) return 152;
                copy = 1 - copy;
            }
            else if (root.sequence != sequence || files != before) return 153;
        }
    }
    SdIndexedDownloadContext context(volume);
    if (!context.begin(root, {key.data(), key.size()}, 1, frame, sizeof(frame))) return 154;
    auto loaded = IndexGetStep::Working;
    for (unsigned i = 0; i < 4096 && loaded == IndexGetStep::Working; ++i) loaded = context.step();
    OutgoingView saved;
    TaskView running;
    CacheHeadView head;
    if (loaded != IndexGetStep::Ready || !context.view(saved, running, head) || saved.state != 4 || running.state != 1 ||
        head.current_hash.size || !context.intentActive() || saved.terminal_data.size != response.size() ||
        std::memcmp(saved.terminal_data.data, response.data(), response.size())) return 155;
    // Drive the production index path from durable receipt through prepare and
    // completion. Rejected inputs and replayed completions must not write.
    std::array<uint8_t, 32> file_hash{};
    file_hash.fill(37);
    const auto receipt_files = files;
    const auto receipt_roots = std::array<IndexRootBytes, 2>{roots[0], roots[1]};
    const auto receipt_copy = copy;
    for (unsigned scenario = 0; scenario < 8; ++scenario)
    {
        const auto before = files;
        const auto sequence = root.sequence;
        auto install = std::make_unique<SdIndexedInstall>(volume, crypto);
        auto observed = file_hash;
        if (scenario == 5) observed[0] ^= 1;
        const bool finishing = scenario == 0 || scenario >= 5;
        const bool begun = finishing
                               ? install->beginFinish(root, copy, {key.data(), key.size()}, 1, observed, frame, sizeof(frame), roots[1 - copy])
                               : install->beginPrepare(root, copy, {key.data(), key.size()}, scenario == 1 ? 2 : 1, observed,
                                                       scenario == 2 ? ByteView{file_hash.data(), file_hash.size()} : ByteView{},
                                                       frame, sizeof(frame), verification, sizeof(verification), roots[1 - copy]);
        if (!begun) return 240;
        auto status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 8192 && status == IndexedCommitStep::Working; ++i)
        {
            step_bytes = 0;
            status = install->step();
            if (step_bytes > 512) return 241;
        }
        if (scenario < 3 || scenario == 5)
        {
            if (status != IndexedCommitStep::Invalid || files != before) return 242;
            continue;
        }
        if (status != IndexedCommitStep::Verified || !install->committed(root)) return 243;
        const bool changed = scenario == 3 || scenario == 6;
        if (changed)
        {
            if (root.sequence != sequence + 1) return 244;
            copy = 1 - copy;
        }
        else if (root.sequence != sequence || files != before) return 245;
        SdIndexGet task_read(volume);
        if (!task_read.begin(root, 10, {task.data(), task.size()}, frame, sizeof(frame))) return 246;
        auto task_status = IndexGetStep::Working;
        for (unsigned i = 0; i < 4096 && task_status == IndexGetStep::Working; ++i) task_status = task_read.step();
        TaskView checked;
        if (task_status != IndexGetStep::Ready || !decodeTask({task.data(), task.size()}, task_read.value(), checked) ||
            checked.state != (scenario >= 6 ? 3 : 1)) return 247;
    }
    // A stop committed after receipt must prevent an installation transaction.
    files = receipt_files;
    roots[0] = receipt_roots[0];
    roots[1] = receipt_roots[1];
    copy = receipt_copy;
    if (!decodeIndexRoot({roots[copy].data(), roots[copy].size()}, volume, root)) return 248;
    auto stop = std::make_unique<SdIndexedStopTask>(volume);
    if (!stop->begin(root, copy, {key.data(), key.size()}, true, frame, sizeof(frame), roots[1 - copy])) return 249;
    auto stop_status = IndexedCommitStep::Working;
    for (unsigned i = 0; i < 8192 && stop_status == IndexedCommitStep::Working; ++i) stop_status = stop->step();
    if (stop_status != IndexedCommitStep::Verified || !stop->committed(root)) return 250;
    copy = 1 - copy;
    const auto stopped_files = files;
    auto rejected_install = std::make_unique<SdIndexedInstall>(volume, crypto);
    if (!rejected_install->beginPrepare(root, copy, {key.data(), key.size()}, 1, file_hash, {}, frame, sizeof(frame),
                                        verification, sizeof(verification), roots[1 - copy])) return 251;
    auto rejected_status = IndexedCommitStep::Working;
    for (unsigned i = 0; i < 8192 && rejected_status == IndexedCommitStep::Working; ++i) rejected_status = rejected_install->step();
    if (rejected_status != IndexedCommitStep::Invalid || files != stopped_files) return 252;
    struct FileDigest
    {
        std::vector<uint8_t> bytes;
        void update(const uint8_t* data, size_t count) { bytes.insert(bytes.end(), data, data + count); }
        bool finalize(uint8_t* hash, size_t size)
        {
            if (size != 32) return false;
            chat::reticulum::fullHash(bytes.data(), bytes.size(), hash);
            return true;
        }
    };
    std::string target = "/trailmate/geocaching/caches/";
    constexpr char hex[] = "0123456789abcdef";
    for (const auto byte : record.id.bytes)
    {
        target += hex[byte >> 4];
        target += hex[byte & 15];
    }
    target += ".gpx";
    struct InterruptedDownload
    {
        decltype(files) disk;
        std::array<uint8_t, 16> task;
        RequestId request;
        uint64_t generation;
        bool recoverable = true;
    };
    std::vector<InterruptedDownload> interrupted;
    for (unsigned scenario = 0; scenario < 2; ++scenario)
    {
        files = receipt_files;
        roots[0] = receipt_roots[0];
        roots[1] = receipt_roots[1];
        copy = receipt_copy;
        if (!decodeIndexRoot({roots[copy].data(), roots[copy].size()}, volume, root)) return 253;
        if (scenario == 1) files[target] = {'u', 's', 'e', 'r'};
        uint8_t incoming[1024];
        IndexWorkspaceOwner owner;
        struct DownloadWorkspace
        {
            IndexWorkspaceOwner& owner;
            QueuedRequestWorkspace& workspace;
            IndexedDownloadStore* store = nullptr;
            std::unique_ptr<uint8_t[]> frame, encoded, payload, verification;
            unsigned releases = 0;
            static bool prepare(void* context, const void*)
            {
                auto& self = *static_cast<DownloadWorkspace*>(context);
                if (!self.frame) self.frame.reset(new uint8_t[1024]);
                if (!self.encoded) self.encoded.reset(new uint8_t[1024]);
                if (!self.payload) self.payload.reset(new uint8_t[1024]);
                if (!self.verification) self.verification.reset(new uint8_t[512]);
                self.workspace.outgoing = self.encoded.get();
                self.store->bindWorkspace(self.frame.get(), self.payload.get(), self.verification.get());
                return true;
            }
            void trim()
            {
                if (owner.holder() || !frame) return;
                std::memset(frame.get(), 0xa5, 1024);
                std::memset(encoded.get(), 0x5a, 1024);
                std::memset(payload.get(), 0xa5, 1024);
                std::memset(verification.get(), 0x5a, 512);
                frame.reset();
                encoded.reset();
                payload.reset();
                verification.reset();
                workspace.outgoing = nullptr;
                ++releases;
            }
        } transient{owner, workspace};
        auto indexed = std::make_unique<IndexedDownloadStore>(volume, root, copy, roots[0], roots[1], owner, workspace,
                                                              frame, sizeof(frame), incoming, sizeof(incoming), verification, sizeof(verification), crypto);
        transient.store = indexed.get();
        if (!owner.setPrepare(DownloadWorkspace::prepare, &transient)) return 406;
        auto port = std::make_unique<SdDownloadPort<FileDigest>>(*indexed, crypto, Destination{}, InstallIdentity{record.id, record.hash, 1}, task, StoredTime{});
        auto result = port->resume({}, id);
        uint64_t last_sequence = root.sequence;
        bool target_seen = false, stage_seen = false;
        for (unsigned i = 0; i < 16384 && result == DownloadOperationResult::Pending; ++i)
        {
            step_bytes = 0;
            result = port->poll();
            transient.trim();
            if (step_bytes > 512) return 254;
            if (scenario == 0)
            {
                const std::string staged = "/trailmate/geocaching/.state/staging/42424242424242424242424242424242.gpx";
                const bool stage_exists = files.count(staged) && !files.at(staged).empty();
                const bool target_exists = files.count(target);
                if ((!stage_seen && stage_exists) || (!target_seen && target_exists) ||
                    (root.sequence != last_sequence && result == DownloadOperationResult::Pending)) interrupted.push_back({files, task, id, 1});
                stage_seen |= stage_exists;
                target_seen |= target_exists;
                last_sequence = root.sequence;
            }
        }
        if (scenario == 0)
        {
            if (result != DownloadOperationResult::Complete || !files.count(target) || owner.holder())
            {
                std::fprintf(stderr, "Indexed GPX: result=%u sequence=%llu target=%u lease=%u blocked=%u\n",
                             unsigned(result), static_cast<unsigned long long>(root.sequence), unsigned(files.count(target)),
                             unsigned(owner.holder() != nullptr), unsigned(indexed->needsRecovery()));
                return 255;
            }
            const std::string gpx(files.at(target).begin(), files.at(target).end());
            if (gpx.find("<name>Test</name>") == std::string::npos || gpx.find("<wpt ") == std::string::npos) return 256;
        }
        else if (result != DownloadOperationResult::Rejected || files.at(target) != std::vector<uint8_t>({'u', 's', 'e', 'r'})) return 257;
        if (!transient.releases || transient.frame || owner.holder()) return 407;
        workspace.outgoing = outgoing;
    }
    protocol::SummaryView summary;
    summary.id = record.id;
    summary.hash = record.hash;
    summary.revision = record.record.revision;
    summary.state = record.record.state;
    summary.latitude_e7 = record.record.latitude_e7;
    summary.longitude_e7 = record.record.longitude_e7;
    summary.name = record.record.name;
    summary.difficulty_x2 = record.record.difficulty_x2;
    summary.terrain_x2 = record.record.terrain_x2;
    summary.container_size = record.record.container_size;
    summary.signed_bytes = reply.signed_cache.size;
    const std::string response_path(path);
    const auto query_path = response_path.substr(0, response_path.find_last_of("/\\") + 1) + "query-response-v1.bin";
    std::ifstream query_input(query_path, std::ios::binary);
    std::vector<uint8_t> query_response((std::istreambuf_iterator<char>(query_input)), {});
    RequestId query_id;
    query_id.bytes.fill(3);
    protocol::QueryPageView preview_page;
    if (!protocol::decodeQueryPage({query_response.data(), query_response.size()}, query_id, 8192, 64, preview_page)) return 303;
    const auto persist_preview = [&](const Destination& remote, uint8_t task_byte)
    {
        uint8_t query_request[256];
        size_t query_size = 0;
        std::array<uint8_t, 16> query_task;
        query_task.fill(task_byte);
        if (!protocol::encodeQueryRequest(query_id, {-900000000, -1800000000, 900000000, 1800000000},
                                          7, {}, 4, {}, 8192, query_request, sizeof(query_request), query_size)) return false;
        auto create_query = std::make_unique<SdIndexedNewTask>(volume);
        if (!create_query->begin(root, copy, {}, remote, query_id, query_task, 3, {query_request, query_size}, {}, {},
                                 workspace, frame, sizeof(frame), roots[1 - copy])) return false;
        auto status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 16384 && status == IndexedCommitStep::Working; ++i) status = create_query->step();
        if (status != IndexedCommitStep::Verified || !create_query->committed(root)) return false;
        copy = 1 - copy;
        create_query.reset();
        std::array<uint8_t, 48> query_key{};
        std::memcpy(query_key.data() + 16, remote.bytes.data(), 16);
        std::memcpy(query_key.data() + 32, query_id.bytes.data(), 16);
        auto save_reply = std::make_unique<SdIndexedDirectoryReply>(volume);
        if (!save_reply->begin(root, copy, {query_key.data(), query_key.size()}, 2, {query_response.data(), query_response.size()},
                               workspace, frame, sizeof(frame), roots[1 - copy])) return false;
        status = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 16384 && status == IndexedCommitStep::Working; ++i) status = save_reply->step();
        if (status != IndexedCommitStep::Verified || !save_reply->committed(root)) return false;
        copy = 1 - copy;
        return true;
    };
    // Use the actual application client and port, including waiting, cancel,
    // source matching and replacement of an already downloaded exact version.
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        if (scenario < 2)
        {
            files = initial_files;
            roots[0] = initial_roots[0];
            roots[1] = initial_roots[1];
            copy = 0;
            if (!decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, root)) return 264;
        }
        auto request_id = id;
        auto task_id = task;
        const uint64_t generation = scenario == 2 ? 2 : 1;
        if (scenario == 2)
        {
            request_id.bytes[0] ^= 0x55;
            task_id[0] ^= 0x55;
        }
        uint8_t incoming[1024];
        IndexWorkspaceOwner owner;
        auto indexed = std::make_unique<IndexedDownloadStore>(volume, root, copy, roots[0], roots[1], owner, workspace,
                                                              frame, sizeof(frame), incoming, sizeof(incoming), verification, sizeof(verification), crypto);
        auto port = std::make_unique<SdDownloadPort<FileDigest>>(*indexed, crypto, Destination{}, InstallIdentity{record.id, record.hash, generation}, task_id, StoredTime{});
        DownloadClient client(*port, crypto);
        int other_owner = 0;
        if (!owner.acquire(&other_owner)) return 265;
        const auto unchanged = files;
        if (client.begin({}, request_id, summary, generation) || files != unchanged) return 266;
        uint64_t next_generation = 99;
        if (indexed->readNextGeneration(record.id, next_generation) != DownloadRecoveryRead::Busy || next_generation || !owner.heldBy(&other_owner)) return 318;
        owner.release(&other_owner);
        if (indexed->readNextGeneration(record.id, next_generation) != DownloadRecoveryRead::Pending || !owner.heldBy(indexed.get())) return 319;
        auto other_cache = record.id;
        other_cache.bytes[0] ^= 1;
        DownloadRecoveryRequest other_recovery;
        if (indexed->readNextGeneration(other_cache, next_generation) != DownloadRecoveryRead::Busy ||
            indexed->readRecovery({}, other_recovery) != DownloadRecoveryRead::Busy || indexed->stopTask(task_id) != JournalWriteResult::Busy) return 320;
        indexed->releaseRead();
        if (owner.holder() || files != unchanged) return 321;
        auto generation_status = DownloadRecoveryRead::Pending;
        for (unsigned i = 0; i < 16384 && generation_status == DownloadRecoveryRead::Pending; ++i)
        {
            step_bytes = 0;
            generation_status = indexed->readNextGeneration(record.id, next_generation);
            if (step_bytes > 512) return 322;
        }
        if (generation_status != DownloadRecoveryRead::Ready || next_generation != generation || !owner.heldBy(indexed.get()) || files != unchanged) return 323;
        if (indexed->persistNewTask({}, {}, request_id, task_id, 2, {request, request_size}, {},
                                    {{record.id.bytes.data(), 32}, {record.hash.bytes.data(), 32}, generation + 1}) != JournalWriteResult::StateRejected ||
            indexed->persistNewTask({}, {}, request_id, task_id, 2, {request, request_size}, {},
                                    {{other_cache.bytes.data(), 32}, {record.hash.bytes.data(), 32}, generation}) != JournalWriteResult::StateRejected ||
            indexed->needsRecovery() || !owner.heldBy(indexed.get()) || files != unchanged) return 324;
        // The actual client promotes the head read lease to its durable task
        // transaction; there is no gap for another operation to change it.
        if (!client.begin({}, request_id, summary, generation)) return 267;
        for (unsigned i = 0; i < 16384 && client.phase() == DownloadPhase::Submitting; ++i)
        {
            step_bytes = 0;
            client.advance();
            if (step_bytes > 512) return 268;
        }
        if (client.phase() != DownloadPhase::Waiting || owner.holder()) return 269;
        if (scenario == 2)
        {
            // A newer waiting task advances the head counter, but the old
            // installed version remains available until replacement succeeds.
            SavedCacheRecord saved;
            auto status = DownloadRecoveryRead::Pending;
            for (unsigned i = 0; i < 32768 && status == DownloadRecoveryRead::Pending; ++i)
                status = indexed->readSavedCache({record.id.bytes.data(), 32}, true, crypto, saved);
            if (status != DownloadRecoveryRead::Ready || saved.id != record.id.bytes || saved.hash != record.hash.bytes || owner.holder()) return 357;
        }
        DownloadRecoveryRequest waiting_request;
        protocol::SummaryView waiting_preview;
        const auto select_waiting = [&](DownloadStore& downloads, const Destination& local)
        {
            auto status = DownloadRecoveryRead::Pending;
            for (unsigned i = 0; i < 32768 && status == DownloadRecoveryRead::Pending; ++i)
            {
                step_bytes = 0;
                status = downloads.readWaitingDownload(local, waiting_request, waiting_preview);
                if (step_bytes > 512) return DownloadRecoveryRead::Invalid;
            }
            return status;
        };
        if (scenario == 0)
        {
            // A preview from another directory must not fill a missing preview
            // for this request, even when cache ID and revision hash match.
            for (unsigned missing = 0; missing < 2; ++missing)
            {
                const auto disk = files;
                IndexedDownloadStore unavailable(volume, root, copy, roots[0], roots[1], owner, workspace,
                                                 frame, sizeof(frame), incoming, sizeof(incoming), verification, sizeof(verification), crypto);
                if (select_waiting(unavailable, {}) != DownloadRecoveryRead::Invalid || owner.holder() || files != disk) return 304;
                if (!missing)
                {
                    Destination other_directory;
                    other_directory.bytes.fill(0x71);
                    if (!persist_preview(other_directory, 0x73)) return 305;
                }
            }
        }
        if (scenario < 2 && !persist_preview({}, 0x72)) return 306;
        {
            // Device-style asynchronous resume. A contended workspace leaves
            // the client pending; a completed read must not persist or resend.
            const auto waiting_files = files;
            const auto waiting_sequence = root.sequence;
            const auto waiting_roots = std::array<IndexRootBytes, 2>{roots[0], roots[1]};
            const auto waiting_copy = copy;
            MutationView reload_mutations[3];
            auto reboot = std::make_unique<SdIndexedRecovery<FileDigest>>(volume, roots[0], roots[1], frame, sizeof(frame),
                                                                          outgoing, sizeof(outgoing), reload_mutations, 3);
            auto reboot_status = IndexedRecoveryStep::Working;
            for (unsigned i = 0; i < 32768 && reboot_status == IndexedRecoveryStep::Working; ++i)
            {
                step_bytes = 0;
                reboot_status = reboot->step();
                if (step_bytes > 512) return 312;
            }
            if (reboot_status != IndexedRecoveryStep::Restored || !reboot->selected(root, copy) || root.sequence != waiting_sequence) return 313;
            reboot.reset();
            IndexedDownloadStore resumed_store(volume, root, copy, roots[0], roots[1], owner, workspace,
                                               frame, sizeof(frame), incoming, sizeof(incoming), verification, sizeof(verification), crypto);
            Destination other_local;
            other_local.bytes.fill(0x74);
            if (select_waiting(resumed_store, other_local) != DownloadRecoveryRead::End || owner.holder()) return 307;
            if (!owner.acquire(&other_owner) || select_waiting(resumed_store, {}) != DownloadRecoveryRead::Busy || !owner.heldBy(&other_owner)) return 308;
            owner.release(&other_owner);
            if (resumed_store.readWaitingDownload({}, waiting_request, waiting_preview) != DownloadRecoveryRead::Pending ||
                resumed_store.readRecovery({}, waiting_request) != DownloadRecoveryRead::Busy ||
                resumed_store.readWaitingDownload(other_local, waiting_request, waiting_preview) != DownloadRecoveryRead::Busy) return 309;
            resumed_store.releaseRead();
            if (owner.holder() || select_waiting(resumed_store, {}) != DownloadRecoveryRead::Ready || !owner.heldBy(&resumed_store) ||
                waiting_request.task != task_id || waiting_request.identity.generation != generation ||
                waiting_preview.id.bytes != summary.id.bytes || waiting_preview.hash.bytes != summary.hash.bytes || waiting_preview.name != summary.name ||
                resumed_store.stopTask(task_id) != JournalWriteResult::Busy) return 310;
            std::array<char, kMaxNameBytes> preview_name{};
            std::memcpy(preview_name.data(), waiting_preview.name.data(), waiting_preview.name.size());
            waiting_preview.name = {preview_name.data(), waiting_preview.name.size()};
            resumed_store.releaseRead();
            std::memset(frame, 0xcc, sizeof(frame));
            auto resumed_port = std::make_unique<SdDownloadPort<FileDigest>>(resumed_store, crypto, Destination{}, waiting_request.identity, waiting_request.task, waiting_request.created);
            DownloadClient resumed(*resumed_port, crypto);
            if (!owner.acquire(&other_owner) ||
                !resumed.resume({}, request_id, waiting_preview, generation, resumed_port->resumeWaiting({}, request_id))) return 280;
            preview_name.fill('x');
            resumed.advance();
            if (resumed.phase() != DownloadPhase::Submitting || files != waiting_files) return 281;
            owner.release(&other_owner);
            for (unsigned i = 0; i < 16384 && resumed.phase() == DownloadPhase::Submitting; ++i)
            {
                step_bytes = 0;
                resumed.advance();
                if (step_bytes > 512) return 282;
            }
            if (resumed.phase() != DownloadPhase::Waiting || owner.holder() || root.sequence != waiting_sequence || files != waiting_files) return 283;
            if (scenario == 1)
            {
                if (resumed.accept({}, {response.data(), response.size()}, verification, sizeof(verification)) || resumed.phase() != DownloadPhase::Installing) return 314;
                for (unsigned i = 0; i < 32768 && resumed.phase() == DownloadPhase::Installing; ++i)
                {
                    step_bytes = 0;
                    resumed.advance();
                    if (step_bytes > 512) return 315;
                }
                if (resumed.phase() != DownloadPhase::Stored || !files.count(target) || owner.holder()) return 316;
                // Continue the original test branch from its waiting snapshot.
                resumed_port.reset();
                files = waiting_files;
                roots[0] = waiting_roots[0];
                roots[1] = waiting_roots[1];
                copy = waiting_copy;
                if (!decodeIndexRoot({roots[copy].data(), roots[copy].size()}, volume, root)) return 317;
            }
        }
        if (scenario == 0)
        {
            if (!owner.acquire(&other_owner) || !client.cancel()) return 270;
            client.advance();
            if (client.phase() != DownloadPhase::Cancelling) return 271;
            owner.release(&other_owner);
            for (unsigned i = 0; i < 16384 && client.phase() == DownloadPhase::Cancelling; ++i) client.advance();
            if (client.phase() != DownloadPhase::Cancelled || files.count(target) || owner.holder()) return 272;
            if (select_waiting(*indexed, {}) != DownloadRecoveryRead::End || owner.holder()) return 311;
            continue;
        }
        auto received = response;
        const auto request_offset = static_cast<size_t>(id_bytes.data - response.data());
        std::memcpy(received.data() + request_offset, request_id.bytes.data(), 16);
        Destination wrong_source;
        wrong_source.bytes[0] = 1;
        if (client.accept(wrong_source, {received.data(), received.size()}, verification, sizeof(verification)) ||
            client.phase() != DownloadPhase::Waiting) return 273;
        if (client.accept({}, {received.data(), received.size()}, verification, sizeof(verification)) ||
            client.phase() != DownloadPhase::Installing) return 274;
        // The callback input must be consumed before returning Pending.
        std::fill(received.begin(), received.end(), 0xcc);
        uint64_t last_sequence = root.sequence;
        bool backup_seen = false;
        std::string backup = "/trailmate/geocaching/.state/staging/";
        for (const auto byte : task_id)
        {
            backup += hex[byte >> 4];
            backup += hex[byte & 15];
        }
        backup += ".old.gpx";
        for (unsigned i = 0; i < 32768 && client.phase() == DownloadPhase::Installing; ++i)
        {
            step_bytes = 0;
            client.advance();
            if (step_bytes > 512) return 275;
            if (scenario == 2 && client.phase() == DownloadPhase::Installing)
            {
                const bool backup_exists = files.count(backup);
                if ((!backup_seen && backup_exists) || root.sequence != last_sequence)
                    interrupted.push_back({files, task_id, request_id, generation});
                backup_seen |= backup_exists;
                last_sequence = root.sequence;
            }
        }
        if (client.phase() != DownloadPhase::Stored || !files.count(target) || owner.holder()) return 276;
        {
            const auto disk = files;
            SavedCacheRecord saved;
            const auto read_saved = [&](DownloadStore& store, ByteView key, bool exact, protocol::RecordCrypto& verifier)
            {
                auto status = DownloadRecoveryRead::Pending;
                for (unsigned i = 0; i < 32768 && status == DownloadRecoveryRead::Pending; ++i)
                {
                    step_bytes = 0;
                    status = store.readSavedCache(key, exact, verifier, saved);
                    if (step_bytes > 512) return DownloadRecoveryRead::Invalid;
                }
                return status;
            };
            if (!owner.acquire(&other_owner) || indexed->readSavedCache({}, false, crypto, saved) != DownloadRecoveryRead::Busy ||
                !owner.heldBy(&other_owner)) return 358;
            owner.release(&other_owner);
            if (indexed->readSavedCache({}, false, crypto, saved) != DownloadRecoveryRead::Pending || !owner.heldBy(indexed.get())) return 359;
            if (indexed->readSavedCache({record.id.bytes.data(), 32}, true, crypto, saved) != DownloadRecoveryRead::Busy ||
                indexed->readNextGeneration(record.id, next_generation) != DownloadRecoveryRead::Busy ||
                indexed->readRecovery({}, other_recovery) != DownloadRecoveryRead::Busy ||
                indexed->readDownload({key.data(), key.size()}, generation) != JournalWriteResult::Busy ||
                indexed->stopTask(task_id) != JournalWriteResult::Busy) return 360;
            indexed->releaseRead();
            if (owner.holder() || files != disk) return 361;
            if (read_saved(*indexed, {}, false, crypto) != DownloadRecoveryRead::Ready || owner.holder()) return 362;
            std::memset(frame, 0xa5, sizeof(frame));
            std::memset(verification, 0xa5, sizeof(verification));
            if (saved.id != record.id.bytes || saved.hash != record.hash.bytes || saved.revision != record.record.revision ||
                saved.latitude_e7 != record.record.latitude_e7 || std::strcmp(saved.name.data(), "Test")) return 363;
            const auto saved_id = saved.id;
            if (read_saved(*indexed, {saved_id.data(), 32}, false, crypto) != DownloadRecoveryRead::End || owner.holder()) return 364;
            if (read_saved(*indexed, {other_cache.bytes.data(), 32}, true, crypto) != DownloadRecoveryRead::End || owner.holder()) return 365;

            SavedCacheCatalog<FileDigest> catalog(*indexed, crypto);
            if (!owner.acquire(&other_owner) || catalog.advance() || !catalog.pending() || !owner.heldBy(&other_owner)) return 366;
            owner.release(&other_owner);
            if (!catalog.advance() || !catalog.reading() || !owner.heldBy(indexed.get())) return 367;
            step_bytes = 0;
            catalog.reset();
            if (step_bytes || !owner.heldBy(indexed.get())) return 368;
            catalog.releaseRead();
            if (owner.holder()) return 369;
            const auto advance_catalog = [&]()
            {
                for (unsigned i = 0; i < 65536 && catalog.pending(); ++i)
                {
                    step_bytes = 0;
                    catalog.advance();
                    if (step_bytes > 512) return false;
                }
                return !catalog.pending() && !catalog.error() && !owner.holder();
            };
            if (!advance_catalog()) return 370;
            ::ui::geocaching::Snapshot snapshot;
            ::ui::geocaching::Item item;
            catalog.snapshot(snapshot);
            step_bytes = 0;
            if (snapshot.count != 1 || !catalog.item(0, snapshot.generation, item) || !item.downloaded ||
                std::strcmp(item.name.data(), "Test") || step_bytes || files != disk) return 371;
            if (!catalog.requestPreview(1, 0, 2)) return 372;
            catalog.previewRow(0, record.id.bytes, record.hash.bytes);
            catalog.previewRow(1, other_cache.bytes, record.hash.bytes);
            if (!advance_catalog() || !catalog.contains(record.id.bytes, record.hash.bytes) ||
                !catalog.checked(other_cache.bytes, record.hash.bytes) || catalog.contains(other_cache.bytes, record.hash.bytes)) return 373;
            files.at(target)[0] ^= 1;
            catalog.reset();
            if (!advance_catalog() || catalog.contains(record.id.bytes, record.hash.bytes) || !catalog.checked(record.id.bytes, record.hash.bytes)) return 374;
            files = disk;
            catalog.requestWindow(0, 4);
            if (!advance_catalog()) return 375;
            catalog.snapshot(snapshot);
            if (snapshot.count != 1) return 376;

            auto make_saved_reader = [&](size_t frame_capacity, size_t verify_capacity, protocol::RecordCrypto& verifier)
            {
                return std::make_unique<IndexedDownloadStore>(volume, root, copy, roots[0], roots[1], owner, workspace,
                                                              frame, frame_capacity, incoming, sizeof(incoming), verification, verify_capacity, verifier);
            };
            {
                auto pinned = make_saved_reader(sizeof(frame), sizeof(verification), crypto);
                if (pinned->readSavedCache({}, false, crypto, saved) != DownloadRecoveryRead::Pending) return 377;
                const auto revision = root.revision;
                ++root.revision;
                const auto status = pinned->readSavedCache({}, false, crypto, saved);
                root.revision = revision;
                if (status != DownloadRecoveryRead::Invalid || !pinned->needsRecovery() || owner.holder()) return 378;
            }
            for (unsigned fault = 0; fault < 5; ++fault)
            {
                auto damaged = make_saved_reader(fault == 3 ? 24 : sizeof(frame), fault == 4 ? 16 : sizeof(verification), crypto);
                if (fault == 0) fail_read_once = true;
                if (fault == 1)
                {
                    auto changed_volume = volume;
                    changed_volume[0] ^= 1;
                    const auto changed_format = encodeVolumeHeader(changed_volume);
                    files["/trailmate/geocaching/.state/format.bin"] = {changed_format.begin(), changed_format.end()};
                }
                if (fault == 2)
                {
                    const auto journal = std::find_if(files.rbegin(), files.rend(), [](const auto& file)
                                                      { return file.first.find("/.state/journal/") != std::string::npos; });
                    if (journal == files.rend() || journal->second.empty()) return 379;
                    journal->second.back() ^= 1;
                }
                const auto damaged_disk = files;
                const auto status = read_saved(*damaged, {record.id.bytes.data(), 32}, true, crypto);
                const auto expected = fault == 0 ? DownloadRecoveryRead::IoError : fault == 1 ? DownloadRecoveryRead::VolumeChanged
                                                                               : fault == 2   ? DownloadRecoveryRead::Invalid
                                                                                              : DownloadRecoveryRead::WorkspaceTooSmall;
                if (status != expected || damaged->needsRecovery() != (fault < 3) || owner.holder() || files != damaged_disk) return 380 + fault;
                files = disk;
            }
            struct UnavailableCrypto : protocol::RecordCrypto
            {
                bool sha256(ByteView, uint8_t[32]) override { return false; }
                protocol::VerificationResult verifyEd25519(ByteView, ByteView, ByteView) override { return protocol::VerificationResult::CryptoUnavailable; }
            } unavailable;
            auto temporary = make_saved_reader(sizeof(frame), sizeof(verification), unavailable);
            if (read_saved(*temporary, {}, false, unavailable) != DownloadRecoveryRead::Unavailable || temporary->needsRecovery() || owner.holder() || files != disk) return 385;
        }
        if (scenario == 2)
        {
            if (!backup_seen || files.count(backup)) return 277;
            std::string history = "/trailmate/geocaching/.state/history/";
            for (const auto byte : record.hash.bytes)
            {
                history += hex[byte >> 4];
                history += hex[byte & 15];
            }
            history += ".gpx";
            if (!files.count(history) || files.at(history) != files.at(target)) return 278;
            interrupted.push_back({files, task_id, request_id, generation});
            auto missing_history = files;
            missing_history.erase(history);
            interrupted.push_back({std::move(missing_history), task_id, request_id, generation, false});
            auto changed_history = files;
            changed_history.at(history)[0] ^= 1;
            interrupted.push_back({std::move(changed_history), task_id, request_id, generation, false});
        }
    }
    // Recreate the root, store, port, digest and read buffers from SD alone at
    // staging, Prepared, and target-replacement boundaries.
    if (interrupted.size() < 3) return 258;
    for (const auto& interrupted_download : interrupted)
    {
        files = interrupted_download.disk;
        MutationView recovery_mutations[3];
        auto recovery = std::make_unique<SdIndexedRecovery<FileDigest>>(volume, roots[0], roots[1], frame, sizeof(frame),
                                                                        outgoing, sizeof(outgoing), recovery_mutations, 3);
        auto recovered = IndexedRecoveryStep::Working;
        for (unsigned i = 0; i < 16384 && recovered == IndexedRecoveryStep::Working; ++i)
        {
            step_bytes = 0;
            recovered = recovery->step();
            if (step_bytes > 512) return 259;
        }
        if (recovered != IndexedRecoveryStep::Restored || !recovery->selected(root, copy)) return 260;
        recovery.reset();
        // The reboot knows neither task ID nor request ID. Discover them from
        // the index, including Installed tasks awaiting history retention.
        uint8_t incoming[1024];
        IndexWorkspaceOwner owner;
        auto indexed = std::make_unique<IndexedDownloadStore>(volume, root, copy, roots[0], roots[1], owner, workspace,
                                                              frame, sizeof(frame), incoming, sizeof(incoming), verification, sizeof(verification), crypto);
        DownloadStore& downloads = *indexed;
        DownloadRecoveryRequest selected;
        int competing_owner;
        if (!owner.acquire(&competing_owner) || downloads.readRecovery({}, selected) != DownloadRecoveryRead::Busy ||
            !owner.heldBy(&competing_owner) || downloads.needsRecovery()) return 289;
        owner.release(&competing_owner);
        if (downloads.readRecovery({}, selected) != DownloadRecoveryRead::Pending || !owner.heldBy(indexed.get())) return 290;
        const std::array<uint8_t, 48> other_cursor{};
        if (downloads.readRecovery({other_cursor.data(), other_cursor.size()}, selected) != DownloadRecoveryRead::Busy ||
            downloads.readDownload({other_cursor.data(), other_cursor.size()}, 1) != JournalWriteResult::Busy ||
            downloads.stopTask(interrupted_download.task) != JournalWriteResult::Busy) return 291;
        downloads.releaseRead();
        if (owner.holder() || downloads.needsRecovery() || files != interrupted_download.disk) return 292;
        if (&interrupted_download == &interrupted.front())
        {
            const auto make_reader = [&](size_t capacity)
            {
                return std::make_unique<IndexedDownloadStore>(volume, root, copy, roots[0], roots[1], owner, workspace,
                                                              frame, capacity, incoming, sizeof(incoming), verification, sizeof(verification), crypto);
            };
            {
                auto cancelled = make_reader(sizeof(frame));
                if (cancelled->readRecovery({}, selected) != DownloadRecoveryRead::Pending) return 293;
            }
            if (owner.holder() || files != interrupted_download.disk) return 294;
            {
                auto pinned = make_reader(sizeof(frame));
                if (pinned->readRecovery({}, selected) != DownloadRecoveryRead::Pending) return 295;
                const auto revision = root.revision;
                ++root.revision;
                const auto changed = pinned->readRecovery({}, selected);
                root.revision = revision;
                if (changed != DownloadRecoveryRead::Invalid || !pinned->needsRecovery() || owner.holder()) return 296;
            }
            {
                auto pinned = make_reader(sizeof(frame));
                uint64_t next_generation = 0;
                if (pinned->readNextGeneration(record.id, next_generation) != DownloadRecoveryRead::Pending) return 325;
                const auto revision = root.revision;
                ++root.revision;
                const auto changed = pinned->readNextGeneration(record.id, next_generation);
                root.revision = revision;
                if (changed != DownloadRecoveryRead::Invalid || !pinned->needsRecovery() || owner.holder()) return 326;
            }
            for (unsigned fault = 0; fault < 4; ++fault)
            {
                auto damaged = make_reader(fault == 3 ? 24 : sizeof(frame));
                if (fault == 0) fail_read_once = true;
                if (fault == 1)
                {
                    auto changed_volume = volume;
                    changed_volume[0] ^= 1;
                    const auto changed_format = encodeVolumeHeader(changed_volume);
                    files["/trailmate/geocaching/.state/format.bin"] = {changed_format.begin(), changed_format.end()};
                }
                if (fault == 2)
                {
                    const auto journal = std::find_if(files.rbegin(), files.rend(), [](const auto& file)
                                                      { return file.first.find("/.state/journal/") != std::string::npos; });
                    if (journal == files.rend() || journal->second.empty()) return 297;
                    journal->second.back() ^= 1;
                }
                const auto damaged_disk = files;
                auto result = DownloadRecoveryRead::Pending;
                for (unsigned i = 0; i < 16384 && result == DownloadRecoveryRead::Pending; ++i)
                {
                    step_bytes = 0;
                    result = damaged->readRecovery({}, selected);
                    if (step_bytes > 512) return 298;
                }
                const auto expected = fault == 0 ? DownloadRecoveryRead::IoError : fault == 1 ? DownloadRecoveryRead::VolumeChanged
                                                                               : fault == 2   ? DownloadRecoveryRead::Invalid
                                                                                              : DownloadRecoveryRead::WorkspaceTooSmall;
                if (result != expected || damaged->needsRecovery() != (fault != 3) || owner.holder() || files != damaged_disk) return 299 + fault;
                files = interrupted_download.disk;
            }
            for (unsigned fault = 0; fault < 3; ++fault)
            {
                auto damaged = make_reader(fault == 2 ? 24 : sizeof(frame));
                if (fault == 0) fail_read_once = true;
                if (fault == 1)
                {
                    auto changed_volume = volume;
                    changed_volume[0] ^= 1;
                    const auto changed_format = encodeVolumeHeader(changed_volume);
                    files["/trailmate/geocaching/.state/format.bin"] = {changed_format.begin(), changed_format.end()};
                }
                const auto disk = files;
                auto status = DownloadRecoveryRead::Pending;
                uint64_t next_generation = 0;
                for (unsigned i = 0; i < 16384 && status == DownloadRecoveryRead::Pending; ++i)
                {
                    step_bytes = 0;
                    status = damaged->readNextGeneration(record.id, next_generation);
                    if (step_bytes > 512) return 327;
                }
                const auto expected = fault == 0 ? DownloadRecoveryRead::IoError : fault == 1 ? DownloadRecoveryRead::VolumeChanged
                                                                                              : DownloadRecoveryRead::WorkspaceTooSmall;
                if (status != expected || next_generation || damaged->needsRecovery() != (fault != 2) || owner.holder() || files != disk) return 328 + fault;
                files = interrupted_download.disk;
            }
        }
        const auto select_download = [&](ByteView after)
        {
            auto status = DownloadRecoveryRead::Pending;
            for (unsigned i = 0; i < 16384 && status == DownloadRecoveryRead::Pending; ++i)
            {
                step_bytes = 0;
                status = downloads.readRecovery(after, selected);
                if (step_bytes > 512) return DownloadRecoveryRead::Invalid;
            }
            return status;
        };
        if (select_download({}) != DownloadRecoveryRead::Ready || owner.holder() ||
            selected.task != interrupted_download.task || selected.identity.generation != interrupted_download.generation ||
            std::memcmp(selected.key.data() + 32, interrupted_download.request.bytes.data(), 16)) return 286;
        // The selector returned owned metadata, so install may immediately
        // reuse its frame and workspace. No view into the selector survives.
        std::memset(frame, 0xa5, sizeof(frame));
        Destination selected_local, selected_remote;
        RequestId selected_request;
        std::memcpy(selected_local.bytes.data(), selected.key.data(), 16);
        std::memcpy(selected_remote.bytes.data(), selected.key.data() + 16, 16);
        std::memcpy(selected_request.bytes.data(), selected.key.data() + 32, 16);
        auto port = std::make_unique<SdDownloadPort<FileDigest>>(*indexed, crypto, selected_local, selected.identity, selected.task, selected.created);
        auto result = port->resume(selected_remote, selected_request);
        for (unsigned i = 0; i < 16384 && result == DownloadOperationResult::Pending; ++i)
        {
            step_bytes = 0;
            result = port->poll();
            if (step_bytes > 512) return 261;
        }
        if (!interrupted_download.recoverable)
        {
            if (result != DownloadOperationResult::Rejected || owner.holder() || files != interrupted_download.disk) return 279;
            continue;
        }
        if (result != DownloadOperationResult::Complete || !files.count(target) || owner.holder()) return 262;
        const std::string gpx(files.at(target).begin(), files.at(target).end());
        if (gpx.find("<name>Test</name>") == std::string::npos) return 263;
        port.reset();
        const auto after = selected.key;
        if (select_download({after.data(), after.size()}) != DownloadRecoveryRead::End || owner.holder() || downloads.needsRecovery()) return 288;
        // The recovered device can immediately list/map the installed file
        // using only the newly loaded index and the public GPX read path.
        SavedCacheCatalog<FileDigest> catalog(downloads, crypto);
        for (unsigned i = 0; i < 65536 && catalog.pending(); ++i)
        {
            step_bytes = 0;
            catalog.advance();
            if (step_bytes > 512) return 386;
        }
        ::ui::geocaching::Snapshot snapshot;
        ::ui::geocaching::Item item;
        catalog.snapshot(snapshot);
        if (catalog.pending() || catalog.error() || snapshot.count != 1 || !catalog.item(0, snapshot.generation, item) ||
            !item.downloaded || item.id != record.id.bytes || item.revision_hash != record.hash.bytes || owner.holder()) return 387;
    }
    // Exercise the real last reservable generation, then reject wraparound
    // without treating an exhausted counter as damaged storage.
    {
        CacheHeadView head;
        head.current_hash = {record.hash.bytes.data(), 32};
        head.install_generation = UINT64_MAX - 1;
        head.highest_seen_revision = record.record.revision;
        uint8_t head_bytes[64], incoming[1024];
        size_t head_size = 0;
        const ByteView cache_key{record.id.bytes.data(), 32};
        if (!encodeCacheHead(cache_key, head, head_bytes, sizeof(head_bytes), head_size)) return 331;
        const MutationView mutation{2, cache_key, {head_bytes, head_size}, false};
        auto seed = std::make_unique<SdIndexedCommit>(volume);
        if (!seed->begin(root, copy, &mutation, 1, frame, sizeof(frame), roots[1 - copy])) return 332;
        auto seeded = IndexedCommitStep::Working;
        for (unsigned i = 0; i < 16384 && seeded == IndexedCommitStep::Working; ++i) seeded = seed->step();
        if (seeded != IndexedCommitStep::Verified || !seed->committed(root)) return 333;
        copy = 1 - copy;
        seed.reset();
        IndexWorkspaceOwner owner;
        IndexedDownloadStore store(volume, root, copy, roots[0], roots[1], owner, workspace,
                                   frame, sizeof(frame), incoming, sizeof(incoming), verification, sizeof(verification), crypto);
        uint64_t generation = 0;
        const auto read_generation = [&]()
        {
            auto status = DownloadRecoveryRead::Pending;
            for (unsigned i = 0; i < 16384 && status == DownloadRecoveryRead::Pending; ++i)
            {
                step_bytes = 0;
                status = store.readNextGeneration(record.id, generation);
                if (step_bytes > 512) return DownloadRecoveryRead::Invalid;
            }
            return status;
        };
        if (read_generation() != DownloadRecoveryRead::Ready || generation != UINT64_MAX || !owner.heldBy(&store)) return 334;
        auto final_task = task;
        final_task[0] ^= 0x67;
        auto final_request = id;
        final_request.bytes[0] ^= 0x67;
        SdDownloadPort<FileDigest> port(store, crypto, {}, {record.id, record.hash, generation}, final_task, {});
        DownloadClient client(port, crypto);
        if (!client.begin({}, final_request, summary, generation)) return 335;
        for (unsigned i = 0; i < 16384 && client.phase() == DownloadPhase::Submitting; ++i) client.advance();
        if (client.phase() != DownloadPhase::Waiting || owner.holder()) return 336;
        const auto before_exhausted = files;
        if (read_generation() != DownloadRecoveryRead::Ready || generation || store.needsRecovery() || !owner.heldBy(&store) ||
            store.persistNewTask({}, {}, id, task, 2, {request, request_size}, {},
                                 {{record.id.bytes.data(), 32}, {record.hash.bytes.data(), 32}, 1}) != JournalWriteResult::StateRejected ||
            store.needsRecovery() || files != before_exhausted) return 337;
        store.releaseRead();
        if (owner.holder()) return 338;
    }
    files.clear();
    index_directories.clear();
    return 0;
}

int checkIndexedAuthorReservation(const char* path)
{
    using namespace ::geocaching;
    using namespace ::geocaching::storage;
    struct Crypto : protocol::RecordCrypto
    {
        bool available = true;
        bool sha256(ByteView bytes, uint8_t out[32]) override
        {
            if (!available) return false;
            chat::reticulum::fullHash(bytes.data, bytes.size, out);
            return true;
        }
        protocol::VerificationResult verifyEd25519(ByteView key, ByteView signature, ByteView message) override
        {
            return ed25519_verify(signature.data, message.data, message.size, key.data) ? protocol::VerificationResult::Valid : protocol::VerificationResult::InvalidSignature;
        }
    } crypto;
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> response((std::istreambuf_iterator<char>(input)), {});
    protocol::CmpReader envelope({response.data(), response.size()});
    size_t fields = 0;
    uint64_t number = 0;
    ByteView id_bytes;
    if (!envelope.array(fields, 6) || !envelope.unsignedInteger(number) || !envelope.unsignedInteger(number) ||
        !envelope.unsignedInteger(number) || !envelope.binary(id_bytes, 16) || id_bytes.size != 16) return 1;
    RequestId id;
    std::memcpy(id.bytes.data(), id_bytes.data, 16);
    protocol::GetResponseView reply;
    uint8_t verification[1024], frame[2048], outgoing[2048], receipt[512], signing[2048], draft_bytes[2048];
    protocol::VerifiedRecordView verified;
    if (!protocol::decodeGetResponse({response.data(), response.size()}, id, 8192, reply) ||
        protocol::verifyGeocache(reply.signed_cache, crypto, verification, sizeof(verification), verified) != protocol::VerificationResult::Valid) return 2;
    protocol::CmpReader signed_record(reply.signed_cache);
    ByteView encoded, signature;
    if (!signed_record.array(fields, 2) || !signed_record.binary(encoded, 4096) || !signed_record.binary(signature, 64)) return 3;
    files.clear();
    index_directories.clear();
    VolumeInstance volume{};
    const auto format = encodeVolumeHeader(volume);
    files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
    IndexRootBytes roots[2];
    SdIndexInitialize initialize(volume);
    if (!initialize.begin(roots[0])) return 4;
    auto initialized = IndexRootWriteStep::Working;
    for (unsigned i = 0; i < 128 && initialized == IndexRootWriteStep::Working; ++i) initialized = initialize.step();
    if (initialized != IndexRootWriteStep::Verified) return 5;
    roots[1] = roots[0];
    IndexRootView root;
    if (!decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, root)) return 6;
    unsigned copy = 0;
    IndexWorkspaceOwner owner;
    QueuedRequestWorkspace workspace(outgoing, sizeof(outgoing));
    IndexedPublicationStore store(volume, root, copy, roots[0], roots[1], owner, workspace, frame, sizeof(frame),
                                  receipt, sizeof(receipt), verification, sizeof(verification), crypto);
    const auto finish = [&](JournalWriteResult status)
    {
        for (unsigned i = 0; i < 32768 && status == JournalWriteResult::InProgress; ++i)
        {
            step_bytes = 0;
            status = store.stepCommit();
            if (step_bytes > 512) return JournalWriteResult::Invalid;
        }
        return status;
    };
    const auto& record = verified.record;
    DraftView draft;
    draft.has_coordinates = true;
    draft.latitude_e7 = record.latitude_e7;
    draft.longitude_e7 = record.longitude_e7;
    draft.name = record.name;
    draft.description = record.description;
    draft.hint = record.hint;
    draft.state = static_cast<uint8_t>(record.state);
    draft.difficulty_x2 = record.difficulty_x2;
    draft.terrain_x2 = record.terrain_x2;
    draft.container_size = static_cast<uint8_t>(record.container_size);
    size_t size = 0;
    const auto key = record.creation_nonce;
    if (!encodeDraft(key, draft, draft_bytes, sizeof(draft_bytes), size) ||
        finish(store.saveDraft(key, {draft_bytes, size}, 0)) != JournalWriteResult::Verified || root.sequence != 1) return 7;
    const auto unbound = files;
    if (finish(store.bindDraftAuthor(key, 2, record.author_public_key, signing, sizeof(signing))) != JournalWriteResult::StateRejected ||
        files != unbound || store.needsRecovery() || owner.holder()) return 8;
    // Device publication reads one draft, releases the shared read frame,
    // then saves the binding through the common publication-store contract.
    PublicationStore& publication_store = store;
    ByteView binding_value;
    auto binding_read = DraftReadResult::Pending;
    for (unsigned i = 0; i < 4096 && binding_read == DraftReadResult::Pending; ++i)
        binding_read = publication_store.readDraft(key, binding_value);
    if (binding_read != DraftReadResult::Ready) return 67;
    const std::vector<uint8_t> binding_copy(binding_value.data, binding_value.data + binding_value.size);
    publication_store.releaseDraftRead();
    std::memset(frame, 0xa5, sizeof(frame));
    DraftView binding_draft;
    if (!decodeDraft(key, {binding_copy.data(), binding_copy.size()}, binding_draft) || binding_draft.author.size ||
        binding_draft.generation != 1) return 68;
    binding_draft.author = record.author_public_key;
    ++binding_draft.generation;
    if (!encodeDraft(key, binding_draft, signing, sizeof(signing), size)) return 69;
    const auto binding_begin = publication_store.saveDraft(key, {signing, size}, 1);
    if (binding_begin != JournalWriteResult::InProgress || publication_store.inputConsumed()) return 70;
    if (finish(binding_begin) != JournalWriteResult::Verified || root.sequence != 2 || owner.holder()) return 9;
    std::memset(signing, 0xa5, sizeof(signing));
    const auto bound = files;
    const auto bound_roots = std::array<IndexRootBytes, 2>{roots[0], roots[1]};
    const auto bound_copy = copy;
    if (finish(store.bindDraftAuthor(key, 2, record.author_public_key, signing, sizeof(signing))) != JournalWriteResult::Verified || files != bound) return 10;
    PublicationHistory history;
    const auto read_history = [&](uint64_t generation)
    {
        auto result = DraftReadResult::Pending;
        for (unsigned i = 0; i < 65536 && result == DraftReadResult::Pending; ++i)
        {
            step_bytes = 0;
            result = publication_store.readPublicationHistory(key, generation, verified.id, record.author_public_key, history);
            if (step_bytes > 512) return DraftReadResult::Invalid;
        }
        return result;
    };
    if (read_history(2) != DraftReadResult::Ready || history.latest_revision || history.confirmed_revision ||
        owner.holder() || files != bound) return 71;
    StoredTime issued;
    issued.has_utc = true;
    issued.utc_seconds = record.updated_at;
    int competing_owner = 0;
    if (!owner.acquire(&competing_owner) ||
        store.reserveDraftUnsignedRecord(key, 2, encoded, crypto, signing, sizeof(signing), issued) != JournalWriteResult::Busy ||
        store.commitPending() || files != bound) return 11;
    owner.release(&competing_owner);
    struct SigningTransport
    {
        IndexedPublicationStore& store;
        IndexWorkspaceOwner& owner;
        ByteView author, canonical, signed_cache;
        unsigned calls = 0;
        bool getGeocachingAuthorKey(uint8_t out[64])
        {
            std::memcpy(out, author.data, 64);
            return true;
        }
        bool signGeocachingRecord(ByteView value, uint8_t*, size_t, uint8_t* output, size_t capacity, size_t& written)
        {
            ++calls;
            if (store.commitPending() || store.needsRecovery() || store.committedSequence() != 3 || owner.holder() ||
                value.size != canonical.size || std::memcmp(value.data, canonical.data, value.size) || capacity < signed_cache.size) return false;
            std::memcpy(output, signed_cache.data, signed_cache.size);
            written = signed_cache.size;
            return true;
        }
    } transport{store, owner, record.author_public_key, encoded, reply.signed_cache};
    SdAuthorIssuePort<SigningTransport> port(transport, store, crypto, issued, key, 2);
    AuthorIssue issue(port);
    if (!issue.begin(encoded, signing, sizeof(signing))) return 17;
    // A competing query/dispatch operation may hold the session lease for many
    // slices. The issue job must wait without signing or mutating its state.
    if (!owner.acquire(&competing_owner)) return 18;
    for (unsigned i = 0; i < 8; ++i) issue.advance();
    if (issue.phase() != AuthorIssuePhase::Reserve || transport.calls || files != bound) return 19;
    owner.release(&competing_owner);
    for (unsigned i = 0; i < 32768 && issue.phase() != AuthorIssuePhase::Signed && issue.phase() != AuthorIssuePhase::Failed; ++i)
    {
        step_bytes = 0;
        issue.advance();
        if (step_bytes > 512) return 20;
    }
    if (issue.phase() != AuthorIssuePhase::Signed || transport.calls != 1 || root.sequence != 3 || owner.holder() ||
        issue.signedRecord().size != reply.signed_cache.size ||
        std::memcmp(issue.signedRecord().data, reply.signed_cache.data, reply.signed_cache.size)) return 12;
    const auto reserved = files;
    if (read_history(3) != DraftReadResult::Ready || history.latest_revision != record.revision || history.confirmed_revision ||
        history.latest_hash.bytes != verified.hash.bytes || history.created_at != issued.utc_seconds ||
        history.latest_time.utc_seconds != issued.utc_seconds || files != reserved) return 72;
    if (read_history(2) != DraftReadResult::NotFound || store.needsRecovery() || owner.holder()) return 73;
    if (finish(store.reserveDraftUnsignedRecord(key, 3, encoded, crypto, signing, sizeof(signing), issued)) != JournalWriteResult::Verified ||
        root.sequence != 3 || files != reserved) return 13;
    if (finish(store.reserveDraftUnsignedRecord(key, 2, encoded, crypto, signing, sizeof(signing), issued)) != JournalWriteResult::StateRejected ||
        files != reserved || store.needsRecovery()) return 14;
    SdIndexGet get(volume);
    if (!get.begin(root, 4, key, frame, sizeof(frame))) return 15;
    auto status = IndexGetStep::Working;
    for (unsigned i = 0; i < 4096 && status == IndexGetStep::Working; ++i) status = get.step();
    DraftView frozen;
    if (status != IndexGetStep::Ready || !decodeDraft(key, get.value(), frozen) || frozen.generation != 3 ||
        frozen.base_hash.size != 32 || std::memcmp(frozen.base_hash.data, verified.hash.bytes.data(), 32) ||
        frozen.author.size != 64 || std::memcmp(frozen.author.data, record.author_public_key.data, 64)) return 16;
    draft.generation = 4;
    draft.name = "Edited on device";
    if (!encodeDraft(key, draft, draft_bytes, sizeof(draft_bytes), size) ||
        finish(store.editDraft(key, draft_bytes, size, sizeof(draft_bytes), 3)) != JournalWriteResult::StateRejected ||
        files != reserved || store.needsRecovery()) return 37;
    Destination local, remote, wrong_source;
    local.bytes.fill(0x41);
    remote.bytes.fill(0x42);
    wrong_source.bytes.fill(0x43);
    std::array<uint8_t, 16> task{};
    task.fill(0x44);
    SdPublishPort publish_port(store, crypto, local, verified.id, verified.hash, task, issued);
    PublishAttempt publication(publish_port, crypto);
    if (!publication.begin(remote, id, reply.signed_cache, signing, sizeof(signing))) return 24;
    std::memset(signing, 0xa5, sizeof(signing));
    for (unsigned i = 0; i < 32768 && publication.phase() == PublishAttemptPhase::Submitting; ++i) publication.advance();
    if (publication.phase() != PublishAttemptPhase::Waiting || root.sequence != 4) return 25;
    PublicationRecoveryFilter recovery_filter;
    recovery_filter.local = local;
    PublicationRecoveryView recovered_publication;
    const auto select_publication = [&](const PublicationRecoveryFilter& filter)
    {
        auto result = DraftReadResult::Pending;
        for (unsigned i = 0; i < 65536 && result == DraftReadResult::Pending; ++i)
        {
            step_bytes = 0;
            result = store.readPublicationRecovery(filter, recovered_publication);
            if (step_bytes > 512) return DraftReadResult::Invalid;
        }
        return result;
    };
    if (!owner.acquire(&competing_owner) || select_publication(recovery_filter) != DraftReadResult::Busy ||
        !owner.heldBy(&competing_owner)) return 89;
    owner.release(&competing_owner);
    if (store.readPublicationRecovery(recovery_filter, recovered_publication) != DraftReadResult::Pending ||
        store.stopTask(task) != JournalWriteResult::Busy) return 90;
    auto another_filter = recovery_filter;
    another_filter.local = wrong_source;
    if (store.readPublicationRecovery(another_filter, recovered_publication) != DraftReadResult::Busy) return 91;
    store.releaseDraftRead();
    if (owner.holder() || select_publication(another_filter) != DraftReadResult::NotFound || store.needsRecovery()) return 92;
    struct RecoveryDigest
    {
        std::vector<uint8_t> bytes;
        void update(const uint8_t* data, size_t count) { bytes.insert(bytes.end(), data, data + count); }
        bool finalize(uint8_t* hash, size_t size)
        {
            if (size != 32) return false;
            chat::reticulum::fullHash(bytes.data(), bytes.size(), hash);
            return true;
        }
    };
    const auto resume_from_disk = [&](const PublicationRecoveryFilter& filter, bool confirmed)
    {
        const auto disk = files;
        IndexRootBytes recovered_roots[2];
        IndexRootView recovered_root;
        unsigned recovered_copy = 0;
        MutationView mutations[3];
        auto recovery = std::make_unique<SdIndexedRecovery<RecoveryDigest>>(volume, recovered_roots[0], recovered_roots[1],
                                                                            frame, sizeof(frame), outgoing, sizeof(outgoing), mutations, 3);
        auto recovered = IndexedRecoveryStep::Working;
        for (unsigned i = 0; i < 65536 && recovered == IndexedRecoveryStep::Working; ++i)
        {
            step_bytes = 0;
            recovered = recovery->step();
            if (step_bytes > 512) return false;
        }
        if (recovered != IndexedRecoveryStep::Restored || !recovery->selected(recovered_root, recovered_copy)) return false;
        recovery.reset();
        IndexWorkspaceOwner recovered_owner;
        IndexedPublicationStore restored_store(volume, recovered_root, recovered_copy, recovered_roots[0], recovered_roots[1], recovered_owner,
                                               workspace, frame, sizeof(frame), receipt, sizeof(receipt), verification, sizeof(verification), crypto);
        PublicationRecoveryView selected;
        auto read = DraftReadResult::Pending;
        for (unsigned i = 0; i < 65536 && read == DraftReadResult::Pending; ++i)
        {
            step_bytes = 0;
            read = restored_store.readPublicationRecovery(filter, selected);
            if (step_bytes > 512) return false;
        }
        if (read != DraftReadResult::Ready || selected.confirmed != confirmed || selected.cache.bytes != verified.id.bytes ||
            selected.hash.bytes != verified.hash.bytes || selected.task != task ||
            std::memcmp(selected.key.data() + 32, id.bytes.data(), 16) || !recovered_owner.heldBy(&restored_store)) return false;
        std::vector<uint8_t> response_copy;
        if (selected.response.size) response_copy.assign(selected.response.data, selected.response.data + selected.response.size);
        std::vector<uint8_t> corrupt_request(selected.request.data, selected.request.data + selected.request.size);
        corrupt_request.back() ^= 1;
        SdPublishPort invalid_port(restored_store, crypto, local, selected.cache, selected.hash, selected.task, selected.created);
        PublishAttempt invalid_attempt(invalid_port, crypto);
        if (!invalid_port.attachRestoredRequest(remote, id) ||
            invalid_attempt.resume(remote, id, {corrupt_request.data(), corrupt_request.size()}, signing, sizeof(signing), selected.cache, selected.hash, selected.response)) return false;
        SdPublishPort restored_port(restored_store, crypto, local, selected.cache, selected.hash, selected.task, selected.created);
        PublishAttempt restored(restored_port, crypto);
        if (!restored_port.attachRestoredRequest(remote, id) ||
            !restored.resume(remote, id, selected.request, signing, sizeof(signing), selected.cache, selected.hash, selected.response)) return false;
        restored_store.releaseDraftRead();
        std::memset(frame, 0xa5, sizeof(frame));
        std::memset(signing, 0x5a, sizeof(signing));
        if (restored.phase() != (confirmed ? PublishAttemptPhase::Confirmed : PublishAttemptPhase::Waiting) || recovered_owner.holder()) return false;
        if (confirmed && !restored.accept(remote, {response_copy.data(), response_copy.size()})) return false;
        return files == disk && recovered_root.sequence == root.sequence && !restored_store.commitPending();
    };
    if (!resume_from_disk(recovery_filter, false)) return 93;
    uint8_t acknowledgement[512];
    protocol::CmpWriter writer(acknowledgement, sizeof(acknowledgement));
    if (!writer.array(6) || !writer.unsignedInteger(1) || !writer.unsignedInteger(1) || !writer.unsignedInteger(1) ||
        !writer.binary({id.bytes.data(), 16}) || !writer.unsignedInteger(200) || !writer.array(7) ||
        !writer.binary({verified.id.bytes.data(), 32}) || !writer.unsignedInteger(record.revision) ||
        !writer.binary({verified.hash.bytes.data(), 32}) || !writer.unsignedInteger(0) || !writer.unsignedInteger(record.revision) ||
        !writer.binary({verified.hash.bytes.data(), 32}) || !writer.unsignedInteger(static_cast<uint8_t>(record.state))) return 26;
    const std::vector<uint8_t> durable_ack(acknowledgement, acknowledgement + writer.size());
    if (publication.accept(wrong_source, {acknowledgement, writer.size()}) || publication.phase() != PublishAttemptPhase::Waiting) return 27;
    if (publication.accept(remote, {acknowledgement, writer.size()}) || publication.phase() != PublishAttemptPhase::Committing) return 28;
    std::memset(acknowledgement, 0x5a, sizeof(acknowledgement));
    for (unsigned i = 0; i < 32768 && publication.phase() == PublishAttemptPhase::Committing; ++i)
    {
        step_bytes = 0;
        publication.advance();
        if (step_bytes > 512) return 29;
    }
    if (publication.phase() != PublishAttemptPhase::Confirmed || root.sequence != 5) return 30;
    const auto confirmed_files = files;
    if (select_publication(recovery_filter) != DraftReadResult::NotFound || owner.holder()) return 94;
    auto exact_publication = recovery_filter;
    exact_publication.has_cache = exact_publication.has_hash = exact_publication.has_remote = true;
    exact_publication.cache = verified.id;
    exact_publication.hash = verified.hash;
    exact_publication.remote = remote;
    if (!resume_from_disk(exact_publication, true)) return 95;
    if (finish(store.commitPublishResult(local, remote, id, {durable_ack.data(), durable_ack.size()}, crypto)) != JournalWriteResult::Verified ||
        root.sequence != 5 || files != confirmed_files) return 31;
    RequestId cancelled_id = id;
    cancelled_id.bytes[0] ^= 1;
    task[0] ^= 1;
    SdPublishPort cancel_port(store, crypto, local, verified.id, verified.hash, task, issued);
    PublishAttempt cancelled(cancel_port, crypto);
    if (!cancelled.begin(remote, cancelled_id, reply.signed_cache, signing, sizeof(signing))) return 32;
    for (unsigned i = 0; i < 32768 && cancelled.phase() == PublishAttemptPhase::Submitting; ++i) cancelled.advance();
    if (cancelled.phase() != PublishAttemptPhase::Waiting || root.sequence != 6 || !owner.acquire(&competing_owner)) return 33;
    const auto waiting_files = files;
    owner.release(&competing_owner);
    // A pending duplicate must not hide the already confirmed exact version.
    if (select_publication(exact_publication) != DraftReadResult::Ready || !recovered_publication.confirmed ||
        std::memcmp(recovered_publication.key.data() + 32, id.bytes.data(), 16)) return 96;
    store.releaseDraftRead();
    if (select_publication(recovery_filter) != DraftReadResult::Ready || recovered_publication.confirmed ||
        std::memcmp(recovered_publication.key.data() + 32, cancelled_id.bytes.data(), 16)) return 97;
    store.releaseDraftRead();
    auto excluded = exact_publication;
    excluded.hash.bytes[0] ^= 1;
    if (select_publication(excluded) != DraftReadResult::NotFound || owner.holder()) return 98;
    excluded = exact_publication;
    excluded.remote = wrong_source;
    if (select_publication(excluded) != DraftReadResult::NotFound || !owner.acquire(&competing_owner)) return 99;
    if (!cancelled.cancel()) return 34;
    for (unsigned i = 0; i < 8; ++i) cancelled.advance();
    if (cancelled.phase() != PublishAttemptPhase::Cancelling || store.commitPending() || files != waiting_files ||
        !owner.heldBy(&competing_owner)) return 35;
    owner.release(&competing_owner);
    for (unsigned i = 0; i < 32768 && cancelled.phase() == PublishAttemptPhase::Cancelling; ++i) cancelled.advance();
    if (cancelled.phase() != PublishAttemptPhase::Cancelled || root.sequence != 7 || store.needsRecovery()) return 36;
    if (select_publication(recovery_filter) != DraftReadResult::NotFound || owner.holder() || store.needsRecovery()) return 100;
    {
        PublicationRecoveryView candidate;
        std::array<uint8_t, 48> candidate_key{};
        std::memcpy(candidate_key.data(), local.bytes.data(), 16);
        std::memcpy(candidate_key.data() + 16, remote.bytes.data(), 16);
        std::memcpy(candidate_key.data() + 32, id.bytes.data(), 16);
        OutgoingView child;
        child.task_id = {task.data(), task.size()};
        child.continue_intent = true;
        TaskView parent;
        parent.kind = 1;
        parent.continue_intent = true;
        parent.cache_id = {verified.id.bytes.data(), 32};
        parent.revision_hash = {verified.hash.bytes.data(), 32};
        if (publicationRecoveryCandidate(recovery_filter, {candidate_key.data(), 48}, child, parent, candidate) != PublicationRecoveryResult::Invalid) return 101;
        parent.request_count = 1;
        parent.requests[0] = {candidate_key.data(), 48};
        if (publicationRecoveryCandidate(recovery_filter, {candidate_key.data(), 48}, child, parent, candidate) != PublicationRecoveryResult::Ready) return 102;
        parent.state = 3;
        if (publicationRecoveryCandidate(recovery_filter, {candidate_key.data(), 48}, child, parent, candidate) != PublicationRecoveryResult::None) return 103;
        parent.state = 0;
        child.continue_intent = false;
        if (publicationRecoveryCandidate(recovery_filter, {candidate_key.data(), 48}, child, parent, candidate) != PublicationRecoveryResult::None) return 104;
    }
    ByteView read_value;
    if (!owner.acquire(&competing_owner) || store.readDraft(key, read_value) != DraftReadResult::Busy || read_value.size) return 38;
    owner.release(&competing_owner);
    if (store.readDraft(key, read_value) != DraftReadResult::Pending || !owner.heldBy(&store)) return 39;
    store.releaseDraftRead();
    if (owner.holder() || store.needsRecovery()) return 40;
    auto read_result = DraftReadResult::Pending;
    for (unsigned i = 0; i < 4096 && read_result == DraftReadResult::Pending; ++i)
    {
        step_bytes = 0;
        read_result = store.readDraft(key, read_value);
        if (step_bytes > 512) return 41;
    }
    if (read_result != DraftReadResult::Ready || !decodeDraft(key, read_value, frozen) || frozen.generation != 3 ||
        !owner.heldBy(&store)) return 42;
    const std::vector<uint8_t> read_snapshot(read_value.data, read_value.data + read_value.size);
    if (!encodeDraft(key, draft, draft_bytes, sizeof(draft_bytes), size) ||
        store.editDraft(key, draft_bytes, size, sizeof(draft_bytes), 3) != JournalWriteResult::Busy) return 43;
    store.releaseDraftRead();
    if (finish(store.editDraft(key, draft_bytes, size, sizeof(draft_bytes), 3)) != JournalWriteResult::Verified || root.sequence != 8) return 44;
    std::memset(draft_bytes, 0xa5, sizeof(draft_bytes));
    read_result = DraftReadResult::Pending;
    for (unsigned i = 0; i < 4096 && read_result == DraftReadResult::Pending; ++i) read_result = store.readDraft(key, read_value);
    if (read_result != DraftReadResult::Ready || !decodeDraft(key, read_value, frozen) || frozen.generation != 4 ||
        frozen.name != "Edited on device" || frozen.author.size != 64 || frozen.base_hash.size != 32 ||
        std::memcmp(frozen.author.data, record.author_public_key.data, 64) ||
        std::memcmp(frozen.base_hash.data, verified.hash.bytes.data(), 32)) return 45;
    store.releaseDraftRead();
    if (!decodeDraft(key, {read_snapshot.data(), read_snapshot.size()}, frozen) || frozen.generation != 3 || frozen.name != record.name) return 46;
    const auto edited_files = files;
    if (!encodeDraft(key, draft, draft_bytes, sizeof(draft_bytes), size) ||
        finish(store.editDraft(key, draft_bytes, size, sizeof(draft_bytes), 3)) != JournalWriteResult::StateRejected ||
        files != edited_files || store.needsRecovery()) return 47;
    IndexedPublicationStore limited(volume, root, copy, roots[0], roots[1], owner, workspace, frame, 24,
                                    receipt, sizeof(receipt), verification, sizeof(verification), crypto);
    read_result = DraftReadResult::Pending;
    for (unsigned i = 0; i < 4096 && read_result == DraftReadResult::Pending; ++i) read_result = limited.readDraft(key, read_value);
    if (read_result != DraftReadResult::WorkspaceTooSmall || limited.needsRecovery() || owner.holder() || files != edited_files) return 48;
    DraftCatalogPage catalog;
    const auto read_catalog = [&](size_t offset)
    {
        auto result = DraftReadResult::Pending;
        for (unsigned i = 0; i < 65536 && result == DraftReadResult::Pending; ++i)
        {
            step_bytes = 0;
            result = store.readDraftCatalog(offset, crypto, catalog);
            if (step_bytes > 512) return DraftReadResult::Invalid;
        }
        return result;
    };
    if (store.readDraftCatalog(0, crypto, catalog) != DraftReadResult::Pending ||
        store.stopTask(task) != JournalWriteResult::Busy) return 49;
    store.releaseDraftRead();
    if (owner.holder() || store.needsRecovery() || read_catalog(0) != DraftReadResult::Ready || catalog.total != 1 || catalog.count != 1) return 50;
    const auto& projection = catalog.rows[0];
    if (projection.generation != 4 || std::strcmp(projection.name.data(), "Edited on device") ||
        projection.publication.latest_revision != 1 || projection.publication.confirmed_revision != 1 ||
        !projection.publication.base_retained || !projection.publication.local_changes || projection.publication.pending ||
        !projection.publication.stopped || files != edited_files) return 51;
    DraftView original = draft;
    original.name = record.name;
    DraftCatalogEntry unchanged;
    bool matching = false;
    if (!describeDraft(key, original, crypto, unchanged) || !draftContentMatches(unchanged, record, crypto, matching) || !matching) return 56;
    auto changed_text = record;
    changed_text.description = "Different description, same name and coordinates";
    if (!draftContentMatches(unchanged, changed_text, crypto, matching) || matching) return 57;
    changed_text = record;
    changed_text.hint = "Different hint, same description";
    if (!draftContentMatches(unchanged, changed_text, crypto, matching) || matching) return 58;
    // More rows than either screen can display. A window never grows with the
    // catalog, and offsets need not align to four (the smaller screen uses 3).
    for (uint8_t i = 1; i <= 8; ++i)
    {
        std::array<uint8_t, 16> sibling;
        std::memcpy(sibling.data(), key.data, sibling.size());
        sibling[0] ^= i;
        DraftView extra;
        extra.name = "Another local draft";
        if (!encodeDraft({sibling.data(), sibling.size()}, extra, draft_bytes, sizeof(draft_bytes), size) ||
            finish(store.editDraft({sibling.data(), sibling.size()}, draft_bytes, size, sizeof(draft_bytes), 0)) != JournalWriteResult::Verified) return 52;
    }
    std::set<std::array<uint8_t, 16>> seen;
    for (size_t offset : {size_t(0), size_t(4), size_t(8)})
    {
        if (read_catalog(offset) != DraftReadResult::Ready || catalog.offset != offset || catalog.total != 9 ||
            catalog.count != std::min(size_t(4), size_t(9) - offset) || owner.holder()) return 53;
        for (size_t i = 0; i < catalog.count; ++i)
            if (!seen.insert(catalog.rows[i].id).second) return 54;
    }
    if (seen.size() != 9 || read_catalog(3) != DraftReadResult::Ready || catalog.count != 4 ||
        read_catalog(SIZE_MAX) != DraftReadResult::Ready || catalog.total != 9 || catalog.count) return 55;
    const auto before_history = files;
    if (!owner.acquire(&competing_owner) || read_history(4) != DraftReadResult::Busy || !owner.heldBy(&competing_owner)) return 74;
    owner.release(&competing_owner);
    if (store.readPublicationHistory(key, 4, verified.id, record.author_public_key, history) != DraftReadResult::Pending ||
        store.readDraft(key, read_value) != DraftReadResult::Busy || store.stopTask(task) != JournalWriteResult::Busy) return 75;
    PublicationHistory other_history;
    if (store.readPublicationHistory(key, 4, verified.id, record.author_public_key, other_history) != DraftReadResult::Busy) return 76;
    store.releaseDraftRead();
    if (owner.holder() || read_history(4) != DraftReadResult::Ready || history.latest_revision != 1 ||
        history.confirmed_revision != 1 || files != before_history || owner.holder()) return 77;
    crypto.available = false;
    if (read_history(4) != DraftReadResult::Unavailable || store.needsRecovery() || owner.holder() ||
        read_catalog(0) != DraftReadResult::Unavailable || store.needsRecovery() || owner.holder()) return 80;
    crypto.available = true;
    if (read_history(4) != DraftReadResult::Ready || files != before_history) return 81;
    auto successor = record;
    successor.name = "Edited on device";
    successor.revision = 2;
    successor.previous_hash = {verified.hash.bytes.data(), verified.hash.bytes.size()};
    successor.updated_at += 1;
    StoredTime successor_time = issued;
    successor_time.utc_seconds = successor.updated_at;
    size_t successor_size = 0;
    if (!protocol::encodeGeocacheRecord(successor, draft_bytes, sizeof(draft_bytes), successor_size) ||
        finish(store.reserveDraftUnsignedRecord(key, 4, {draft_bytes, successor_size}, crypto, signing, sizeof(signing), successor_time)) != JournalWriteResult::Verified) return 78;
    GeocacheId successor_id;
    RevisionHash successor_hash;
    if (protocol::deriveGeocacheHashes({draft_bytes, successor_size}, crypto, verification, sizeof(verification), successor_id, successor_hash) != protocol::VerificationResult::Valid ||
        read_history(5) != DraftReadResult::Ready || history.latest_revision != 2 || history.confirmed_revision != 1 ||
        history.previous_hash.bytes != verified.hash.bytes || history.latest_hash.bytes != successor_hash.bytes ||
        history.created_at != record.created_at || history.latest_time.utc_seconds != successor.updated_at) return 79;
    auto limited_history = DraftReadResult::Pending;
    for (unsigned i = 0; i < 4096 && limited_history == DraftReadResult::Pending; ++i)
        limited_history = limited.readPublicationHistory(key, 5, verified.id, record.author_public_key, other_history);
    if (limited_history != DraftReadResult::WorkspaceTooSmall || limited.needsRecovery() || owner.holder()) return 82;
    auto limited_recovery = DraftReadResult::Pending;
    for (unsigned i = 0; i < 65536 && limited_recovery == DraftReadResult::Pending; ++i)
        limited_recovery = limited.readPublicationRecovery(exact_publication, recovered_publication);
    if (limited_recovery != DraftReadResult::WorkspaceTooSmall || limited.needsRecovery() || owner.holder()) return 105;
    const auto healthy_files = files;
    {
        IndexedPublicationStore pinned(volume, root, copy, roots[0], roots[1], owner, workspace, frame, sizeof(frame),
                                       receipt, sizeof(receipt), verification, sizeof(verification), crypto);
        if (pinned.readPublicationHistory(key, 5, verified.id, record.author_public_key, other_history) != DraftReadResult::Pending) return 87;
        const auto revision = root.revision;
        ++root.revision;
        const auto changed = pinned.readPublicationHistory(key, 5, verified.id, record.author_public_key, other_history);
        root.revision = revision;
        if (changed != DraftReadResult::Invalid || !pinned.needsRecovery() || owner.holder() || files != healthy_files) return 88;
    }
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        IndexedPublicationStore damaged(volume, root, copy, roots[0], roots[1], owner, workspace, frame, sizeof(frame),
                                        receipt, sizeof(receipt), verification, sizeof(verification), crypto);
        if (fault == 0) fail_read_once = true;
        if (fault == 1)
        {
            auto changed_volume = volume;
            changed_volume[0] ^= 1;
            const auto changed_format = encodeVolumeHeader(changed_volume);
            files["/trailmate/geocaching/.state/format.bin"] = {changed_format.begin(), changed_format.end()};
        }
        if (fault == 2)
        {
            const auto last_journal = std::find_if(files.rbegin(), files.rend(), [](const auto& file)
                                                   { return file.first.find("/.state/journal/") != std::string::npos; });
            if (last_journal == files.rend() || last_journal->second.empty()) return 83;
            last_journal->second.back() ^= 1;
        }
        auto damaged_result = DraftReadResult::Pending;
        for (unsigned i = 0; i < 65536 && damaged_result == DraftReadResult::Pending; ++i)
            damaged_result = damaged.readPublicationHistory(key, 5, verified.id, record.author_public_key, other_history);
        const auto expected = fault == 0 ? DraftReadResult::IoError : fault == 1 ? DraftReadResult::VolumeChanged
                                                                                 : DraftReadResult::Invalid;
        if (damaged_result != expected || !damaged.needsRecovery() || owner.holder()) return 84 + fault;
        files = healthy_files;
    }
    // The same version at another directory is a distinct durable request.
    Destination another_directory;
    another_directory.bytes.fill(0x62);
    std::array<uint8_t, 16> another_task;
    another_task.fill(0x63);
    SdPublishPort another_port(store, crypto, local, verified.id, verified.hash, another_task, issued);
    PublishAttempt another_publication(another_port, crypto);
    if (!another_publication.begin(another_directory, id, reply.signed_cache, signing, sizeof(signing))) return 106;
    for (unsigned i = 0; i < 32768 && another_publication.phase() == PublishAttemptPhase::Submitting; ++i) another_publication.advance();
    if (another_publication.phase() != PublishAttemptPhase::Waiting || select_publication(recovery_filter) != DraftReadResult::Ready ||
        recovered_publication.task != another_task || std::memcmp(recovered_publication.key.data() + 16, another_directory.bytes.data(), 16)) return 107;
    store.releaseDraftRead();
    if (select_publication(exact_publication) != DraftReadResult::Ready || !recovered_publication.confirmed) return 108;
    store.releaseDraftRead();
    auto another_exact = exact_publication;
    another_exact.remote = another_directory;
    if (select_publication(another_exact) != DraftReadResult::Ready || recovered_publication.confirmed || recovered_publication.task != another_task) return 109;
    store.releaseDraftRead();
    if (!another_publication.cancel()) return 110;
    for (unsigned i = 0; i < 32768 && another_publication.phase() == PublishAttemptPhase::Cancelling; ++i) another_publication.advance();
    if (another_publication.phase() != PublishAttemptPhase::Cancelled || select_publication(recovery_filter) != DraftReadResult::NotFound) return 111;
    // Recreate the pre-reservation disk state and fail the first flush. A
    // possibly partial durable write must never reach the signing transport.
    files = bound;
    roots[0] = bound_roots[0];
    roots[1] = bound_roots[1];
    copy = bound_copy;
    if (!decodeIndexRoot({roots[copy].data(), roots[copy].size()}, volume, root)) return 21;
    SigningTransport failed_transport{store, owner, record.author_public_key, encoded, reply.signed_cache};
    SdAuthorIssuePort<SigningTransport> failed_port(failed_transport, store, crypto, issued, key, 2);
    AuthorIssue failed_issue(failed_port);
    if (!failed_issue.begin(encoded, signing, sizeof(signing))) return 22;
    flush_ok = false;
    for (unsigned i = 0; i < 32768 && failed_issue.phase() != AuthorIssuePhase::Failed && failed_issue.phase() != AuthorIssuePhase::Signed; ++i)
        failed_issue.advance();
    flush_ok = true;
    if (failed_issue.phase() != AuthorIssuePhase::Failed || failed_transport.calls || !store.needsRecovery() || owner.holder() ||
        root.sequence != 2) return 23;
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    // Unix exit statuses are eight bits. Preserve diagnostics without allowing
    // an assertion code such as 256 to turn an early failure into test success.
    const auto failed = [](const char* name, int result)
    {
        if (result) std::fprintf(stderr, "%s failed at check %d\n", name, result);
        return result != 0;
    };
    if (failed("indexed download", checkIndexedDownloadReceipt(argv[1]))) return 1;
    if (failed("indexed author reservation", checkIndexedAuthorReservation(argv[1]))) return 1;
    if (failed("checkpoint indexed read", checkCheckpointIndexedRead())) return 1;
    if (failed("indexed draft publication", checkIndexedDraftPublication())) return 1;
    if (failed("indexed commit capacity", checkIndexedCommitCapacity())) return 1;
    if (failed("index transactions", checkIndexTransactions())) return 1;
    using namespace platform::esp::arduino_common::geocaching;
    ::geocaching::storage::VolumeInstance volume{};
    auto format = ::geocaching::storage::encodeVolumeHeader(volume);
    files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
    const uint8_t payload[] = {0x93, 1, 0, 0x91, 0x93, 5, 0xc4, 1, 0xab, 0xc0};
    ::geocaching::storage::RecordHeader header;
    if (!::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::Transaction, 1, {payload, sizeof(payload)}, header)) return 1;
    auto& file = files["/trailmate/geocaching/.state/journal/0000000000000001.gcj"];
    file.assign(header.begin(), header.end());
    file.insert(file.end(), payload, payload + sizeof(payload));
    uint8_t bytes[256];
    ::geocaching::storage::MutationView mutation;
    ::geocaching::storage::TransactionView transaction;
    SdJournalReplay replay(volume, 0, {true, 1, 1}, bytes, sizeof(bytes), &mutation, 1);
    if (nextReady(replay, transaction) != ReplayStep::Transaction || replay.appliedSequence() != 0 ||
        nextReady(replay, transaction) != ReplayStep::Transaction || replay.acknowledgeApplied(2) ||
        !replay.acknowledgeApplied(1) || replay.acknowledgeApplied(1) || nextReady(replay, transaction) != ReplayStep::JournalComplete) return 2;
    SdJournalReplay gap(volume, 1, {true, 2, 2}, bytes, sizeof(bytes), &mutation, 1);
    if (nextReady(gap, transaction) != ReplayStep::Corrupt || gap.appliedSequence() != 1) return 3;
    uint8_t next_payload[sizeof(payload)];
    std::memcpy(next_payload, payload, sizeof(payload));
    next_payload[2] = 1;
    if (!::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::Transaction, 2, {next_payload, sizeof(next_payload)}, header)) return 5;
    file.insert(file.end(), header.begin(), header.end());
    file.insert(file.end(), next_payload, next_payload + sizeof(next_payload));
    SdJournalReplay spanning(volume, 1, {true, 1, 1}, bytes, sizeof(bytes), &mutation, 1);
    ::geocaching::storage::TransactionIndexCursor index;
    ::geocaching::storage::IndexedMutation located;
    if (spanning.pendingIndex(index) || nextReady(spanning, transaction) != ReplayStep::Transaction ||
        !spanning.pendingIndex(index) || !index.next(located) || located.location.segment_first_sequence != 1 ||
        located.location.record_sequence != 2 || located.location.frame_offset != 24 + sizeof(payload) ||
        !spanning.acknowledgeApplied(2) || nextReady(spanning, transaction) != ReplayStep::JournalComplete) return 6;
    if (spanning.pendingIndex(index) || index.next(located)) return 17;
    SdJournalReplay retry(volume, 0, {true, 1, 1}, bytes, sizeof(bytes), &mutation, 1);
    if (nextReady(retry, transaction) != ReplayStep::Transaction || !retry.acknowledgeApplied(1)) return 8;
    fail_read_once = true;
    if (nextReady(retry, transaction) != ReplayStep::RetryLater || retry.appliedSequence() != 1 ||
        nextReady(retry, transaction) != ReplayStep::Transaction ||
        !retry.acknowledgeApplied(2) || nextReady(retry, transaction) != ReplayStep::JournalComplete) return 9;
    file.pop_back();
    SdJournalReplay truncated(volume, 0, {true, 1, 1}, bytes, sizeof(bytes), &mutation, 1);
    if (nextReady(truncated, transaction) != ReplayStep::Transaction || !truncated.acknowledgeApplied(1) ||
        nextReady(truncated, transaction) != ReplayStep::TailTruncated || truncated.appliedSequence() != 1) return 7;
    SdJournalReplay broken_middle(volume, 0, {true, 1, 3}, bytes, sizeof(bytes), &mutation, 1);
    if (nextReady(broken_middle, transaction) != ReplayStep::Transaction || !broken_middle.acknowledgeApplied(1) ||
        nextReady(broken_middle, transaction) != ReplayStep::Corrupt) return 10;
    file.resize(24 + sizeof(payload));
    auto& second_file = files["/trailmate/geocaching/.state/journal/0000000000000002.gcj"];
    second_file.assign(header.begin(), header.end());
    second_file.insert(second_file.end(), next_payload, next_payload + sizeof(next_payload));
    SdJournalReplay cross_segment(volume, 0, {true, 1, 2}, bytes, sizeof(bytes), &mutation, 1);
    if (nextReady(cross_segment, transaction) != ReplayStep::Transaction || !cross_segment.acknowledgeApplied(1) ||
        nextReady(cross_segment, transaction) != ReplayStep::Transaction ||
        !cross_segment.acknowledgeApplied(2) || nextReady(cross_segment, transaction) != ReplayStep::JournalComplete) return 11;
    uint8_t key = 1, value = 7, state_payload[64]{};
    size_t state_payload_size = 0;
    ::geocaching::storage::MutationView update{5, {&key, 1}, {&value, 1}, false};
    if (!::geocaching::storage::encodeTransaction(0, &update, 1, state_payload, sizeof(state_payload), state_payload_size) ||
        !::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::Transaction, 1, {state_payload, state_payload_size}, header)) return 12;
    file.assign(header.begin(), header.end());
    file.insert(file.end(), state_payload, state_payload + state_payload_size);
    uint8_t first[128]{}, second[128]{};
    ::geocaching::storage::LogicalState state(first, second, sizeof(first));
    SdJournalReplay application(volume, 0, {true, 1, 1}, bytes, sizeof(bytes), &mutation, 1);
    if (applyReady(application, state, [](const auto&)
                   { return false; }) != ReplayStep::ApplicationRejected ||
        application.appliedSequence() != 0 || state.view().size() != 0) return 13;
    if (applyReady(application, state, [](const auto&)
                   { return true; }) != ReplayStep::Applied ||
        application.appliedSequence() != 1 || state.view().size() != 1) return 14;
    ::geocaching::ByteView stored;
    if (!state.view().find(5, {&key, 1}, stored) || stored.size != 1 || stored.data[0] != 7 ||
        applyReady(application, state, [](const auto&)
                   { return true; }) != ReplayStep::JournalComplete) return 15;
    std::vector<uint8_t> large_value(1300, 0x5a), large_payload(1400), large_frame(1500);
    update.value = {large_value.data(), large_value.size()};
    if (!::geocaching::storage::encodeTransaction(0, &update, 1, large_payload.data(), large_payload.size(), state_payload_size) ||
        !::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::Transaction, 1, {large_payload.data(), state_payload_size}, header)) return 18;
    file.assign(header.begin(), header.end());
    file.insert(file.end(), large_payload.begin(), large_payload.begin() + state_payload_size);
    SdJournalReplay indexed(volume, 0, {true, 1, 1}, large_frame.data(), large_frame.size(), &mutation, 1);
    bool ready = false, full_slice = false;
    for (unsigned step = 0; step < 128 && !ready; ++step)
    {
        step_bytes = 0;
        const auto result = indexed.next(transaction);
        if (step_bytes > 512) return 19;
        full_slice = full_slice || step_bytes == 512;
        if (result == ReplayStep::Advancing)
        {
            if (indexed.pendingIndex(index)) return 20;
        }
        else if (result == ReplayStep::Transaction) ready = true;
        else return 21;
    }
    if (!ready || !full_slice || !indexed.pendingIndex(index) || !index.next(located) || located.erase ||
        located.location.value_size != large_value.size() || located.location.frame_offset ||
        located.location.value_offset + located.location.value_size > file.size() ||
        std::memcmp(file.data() + located.location.value_offset, large_value.data(), large_value.size())) return 22;
    const auto saved_location = located;
    if (!indexed.acknowledgeApplied(1) || indexed.pendingIndex(index) || index.next(located)) return 23;
    for (unsigned scenario = 0; scenario < 5; ++scenario)
    {
        auto hint = saved_location;
        uint8_t wrong_key = 0xee;
        uint8_t small[64];
        if (scenario == 1) ++hint.location.value_offset;
        if (scenario == 2) hint.key = {&wrong_key, 1};
        SdIndexedValueReader reader(volume);
        if (!reader.begin(hint, scenario == 3 ? small : large_frame.data(), scenario == 3 ? sizeof(small) : large_frame.size())) return 24;
        auto result = IndexedReadStep::Working;
        for (unsigned step = 0; step < 128 && result == IndexedReadStep::Working; ++step)
        {
            if (reader.value().data) return 25;
            if (scenario == 4 && step == 8)
            {
                auto changed = volume;
                changed[0] = 1;
                const auto changed_header = ::geocaching::storage::encodeVolumeHeader(changed);
                files["/trailmate/geocaching/.state/format.bin"] = {changed_header.begin(), changed_header.end()};
            }
            step_bytes = 0;
            result = reader.step();
            if (step_bytes > 512) return 26;
        }
        files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
        if (scenario == 0)
        {
            if (result != IndexedReadStep::Ready || reader.value().size != large_value.size() ||
                std::memcmp(reader.value().data, large_value.data(), large_value.size())) return 27;
        }
        else if (reader.value().data || result != (scenario == 3 ? IndexedReadStep::WorkspaceTooSmall : scenario == 4 ? IndexedReadStep::VolumeChanged
                                                                                                                      : IndexedReadStep::Invalid)) return 28;
    }
    auto deletion = saved_location;
    deletion.erase = true;
    deletion.location = {2, 2, 0, 0, 0};
    ::geocaching::storage::MutationView erased{5, {&key, 1}, {}, true};
    if (!::geocaching::storage::encodeTransaction(1, &erased, 1, state_payload, sizeof(state_payload), state_payload_size) ||
        !::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::Transaction, 2, {state_payload, state_payload_size}, header)) return 29;
    second_file.assign(header.begin(), header.end());
    second_file.insert(second_file.end(), state_payload, state_payload + state_payload_size);
    char shard_path[80];
    if (!indexShardPath('a', 5, {&key, 1}, shard_path, sizeof(shard_path))) return 30;
    auto& shard = files[shard_path];
    ::geocaching::storage::IndexEntryBytes index_bytes;
    if (!::geocaching::storage::encodeIndexEntry(volume, saved_location, index_bytes)) return 31;
    shard.assign(index_bytes.begin(), index_bytes.end());
    if (!::geocaching::storage::encodeIndexEntry(volume, deletion, index_bytes)) return 32;
    shard.insert(shard.end(), index_bytes.begin(), index_bytes.end());
    uint8_t collision[2] = {key, 0};
    bool collided = false;
    for (unsigned i = 0; i < 256; ++i)
    {
        collision[1] = static_cast<uint8_t>(i);
        if ((::sys::crc32(collision, 2) & 0xff) == (::sys::crc32(&key, 1) & 0xff))
        {
            collided = true;
            break;
        }
    }
    auto other = deletion;
    other.key = {collision, 2};
    other.location = {3, 3, 0, 0, 0};
    if (!collided || !::geocaching::storage::encodeIndexEntry(volume, other, index_bytes)) return 33;
    shard.insert(shard.end(), index_bytes.begin(), index_bytes.end());
    for (uint64_t visible = 0; visible <= 3; ++visible)
    {
        SdIndexLookup lookup(volume, 'a', visible, visible * ::geocaching::storage::kIndexEntrySize);
        if (!lookup.begin(5, {&key, 1})) return 34;
        auto result = IndexLookupStep::Working;
        for (unsigned step = 0; step < 128 && result == IndexLookupStep::Working; ++step)
        {
            step_bytes = 0;
            result = lookup.step();
            if (step_bytes > 512) return 35;
        }
        ::geocaching::storage::IndexedMutation hint;
        if (!visible)
        {
            if (result != IndexLookupStep::NotFound || lookup.result(hint)) return 36;
            continue;
        }
        if (result != IndexLookupStep::Found || !lookup.result(hint) || hint.location.record_sequence != (visible == 1 ? 1 : 2) || hint.erase != (visible >= 2)) return 37;
        SdIndexedValueReader reader(volume);
        if (!reader.begin(hint, large_frame.data(), large_frame.size())) return 38;
        auto read = IndexedReadStep::Working;
        for (unsigned step = 0; step < 128 && read == IndexedReadStep::Working; ++step) read = reader.step();
        if (read != (visible == 1 ? IndexedReadStep::Ready : IndexedReadStep::Erased)) return 39;
    }
    shard.pop_back();
    SdIndexLookup old_prefix(volume, 'a', 1, ::geocaching::storage::kIndexEntrySize);
    if (!old_prefix.begin(5, {&key, 1})) return 51;
    auto prefix_result = IndexLookupStep::Working;
    for (unsigned step = 0; step < 64 && prefix_result == IndexLookupStep::Working; ++step) prefix_result = old_prefix.step();
    if (prefix_result != IndexLookupStep::Found) return 52;
    SdIndexLookup torn(volume, 'a', 3, 3 * ::geocaching::storage::kIndexEntrySize);
    if (!torn.begin(5, {&key, 1})) return 40;
    auto invalid = IndexLookupStep::Working;
    for (unsigned step = 0; step < 64 && invalid == IndexLookupStep::Working; ++step) invalid = torn.step();
    if (invalid != IndexLookupStep::Invalid) return 41;
    const auto append_entry = [&](const ::geocaching::storage::IndexedMutation& entry)
    {
        SdIndexAppend writer(volume, 'b');
        if (!writer.begin(entry)) return IndexAppendStep::Invalid;
        auto result = IndexAppendStep::Working;
        for (unsigned step = 0; step < 128 && result == IndexAppendStep::Working; ++step)
        {
            step_bytes = 0;
            result = writer.step();
            if (step_bytes > 512)
            {
                exceeded_budget = true;
                return IndexAppendStep::Invalid;
            }
        }
        return result;
    };
    char written_path[80];
    if (!indexShardPath('b', saved_location.table, saved_location.key, written_path, sizeof(written_path)) ||
        append_entry(saved_location) != IndexAppendStep::Verified || files[written_path].size() != ::geocaching::storage::kIndexEntrySize) return 42;
    const auto flushed = flush_calls;
    if (append_entry(saved_location) != IndexAppendStep::Verified || files[written_path].size() != ::geocaching::storage::kIndexEntrySize || flush_calls != flushed + 1) return 43;
    if (append_entry(deletion) != IndexAppendStep::Verified || files[written_path].size() != 2 * ::geocaching::storage::kIndexEntrySize) return 44;
    auto same_transaction = other;
    same_transaction.location = {2, 2, 0, 0, 0};
    if (append_entry(same_transaction) != IndexAppendStep::Verified || files[written_path].size() != 3 * ::geocaching::storage::kIndexEntrySize) return 48;
    const auto durable_prefix = files[written_path];
    if (append_entry(deletion) != IndexAppendStep::Verified || files[written_path] != durable_prefix) return 49;
    auto conflicting = deletion;
    conflicting.location.frame_offset = 1;
    if (append_entry(conflicting) != IndexAppendStep::Invalid || files[written_path] != durable_prefix) return 50;
    if (append_entry(saved_location) != IndexAppendStep::Invalid || files[written_path] != durable_prefix) return 45;
    auto next = deletion;
    next.location.segment_first_sequence = next.location.record_sequence = 3;
    write_limit = 7;
    const auto partial = append_entry(next);
    write_limit = SIZE_MAX;
    if (partial != IndexAppendStep::IoError || files[written_path].size() != durable_prefix.size() + 7 ||
        append_entry(next) != IndexAppendStep::Invalid) return 46;
    auto separate = saved_location;
    separate.table = 4;
    flush_ok = false;
    const auto failed_flush = append_entry(separate);
    flush_ok = true;
    if (failed_flush != IndexAppendStep::IoError || append_entry(separate) != IndexAppendStep::Verified) return 47;
    char head_paths[2][80];
    const auto bucket = static_cast<uint8_t>(::sys::crc32(&key, 1));
    for (unsigned copy = 0; copy < 2; ++copy)
    {
        ::geocaching::storage::IndexShardHead head{17, copy + 1, (copy + 1) * ::geocaching::storage::kIndexEntrySize, 5, bucket};
        ::geocaching::storage::IndexShardHeadBytes encoded;
        if (!indexShardHeadPath('a', 5, {&key, 1}, copy, head_paths[copy], sizeof(head_paths[copy])) ||
            !::geocaching::storage::encodeIndexShardHead(volume, head, encoded)) return 53;
        files[head_paths[copy]] = {encoded.begin(), encoded.end()};
    }
    for (unsigned visible = 1; visible <= 2; ++visible)
    {
        SdIndexHeadReader heads(volume, 'a', 17, visible);
        if (!heads.begin(5, {&key, 1})) return 54;
        auto result = IndexHeadReadStep::Working;
        ::geocaching::storage::IndexShardHead selected;
        for (unsigned step = 0; step < 8 && result == IndexHeadReadStep::Working; ++step)
        {
            if (heads.selected(selected)) return 55;
            step_bytes = 0;
            result = heads.step();
            if (step_bytes > 512) return 56;
        }
        if (result != IndexHeadReadStep::Ready || !heads.selected(selected) || selected.sequence != visible ||
            selected.length != visible * ::geocaching::storage::kIndexEntrySize || heads.selectedCopy() != int(visible - 1)) return 57;
    }
    const auto valid_head = files[head_paths[0]];
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        if (fault == 0) files[head_paths[0]][0] ^= 1;
        if (fault == 1) files.erase(head_paths[0]);
        SdIndexHeadReader heads(volume, 'a', fault == 2 ? 18 : 17, 2);
        if (!heads.begin(5, {&key, 1})) return 58;
        auto result = IndexHeadReadStep::Working;
        for (unsigned step = 0; step < 8 && result == IndexHeadReadStep::Working; ++step) result = heads.step();
        ::geocaching::storage::IndexShardHead selected;
        if (result != IndexHeadReadStep::Invalid || heads.selected(selected) || heads.selectedCopy() != -1) return 59;
        files[head_paths[0]] = valid_head;
    }
    const auto preserved = files[head_paths[1]];
    const auto write_head = [&]()
    {
        SdIndexHeadWriter writer(volume, 'a');
        ::geocaching::storage::IndexShardHead head{17, 3, 3 * ::geocaching::storage::kIndexEntrySize, 5, bucket};
        if (!writer.begin({&key, 1}, 0, head)) return IndexHeadWriteStep::Invalid;
        auto result = IndexHeadWriteStep::Working;
        for (unsigned step = 0; step < 16 && result == IndexHeadWriteStep::Working; ++step)
        {
            step_bytes = 0;
            result = writer.step();
            if (step_bytes > 512)
            {
                exceeded_budget = true;
                return IndexHeadWriteStep::Invalid;
            }
        }
        return result;
    };
    if (write_head() != IndexHeadWriteStep::Verified || files[head_paths[1]] != preserved) return 60;
    for (unsigned visible = 2; visible <= 3; ++visible)
    {
        SdIndexHeadReader reader(volume, 'a', 17, visible);
        if (!reader.begin(5, {&key, 1})) return 61;
        auto result = IndexHeadReadStep::Working;
        for (unsigned step = 0; step < 8 && result == IndexHeadReadStep::Working; ++step) result = reader.step();
        ::geocaching::storage::IndexShardHead selected;
        if (result != IndexHeadReadStep::Ready || !reader.selected(selected) || selected.sequence != visible) return 62;
    }
    write_limit = 7;
    const auto short_head = write_head();
    write_limit = SIZE_MAX;
    if (short_head != IndexHeadWriteStep::IoError || files[head_paths[0]].size() != 7 || files[head_paths[1]] != preserved) return 63;
    flush_ok = false;
    const auto unflushed_head = write_head();
    flush_ok = true;
    if (unflushed_head != IndexHeadWriteStep::IoError || files[head_paths[1]] != preserved || write_head() != IndexHeadWriteStep::Verified) return 64;
    std::array<uint8_t, ::geocaching::storage::kIndexShardBitmapSize> bitmap{};
    ::geocaching::storage::IndexRootBytes roots[2], loaded[2];
    ::geocaching::storage::IndexRootView initial{17, 0, 1, 'a', {bitmap.data(), bitmap.size()}};
    if (!::geocaching::storage::encodeIndexRoot(volume, initial, roots[0])) return 65;
    bitmap[(5 - 1) * 32 + bucket / 8] |= 1u << (bucket % 8);
    auto committed = initial;
    committed.sequence = 1;
    committed.revision = 2;
    if (!::geocaching::storage::encodeIndexRoot(volume, committed, roots[1])) return 66;
    const char* root_paths[] = {"/trailmate/geocaching/.state/index/root.h0", "/trailmate/geocaching/.state/index/root.h1"};
    for (unsigned copy = 0; copy < 2; ++copy) files[root_paths[copy]] = {roots[copy].begin(), roots[copy].end()};
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        if (fault == 1) files[root_paths[1]][0] ^= 1;
        if (fault == 2) files.erase(root_paths[1]);
        SdIndexRootReader reader(volume);
        if (reader.begin(loaded[0], loaded[0]) || !reader.begin(loaded[0], loaded[1])) return 67;
        auto result = IndexRootReadStep::Working;
        ::geocaching::storage::IndexRootView selected;
        for (unsigned step = 0; step < 8 && result == IndexRootReadStep::Working; ++step)
        {
            if (reader.selected(selected)) return 68;
            step_bytes = 0;
            result = reader.step();
            if (step_bytes > 512) return 69;
        }
        if (!fault)
        {
            if (result != IndexRootReadStep::Ready || !reader.selected(selected) || selected.sequence != 1 ||
                reader.selectedCopy() != 1 || !::geocaching::storage::indexHasShard(selected, 5, bucket)) return 70;
        }
        else if (result != IndexRootReadStep::Invalid || reader.selected(selected)) return 71;
        files[root_paths[1]] = {roots[1].begin(), roots[1].end()};
    }
    const auto publish_root = [&](unsigned copy, const ::geocaching::storage::IndexRootBytes& encoded)
    {
        SdIndexRootWriter writer(volume);
        if (!writer.begin(copy, encoded)) return IndexRootWriteStep::Invalid;
        auto result = IndexRootWriteStep::Working;
        for (unsigned step = 0; step < 32 && result == IndexRootWriteStep::Working; ++step)
        {
            step_bytes = 0;
            result = writer.step();
            if (step_bytes > 512)
            {
                exceeded_budget = true;
                return IndexRootWriteStep::Invalid;
            }
        }
        return result;
    };
    auto next_root = committed;
    next_root.sequence = 2;
    next_root.revision = 3;
    ::geocaching::storage::IndexRootBytes next_bytes;
    if (!::geocaching::storage::encodeIndexRoot(volume, next_root, next_bytes) || publish_root(0, next_bytes) != IndexRootWriteStep::Verified ||
        files[root_paths[1]] != std::vector<uint8_t>(roots[1].begin(), roots[1].end())) return 72;
    SdIndexRootReader latest_root(volume);
    if (!latest_root.begin(loaded[0], loaded[1])) return 73;
    auto root_result = IndexRootReadStep::Working;
    for (unsigned step = 0; step < 8 && root_result == IndexRootReadStep::Working; ++step) root_result = latest_root.step();
    ::geocaching::storage::IndexRootView latest;
    if (root_result != IndexRootReadStep::Ready || !latest_root.selected(latest) || latest.sequence != 2 || latest_root.selectedCopy() != 0) return 74;
    const auto committed_root = files[root_paths[0]];
    next_root.sequence = 3;
    next_root.revision = 4;
    if (!::geocaching::storage::encodeIndexRoot(volume, next_root, next_bytes)) return 75;
    write_limit = 7;
    const auto short_root = publish_root(1, next_bytes);
    write_limit = SIZE_MAX;
    if (short_root != IndexRootWriteStep::IoError || files[root_paths[0]] != committed_root || files[root_paths[1]].size() != 7) return 76;
    flush_ok = false;
    const auto unflushed_root = publish_root(1, next_bytes);
    flush_ok = true;
    if (unflushed_root != IndexRootWriteStep::IoError || files[root_paths[0]] != committed_root || publish_root(1, next_bytes) != IndexRootWriteStep::Verified) return 77;
    volume[0] = 1;
    format = ::geocaching::storage::encodeVolumeHeader(volume);
    files["/trailmate/geocaching/.state/format.bin"] = {format.begin(), format.end()};
    if (nextReady(replay, transaction) != ReplayStep::VolumeChanged) return 4;
    if (exceeded_budget) return 16;
    return 0;
}
