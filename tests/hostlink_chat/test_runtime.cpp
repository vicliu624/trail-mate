#include "../../platform/esp/arduino_common/src/hostlink/hostlink_chat_runtime.cpp"
#include <cassert>
using namespace hostlink;
using namespace hostlink::chat_management;
using management::Operation;
using management::Status;
namespace
{
struct SnapshotStore final : team::ui::ITeamUiSnapshotStore
{
    team::ui::TeamUiSnapshot value;
    bool load(team::ui::TeamUiSnapshot& out) override
    {
        out = value;
        return true;
    }
    void save(const team::ui::TeamUiSnapshot& in) override { value = in; }
    void clear() override { value = {}; }
} team_store;
struct LogStore final : team::ui::ITeamUiChatLogStore
{
    unsigned appends = 0;
    bool appendText(const team::TeamId&, uint32_t, bool, uint32_t, const std::string&) override { return false; }
    bool appendStructured(const team::TeamId&, uint32_t, bool, uint32_t, team::proto::TeamChatType, const std::vector<uint8_t>&) override
    {
        ++appends;
        return true;
    }
    bool loadRecent(const team::TeamId&, size_t, std::vector<team::ui::TeamChatLogEntry>&) override { return true; }
} log_store;
unsigned team_sends = 0;
uint32_t team_message_id = 0;
} // namespace
namespace team::ui
{
ITeamUiSnapshotStore& team_ui_snapshot_store() { return team_store; }
ITeamUiChatLogStore& team_ui_chat_log_store() { return log_store; }
} // namespace team::ui
namespace ui::team_actions
{
TeamControllerChatCommandPort::TeamControllerChatCommandPort(team::TeamController& controller) : controller_(controller) {}
bool TeamControllerChatCommandPort::setKeysFromPsk(const team::TeamId&, uint32_t, const uint8_t*, size_t) { return true; }
bool TeamControllerChatCommandPort::sendTeamChat(const team::proto::TeamChatMessage& message, uint8_t channel)
{
    assert(channel == 2);
    ++team_sends;
    team_message_id = message.header.msg_id;
    return true;
}
} // namespace ui::team_actions
namespace sys
{
uint32_t epoch_seconds_now() { return 1800000000; }
uint32_t millis_now() { return 1000; }
} // namespace sys
uint8_t session[16] = {7}, request_id[16] = {8};
uint8_t output[512];
std::vector<uint8_t> body;
Status command(Operation operation, bool& deferred)
{
    management::EnvelopeView request{session, request_id, uint16_t(operation), body.data(), body.size()};
    size_t size;
    return chat_runtime::process(request, 99, output, sizeof(output), size, deferred);
}
void prepare(uint8_t op, const char* text)
{
    auto* state = chat_runtime::get();
    uint8_t op_id[16] = {op};
    CmpWriter writer(output, sizeof(output));
    assert(writer.array(4) && writer.array(2) && writer.binary({state->store, 16}) && writer.binary({state->current_context, 16}) &&
           writer.binary({op_id, 16}) && writer.array(4) && writer.unsignedInteger(1) && writer.unsignedInteger(0) &&
           writer.unsignedInteger(0) && writer.binary({nullptr, 0}) && writer.text(text));
    body.assign(output, output + writer.size());
}
void pump(bool connected)
{
    for (unsigned i = 0; i < 30 && chat_runtime::busy(); ++i) chat_runtime::advance(output, sizeof(output), connected);
    assert(!chat_runtime::busy());
}
void reboot()
{
    auto*& state = chat_runtime::runtime;
    nvs_close(state->nvs);
    state->~Runtime();
    std::free(state);
    state = nullptr;
}
int main()
{
    bool deferred = false;
    body = {0x90};
    assert(command(Operation::ChatCapabilities, deferred) == Status::Ok && !deferred);
    fake_app::service.before_send = []
    {
        assert(fake_nvs::durable.count("s0"));
        SubmissionRecord record;
        const auto& blob = fake_nvs::durable.at("s0");
        assert(decodeRecord({blob.data(), blob.size()}, record));
        assert(record.state == SubmissionState::Reserved);
    };
    prepare(1, "hello");
    assert(command(Operation::ChatSend, deferred) == Status::Ok && deferred);
    std::fill(body.begin(), body.end(), 0xff); // Request arena may be reused immediately.
    pump(false);                               // Disconnect does not cancel or repeat accepted work.
    assert(fake_app::service.sends == 1 && fake_app::service.message.text == "hello");
    assert(fake_usb::replies.empty());
    prepare(1, "hello");
    assert(command(Operation::ChatSend, deferred) == Status::Ok && deferred);
    pump(true);
    assert(fake_app::service.sends == 1 && fake_usb::sequence == 99);
    prepare(1, "different");
    assert(command(Operation::ChatSend, deferred) == Status::Ok);
    pump(true);
    management::EnvelopeView response;
    assert(management::decodeEnvelope(fake_usb::replies.back().data(), fake_usb::replies.back().size(), response));
    assert(response.code == uint16_t(Status::Conflict) && fake_app::service.sends == 1);
    reboot();
    body = {0x90};
    assert(command(Operation::ChatCapabilities, deferred) == Status::Ok);
    prepare(1, "hello");
    assert(command(Operation::ChatSend, deferred) == Status::Ok);
    pump(true);
    assert(fake_app::service.sends == 1); // Durable duplicate survives reboot.
    prepare(2, "no persistence");
    fake_nvs::fail_commit = true;
    assert(command(Operation::ChatSend, deferred) == Status::Ok);
    pump(true);
    assert(fake_app::service.sends == 1);
    fake_nvs::fail_commit = false;
    fake_nvs::pending.clear();
    prepare(3, "changed context");
    assert(command(Operation::ChatSend, deferred) == Status::Ok);
    fake_app::config.value.mesh_protocol = chat::MeshProtocol::MeshCore;
    pump(true);
    assert(fake_app::service.sends == 1);
    fake_app::config.value.mesh_protocol = chat::MeshProtocol::Meshtastic;
    team_store.value.in_team = true;
    team_store.value.has_team_id = true;
    team_store.value.has_team_psk = true;
    team_store.value.team_id.fill(9);
    body = {0x90};
    assert(command(Operation::ChatCapabilities, deferred) == Status::Ok);
    auto* state = chat_runtime::get();
    uint8_t operation[16] = {4};
    CmpWriter writer(output, sizeof(output));
    assert(writer.array(4) && writer.array(2) && writer.binary({state->store, 16}) && writer.binary({state->current_context, 16}) &&
           writer.binary({operation, 16}) && writer.array(4) && writer.unsignedInteger(1) && writer.unsignedInteger(2) &&
           writer.unsignedInteger(2) && writer.binary({team_store.value.team_id.data(), 8}) && writer.text("Team common path"));
    body.assign(output, output + writer.size());
    assert(command(Operation::ChatSend, deferred) == Status::Ok);
    pump(true);
    assert(team_sends == 1 && team_message_id != 0 && log_store.appends == 1);
    reboot();
}
