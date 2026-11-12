#include "platform/esp/arduino_common/hostlink/hostlink_management_service.h"
namespace platform::esp::arduino_common::geocaching::browse_runtime
{
bool webLease(bool, bool) { return true; }
WebReadResult requestWebPublicationStop(const uint8_t*, const uint8_t*) { return WebReadResult::Unavailable; }
WebReadResult readWebPublicationJob(const uint8_t*, const uint8_t*, uint8_t*, size_t, size_t& written)
{
    written = 0;
    return WebReadResult::Unavailable;
}
WebReadResult readWebPublicationHistory(const uint8_t*, const uint8_t*, uint8_t*, size_t, size_t& written)
{
    written = 0;
    return WebReadResult::Unavailable;
}
bool queueWebDraftPublication(const uint8_t*, uint64_t, const uint8_t*, const uint8_t*, uint32_t, const uint8_t*, const uint8_t*) { return false; }
bool publicationContext(uint8_t*, uint64_t&) { return false; }
WebReadResult readWebPublicationBinding(const uint8_t*, uint64_t, const uint8_t*, const uint8_t*, uint8_t*, size_t) { return WebReadResult::Unavailable; }
WebReadResult readWebLatestRevision(const uint8_t*, const uint8_t*, uint8_t*, size_t) { return WebReadResult::Unavailable; }
WebReadResult readWebDirectories(uint32_t, uint8_t*, size_t, size_t& written)
{
    written = 0;
    return WebReadResult::Unavailable;
}
bool fake_release_draft_save = true;
bool releaseWebDraftSave() { return fake_release_draft_save; }
WebReadResult fake_owner_result = WebReadResult::Unavailable;
WebReadResult checkWebDraftEditOwner(const uint8_t*, uint64_t, const uint8_t*, const uint8_t*) { return fake_owner_result; }
WebReadResult queueWebDraftFile(const uint8_t*, uint32_t, uint64_t, const uint8_t*, const uint8_t*, uint32_t) { return WebReadResult::Unavailable; }
WebReadResult readWebDraftCatalog(uint32_t, bool, uint64_t, uint8_t*, size_t, size_t& written)
{
    written = 0;
    return WebReadResult::Unavailable;
}
WebReadResult readWebDraftChunk(const uint8_t*, uint32_t, uint16_t, uint64_t, uint8_t*, size_t, size_t& written)
{
    written = 0;
    return WebReadResult::Unavailable;
}
} // namespace platform::esp::arduino_common::geocaching::browse_runtime
#include "geocaching/storage/draft_record.h"
#include <array>
#include <cassert>
#include <vector>

static std::vector<uint8_t> response;
struct RecoveryAuthor final : chat::IMeshAdapter
{
    uint32_t generation = 7;
    chat::DeviceAuthorResult result = chat::DeviceAuthorResult::Ok;
    chat::DeviceAuthorResult readDeviceAuthorKey(uint8_t* key, uint32_t& actual) const override
    {
        if (result != chat::DeviceAuthorResult::Ok) return result;
        std::memset(key, 0, 64);
        actual = generation;
        return result;
    }
    bool sendText(chat::ChannelId, const std::string&, chat::MessageId*, chat::NodeId) override { return false; }
    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }
    bool sendAppData(chat::ChannelId, uint32_t, const uint8_t*, size_t, chat::NodeId, bool, chat::MessageId, bool) override { return false; }
    bool pollIncomingData(chat::MeshIncomingData*) override { return false; }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }
};
namespace hostlink
{
bool enqueue_event(uint8_t, const uint8_t* bytes, size_t size, bool, uint16_t, uint32_t completion_cookie)
{
    assert(completion_cookie == 0);
    response.assign(bytes, bytes + size);
    return true;
}
} // namespace hostlink
int main()
{
    using namespace hostlink::management;
    using namespace platform::esp::arduino_common::storage;
    std::array<uint8_t, 16> owner{}, request{}, id{};
    owner[0] = 1;
    id[0] = 2;
    const auto original = fake_file;
    const std::string candidate = "TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:NewDevice\nEND\n";
    uint8_t scratch[512], base[32], hash[32];
    assert(web_config::hash_file("/trailmate/config.tms", scratch, base));
    mbedtls_sha256_context ctx;
    platform::esp::common::crypto::sha256_update(&ctx, reinterpret_cast<const uint8_t*>(candidate.data()), candidate.size());
    platform::esp::common::crypto::sha256_finish(&ctx, hash);
    auto invoke = [&](Operation op, auto body)
    {uint8_t input[512];geocaching::protocol::CmpWriter writer(input,sizeof(input));body(writer);assert(writer.good());hostlink::PendingCommand command;size_t size;assert(encodeEnvelope(owner.data(),request.data(),static_cast<uint16_t>(op),input,writer.size(),command.payload,sizeof(command.payload),size));command.payload_len=size;assert(execute_management(command)==hostlink::ErrorCode::Ok);EnvelopeView result;assert(decodeEnvelope(response.data(),response.size(),result));return result.code; };
    auto begin = [&](auto& w)
    {w.array(4);w.binary({id.data(),16});w.unsignedInteger(1);w.unsignedInteger(candidate.size());w.binary({hash,32}); };
    assert(invoke(Operation::TransferBegin, begin) == 0);
    assert(invoke(Operation::TransferBegin, begin) == 0);
    auto chunk = [&](auto& w)
    {w.array(3);w.binary({id.data(),16});w.unsignedInteger(0);w.binary({reinterpret_cast<const uint8_t*>(candidate.data()),candidate.size()}); };
    assert(invoke(Operation::TransferChunk, chunk) == 0);
    assert(invoke(Operation::TransferChunk, chunk) == 0);
    auto only_id = [&](auto& w)
    {w.array(1);w.binary({id.data(),16}); };
    assert(invoke(Operation::TransferFinish, only_id) == 0);
    fake_file[0] ^= 1;
    auto commit = [&](auto& w)
    {w.array(2);w.binary({id.data(),16});w.binary({base,32}); };
    assert(invoke(Operation::ConfigCommit, commit) == 4);
    fake_file = original;
    assert(invoke(Operation::ConfigCommit, commit) == 0);
    assert(fake_file == candidate && app::sd_tms::fake_commits == 1);
    assert(invoke(Operation::ConfigCommit, commit) == 0);
    assert(app::sd_tms::fake_commits == 1);
    owner[0] = 9;
    assert(invoke(Operation::TransferStatus, only_id) == 2);
    owner[0] = 1;
    id[0] = 3;
    app::sd_tms::fake_restart = false;
    const auto config_after_commit = fake_file;
    std::array<uint8_t, 1024> encoded_draft{};
    std::string description(650, 'g');
    geocaching::storage::DraftView draft;
    draft.generation = 1;
    draft.name = "Web draft";
    draft.description = description;
    size_t draft_size = 0;
    assert(geocaching::storage::encodeDraft({id.data(), 16}, draft, encoded_draft.data(), encoded_draft.size(), draft_size));
    mbedtls_sha256_context draft_ctx;
    platform::esp::common::crypto::sha256_update(&draft_ctx, encoded_draft.data(), draft_size);
    platform::esp::common::crypto::sha256_finish(&draft_ctx, hash);
    auto draft_begin = [&](auto& w)
    {w.array(4);w.binary({id.data(),16});w.unsignedInteger(2);w.unsignedInteger(draft_size);w.binary({hash,32}); };
    assert(invoke(Operation::TransferBegin, draft_begin) == 0);
    assert(invoke(Operation::TransferBegin, draft_begin) == 0);
    auto same_content_wrong_kind = [&](auto& w)
    {w.array(4);w.binary({id.data(),16});w.unsignedInteger(1);w.unsignedInteger(draft_size);w.binary({hash,32}); };
    assert(invoke(Operation::TransferBegin, same_content_wrong_kind) == 4);
    // One shared slot prevents a configuration upload from replacing a draft.
    assert(invoke(Operation::TransferBegin, begin) == 4);
    for (size_t offset = 0; offset < draft_size; offset += 320)
    {
        const size_t length = std::min(size_t(320), draft_size - offset);
        auto draft_chunk = [&](auto& w)
        {w.array(3);w.binary({id.data(),16});w.unsignedInteger(offset);w.binary({encoded_draft.data()+offset,length}); };
        assert(invoke(Operation::TransferChunk, draft_chunk) == 0);
        assert(invoke(Operation::TransferChunk, draft_chunk) == 0);
    }
    fake_files[web_config::candidate_path(2)][0] ^= 1;
    assert(invoke(Operation::TransferFinish, only_id) == 8);
    fake_files[web_config::candidate_path(2)][0] ^= 1;
    assert(invoke(Operation::TransferFinish, only_id) == 0);
    web_config::Metadata metadata;
    assert(web_config::load(metadata, scratch) && metadata.kind == 2 && metadata.phase == 1 && metadata.offset == draft_size);
    const auto original_draft_input = fake_files.at(web_config::candidate_path(2));
    assert(fake_files[web_config::kMetaPath].size() == 111);
    assert(invoke(Operation::ConfigCommit, commit) == 3);
    assert(invoke(Operation::ConfigValidate, only_id) == 3);
    assert(fake_file == config_after_commit && app::sd_tms::fake_commits == 1);
    owner[0] = 9;
    assert(invoke(Operation::TransferAbort, only_id) == 2);
    owner[0] = 1;
    assert(invoke(Operation::TransferAbort, only_id) == 0);
    assert(!sd_exists(web_config::candidate_path(2)) && !sd_exists(web_config::kMetaPath));
    id[0] = 4;
    auto signature_begin = [&](auto& w)
    { w.array(4); w.binary({id.data(),16}); w.unsignedInteger(5); w.unsignedInteger(draft_size); w.binary({hash,32}); };
    assert(invoke(Operation::TransferBegin, signature_begin) == 0);
    assert(web_config::load(metadata, scratch) && metadata.kind == 5 && metadata.phase == 0);
    for (size_t offset = 0; offset < draft_size; offset += 320)
    {
        const auto count = std::min(size_t(320), draft_size - offset);
        auto chunk = [&](auto& w)
        { w.array(3); w.binary({id.data(),16}); w.unsignedInteger(offset); w.binary({encoded_draft.data()+offset,count}); };
        assert(invoke(Operation::TransferChunk, chunk) == 0);
    }
    assert(invoke(Operation::TransferFinish, only_id) == 0);
    assert(web_config::load(metadata, scratch) && metadata.kind == 5 && metadata.phase == 1);
    assert(static_cast<uint8_t>(fake_files[web_config::kMetaPath][76]) == 0x41);
    assert(fake_files[web_config::candidate_path(5)].size() == draft_size);
    assert(std::memcmp(fake_files[web_config::candidate_path(5)].data(), encoded_draft.data(), draft_size) == 0);
    assert(invoke(Operation::ConfigValidate, only_id) == 3 && invoke(Operation::ConfigCommit, commit) == 3);
    assert(fake_file == config_after_commit && app::sd_tms::fake_commits == 1);
    metadata.kind = 9;
    const auto retained_meta = fake_files[web_config::kMetaPath];
    assert(!web_config::save(metadata, scratch) && fake_files[web_config::kMetaPath] == retained_meta);
    assert(invoke(Operation::TransferAbort, only_id) == 0);
    auto oversized_signature = [&](auto& w)
    { w.array(4); w.binary({id.data(),16}); w.unsignedInteger(5); w.unsignedInteger(4097); w.binary({hash,32}); };
    assert(invoke(Operation::TransferBegin, oversized_signature) == 3);
    assert(!sd_exists(web_config::candidate_path(5)) && !sd_exists(web_config::kMetaPath));
    auto confirmation_begin = [&](auto& w)
    { w.array(4); w.binary({id.data(),16}); w.unsignedInteger(6); w.unsignedInteger(draft_size); w.binary({hash,32}); };
    assert(invoke(Operation::TransferBegin, confirmation_begin) == 0);
    for (size_t offset = 0; offset < draft_size; offset += 320)
    {
        const auto count = std::min(size_t(320), draft_size - offset);
        auto chunk = [&](auto& w)
        {w.array(3);w.binary({id.data(),16});w.unsignedInteger(offset);w.binary({encoded_draft.data()+offset,count}); };
        assert(invoke(Operation::TransferChunk, chunk) == 0);
    }
    assert(invoke(Operation::TransferFinish, only_id) == 0);
    assert(web_config::load(metadata, scratch) && metadata.kind == 6 && metadata.phase == 1);
    assert(static_cast<uint8_t>(fake_files[web_config::kMetaPath][76]) == 0xc1);
    assert(fake_files[web_config::candidate_path(6)].size() == draft_size);
    assert(invoke(Operation::ConfigValidate, only_id) == 3 && invoke(Operation::ConfigCommit, commit) == 3);
    assert(invoke(Operation::TransferAbort, only_id) == 0);
    auto oversized_confirmation = [&](auto& w)
    {w.array(4);w.binary({id.data(),16});w.unsignedInteger(6);w.unsignedInteger(4167);w.binary({hash,32}); };
    assert(invoke(Operation::TransferBegin, oversized_confirmation) == 3);
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    metadata.kind = 2;
    metadata.phase = 3;
    web_config::put32(metadata.base_hash + 24, 7);
    assert(web_config::save(metadata, scratch));
    std::array<uint8_t, 64> author{};
    const auto before_recovery = fake_files;
    for (const auto result : {runtime::WebReadResult::Pending, runtime::WebReadResult::Ready, runtime::WebReadResult::Forbidden, runtime::WebReadResult::Conflict, runtime::WebReadResult::Unavailable})
    {
        runtime::fake_owner_result = result;
        assert(web_config::reconcileCommittedDraft(metadata, author.data(), 7, scratch) != Status::Ok);
        assert(metadata.phase == 3 && fake_files == before_recovery);
    }
    runtime::fake_owner_result = runtime::WebReadResult::AlreadySaved;
    assert(web_config::reconcileCommittedDraft(metadata, author.data(), 8, scratch) == Status::StaleIdentity);
    assert(fake_files == before_recovery && metadata.phase == 3);
    assert(web_config::reconcileCommittedDraft(metadata, author.data(), 7, scratch) == Status::Ok);
    assert(metadata.phase == 4 && web_config::load(metadata, scratch) && metadata.phase == 4);
    auto new_id = id;
    new_id[0]++;
    owner[0] = 9; // A new browser session can reuse an already-committed slot.
    auto new_signature = [&](auto& w)
    {w.array(4);w.binary({new_id.data(),16});w.unsignedInteger(5);w.unsignedInteger(draft_size);w.binary({hash,32}); };
    assert(invoke(Operation::TransferBegin, new_signature) == 0);
    assert(web_config::load(metadata, scratch) && metadata.phase == 0 && metadata.kind == 5);
    assert(!std::memcmp(metadata.owner, owner.data(), 16) && !std::memcmp(metadata.id, new_id.data(), 16));
    metadata.kind = 2;
    metadata.phase = 3;
    metadata.offset = metadata.total;
    metadata.id[0] = 44;
    web_config::put32(metadata.base_hash + 24, 7);
    assert(web_config::save(metadata, scratch));
    const auto stranded_upload = fake_files;
    RecoveryAuthor recovery;
    app::fake_messaging.adapter = &recovery;
    owner[0] = 22;
    runtime::fake_owner_result = runtime::WebReadResult::Pending;
    assert(invoke(Operation::TransferBegin, new_signature) == 5 && fake_files == stranded_upload);
    recovery.generation = 8;
    assert(invoke(Operation::TransferBegin, new_signature) == 9 && fake_files == stranded_upload);
    recovery.generation = 7;
    recovery.result = chat::DeviceAuthorResult::StorageError;
    assert(invoke(Operation::TransferBegin, new_signature) == 6 && fake_files == stranded_upload);
    recovery.result = chat::DeviceAuthorResult::Ok;
    runtime::fake_owner_result = runtime::WebReadResult::Forbidden;
    assert(invoke(Operation::TransferBegin, new_signature) == 2 && fake_files == stranded_upload);
    runtime::fake_owner_result = runtime::WebReadResult::AlreadySaved;
    assert(invoke(Operation::TransferBegin, new_signature) == 0);
    assert(web_config::load(metadata, scratch) && metadata.kind == 5 && metadata.phase == 0);
    assert(!std::memcmp(metadata.owner, owner.data(), 16));
    // Recovery rotates only transfer metadata, not device config or the saved draft.
    assert(fake_file == config_after_commit && app::sd_tms::fake_commits == 1);
    assert(fake_files.count(web_config::candidate_path(2)) == stranded_upload.count(web_config::candidate_path(2)));
    if (stranded_upload.count(web_config::candidate_path(2)))
        assert(fake_files.at(web_config::candidate_path(2)) == stranded_upload.at(web_config::candidate_path(2)));
    app::fake_messaging.adapter = nullptr;
    // Recovery only exposes the owner of exact sealed unsigned input belonging
    // to the current author. Query never changes files or signs/deletes input.
    std::array<uint8_t, 64> current_author{};
    current_author[0] = 27;
    std::array<uint8_t, 16> nonce{};
    nonce[0] = 28;
    geocaching::protocol::CmpWriter unsigned_writer(encoded_draft.data(), encoded_draft.size());
    assert(unsigned_writer.array(16) && unsigned_writer.unsignedInteger(1) &&
           unsigned_writer.binary({current_author.data(), 64}) && unsigned_writer.binary({nonce.data(), 16}));
    for (unsigned i = 0; i < 13; ++i) assert(unsigned_writer.nil());
    const auto raw_size = unsigned_writer.size();
    fake_files[web_config::candidate_path(5)].assign(reinterpret_cast<const char*>(encoded_draft.data()), raw_size);
    metadata.kind = 5;
    metadata.phase = 1;
    metadata.total = metadata.offset = raw_size;
    std::memcpy(metadata.owner, owner.data(), 16);
    std::memcpy(metadata.id, id.data(), 16);
    assert(web_config::hash_file(web_config::candidate_path(5), scratch, metadata.hash, raw_size));
    assert(web_config::save(metadata, scratch));
    const uint8_t query_body[]{0x92, 5, 0xc0};
    EnvelopeView query;
    query.session = request.data(); // A different session is allowed to query.
    query.body = query_body;
    query.body_size = sizeof(query_body);
    size_t written = 0;
    const auto sealed_files = fake_files;
    auto query_upload = [&]
    { return web_config::listSharedSignatureUpload(query, current_author.data(), 7, 11, scratch, sizeof(scratch), written); };
    assert(query_upload() == Status::Ok && fake_files == sealed_files);
    geocaching::protocol::CmpReader recovery_reader({scratch, written});
    size_t fields = 0;
    uint64_t value = 0;
    geocaching::ByteView view;
    assert(recovery_reader.array(fields, 3) && fields == 3 && recovery_reader.unsignedInteger(value) && value == 11 && recovery_reader.nil());
    assert(recovery_reader.array(fields, 9) && fields == 9 && recovery_reader.binary(view, 16) && !std::memcmp(view.data, metadata.id, 16));
    assert(recovery_reader.unsignedInteger(value) && value == 5 && recovery_reader.unsignedInteger(value) && value == raw_size);
    assert(recovery_reader.binary(view, 32) && !std::memcmp(view.data, metadata.hash, 32));
    assert(recovery_reader.unsignedInteger(value) && value == 7 && recovery_reader.unsignedInteger(value) && value == 1 &&
           recovery_reader.unsignedInteger(value) && value == raw_size);
    assert(recovery_reader.binary(view, 16) && !std::memcmp(view.data, owner.data(), 16));
    assert(recovery_reader.binary(view, 16) && !std::memcmp(view.data, nonce.data(), 16) && recovery_reader.finished());
    current_author[0]++;
    assert(query_upload() == Status::Ok && scratch[written - 1] == 0xc0 && fake_files == sealed_files);
    current_author[0]--;
    fake_files[web_config::candidate_path(5)].back() ^= 1;
    const auto corrupted_files = fake_files;
    assert(query_upload() == Status::HashMismatch && written == 0 && fake_files == corrupted_files);
    fake_files = sealed_files;
    for (const auto phase : {0, 2})
    {
        metadata.phase = phase;
        assert(web_config::save(metadata, scratch));
        const auto before = fake_files;
        assert(query_upload() == Status::Ok && scratch[written - 1] == 0xc0 && fake_files == before);
    }
    metadata.kind = 6;
    metadata.phase = 1;
    assert(web_config::save(metadata, scratch));
    const auto confirmation_files = fake_files;
    assert(query_upload() == Status::Ok && scratch[written - 1] == 0xc0 && fake_files == confirmation_files);
    query.body_size--;
    assert(query_upload() == Status::InvalidField && written == 0 && fake_files == confirmation_files);
    const uint8_t confirm_query[]{0x92, 6, 0xc0};
    query.body = confirm_query;
    query.body_size = sizeof(confirm_query);
    std::array<uint8_t, 1024> signed_bytes{};
    std::array<uint8_t, 64> signature{};
    geocaching::protocol::CmpWriter signed_writer(signed_bytes.data(), signed_bytes.size());
    assert(signed_writer.array(2) && signed_writer.binary({encoded_draft.data(), raw_size}) && signed_writer.binary({signature.data(), signature.size()}));
    metadata.total = metadata.offset = signed_writer.size();
    fake_files[web_config::candidate_path(6)].assign(reinterpret_cast<const char*>(signed_bytes.data()), metadata.total);
    assert(web_config::hash_file(web_config::candidate_path(6), scratch, metadata.hash, metadata.total));
    assert(web_config::save(metadata, scratch));
    const auto sealed_confirmation = fake_files;
    assert(query_upload() == Status::Ok && fake_files == sealed_confirmation);
    geocaching::protocol::CmpReader confirmed_row({scratch, written});
    assert(confirmed_row.array(fields, 3) && fields == 3 && confirmed_row.unsignedInteger(value) && confirmed_row.nil());
    assert(confirmed_row.array(fields, 9) && fields == 9 && confirmed_row.binary(view, 16) && confirmed_row.unsignedInteger(value) && value == 6);
    auto completion = [&](unsigned phase, bool wrong_hash = false)
    {
        geocaching::protocol::CmpWriter writer(scratch, sizeof(scratch));
        auto hash = metadata.hash;
        uint8_t bad_hash[32]{};
        assert(writer.array(8) && writer.binary({metadata.id, 16}) && writer.binary({wrong_hash ? bad_hash : hash, 32}) &&
               writer.unsignedInteger(7) && writer.unsignedInteger(phase));
        if (phase == 1) assert(writer.binary({base, 32}) && writer.unsignedInteger(1) && writer.binary({base, 32}) && writer.unsignedInteger(1));
        else assert(writer.nil() && writer.nil() && writer.nil() && writer.unsignedInteger(0));
        written = writer.size();
        return web_config::finishConfirmedUpload(query, scratch, sizeof(scratch), written);
    };
    assert(completion(1) == Status::Conflict && written == 0 && fake_files == sealed_confirmation); // foreign correlation
    query.session = metadata.owner;
    assert(completion(1, true) == Status::Conflict && written == 0 && fake_files == sealed_confirmation);
    assert(completion(0) == Status::Ok && fake_files == sealed_confirmation);
    fake_write_failure = true;
    assert(completion(1) == Status::StorageUnavailable && written == 0);
    fake_write_failure = false;
    assert(web_config::load(metadata, scratch) && metadata.phase == 1);
    assert(completion(1) == Status::Ok);
    assert(web_config::load(metadata, scratch) && metadata.kind == 6 && metadata.phase == 2);
    const auto finished_confirmation = fake_files;
    assert(completion(1) == Status::Ok && fake_files == finished_confirmation); // exact lost-reply replay
    assert(query_upload() == Status::Ok && scratch[written - 1] == 0xc0);
    id[0]++;
    assert(invoke(Operation::TransferBegin, draft_begin) == 0);
    assert(web_config::load(metadata, scratch) && metadata.kind == 2 && metadata.phase == 0);
    assert(fake_file == config_after_commit && app::sd_tms::fake_commits == 1);

    // A failed draft save leaves bound input in phase 2. A new browser must be
    // able to retry those exact bytes without knowing the abandoned session ID.
    recovery.result = chat::DeviceAuthorResult::Ok;
    recovery.generation = 7;
    app::fake_messaging.adapter = &recovery;
    fake_files[web_config::candidate_path(2)] = original_draft_input;
    mbedtls_sha256_context retry_hash;
    platform::esp::common::crypto::sha256_update(&retry_hash,
                                                 reinterpret_cast<const uint8_t*>(original_draft_input.data()), original_draft_input.size());
    platform::esp::common::crypto::sha256_finish(&retry_hash, hash);
    const auto new_owner = owner;
    const auto new_transfer = id;
    for (const uint8_t phase : {uint8_t(2), uint8_t(3)})
    {
        metadata = {};
        metadata.kind = 2;
        metadata.phase = phase;
        metadata.total = metadata.offset = static_cast<uint32_t>(original_draft_input.size());
        std::memcpy(metadata.owner, new_owner.data(), 16);
        metadata.owner[0] ^= 0x40;
        std::memcpy(metadata.id, new_transfer.data(), 16);
        metadata.id[0] ^= 0x40;
        std::memcpy(metadata.hash, hash, 32);
        metadata.base_hash[0] = 17; // Original draft and compare-and-swap version.
        metadata.base_hash[16] = 1;
        web_config::put32(metadata.base_hash + 24, 7);
        assert(web_config::save(metadata, scratch));
        const auto bound = metadata;
        const auto pending_files = fake_files;
        runtime::fake_owner_result = runtime::WebReadResult::Ready;
        runtime::fake_release_draft_save = false;
        assert(invoke(Operation::TransferBegin, draft_begin) == 5 && fake_files == pending_files);
        runtime::fake_release_draft_save = true;
        hash[0] ^= 1;
        assert(invoke(Operation::TransferBegin, draft_begin) == 5 && fake_files == pending_files);
        hash[0] ^= 1;
        runtime::fake_owner_result = runtime::WebReadResult::Forbidden;
        assert(invoke(Operation::TransferBegin, draft_begin) == 2 && fake_files == pending_files);
        runtime::fake_owner_result = runtime::WebReadResult::Ready;
        recovery.generation = 8;
        assert(invoke(Operation::TransferBegin, draft_begin) == 9 && fake_files == pending_files);
        recovery.generation = 7;
        assert(invoke(Operation::TransferBegin, draft_begin) == 0);
        assert(web_config::load(metadata, scratch) && metadata.phase == 2);
        assert(!std::memcmp(metadata.owner, new_owner.data(), 16) && !std::memcmp(metadata.id, new_transfer.data(), 16));
        assert(!std::memcmp(metadata.base_hash, bound.base_hash, 32) && !std::memcmp(metadata.hash, hash, 32));
        assert(metadata.total == bound.total && metadata.offset == bound.offset);
        assert(fake_files.at(web_config::candidate_path(2)) == original_draft_input);
        assert(invoke(Operation::TransferBegin, draft_begin) == 0); // Exact retry is idempotent.
        assert(fake_file == config_after_commit && app::sd_tms::fake_commits == 1);
    }
    app::fake_messaging.adapter = nullptr;
}
