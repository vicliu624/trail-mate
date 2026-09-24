#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "geocaching/protocol/query_request.h"
#include "geocaching/protocol/record_encoder.h"
#include "geocaching/storage/draft_record.h"
#include "geocaching/storage/queued_request.h"
#include "platform/esp/arduino_common/geocaching/indexed_dispatch_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_query_store_port.h"
#include "platform/esp/arduino_common/geocaching/request_dispatcher.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_index_import.h"
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
#include "platform/esp/arduino_common/geocaching/sd_indexed_download_reply.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_draft_save.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_new_task.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_pending_request.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_recovery.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_stop_task.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_value_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_replay.h"
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
        if (read_bytes[journal_path] != files[journal_path].size()) return 118;
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
    if (aliased_result != IndexedCommitStep::Verified || !released_alias || read_bytes[alias_path] != 2 * files[alias_path].size()) return 121;
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
    SdGeocachingJournal journal(volume);
    if (journal.begin(current.sequence, &resume_row, 1) != JournalWriteResult::InProgress) return 179;
    auto journal_status = JournalWriteResult::InProgress;
    for (unsigned i = 0; i < 256 && journal_status == JournalWriteResult::InProgress; ++i) journal_status = journal.step();
    if (journal_status != JournalWriteResult::Verified) return 180;
    const auto resumed_sequence = current.sequence + 1;
    MutationView pending_rows[3];
    uint8_t validation_frame[512];
    const auto before_replay = files;
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
                if (asked || files != before_replay || !replay->pending(pending) || pending.count != 1 ||
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
    auto dispatch_store = std::make_unique<IndexedDispatchStore>(volume, current, copy, roots[0], roots[1], workspace_owner, workspace, frame, sizeof(frame));
    chat::MeshAdapterRouter router;
    RequestDispatcher dispatcher(router, *dispatch_store, 100, 1000);
    DispatchResult dispatched;
    for (unsigned i = 0; i < 32768; ++i)
    {
        step_bytes = 0;
        dispatched = dispatcher.dispatchOne({});
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
    IndexedQueryStorePort port(
        volume, current, copy, roots[0], roots[1], {}, workspace_owner, workspace, frame, sizeof(frame), page_cache, sizeof(page_cache), receipt_crypto,
        [](void* context, uint8_t out[16])
        { const auto next = ++*static_cast<uint8_t*>(context); std::memset(out, next, 16); return true; },
        [](void*)
        { return StoredTime{}; },
        &random_counter);
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
        const std::vector<uint8_t> prior_frame(frame, frame + sizeof(frame)), prior_encoding(outgoing_bytes, outgoing_bytes + sizeof(outgoing_bytes));
        const auto submitted = port.submit(directory, request, {request_bytes, request_size});
        step_bytes = 0;
        if (submitted != QueryPersistence::Pending || port.pollPersistence() != QueryPersistence::Pending || step_bytes ||
            std::memcmp(frame, prior_frame.data(), sizeof(frame)) || std::memcmp(outgoing_bytes, prior_encoding.data(), sizeof(outgoing_bytes))) return 132;
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
    for (unsigned i = 0; i < 8192 && port.maintenancePending(); ++i) port.maintenanceStep();
    step_bytes = 0;
    if (!port.accepted({}, first_page_id, {first_page_response.data(), first_page_response.size()}) || step_bytes) return 128;
    first_page_response.back() = 61;
    if (port.accepted({}, first_page_id, {first_page_response.data(), first_page_response.size()})) return 129;
    for (unsigned i = 0; i < 8192 && port.maintenancePending(); ++i) port.maintenanceStep();
    if (port.accepted({}, first_page_id, {first_page_response.data(), first_page_response.size()})) return 130;
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
    const auto source_files = files;
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
    size_t request_size = 0;
    if (!protocol::encodeGetRequest(id, record.id, &record.hash, nullptr, 8192, request, sizeof(request), request_size)) return 145;
    QueuedRequestWorkspace workspace(outgoing, sizeof(outgoing));
    std::array<uint8_t, 16> task{};
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
    files.clear();
    index_directories.clear();
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    if (const int result = checkIndexedDownloadReceipt(argv[1])) return result;
    if (const int result = checkCheckpointIndexedRead()) return result;
    if (const int result = checkIndexedDraftPublication()) return result;
    if (const int result = checkIndexedCommitCapacity()) return result;
    if (const int result = checkIndexTransactions()) return result;
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
