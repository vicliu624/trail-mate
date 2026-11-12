#include "platform/esp/arduino_common/hostlink/hostlink_chat_runtime.h"

#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
#include "app/app_facade_access.h"
#include "chat/usecase/chat_service.h"
#include "hostlink/chat_management_codec.h"
#include "hostlink/chat_submission_journal.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "ui/presentation_sources/team_chat_action_sink.h"
#include "ui/team_actions/team_runtime_adapters.h"
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <new>
#include <nvs.h>
#include <string>

namespace hostlink::chat_runtime
{
namespace
{
using namespace chat_management;
using management::Status;
namespace sha = platform::esp::common::crypto;

class Runtime final : public JournalIo, public SubmissionPort
{
  public:
    Runtime() : journal(*this), submission(*this) { esp_fill_random(boot, sizeof(boot)); }
    bool initialize()
    {
        if (nvs) return true;
        if (nvs_open("hl_chat", NVS_READWRITE, &nvs) != ESP_OK) return false;
        size_t length = kIdSize;
        auto error = nvs_get_blob(nvs, "store", store, &length);
        if (error == ESP_ERR_NVS_NOT_FOUND)
        {
            esp_fill_random(store, sizeof(store));
            if (nvs_set_blob(nvs, "store", store, sizeof(store)) == ESP_OK && nvs_commit(nvs) == ESP_OK) return true;
        }
        else if (error == ESP_OK && length == kIdSize) return true;
        nvs_close(nvs);
        nvs = 0;
        return false;
    }
    bool context()
    {
        if (!app::hasAppFacade()) return false;
        const auto& config = app::configFacade().readConfig();
        const uint32_t self = app::messagingFacade().getSelfNodeId();
        team_ready = team::ui::team_ui_snapshot_store().load(team_snapshot) && team_snapshot.in_team &&
                     team_snapshot.has_team_id && team_snapshot.has_team_psk && !team_snapshot.pending_join && !team_snapshot.kicked_out;
        // Only the existing Team command reads the PSK. The HostLink owner
        // retains a non-secret target/context projection, not another key copy.
        team_snapshot.team_psk.fill(0);
        mbedtls_sha256_context hash;
        mbedtls_sha256_init(&hash);
        const bool ok = sha::sha256_starts(&hash, 0) == 0 && sha::sha256_update(&hash, boot, sizeof(boot)) == 0 &&
                        sha::sha256_update(&hash, reinterpret_cast<const uint8_t*>(&self), sizeof(self)) == 0 &&
                        sha::sha256_update(&hash, reinterpret_cast<const uint8_t*>(&config.mesh_protocol), sizeof(config.mesh_protocol)) == 0 &&
                        sha::sha256_update(&hash, reinterpret_cast<const uint8_t*>(&config.activeMeshConfig()), sizeof(chat::MeshConfig)) == 0 &&
                        sha::sha256_update(&hash, team_snapshot.team_id.data(), team_snapshot.team_id.size()) == 0 &&
                        sha::sha256_update(&hash, reinterpret_cast<const uint8_t*>(&team_snapshot.security_round), sizeof(team_snapshot.security_round)) == 0 &&
                        sha::sha256_update(&hash, reinterpret_cast<const uint8_t*>(&team_ready), sizeof(team_ready)) == 0 &&
                        sha::sha256_finish(&hash, digest) == 0;
        mbedtls_sha256_free(&hash);
        if (ok) std::memcpy(current_context, digest, kIdSize);
        return ok;
    }
    SlotResult read(uint8_t slot, SubmissionRecord& out) override
    {
        char key[5] = {'s', "0123456789abcdef"[slot], 0};
        size_t size = sizeof(storage);
        const auto error = nvs_get_blob(nvs, key, storage, &size);
        if (error == ESP_ERR_NVS_NOT_FOUND) return SlotResult::Empty;
        return error == ESP_OK && decodeRecord({storage, size}, out) ? SlotResult::Found : SlotResult::Unavailable;
    }
    bool write(uint8_t slot, const SubmissionRecord& record) override
    {
        char key[5] = {'s', "0123456789abcdef"[slot], 0};
        size_t size = 0;
        return encodeRecord(record, storage, sizeof(storage), size) &&
               nvs_set_blob(nvs, key, storage, size) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    }
    Status validate(const SubmissionRecord& binding, const uint8_t* expected_context, const uint8_t*, size_t) override
    {
        if (!initialize()) return Status::StorageUnavailable;
        if (platform::esp::arduino_common::storage::sd_external_block_owner_active()) return Status::Busy;
        if (std::memcmp(binding.store, store, kIdSize) != 0 || !context() ||
            std::memcmp(expected_context, current_context, kIdSize) != 0) return Status::StaleIdentity;
        TargetView target;
        if (!decodeTarget(binding, target)) return Status::InvalidField;
        const auto protocol = app::configFacade().readConfig().mesh_protocol;
        if (target.protocol != static_cast<uint8_t>(protocol)) return Status::StaleIdentity;
        if (target.protocol == 3) return Status::Unsupported;
        if (target.kind == 2)
        {
            return team_ready && app::teamFacade().getTeamController() &&
                           std::memcmp(target.identity.data, team_snapshot.team_id.data(), target.identity.size) == 0
                       ? Status::Ok
                       : Status::Conflict;
        }
        if (target.protocol == 3 || (target.protocol == 4 && target.kind == 0)) return Status::Unsupported;
        if (target.channel >= (protocol == chat::MeshProtocol::MeshCore ? 8 : 2)) return Status::InvalidField;
        if (!app::messagingFacade().getChatService().isDataReady()) return Status::Busy;
        return Status::Ok;
    }
    LookupResult lookup(const SubmissionRecord& key, SubmissionRecord& out) override { return journal.lookup(key, out); }
    ReservationResult reserve(const SubmissionRecord& record) override { return journal.reserve(record); }
    ReservationResult pollReservation(const SubmissionRecord&) override { return ReservationResult::Ambiguous; }
    void submit(SubmissionRecord& record, const uint8_t* text, size_t length) override
    {
        TargetView target;
        if (!decodeTarget(record, target))
        {
            record.state = SubmissionState::Rejected;
            return;
        }
        if (target.kind == 2)
        {
            auto* controller = app::teamFacade().getTeamController();
            if (!controller)
            {
                record.state = SubmissionState::Rejected;
                return;
            }
            uint32_t message_id = 0;
            do
            {
                esp_fill_random(&message_id, sizeof(message_id));
            } while (message_id == 0);
            ui::team_actions::TeamControllerChatCommandPort port(*controller);
            ui::presentation_sources::TeamChatActionSink sink(team::ui::team_ui_snapshot_store(),
                                                              team::ui::team_ui_chat_log_store(), &port, target.channel, message_id);
            ui::chat::SendMessageView message;
            message.conversation.kind = ui::chat::ConversationKind::Team;
            message.conversation.protocol = ui::chat::ChatProtocolKind::TrailMate;
            message.text = reinterpret_cast<const char*>(text);
            message.text_len = length;
            const auto result = sink.sendMessage(message);
            record.state = result.ok ? SubmissionState::Submitted : SubmissionState::Rejected;
            record.failure = result.ok ? 0 : uint16_t(0x4000U | uint16_t(result.failure));
            record.delivery_state = result.ok ? 1 : 4;
            if (result.ok)
            {
                std::memcpy(record.message, record.operation, kIdSize);
                for (unsigned i = 0; i < 4; ++i) record.message[i] = uint8_t(message_id >> (i * 8));
            }
            record.revision = journal.nextRevision();
            return;
        }
        chat::ConversationId conversation(static_cast<chat::ChannelId>(target.channel), 0,
                                          static_cast<chat::MeshProtocol>(target.protocol));
        if (target.kind == 1 && target.protocol == 1)
        {
            for (unsigned i = 0; i < 4; ++i) conversation.peer = (conversation.peer << 8) | target.identity.data[i];
        }
        else if (target.kind == 1) conversation.reticulum_identity = chat::makeReticulumDestinationIdentity(target.identity.data);
        const auto result = app::messagingFacade().getChatService().sendTextToConversationDetailed(
            conversation, std::string(reinterpret_cast<const char*>(text), length));
        record.state = result.ok ? SubmissionState::Submitted : SubmissionState::Rejected;
        record.failure = static_cast<uint16_t>(result.failure);
        record.delivery_state = result.ok ? 1 : 4; // 0 unknown, 1 queued, 2 sent, 3 delivered, 4 failed
        if (result.msg_id)
        {
            // Opaque operation receipt ref, not a cross-store message locator.
            std::memcpy(record.message, record.operation, kIdSize);
            for (unsigned i = 0; i < 4; ++i) record.message[i] = uint8_t(result.msg_id >> (i * 8));
        }
        record.revision = journal.nextRevision();
    }
    CommitResult commit(SubmissionRecord& record) override { return journal.commit(record); }
    CommitResult pollCommit(SubmissionRecord&) override { return CommitResult::Unavailable; }

    SubmissionJournal journal;
    ChatSubmissionOwner submission;
    SubmissionRecord record{};
    uint8_t session[kIdSize]{};
    uint8_t boot[kIdSize]{};
    uint8_t store[kIdSize]{};
    uint8_t current_context[kIdSize]{};
    uint8_t digest[kDigestSize]{};
    uint8_t storage[320]{};
    team::ui::TeamUiSnapshot team_snapshot;
    bool team_ready = false;
    nvs_handle_t nvs = 0;
};
Runtime* runtime = nullptr;
Runtime* get()
{
    if (!runtime)
    {
        void* memory = heap_caps_calloc(1, sizeof(Runtime), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (memory) runtime = new (memory) Runtime;
    }
    return runtime;
}
void refreshDelivery(SubmissionRecord& record)
{
    if (record.state != SubmissionState::Submitted || (record.message[0] == 0 && record.message[1] == 0 &&
                                                       record.message[2] == 0 && record.message[3] == 0)) return;
    TargetView target;
    if (!decodeTarget(record, target)) return;
    if (target.kind == 2) return; // Team admission has no remote delivery proof.
    uint32_t message_id = 0;
    for (unsigned i = 0; i < 4; ++i) message_id |= uint32_t(record.message[i]) << (i * 8);
    const auto* message = app::messagingFacade().getChatService().getMessageForProtocol(message_id, static_cast<chat::MeshProtocol>(target.protocol));
    // Never bind a colliding protocol message ID from a different conversation.
    record.delivery_state = 0;
    if (!message || uint8_t(message->channel) != target.channel || message->text.size() != record.text_size) return;
    if (message->from != 0 && message->from != app::messagingFacade().getSelfNodeId()) return;
    if (target.protocol == 1)
    {
        uint32_t peer = 0;
        for (size_t i = 0; i < target.identity.size; ++i) peer = (peer << 8) | target.identity.data[i];
        if (message->peer != peer) return;
    }
    if (target.kind == 1 && target.protocol == 4 && !chat::sameReticulumDestinationHash(message->reticulum_identity, target.identity.data)) return;
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    const bool match = sha::sha256_starts(&hash, 0) == 0 &&
                       sha::sha256_update(&hash, reinterpret_cast<const uint8_t*>(message->text.data()), message->text.size()) == 0 &&
                       sha::sha256_finish(&hash, runtime->digest) == 0 && std::memcmp(runtime->digest, record.content_hash, kDigestSize) == 0;
    mbedtls_sha256_free(&hash);
    if (!match) return;
    switch (message->status)
    {
    case chat::MessageStatus::Queued:
        record.delivery_state = 1;
        break;
    case chat::MessageStatus::Sent:
        record.delivery_state = 2;
        break;
    case chat::MessageStatus::Delivered:
        record.delivery_state = 3;
        break;
    case chat::MessageStatus::Failed:
        record.delivery_state = 4;
        break;
    default:
        break;
    }
}
} // namespace

bool busy() { return runtime && runtime->submission.active(); }
management::Status process(const management::EnvelopeView& request, uint16_t sequence,
                           uint8_t* body, size_t capacity, size_t& written, bool& deferred)
{
    deferred = false;
    auto* state = get();
    if (!state) return Status::Busy;
    if (!state->initialize()) return Status::StorageUnavailable;
    if (state->submission.active()) return Status::Busy;
    CmpWriter writer(body, capacity);
    if (request.code == uint16_t(management::Operation::ChatCapabilities))
    {
        if (request.body_size != 1 || request.body[0] != 0x90) return Status::InvalidField;
        if (!state->context()) return Status::Busy;
        const auto protocol = uint8_t(app::configFacade().readConfig().mesh_protocol);
        // v1 is a deliberately bounded send/receipt API. Queryable message and
        // contact history is not advertised by this feature set.
        const bool ok = writer.array(8) && writer.unsignedInteger(1) && writer.binary({state->store, kIdSize}) &&
                        writer.binary({state->current_context, kIdSize}) && writer.unsignedInteger(protocol) &&
                        writer.unsignedInteger(kTextBytes) && writer.unsignedInteger(kJournalSlots) && writer.unsignedInteger(15) &&
                        (state->team_ready ? (writer.array(3) && writer.binary({state->team_snapshot.team_id.data(), state->team_snapshot.team_id.size()}) &&
                                              writer.unsignedInteger(state->team_snapshot.security_round) && writer.unsignedInteger(2))
                                           : writer.nil());
        written = writer.size();
        return ok ? Status::Ok : Status::Internal;
    }
    if (request.code == uint16_t(management::Operation::ChatSendGet))
    {
        CmpReader reader({request.body, request.body_size});
        ByteView store, owner, operation;
        size_t count;
        if (!reader.array(count, 3) || count != 3 || !id(reader, store) || !id(reader, owner) || !id(reader, operation) || !reader.finished()) return Status::InvalidField;
        if (std::memcmp(store.data, state->store, kIdSize) != 0) return Status::StaleIdentity;
        std::memcpy(state->record.store, store.data, kIdSize);
        std::memcpy(state->record.owner, owner.data, kIdSize);
        std::memcpy(state->record.operation, operation.data, kIdSize);
        // Get is bounded to 16 small metadata records. No message bodies or
        // SD ownership are touched; sends use the one-slot-per-tick scan.
        LookupResult result;
        do
        {
            result = state->journal.lookup(state->record, state->record);
        } while (result == LookupResult::Pending);
        if (result == LookupResult::Unavailable) return Status::StorageUnavailable;
        if (result == LookupResult::Found)
        {
            if (state->record.state == SubmissionState::Reserved) state->record.state = SubmissionState::Uncertain;
            refreshDelivery(state->record);
        }
        const bool ok = writer.array(2) && writer.unsignedInteger(result == LookupResult::Found ? 0 : 1) &&
                        (result == LookupResult::Found ? writeReceipt(writer, state->record) : writer.nil());
        written = writer.size();
        return ok ? Status::Ok : Status::Internal;
    }
    SendRequestView view;
    if (!decodeSend({request.body, request.body_size}, view)) return Status::InvalidField;
    auto& record = state->record;
    std::memcpy(record.store, view.store.data, kIdSize);
    std::memcpy(record.owner, request.session, kIdSize);
    std::memcpy(record.operation, view.operation.data, kIdSize);
    std::memcpy(record.conversation, view.conversation.data, view.conversation.size);
    record.conversation_size = static_cast<uint8_t>(view.conversation.size);
    record.text_size = static_cast<uint16_t>(view.text.size());
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    const bool hashed = sha::sha256_starts(&hash, 0) == 0 &&
                        sha::sha256_update(&hash, reinterpret_cast<const uint8_t*>(view.text.data()), view.text.size()) == 0 &&
                        sha::sha256_finish(&hash, record.content_hash) == 0;
    mbedtls_sha256_free(&hash);
    if (!hashed) return Status::Internal;
    const auto status = state->submission.begin(record, view.context.data, request.request, sequence,
                                                reinterpret_cast<const uint8_t*>(view.text.data()), view.text.size());
    if (status == Status::Ok)
    {
        std::memcpy(state->session, request.session, kIdSize);
        deferred = true;
    }
    return status;
}
void advance(uint8_t* scratch, size_t capacity, bool connected)
{
    if (!runtime || !runtime->submission.active()) return;
    auto& owner = runtime->submission;
    owner.advance();
    if (!owner.ready()) return;
    // Finish accepted work even after disconnect. A lost reply is recovered
    // by lookup with the original owner/operation, never by another send.
    if (connected && capacity > 41)
    {
        CmpWriter writer(scratch + 41, capacity - 41);
        const bool body_ok = owner.status() == Status::Ok ? writeReceipt(writer, owner.record()) : writer.array(0);
        size_t written = 0;
        if (body_ok && management::encodeEnvelope(runtime->session, owner.requestId(), uint16_t(owner.status()),
                                                  scratch + 41, writer.size(), scratch, capacity, written))
            enqueue_event(uint8_t(FrameType::ManagementResponse), scratch, written, false, owner.sequence());
    }
    owner.releaseReply();
}
} // namespace hostlink::chat_runtime
#endif
