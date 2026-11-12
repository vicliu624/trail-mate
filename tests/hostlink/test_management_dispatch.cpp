#include "platform/esp/arduino_common/hostlink/hostlink_management_service.h"
#include "platform/esp/arduino_common/hostlink/web_geocache_confirmation.h"
#include "platform/esp/arduino_common/hostlink/web_geocache_signature.h"
#include <array>
#include <cassert>
#include <vector>
static std::vector<uint8_t> response;
static uint16_t response_sequence;
static unsigned draft_reads = 0;
static uint64_t draft_expected = 0;
static unsigned draft_put_queues = 0;
static bool draft_put_release = false;
using WebResult = platform::esp::arduino_common::geocaching::browse_runtime::WebReadResult;
static WebResult job_current_result = WebResult::Ready, job_history_result = WebResult::Ready;
static unsigned job_current_reads = 0, job_history_reads = 0;
static WebResult job_stop_result = WebResult::Ready;
static unsigned job_stop_calls = 0;
static unsigned job_queue_calls = 0, job_admissions = 0;
static bool job_queue_admit = true;
static unsigned local_signature_queues = 0;
static std::array<uint8_t, 16> local_signature_operation{};
static std::array<uint8_t, 32> local_signature_hash{};
namespace platform::esp::arduino_common::geocaching::browse_runtime
{
WebReadResult queueWebSignature(const uint8_t* operation, const uint8_t*, const uint8_t* transfer,
                                const uint8_t* hash, uint32_t total, const uint8_t* author, uint32_t generation,
                                const uint8_t*, uint64_t)
{
    assert(transfer[0] == 33 && total == 100 && generation == 7);
    for (unsigned i = 0; i < 64; ++i) assert(author[i] == i);
    std::memcpy(local_signature_operation.data(), operation, 16);
    std::memcpy(local_signature_hash.data(), hash, 32);
    ++local_signature_queues;
    return WebReadResult::Ready;
}
WebReadResult readWebSignature(const uint8_t* operation, const uint8_t*, bool status, uint32_t, uint16_t,
                               uint8_t* output, size_t capacity, size_t& written)
{
    assert(status && !std::memcmp(operation, local_signature_operation.data(), 16));
    ::geocaching::protocol::CmpWriter writer(output, capacity);
    assert(writer.array(5) && writer.binary({local_signature_operation.data(), 16}) && writer.binary({local_signature_hash.data(), 32}) &&
           writer.unsignedInteger(7) && writer.unsignedInteger(0) && writer.unsignedInteger(0));
    written = writer.size();
    return WebReadResult::Ready;
}
WebReadResult releaseWebSignature(const uint8_t*, const uint8_t*) { return WebReadResult::Ready; }
WebReadResult queueWebConfirmation(const uint8_t* transfer, const uint8_t*, const uint8_t* hash, uint32_t total, const uint8_t*, uint32_t generation, const uint8_t* proof, size_t proof_size)
{
    assert(transfer[0] == 33 && hash[0] == 44 && total == 100 && generation == 7 && proof && proof_size > 0);
    ++local_signature_queues;
    return WebReadResult::Ready;
}
WebReadResult readWebConfirmation(const uint8_t* transfer, const uint8_t*, uint8_t* output, size_t capacity, size_t& written)
{
    ::geocaching::protocol::CmpWriter writer(output, capacity);
    assert(writer.array(8) && writer.binary({transfer, 16}) && writer.binary({local_signature_hash.data(), 32}) &&
           writer.unsignedInteger(7) && writer.unsignedInteger(0) && writer.nil() && writer.nil() && writer.nil() && writer.nil());
    written = writer.size();
    return WebReadResult::Ready;
}
WebReadResult releaseWebConfirmation(const uint8_t*, const uint8_t*) { return WebReadResult::Ready; }
} // namespace platform::esp::arduino_common::geocaching::browse_runtime
static std::array<uint8_t, 16> job_operation{};
static WebResult draft_put_check = WebResult::Ready, draft_put_queue = WebResult::Pending;
static WebResult prepare_binding_result = WebResult::Ready, prepare_latest_result = WebResult::Pending;
static std::array<uint8_t, 88> prepare_binding{};
static std::array<uint8_t, 36> prepare_latest{};
static uint64_t prepare_clock = 1000;
static uint32_t commit_binding_delay = 0;
static uint8_t prepare_boot = 1;
static unsigned prepare_network_reads = 0;
static platform::esp::arduino_common::geocaching::browse_runtime::WebReadResult draft_result =
    platform::esp::arduino_common::geocaching::browse_runtime::WebReadResult::Pending;
namespace platform::esp::arduino_common::geocaching::browse_runtime
{
bool webLease(bool, bool) { return true; }
WebReadResult requestWebPublicationStop(const uint8_t* id, const uint8_t* author)
{
    ++job_stop_calls;
    assert(!std::memcmp(id, job_operation.data(), 16));
    for (unsigned i = 0; i < 64; ++i) assert(author[i] == i);
    return job_stop_result;
}
WebReadResult readWebPublicationJob(const uint8_t* id, const uint8_t* author, uint8_t* output, size_t capacity, size_t& written)
{
    ++job_current_reads;
    assert(!std::memcmp(id, job_operation.data(), 16));
    for (unsigned i = 0; i < 64; ++i) assert(author[i] == i);
    if (job_current_result != WebReadResult::Ready) return job_current_result;
    std::array<uint8_t, 32> hash{};
    ::geocaching::protocol::CmpWriter writer(output, capacity);
    assert(writer.array(8) && writer.binary({id, 16}) && writer.unsignedInteger(3) && writer.binary({hash.data(), 32}) &&
           writer.binary({hash.data(), 32}) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.nil());
    written = writer.size();
    return WebReadResult::Ready;
}
WebReadResult readWebPublicationHistory(const uint8_t* id, const uint8_t* author, uint8_t* output, size_t capacity, size_t& written)
{
    ++job_history_reads;
    if (job_history_result != WebReadResult::Ready) return job_history_result;
    const auto current = job_current_result;
    job_current_result = WebReadResult::Ready;
    const auto result = readWebPublicationJob(id, author, output, capacity, written);
    --job_current_reads;
    job_current_result = current;
    return result;
}
bool publicationContext(uint8_t* boot, uint64_t& clock)
{
    std::memset(boot, prepare_boot, 16);
    clock = prepare_clock;
    return true;
}
WebReadResult readWebPublicationBinding(const uint8_t*, uint64_t, const uint8_t*, const uint8_t*, uint8_t* output, size_t capacity)
{
    assert(capacity == 88 || capacity == 56);
    if (capacity == 56) prepare_clock += commit_binding_delay;
    if (prepare_binding_result == WebReadResult::Ready) std::memcpy(output, prepare_binding.data(), capacity);
    return prepare_binding_result;
}
bool queueWebDraftPublication(const uint8_t*, uint64_t generation, const uint8_t*, const uint8_t* author,
                              uint32_t revision, const uint8_t* binding, const uint8_t* operation)
{
    ++job_queue_calls;
    assert(generation > 0 && revision > 0 && !std::memcmp(binding, prepare_binding.data(), 56));
    assert(!std::memcmp(operation, job_operation.data(), 16));
    for (unsigned i = 0; i < 64; ++i) assert(author[i] == i);
    assert(platform::esp::arduino_common::storage::fake_files.at(hostlink::management::kPublicationTicketPath)[4] == 1);
    if (job_queue_admit)
    {
        ++job_admissions;
        job_current_result = WebResult::Ready;
    }
    return job_queue_admit;
}
WebReadResult readWebLatestRevision(const uint8_t*, const uint8_t*, uint8_t* output, size_t capacity)
{
    ++prepare_network_reads;
    assert(capacity == 36);
    std::memcpy(output, prepare_latest.data(), prepare_latest.size());
    return prepare_latest_result;
}
WebReadResult readWebDirectories(uint32_t offset, uint8_t* output, size_t capacity, size_t& written)
{
    ::geocaching::protocol::CmpWriter writer(output, capacity);
    assert(writer.array(3) && writer.unsignedInteger(offset) && writer.unsignedInteger(0) && writer.nil());
    written = writer.size();
    return WebReadResult::Ready;
}
bool releaseWebDraftSave() { return draft_put_release; }
WebReadResult checkWebDraftEditOwner(const uint8_t*, uint64_t, const uint8_t* author, const uint8_t*)
{
    for (unsigned i = 0; i < 64; ++i) assert(author[i] == i);
    return draft_put_check;
}
WebReadResult queueWebDraftFile(const uint8_t*, uint32_t, uint64_t, const uint8_t*, const uint8_t* author, uint32_t generation)
{
    for (unsigned i = 0; i < 64; ++i) assert(author[i] == i);
    assert(generation == 7);
    ++draft_put_queues;
    return draft_put_queue;
}
WebReadResult readWebDraftCatalog(uint32_t, bool, uint64_t, uint8_t* output, size_t, size_t& written)
{
    output[0] = 0x94;
    output[1] = 1;
    output[2] = 0;
    output[3] = 0;
    output[4] = 0xc0;
    written = 5;
    return WebReadResult::Ready;
}
WebReadResult readWebDraftChunk(const uint8_t id[16], uint32_t offset, uint16_t count, uint64_t expected,
                                uint8_t* output, size_t capacity, size_t& written)
{
    ++draft_reads;
    draft_expected = expected;
    if (draft_result != WebReadResult::Ready) return draft_result;
    const uint8_t data[] = {1, 2, 3};
    ::geocaching::protocol::CmpWriter writer(output, capacity);
    assert(offset == 0 && count == 320);
    assert(writer.array(5) && writer.binary({id, 16}) && writer.unsignedInteger(7) && writer.unsignedInteger(sizeof(data)) &&
           writer.unsignedInteger(offset) && writer.binary({data, sizeof(data)}));
    written = writer.size();
    return WebReadResult::Ready;
}
} // namespace platform::esp::arduino_common::geocaching::browse_runtime
struct FakeAuthor final : chat::IMeshAdapter
{
    mutable std::vector<uint8_t> message;
    chat::DeviceAuthorResult failure = chat::DeviceAuthorResult::Ok;
    chat::DeviceAuthorResult readDeviceAuthorKey(uint8_t* public_key, uint32_t& generation) const override
    {
        if (failure != chat::DeviceAuthorResult::Ok) return failure;
        for (unsigned index = 0; index < 64; ++index) public_key[index] = static_cast<uint8_t>(index);
        generation = 7;
        return chat::DeviceAuthorResult::Ok;
    }
    chat::DeviceAuthorResult proveDeviceAuthor(const uint8_t* session, const uint8_t* challenge, uint32_t expected,
                                               uint8_t* scratch, size_t capacity, uint8_t* key,
                                               uint32_t& generation, uint8_t* signature) const override
    {
        const auto result = readDeviceAuthorKey(key, generation);
        if (result != chat::DeviceAuthorResult::Ok) return result;
        if (expected != generation) return chat::DeviceAuthorResult::StaleIdentity;
        size_t written;
        if (!chat::deviceAuthorProofMessage(session, challenge, key, generation, scratch, capacity, written))
            return chat::DeviceAuthorResult::InvalidInput;
        message.assign(scratch, scratch + written);
        std::memset(signature, 0x66, 64);
        return chat::DeviceAuthorResult::Ok;
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
bool enqueue_event(uint8_t type, const uint8_t* payload, size_t length, bool, uint16_t sequence, uint32_t completion_cookie)
{
    assert(completion_cookie == 0);
    assert(type == static_cast<uint8_t>(FrameType::ManagementResponse));
    response.assign(payload, payload + length);
    response_sequence = sequence;
    return true;
}
} // namespace hostlink
int main()
{
    namespace web_config = hostlink::management::web_config;
    using namespace hostlink;
    std::array<uint8_t, 16> session{}, request{};
    session[0] = 1;
    request[0] = 2;
    PendingCommand command;
    command.type = PendingCommandType::Management;
    command.sequence = 92;
    const uint8_t empty[] = {0x90};
    size_t written;
    auto dispatch = [&](uint16_t op, const uint8_t* body, size_t length)
    {assert(management::encodeEnvelope(session.data(),request.data(),op,body,length,command.payload,sizeof(command.payload),written));command.payload_len=static_cast<uint16_t>(written);assert(management::execute_management(command)==ErrorCode::Ok);management::EnvelopeView view;assert(management::decodeEnvelope(response.data(),response.size(),view));assert(response_sequence==92);assert(!std::memcmp(view.session,session.data(),16));return view; };
    chat_runtime::submitting = true;
    assert(dispatch(17, empty, sizeof(empty)).code == uint16_t(management::Status::Busy));
    assert(dispatch(10, empty, sizeof(empty)).code == uint16_t(management::Status::Busy));
    assert(dispatch(3, empty, sizeof(empty)).code == 0);
    assert(chat_runtime::submitting); // Closing a session cannot cancel accepted work.
    chat_runtime::submitting = false;
    auto caps = dispatch(0, empty, sizeof(empty));
    assert(caps.code == 0);
    geocaching::protocol::CmpReader cap_reader({caps.body, caps.body_size});
    size_t cap_fields = 0, operation_count = 0;
    uint64_t cap_value = 0;
    assert(cap_reader.array(cap_fields, 6) && cap_fields == 6);
    for (unsigned i = 0; i < 4; ++i) assert(cap_reader.unsignedInteger(cap_value));
    assert(cap_reader.array(operation_count, 32) && operation_count == 20);
    bool has_job_get = false, has_unimplemented_commit = false, has_job_stop = false;
    for (size_t i = 0; i < operation_count; ++i)
    {
        assert(cap_reader.unsignedInteger(cap_value));
        has_job_get |= cap_value == 48;
        has_job_stop |= cap_value == 50;
        has_unimplemented_commit |= cap_value == 68;
    }
    assert(cap_reader.unsignedInteger(cap_value) && cap_value == 7 && cap_reader.finished() && has_job_get && has_unimplemented_commit && has_job_stop);
    assert(dispatch(1, empty, sizeof(empty)).code == 5);
    FakeAuthor author;
    app::fake_messaging.adapter = &author;
    job_operation.fill(0x5b);
    uint8_t job_body[32];
    geocaching::protocol::CmpWriter job_request(job_body, sizeof(job_body));
    assert(job_request.array(2) && job_request.binary({job_operation.data(), 16}) && job_request.unsignedInteger(7));
    auto job_reply = dispatch(48, job_body, job_request.size());
    assert(job_reply.code == 0 && job_current_reads == 1 && job_history_reads == 0);
    geocaching::protocol::CmpReader job_projection({job_reply.body, job_reply.body_size});
    size_t job_fields = 0;
    geocaching::ByteView returned_job;
    uint64_t phase = 0;
    assert(job_projection.array(job_fields, 8) && job_fields == 8 && job_projection.binary(returned_job, 16) &&
           !std::memcmp(returned_job.data, job_operation.data(), 16) && job_projection.unsignedInteger(phase) && phase == 3);
    assert(dispatch(50, job_body, job_request.size()).code == 0 && job_stop_calls == 1);
    const auto reads_before_stop_pending = job_current_reads;
    job_stop_result = WebResult::Pending;
    assert(dispatch(50, job_body, job_request.size()).code == 5 && job_current_reads == reads_before_stop_pending);
    job_stop_result = WebResult::Forbidden;
    assert(dispatch(50, job_body, job_request.size()).code == 2);
    job_stop_result = WebResult::NotFound;
    assert(dispatch(50, job_body, job_request.size()).code == 10);
    job_stop_result = WebResult::StaleIdentity;
    assert(dispatch(50, job_body, job_request.size()).code == 9);
    job_stop_result = WebResult::Ready;
    job_current_result = WebResult::NotFound;
    assert(dispatch(48, job_body, job_request.size()).code == 0 && job_history_reads == 1);
    job_history_result = WebResult::Pending;
    assert(dispatch(48, job_body, job_request.size()).code == 5);
    job_history_result = WebResult::NotFound;
    assert(dispatch(48, job_body, job_request.size()).code == 10);
    job_history_result = WebResult::Forbidden;
    assert(dispatch(48, job_body, job_request.size()).code == 2);
    job_history_result = WebResult::Invalid;
    assert(dispatch(48, job_body, job_request.size()).code == 6);
    job_current_result = WebResult::StaleIdentity;
    const auto history_reads_before_stale = job_history_reads;
    assert(dispatch(48, job_body, job_request.size()).code == 9 && job_history_reads == history_reads_before_stale);
    job_body[job_request.size() - 1] = 8;
    const auto reads_before_stale = job_current_reads;
    const auto stops_before_stale = job_stop_calls;
    assert(dispatch(50, job_body, job_request.size()).code == 9 && job_stop_calls == stops_before_stale);
    assert(dispatch(48, job_body, job_request.size()).code == 9 && job_current_reads == reads_before_stale);
    assert(dispatch(48, empty, sizeof(empty)).code == 3);
    assert(dispatch(50, empty, sizeof(empty)).code == 3 && job_stop_calls == stops_before_stale);
    std::memset(job_body + 3, 0, 16);
    job_body[job_request.size() - 1] = 7;
    assert(dispatch(48, job_body, job_request.size()).code == 3 && job_current_reads == reads_before_stale);
    auto author_info = dispatch(1, empty, sizeof(empty));
    assert(author_info.code == 0);
    geocaching::protocol::CmpReader facts({author_info.body, author_info.body_size});
    size_t fact_count;
    uint64_t generation;
    geocaching::ByteView public_key, identity_hash, destination;
    assert(facts.array(fact_count, 4) && fact_count == 4 && facts.binary(public_key, 64) &&
           facts.binary(identity_hash, 16) && facts.binary(destination, 16) && facts.unsignedInteger(generation) && facts.finished());
    assert(public_key.size == 64 && generation == 7 && identity_hash.size == 16 && destination.size == 16);
    uint8_t nonce[32], expected_key[64], proof_body[64], expected_message[160];
    std::memset(nonce, 0x77, sizeof(nonce));
    for (unsigned index = 0; index < 64; ++index) expected_key[index] = static_cast<uint8_t>(index);
    geocaching::protocol::CmpWriter proof_writer(proof_body, sizeof(proof_body));
    assert(proof_writer.array(2) && proof_writer.binary({nonce, 32}) && proof_writer.unsignedInteger(7));
    auto proof = dispatch(2, proof_body, proof_writer.size());
    assert(proof.code == 0 && proof.body[0] == 0x95);
    size_t expected_size;
    assert(chat::deviceAuthorProofMessage(session.data(), nonce, expected_key, 7, expected_message, sizeof(expected_message), expected_size));
    assert(author.message == std::vector<uint8_t>(expected_message, expected_message + expected_size));
    geocaching::protocol::CmpReader proof_reader({proof.body, proof.body_size});
    geocaching::ByteView signature;
    assert(proof_reader.array(fact_count, 5) && proof_reader.binary(public_key, 64) && proof_reader.binary(identity_hash, 16) &&
           proof_reader.binary(destination, 16) && proof_reader.unsignedInteger(generation) && proof_reader.binary(signature, 64) && proof_reader.finished());
    assert(!std::memcmp(public_key.data, expected_key, 64) && generation == 7 && signature.size == 64 && signature.data[0] == 0x66);
    proof_body[proof_writer.size() - 1] = 8;
    assert(dispatch(2, proof_body, proof_writer.size()).code == 9);
    assert(dispatch(2, empty, sizeof(empty)).code == 3);
    uint8_t draft_request[64];
    geocaching::protocol::CmpWriter draft_writer(draft_request, sizeof(draft_request));
    assert(draft_writer.array(5) && draft_writer.binary({session.data(), 16}) && draft_writer.unsignedInteger(0) &&
           draft_writer.unsignedInteger(320) && draft_writer.unsignedInteger(0) && draft_writer.unsignedInteger(7));
    assert(dispatch(65, draft_request, draft_writer.size()).code == 5 && draft_reads == 1 && draft_expected == 0);
    draft_result = platform::esp::arduino_common::geocaching::browse_runtime::WebReadResult::Ready;
    assert(dispatch(65, draft_request, draft_writer.size()).code == 0 && draft_reads == 2);
    draft_request[draft_writer.size() - 1] = 8;
    assert(dispatch(65, draft_request, draft_writer.size()).code == 9 && draft_reads == 2);
    assert(dispatch(65, empty, sizeof(empty)).code == 3 && draft_reads == 2);
    const uint8_t list_first[] = {0x93, 0, 0xc0, 7};
    assert(dispatch(64, list_first, sizeof(list_first)).code == 0);
    const uint8_t list_stale_author[] = {0x93, 0, 0xc0, 8};
    assert(dispatch(64, list_stale_author, sizeof(list_stale_author)).code == 9);
    const uint8_t list_unbound_continuation[] = {0x93, 1, 0xc0, 7};
    assert(dispatch(64, list_unbound_continuation, sizeof(list_unbound_continuation)).code == 3);
    assert(dispatch(64, empty, sizeof(empty)).code == 3);
    const uint8_t directory_first[] = {0x91, 0}, directory_outside[] = {0x91, 5};
    assert(dispatch(70, directory_first, sizeof(directory_first)).code == 0);
    assert(dispatch(70, directory_outside, sizeof(directory_outside)).code == 3);
    assert(dispatch(70, empty, sizeof(empty)).code == 3);
    web_config::Metadata upload;
    std::memcpy(upload.owner, session.data(), 16);
    std::memcpy(upload.id, session.data(), 16);
    upload.kind = 2;
    upload.phase = 1;
    upload.total = upload.offset = 5;
    platform::esp::arduino_common::storage::fake_files[web_config::candidate_path(2)] = "draft";
    uint8_t upload_scratch[128];
    assert(web_config::hash_file(web_config::candidate_path(2), upload_scratch, upload.hash, 5));
    assert(web_config::save(upload, upload_scratch));
    uint8_t put_body[80];
    geocaching::protocol::CmpWriter put_writer(put_body, sizeof(put_body));
    assert(put_writer.array(4) && put_writer.binary({upload.id, 16}) && put_writer.binary({request.data(), 16}) &&
           put_writer.unsignedInteger(0) && put_writer.unsignedInteger(7));
    draft_put_check = WebResult::Forbidden;
    assert(dispatch(66, put_body, put_writer.size()).code == 2 && draft_put_queues == 0);
    assert(web_config::load(upload, upload_scratch) && upload.phase == 2 && !std::memcmp(upload.base_hash, request.data(), 16));
    auto changed_target = std::array<uint8_t, 80>{};
    std::memcpy(changed_target.data(), put_body, put_writer.size());
    changed_target[21] ^= 1;
    assert(dispatch(66, changed_target.data(), put_writer.size()).code == 4);
    draft_put_check = WebResult::Ready;
    assert(dispatch(66, put_body, put_writer.size()).code == 5 && draft_put_queues == 1);
    assert(web_config::load(upload, upload_scratch) && upload.phase == 3);
    uint8_t abort_body[20];
    geocaching::protocol::CmpWriter abort_writer(abort_body, sizeof(abort_body));
    assert(abort_writer.array(1) && abort_writer.binary({upload.id, 16}));
    assert(dispatch(36, abort_body, abort_writer.size()).code == 4);
    uint8_t replacement_body[80];
    std::array<uint8_t, 16> replacement_id{};
    replacement_id.fill(9);
    geocaching::protocol::CmpWriter replacement_writer(replacement_body, sizeof(replacement_body));
    assert(replacement_writer.array(4) && replacement_writer.binary({replacement_id.data(), 16}) && replacement_writer.unsignedInteger(2) &&
           replacement_writer.unsignedInteger(5) && replacement_writer.binary({upload.hash, 32}));
    assert(dispatch(32, replacement_body, replacement_writer.size()).code == 5);
    auto changed_author = std::array<uint8_t, 80>{};
    std::memcpy(changed_author.data(), put_body, put_writer.size());
    changed_author[put_writer.size() - 1] = 8;
    assert(dispatch(66, changed_author.data(), put_writer.size()).code == 9);
    draft_put_check = WebResult::Unavailable;
    assert(dispatch(66, put_body, put_writer.size()).code == 6);
    assert(web_config::load(upload, upload_scratch) && upload.phase == 3);
    platform::esp::arduino_common::storage::fake_files.erase(web_config::candidate_path(2));
    draft_put_check = WebResult::AlreadySaved;
    auto committed_draft = dispatch(66, put_body, put_writer.size());
    assert(committed_draft.code == 0 && draft_put_queues == 1);
    geocaching::protocol::CmpReader committed_reader({committed_draft.body, committed_draft.body_size});
    geocaching::ByteView committed_transfer, committed_id;
    uint64_t committed_generation = 0;
    assert(committed_reader.array(fact_count, 3) && fact_count == 3 && committed_reader.binary(committed_transfer, 16) &&
           committed_reader.binary(committed_id, 16) && committed_reader.unsignedInteger(committed_generation) && committed_reader.finished());
    assert(committed_generation == 1 && !std::memcmp(committed_id.data, request.data(), 16));
    assert(web_config::load(upload, upload_scratch) && upload.phase == 4);
    draft_put_check = WebResult::Unavailable;
    assert(dispatch(66, put_body, put_writer.size()).code == 0 && draft_put_queues == 1);
    assert(dispatch(36, abort_body, abort_writer.size()).code == 4);
    session[0] = 8;
    assert(dispatch(66, put_body, put_writer.size()).code == 2);
    session[0] = 1;
    upload.id[0] = 3;
    upload.phase = 1;
    platform::esp::arduino_common::storage::fake_files[web_config::candidate_path(2)] = "draft";
    assert(web_config::save(upload, upload_scratch));
    put_body[3] = 3;
    abort_body[3] = 3;
    draft_put_check = WebResult::Ready;
    draft_put_queue = WebResult::Invalid;
    assert(dispatch(66, put_body, put_writer.size()).code == 3);
    assert(web_config::load(upload, upload_scratch) && upload.phase == 3);
    draft_put_release = true;
    assert(dispatch(66, put_body, put_writer.size()).code == 3);
    assert(web_config::load(upload, upload_scratch) && upload.phase == 2);
    assert(dispatch(36, abort_body, abort_writer.size()).code == 0);
    author.failure = chat::DeviceAuthorResult::StorageError;
    assert(dispatch(1, empty, sizeof(empty)).code == 6);
    app::fake_messaging.adapter = nullptr;
    auto unsupported = dispatch(18, empty, sizeof(empty));
    assert(unsupported.code == 1);
    const uint8_t read[] = {0x93, 0, 32, 0xc0};
    auto snapshot = dispatch(17, read, sizeof(read));
    assert(snapshot.code == 0);
    geocaching::protocol::CmpReader reader({snapshot.body, snapshot.body_size});
    size_t fields;
    uint64_t offset, total;
    geocaching::ByteView hash, chunk;
    assert(reader.array(fields, 4) && fields == 4 && reader.unsignedInteger(offset) && reader.unsignedInteger(total) && reader.binary(hash, 32) && reader.binary(chunk, 320) && reader.finished());
    assert(offset == 0 && total == platform::esp::arduino_common::storage::fake_file.size() && chunk.size == 32);
    uint8_t saved_hash[32];
    std::memcpy(saved_hash, hash.data, 32);
    const uint8_t no_hash[] = {0x93, 1, 1, 0xc0};
    assert(dispatch(17, no_hash, sizeof(no_hash)).code == 3);
    uint8_t continued[64];
    geocaching::protocol::CmpWriter writer(continued, sizeof(continued));
    assert(writer.array(3) && writer.unsignedInteger(32) && writer.unsignedInteger(32) && writer.binary({saved_hash, 32}));
    assert(dispatch(17, continued, writer.size()).code == 0);
    platform::esp::arduino_common::storage::fake_file[0] ^= 1;
    assert(dispatch(17, continued, writer.size()).code == 4);
    platform::esp::arduino_common::storage::fake_available = false;
    assert(dispatch(17, read, sizeof(read)).code == 6);
    using namespace platform::esp::arduino_common::storage;
    fake_available = true;
    app::fake_messaging.adapter = &author;
    author.failure = chat::DeviceAuthorResult::Ok;
    prepare_binding.fill(0);
    std::memset(prepare_binding.data(), 0xa5, 32);
    std::memset(prepare_binding.data() + 32, 0x44, 16);
    prepare_binding[52] = 1;
    std::array<uint8_t, 16> prepare_id{};
    prepare_id[0] = 0x23;
    std::array<uint8_t, 96> prepare_body{};
    auto prepare = [&](uint64_t edit_generation = 1, uint32_t identity_generation = 7)
    {
        geocaching::protocol::CmpWriter encoder(prepare_body.data(), prepare_body.size());
        assert(encoder.array(5) && encoder.binary({request.data(), 16}) && encoder.unsignedInteger(edit_generation) &&
               encoder.binary({session.data(), 16}) && encoder.unsignedInteger(identity_generation) &&
               encoder.binary({prepare_id.data(), 16}));
        return dispatch(67, prepare_body.data(), encoder.size());
    };
    assert(prepare().code == 5);
    std::array<uint8_t, 512> ticket{};
    assert(management::loadPublicationTicket(ticket.data(), ticket.size()) && ticket[4] == 3);
    const auto checking_disk = fake_files.at(management::kPublicationTicketPath);
    assert(prepare().code == 5 && fake_files.at(management::kPublicationTicketPath) == checking_disk);
    assert(prepare(2).code == 4);
    const auto network_calls = prepare_network_reads;
    assert(prepare(1, 8).code == 9 && prepare_network_reads == network_calls);
    prepare_latest_result = WebResult::Ready;
    prepare_latest[0] = 1;
    assert(prepare().code == 4); // A different publication already exists in the directory.
    prepare_latest_result = WebResult::Absent;
    auto prepared = prepare();
    assert(prepared.code == 0);
    geocaching::protocol::CmpReader confirmation({prepared.body, prepared.body_size});
    geocaching::ByteView confirmed_token, confirmed_draft, confirmed_author, confirmed_directory, confirmed_epoch, confirmed_hash;
    uint64_t confirmed_generation = 0, from_revision = 0, to_revision = 0, remaining = 0;
    assert(confirmation.array(fields, 10) && fields == 10 && confirmation.binary(confirmed_token, 16) &&
           confirmation.binary(confirmed_draft, 16) && confirmation.unsignedInteger(confirmed_generation) &&
           confirmation.binary(confirmed_author, 64) && confirmation.binary(confirmed_directory, 16) &&
           confirmation.binary(confirmed_epoch, 16) && confirmation.binary(confirmed_hash, 32) &&
           confirmation.unsignedInteger(from_revision) && confirmation.unsignedInteger(to_revision) &&
           confirmation.unsignedInteger(remaining) && confirmation.finished());
    assert(confirmed_token.size == 16 && !std::memcmp(confirmed_token.data, prepare_id.data(), 16) &&
           confirmed_draft.size == 16 && !std::memcmp(confirmed_draft.data, request.data(), 16) && confirmed_generation == 1 &&
           confirmed_directory.size == 16 && !std::memcmp(confirmed_directory.data, session.data(), 16) &&
           confirmed_epoch.size == 16 && !std::memcmp(confirmed_epoch.data, prepare_binding.data() + 32, 16) &&
           confirmed_hash.size == 32 && !std::memcmp(confirmed_hash.data, prepare_binding.data(), 32) &&
           from_revision == 0 && to_revision == 1 && remaining == 120000);
    for (unsigned i = 0; i < 64; ++i) assert(confirmed_author.data[i] == i);
    const auto completed_calls = prepare_network_reads;
    prepare_clock += 100;
    assert(prepare().code == 0 && prepare_network_reads == completed_calls);
    session[0] ^= 1;
    assert(prepare().code == 2);
    session[0] ^= 1;
    prepare_clock = 121000;
    assert(prepare().code == 10);
    prepare_id[0] += 1;
    prepare_latest_result = WebResult::Pending;
    assert(prepare().code == 5);
    prepare_binding[0] ^= 1;
    assert(prepare().code == 4);
    prepare_binding[0] ^= 1;
    prepare_boot = 2;
    assert(prepare().code == 4);
    prepare_id[0] += 1;
    fake_write_failure = true;
    const auto protected_disk = fake_files.at(management::kPublicationTicketPath);
    assert(prepare().code == 6 && fake_files.at(management::kPublicationTicketPath) == protected_disk);
    fake_write_failure = false;
    assert(prepare().code == 5);
    assert(management::loadPublicationTicket(ticket.data(), ticket.size()));
    assert(management::completePublicationCheck(ticket.data(), management::kPublicationTicketBytes, prepare_clock));
    assert(management::advancePublicationTicket(ticket.data(), management::kPublicationTicketBytes, management::PublicationTicketPhase::Intent));
    assert(management::savePublicationTicket(ticket.data(), management::kPublicationTicketBytes));
    prepare_id[0] += 1;
    prepare_clock += 500000;
    assert(prepare().code == 4); // Unresolved commit intent is never overwritten by PREPARE.
    fake_files[management::kPublicationTicketPath][53] ^= 1;
    assert(prepare().code == 6);
    fake_files.erase(management::kPublicationTicketPath);
    prepare_id[0] += 1;
    prepare_binding[48] = 1;
    prepare_binding[52] = 2;
    std::memset(prepare_binding.data() + 56, 0xb1, 32);
    prepare_latest_result = WebResult::Ready;
    prepare_latest[0] = 2;
    assert(prepare(2).code == 4);
    prepare_latest[0] = 1;
    assert(prepare(2).code == 4); // Same revision number is insufficient without the predecessor hash.
    std::memset(prepare_latest.data() + 4, 0xb1, 32);
    assert(prepare(2).code == 0);
    ++prepare_id[0];
    session[0] ^= 1;
    assert(prepare(2).code == 2);
    session[0] ^= 1;
    assert(prepare(2).code == 0); // The same owner may replace an unsubmitted review without waiting for expiry.
    job_operation = prepare_id;
    job_current_result = job_history_result = WebResult::NotFound;
    std::array<uint8_t, 64> commit_body{};
    geocaching::protocol::CmpWriter commit_encoder(commit_body.data(), commit_body.size());
    assert(commit_encoder.array(3) && commit_encoder.binary({prepare_id.data(), 16}) &&
           commit_encoder.binary({prepare_binding.data(), 32}) && commit_encoder.unsignedInteger(7));
    const auto commit = [&]
    { return dispatch(68, commit_body.data(), commit_encoder.size()); };
    job_history_result = WebResult::Ready;
    assert(commit().code == 4 && !job_queue_calls); // A retained ID cannot be reused by a new ticket.
    job_history_result = WebResult::NotFound;
    const auto clock_before_expiry = prepare_clock;
    commit_binding_delay = 120000;
    assert(commit().code == 10 && !job_queue_calls);
    commit_binding_delay = 0;
    prepare_clock = clock_before_expiry;
    session[0] ^= 1;
    assert(commit().code == 2 && !job_queue_calls);
    session[0] ^= 1;
    commit_body[21] ^= 1;
    assert(commit().code == 4 && !job_queue_calls);
    commit_body[21] ^= 1;
    commit_body[commit_encoder.size() - 1] = 8;
    assert(commit().code == 9 && !job_queue_calls);
    commit_body[commit_encoder.size() - 1] = 7;
    prepare_binding[0] ^= 1;
    assert(commit().code == 4 && !job_queue_calls);
    prepare_binding[0] ^= 1;
    fake_write_failure = true;
    assert(commit().code == 6 && !job_queue_calls);
    fake_write_failure = false;
    job_queue_admit = false;
    assert(commit().code == 5 && job_queue_calls == 1 && !job_admissions);
    assert(management::loadPublicationTicket(ticket.data(), ticket.size()) && ticket[4] == 1);
    job_queue_admit = true;
    auto accepted = commit();
    assert(accepted.code == 0 && job_queue_calls == 2 && job_admissions == 1);
    geocaching::protocol::CmpReader admission({accepted.body, accepted.body_size});
    geocaching::ByteView operation_id, draft_id, selected_directory, content_hash;
    uint64_t admitted_generation = 0, admitted_revision = 0, admitted = 0;
    assert(admission.array(fields, 7) && fields == 7 && admission.binary(operation_id, 16) &&
           admission.binary(draft_id, 16) && admission.unsignedInteger(admitted_generation) &&
           admission.binary(selected_directory, 16) && admission.binary(content_hash, 32) &&
           admission.unsignedInteger(admitted_revision) && admission.unsignedInteger(admitted) && admission.finished());
    assert(!std::memcmp(operation_id.data, prepare_id.data(), 16) && !std::memcmp(draft_id.data, request.data(), 16) &&
           admitted_generation == 2 && !std::memcmp(content_hash.data, prepare_binding.data(), 32) && admitted_revision == 2 && admitted == 1);
    assert(management::loadPublicationTicket(ticket.data(), ticket.size()) && ticket[4] == 2);
    const auto admitted_disk = fake_files.at(management::kPublicationTicketPath);
    assert(commit().code == 0 && job_admissions == 1 && fake_files.at(management::kPublicationTicketPath) == admitted_disk);
    ++prepare_boot;
    prepare_clock += 500000;
    job_current_result = WebResult::NotFound;
    job_history_result = WebResult::Ready;
    assert(commit().code == 0 && job_admissions == 1); // A saved result survives boot/expiry.
    job_history_result = WebResult::NotFound;
    assert(commit().code == 5 && job_admissions == 1); // Unknown admission is never requeued.
    const auto old_prepare_id = prepare_id;
    ++prepare_id[0];
    assert(prepare(2).code == 4 && fake_files.at(management::kPublicationTicketPath) == admitted_disk);
    job_history_result = WebResult::Ready;
    assert(prepare(2).code == 0); // A verified terminal result allows the next prepare.
    assert(job_admissions == 1 && job_operation == old_prepare_id);
    management::web_config::Metadata signature_metadata;
    signature_metadata.owner[0] = 22;
    signature_metadata.id[0] = 33;
    signature_metadata.hash[0] = 44;
    signature_metadata.kind = 5;
    signature_metadata.phase = 1;
    signature_metadata.total = signature_metadata.offset = 100;
    std::array<uint8_t, 512> arena{};
    assert(management::web_config::save(signature_metadata, arena.data()));
    std::array<uint8_t, 16> signature_operation{};
    signature_operation[0] = 55;
    geocaching::protocol::CmpWriter signature_writer(arena.data() + 41, arena.size() - 41);
    assert(signature_writer.array(5) && signature_writer.binary({signature_metadata.id, 16}) && signature_writer.binary({signature_metadata.hash, 32}) &&
           signature_writer.unsignedInteger(100) && signature_writer.unsignedInteger(7) && signature_writer.binary({signature_operation.data(), 16}));
    management::EnvelopeView signature_request;
    signature_request.session = signature_metadata.owner;
    signature_request.code = uint16_t(management::Operation::CacheSignBegin);
    signature_request.body = arena.data() + 41;
    signature_request.body_size = signature_writer.size();
    size_t signature_written = 0;
    assert(management::cache_signature(signature_request, arena.data() + 41, arena.size() - 41, signature_written) == management::Status::Ok);
    assert(local_signature_queues == 1 && local_signature_operation == signature_operation && local_signature_hash[0] == 44);
    {
        signature_metadata.kind = 6;
        assert(management::web_config::save(signature_metadata, arena.data() + 41));
        std::array<uint8_t, 192> proof_raw{};
        std::array<uint8_t, 16> epoch{};
        epoch.fill(9);
        std::array<uint8_t, 64> proof_signature{}, directory_key{};
        ::geocaching::protocol::CmpWriter proof_writer(proof_raw.data(), proof_raw.size());
        assert(proof_writer.array(8) && proof_writer.unsignedInteger(1) && proof_writer.binary({epoch.data(), 16}) && proof_writer.binary({epoch.data(), 16}) &&
               proof_writer.binary({signature_metadata.hash, 32}) && proof_writer.unsignedInteger(1) && proof_writer.binary({signature_metadata.hash, 32}) && proof_writer.unsignedInteger(0) && proof_writer.unsignedInteger(1800000000));
        ::geocaching::protocol::CmpWriter confirmation_writer(arena.data() + 41, arena.size() - 41);
        assert(confirmation_writer.array(7) && confirmation_writer.binary({signature_metadata.id, 16}) && confirmation_writer.binary({signature_metadata.hash, 32}) && confirmation_writer.unsignedInteger(7) &&
               confirmation_writer.binary({proof_raw.data(), proof_writer.size()}) && confirmation_writer.binary({proof_signature.data(), 64}) && confirmation_writer.binary({directory_key.data(), 64}) && confirmation_writer.binary({epoch.data(), 16}));
        signature_request.code = uint16_t(management::Operation::CacheConfirmBegin);
        signature_request.body_size = confirmation_writer.size();
        assert(management::cache_confirmation(signature_request, arena.data() + 41, arena.size() - 41, signature_written) == management::Status::Ok && local_signature_queues == 2);
    }
}
