#include "chat/infra/reticulum/reticulum_wire.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_loader.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_selection.h"
#include "platform/esp/arduino_common/geocaching/sd_state_recovery.h"
#include <vector>

std::vector<uint8_t> file_bytes;
std::vector<uint8_t> slot_b_bytes;
bool separate_b = false, b_busy = false;
bool slots_missing = false;
size_t step_data_bytes = 0;
namespace platform::esp::arduino_common::storage
{
class SdRuntimeDir::Impl
{
  public:
    bool open = false;
};
SdRuntimeDir::SdRuntimeDir() : impl_(new Impl) {}
SdRuntimeDir::~SdRuntimeDir() { delete impl_; }
bool SdRuntimeDir::open(const char*)
{
    impl_->open = true;
    return true;
}
void SdRuntimeDir::close() { impl_->open = false; }
bool SdRuntimeDir::is_open() const { return impl_->open; }
SdDirReadStatus SdRuntimeDir::read_next_status(char*, size_t, bool*) { return SdDirReadStatus::End; }
SdFileReadResult sd_read_file(const char* path, uint8_t* buffer, size_t capacity)
{
    step_data_bytes += capacity;
    SdFileReadResult result;
    if (std::strstr(path, "format.bin"))
    {
        const auto header = ::geocaching::storage::encodeVolumeHeader({});
        result.file_size = header.size();
        result.bytes_read = std::min(capacity, header.size());
        std::memcpy(buffer, header.data(), result.bytes_read);
    }
    else
    {
        if (slots_missing)
        {
            result.status = SdFileReadStatus::Missing;
            return result;
        }
        const bool slot_b = std::strstr(path, "b.gcs") != nullptr;
        if (slot_b && b_busy)
        {
            result.status = SdFileReadStatus::Busy;
            return result;
        }
        const auto& bytes = separate_b && slot_b ? slot_b_bytes : file_bytes;
        result.file_size = bytes.size();
        result.bytes_read = std::min(capacity, bytes.size());
        std::memcpy(buffer, bytes.data(), result.bytes_read);
    }
    result.status = SdFileReadStatus::Ready;
    return result;
}
class SdRuntimeFile::Impl
{
  public:
    bool open = false;
    size_t offset = 0;
    const std::vector<uint8_t>* bytes = &file_bytes;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char* path, const char*)
{
    impl_->bytes = separate_b && std::strstr(path, "b.gcs") ? &slot_b_bytes : &file_bytes;
    impl_->open = true;
    impl_->offset = 0;
    return true;
}
void SdRuntimeFile::close() { impl_->open = false; }
bool SdRuntimeFile::is_open() const { return impl_->open; }
uint64_t SdRuntimeFile::size() const { return impl_->bytes->size(); }
int SdRuntimeFile::read(void* output, size_t count)
{
    step_data_bytes += count;
    const auto n = std::min(count, impl_->bytes->size() - impl_->offset);
    std::memcpy(output, impl_->bytes->data() + impl_->offset, n);
    impl_->offset += n;
    return static_cast<int>(n);
}
} // namespace platform::esp::arduino_common::storage
struct Digest
{
    std::vector<uint8_t> bytes;
    void update(const uint8_t* data, size_t size) { bytes.insert(bytes.end(), data, data + size); }
    bool finalize(uint8_t* out, size_t)
    {
        chat::reticulum::fullHash(bytes.data(), bytes.size(), out);
        return true;
    }
};
std::vector<uint8_t> checkpointBytes(uint64_t sequence, uint8_t key)
{
    uint8_t page[] = {0x92, 0, 0x91, 0x93, 5, 0xc4, 1, key, 0xc4, 0};
    ::geocaching::storage::RecordHeader header;
    ::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::CheckpointPage, sequence, {page, sizeof(page)}, header);
    std::vector<uint8_t> result(header.begin(), header.end());
    result.insert(result.end(), page, page + sizeof(page));
    uint8_t tail[37] = {0x93, 1, 1, 0xc4, 32};
    chat::reticulum::fullHash(result.data(), result.size(), tail + 5);
    ::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::CheckpointTail, sequence, {tail, sizeof(tail)}, header);
    result.insert(result.end(), header.begin(), header.end());
    result.insert(result.end(), tail, tail + sizeof(tail));
    return result;
}
int main()
{
    using namespace platform::esp::arduino_common::geocaching;
    const uint8_t page[] = {0x92, 0, 0x91, 0x93, 5, 0xc4, 1, 1, 0xc4, 0};
    ::geocaching::storage::RecordHeader header;
    if (!::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::CheckpointPage, 7, {page, sizeof(page)}, header)) return 1;
    file_bytes.assign(header.begin(), header.end());
    file_bytes.insert(file_bytes.end(), page, page + sizeof(page));
    const auto page_size = file_bytes.size();
    uint8_t tail[37] = {0x93, 1, 1, 0xc4, 32};
    chat::reticulum::fullHash(file_bytes.data(), file_bytes.size(), tail + 5);
    if (!::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::CheckpointTail, 7, {tail, sizeof(tail)}, header)) return 2;
    file_bytes.insert(file_bytes.end(), header.begin(), header.end());
    file_bytes.insert(file_bytes.end(), tail, tail + sizeof(tail));
    const auto complete_file = file_bytes;
    uint8_t buffer[256];
    ::geocaching::storage::MutationView entry;
    size_t count = 0;
    Digest cursor_digest;
    SdCheckpointReader<Digest> cursor_reader(cursor_digest);
    ::geocaching::storage::CheckpointPageCursor page_cursor;
    ::geocaching::storage::CheckpointIndexCursor index_cursor;
    ::geocaching::storage::IndexedMutation indexed;
    if (!cursor_reader.open('b') || cursor_reader.stepCursor(buffer, sizeof(buffer), page_cursor) != CheckpointReadStep::Reading ||
        page_cursor.count() != 1 || !page_cursor.next(entry) || !page_cursor.complete() || cursor_reader.sequence() != 0 ||
        !cursor_reader.pendingIndex(index_cursor) || !index_cursor.next(indexed) || !index_cursor.complete() ||
        indexed.table != entry.table || indexed.location.source != ::geocaching::storage::IndexedValueSource::CheckpointB ||
        indexed.location.record_sequence != 7 || indexed.location.frame_offset != 0 || indexed.location.value_size != entry.value.size) return 21;
    if (cursor_reader.stepCursor(buffer, sizeof(buffer), page_cursor) != CheckpointReadStep::Reading ||
        cursor_reader.pendingIndex(index_cursor) || cursor_reader.sequence() != 0 ||
        cursor_reader.stepCursor(buffer, sizeof(buffer), page_cursor) != CheckpointReadStep::Verified ||
        cursor_reader.sequence() != 7 || std::memcmp(cursor_reader.digest().data(), tail + 5, 32)) return 22;
    Digest valid_digest;
    SdCheckpointReader<Digest> valid(valid_digest);
    if (!valid.open('a') || valid.step(buffer, sizeof(buffer), &entry, 1, count) != CheckpointReadStep::Reading || count != 1 ||
        valid.sequence() != 0 || valid.step(buffer, sizeof(buffer), &entry, 1, count) != CheckpointReadStep::Reading ||
        valid.sequence() != 0 || valid.step(buffer, sizeof(buffer), &entry, 1, count) != CheckpointReadStep::Verified || valid.sequence() != 7) return 3;
    file_bytes.push_back(0);
    Digest extra_digest;
    SdCheckpointReader<Digest> extra(extra_digest);
    if (!extra.open('b') || extra.step(buffer, sizeof(buffer), &entry, 1, count) != CheckpointReadStep::Reading ||
        extra.step(buffer, sizeof(buffer), &entry, 1, count) != CheckpointReadStep::Reading ||
        extra.step(buffer, sizeof(buffer), &entry, 1, count) != CheckpointReadStep::Invalid || extra.sequence() != 0) return 4;
    file_bytes.resize(page_size);
    Digest missing_digest;
    SdCheckpointReader<Digest> missing(missing_digest);
    if (!missing.open('a') || missing.step(buffer, sizeof(buffer), &entry, 1, count) != CheckpointReadStep::Reading ||
        missing.step(buffer, sizeof(buffer), &entry, 1, count) != CheckpointReadStep::Invalid) return 5;
    file_bytes = complete_file;
    SdCheckpointSelection<Digest> selection({});
    auto selected = CheckpointSelectionStep::Reading;
    for (unsigned i = 0; i < 64 && selected == CheckpointSelectionStep::Reading; ++i)
        selected = selection.step(buffer, sizeof(buffer), &entry, 1);
    if (selected != CheckpointSelectionStep::Selected || selection.choice() != ::geocaching::storage::CheckpointChoice::SlotA ||
        selection.candidate(false).sequence != 7 || selection.candidate(true).sequence != 7) return 6;
    slots_missing = true;
    SdCheckpointSelection<Digest> empty({});
    auto empty_result = CheckpointSelectionStep::Reading;
    for (unsigned i = 0; i < 16 && empty_result == CheckpointSelectionStep::Reading; ++i) empty_result = empty.step(buffer, sizeof(buffer), &entry, 1);
    if (empty_result != CheckpointSelectionStep::NoCheckpoint) return 7;
    slots_missing = false;
    separate_b = true;
    auto check_selection = [&](CheckpointSelectionStep expected, ::geocaching::storage::CheckpointChoice choice)
    {
        SdCheckpointSelection<Digest> candidate({});
        auto state = CheckpointSelectionStep::Reading;
        for (unsigned i = 0; i < 64 && state == CheckpointSelectionStep::Reading; ++i)
            state = candidate.step(buffer, sizeof(buffer), &entry, 1);
        return state == expected && (state != CheckpointSelectionStep::Selected || candidate.choice() == choice);
    };
    using Choice = ::geocaching::storage::CheckpointChoice;
    slot_b_bytes = checkpointBytes(8, 1);
    if (!check_selection(CheckpointSelectionStep::Selected, Choice::SlotB)) return 8;
    slot_b_bytes.pop_back();
    if (!check_selection(CheckpointSelectionStep::Selected, Choice::SlotA)) return 9;
    slot_b_bytes = checkpointBytes(7, 2);
    if (!check_selection(CheckpointSelectionStep::Corrupt, Choice::Corrupt)) return 10;
    b_busy = true;
    if (!check_selection(CheckpointSelectionStep::RetryLater, Choice::RetryLater)) return 11;
    b_busy = false;
    uint8_t live[128]{}, spare[128]{}, old_key = 9;
    ::geocaching::storage::LogicalState state(live, spare, sizeof(live));
    ::geocaching::storage::MutationView old{1, {&old_key, 1}, {}, false};
    auto allow = [](const auto&)
    { return true; };
    if (!state.apply(&old, 1, allow)) return 12;
    Digest load_digest;
    SdCheckpointLoader<Digest> loader(load_digest, state, {}, 'a', selection.candidate(false));
    ::geocaching::ByteView value;
    auto load_result = CheckpointLoadStep::Loading;
    for (unsigned i = 0; i < 64 && load_result == CheckpointLoadStep::Loading; ++i)
    {
        if (!state.view().find(1, {&old_key, 1}, value)) return 13;
        load_result = loader.step(buffer, sizeof(buffer), &entry, 1, allow);
    }
    if (load_result != CheckpointLoadStep::Applied || state.view().find(1, {&old_key, 1}, value) || state.view().size() != 1) return 13;
    if (!state.apply(&old, 1, allow)) return 14;
    auto wrong_candidate = selection.candidate(false);
    wrong_candidate.digest[0] ^= 1;
    Digest changed_digest;
    SdCheckpointLoader<Digest> changed(changed_digest, state, {}, 'a', wrong_candidate);
    load_result = CheckpointLoadStep::Loading;
    for (unsigned i = 0; i < 64 && load_result == CheckpointLoadStep::Loading; ++i) load_result = changed.step(buffer, sizeof(buffer), &entry, 1, allow);
    if (load_result != CheckpointLoadStep::Invalid || !state.view().find(1, {&old_key, 1}, value) || state.view().size() != 2) return 15;
    separate_b = false;
    uint8_t recovered_a[256]{}, recovered_b[256]{};
    ::geocaching::storage::LogicalState recovered(recovered_a, recovered_b, sizeof(recovered_a));
    SdStateRecovery<Digest> recovery({}, recovered);
    auto recovery_result = StateRecoveryStep::Working;
    for (unsigned i = 0; i < 128 && recovery_result == StateRecoveryStep::Working; ++i)
        recovery_result = recovery.step(buffer, sizeof(buffer), &entry, 1, allow);
    if (recovery_result != StateRecoveryStep::JournalRestored || recovery.replayedSequence() != 7 || recovered.view().size() != 1) return 16;
    slots_missing = true;
    SdStateRecovery<Digest> fresh({}, recovered);
    recovery_result = StateRecoveryStep::Working;
    for (unsigned i = 0; i < 10 && recovery_result == StateRecoveryStep::Working; ++i)
        recovery_result = fresh.step(buffer, sizeof(buffer), &entry, 1, allow);
    if (recovery_result != StateRecoveryStep::JournalRestored || fresh.replayedSequence() != 0 || recovered.view().size() != 0) return 17;
    slots_missing = false;
    Digest small_digest;
    SdCheckpointReader<Digest> small(small_digest);
    if (!small.open('a') || small.step(buffer, 24, &entry, 1, count) != CheckpointReadStep::WorkspaceTooSmall) return 18;
    Digest entries_digest;
    SdCheckpointReader<Digest> no_entries(entries_digest);
    if (!no_entries.open('a') || no_entries.step(buffer, sizeof(buffer), &entry, 0, count) != CheckpointReadStep::WorkspaceTooSmall) return 19;
    SdCheckpointSelection<Digest> constrained({});
    auto constrained_result = CheckpointSelectionStep::Reading;
    for (unsigned i = 0; i < 32 && constrained_result == CheckpointSelectionStep::Reading; ++i) constrained_result = constrained.step(buffer, sizeof(buffer), &entry, 0);
    if (constrained_result != CheckpointSelectionStep::RetryLater || constrained.choice() != Choice::RetryLater) return 20;
    SdCheckpointSelection<Digest> streaming({});
    auto streaming_result = CheckpointSelectionStep::Reading;
    for (unsigned i = 0; i < 64 && streaming_result == CheckpointSelectionStep::Reading; ++i) streaming_result = streaming.stepCursor(buffer, sizeof(buffer));
    if (streaming_result != CheckpointSelectionStep::Selected || streaming.candidate(false).sequence != 7) return 23;
    // A page larger than one SPI slice catches accidental accumulation of
    // volume-header, probe and page reads in the same maintenance step.
    std::vector<uint8_t> large_value(700, 42), large_payload(1024);
    ::geocaching::protocol::CmpWriter large_writer(large_payload.data(), large_payload.size());
    const uint8_t large_key = 1;
    if (!large_writer.array(2) || !large_writer.unsignedInteger(0) || !large_writer.array(1) || !large_writer.array(3) ||
        !large_writer.unsignedInteger(5) || !large_writer.binary({&large_key, 1}) ||
        !large_writer.binary({large_value.data(), large_value.size()}) ||
        !::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::CheckpointPage, 7,
                                                 {large_payload.data(), large_writer.size()}, header)) return 24;
    file_bytes.assign(header.begin(), header.end());
    file_bytes.insert(file_bytes.end(), large_payload.data(), large_payload.data() + large_writer.size());
    chat::reticulum::fullHash(file_bytes.data(), file_bytes.size(), tail + 5);
    if (!::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::CheckpointTail, 7, {tail, sizeof(tail)}, header)) return 25;
    file_bytes.insert(file_bytes.end(), header.begin(), header.end());
    file_bytes.insert(file_bytes.end(), tail, tail + sizeof(tail));
    SdCheckpointSelection<Digest> budgeted({});
    auto budgeted_result = CheckpointSelectionStep::Reading;
    for (unsigned i = 0; i < 128 && budgeted_result == CheckpointSelectionStep::Reading; ++i)
    {
        step_data_bytes = 0;
        budgeted_result = budgeted.stepCursor(large_payload.data(), large_payload.size());
        if (step_data_bytes > 512) return 26;
    }
    if (budgeted_result != CheckpointSelectionStep::Selected) return 27;
    std::vector<uint8_t> large_live(1024), large_spare(1024);
    ::geocaching::storage::LogicalState large_state(large_live.data(), large_spare.data(), large_live.size());
    Digest large_digest;
    SdCheckpointLoader<Digest> large_loader(large_digest, large_state, {}, 'a', budgeted.candidate(false));
    load_result = CheckpointLoadStep::Loading;
    for (unsigned i = 0; i < 128 && load_result == CheckpointLoadStep::Loading; ++i)
    {
        step_data_bytes = 0;
        load_result = large_loader.step(large_payload.data(), large_payload.size(), &entry, 1, allow);
        if (step_data_bytes > 512) return 28;
    }
    if (load_result != CheckpointLoadStep::Applied || large_state.view().size() != 1) return 29;
    return 0;
}
