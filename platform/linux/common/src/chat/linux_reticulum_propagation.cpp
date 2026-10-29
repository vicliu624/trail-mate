#include "chat/linux_reticulum_propagation.h"
#include "chat/infra/lxmf/lxmf_propagation_service_runtime.h"
#include "chat/infra/lxmf/lxmf_propagation_sync.h"

namespace trailmate::linux_app
{
namespace state = ::chat::lxmf::runtime;
namespace wire = ::chat::lxmf;
using Stage = state::PropagationSyncStage;
namespace
{
constexpr uint32_t kRequestTtlMs = 120000;
constexpr size_t kMaxTransientHistory = 1024;
bool sameConfig(const chat::reticulum::LxmfPropagationClientConfig& a, const chat::reticulum::LxmfPropagationClientConfig& b)
{
    return a.enabled == b.enabled && a.automatic_node == b.automatic_node &&
           !std::memcmp(a.node_hash, b.node_hash, sizeof(a.node_hash)) &&
           a.sync_on_start == b.sync_on_start && a.sync_interval_s == b.sync_interval_s &&
           a.max_messages_per_sync == b.max_messages_per_sync;
}
} // namespace
bool LinuxReticulumPropagation::commit(const chat::MeshIncomingText& message, bool durable, uint32_t utc)
{
    return chat::hasReticulumLxmfMessageHash(message) &&
           state::notePropagationDeliveryCommit(runtime_, message.reticulum_lxmf_hash, durable, utc, kMaxTransientHistory);
}
void LinuxReticulumPropagation::finish(Transport& transport, uint32_t utc, bool success)
{
    if (active_) transport.cancel(active_->propagation_hash);
    if (success) state::finishPropagationSyncComplete(runtime_, utc);
    else state::finishPropagationSyncFailed(runtime_, utc);
    active_.reset();
    runtime_.has_active_node = false;
    std::memset(runtime_.active_node_hash, 0, sizeof(runtime_.active_node_hash));
}
bool LinuxReticulumPropagation::sendGet(Transport& transport, Stage next, uint32_t now, uint32_t utc)
{
    const auto* wants = next == Stage::Downloading ? &runtime_.sync_wants : nullptr;
    const auto* haves = next == Stage::Acknowledging ? &runtime_.sync_haves : nullptr;
    const auto want_spans = wants ? state::makeRuntimeByteSpans(*wants) : state::RuntimeByteSpanList{};
    const auto have_spans = haves ? state::makeRuntimeByteSpans(*haves) : state::RuntimeByteSpanList{};
    const auto want_view = state::viewRuntimeByteSpans(want_spans);
    const auto have_view = state::viewRuntimeByteSpans(have_spans);
    state::ResourcePayloadBuffer data(32 + 35 * (want_spans.size() + have_spans.size()));
    size_t size = data.size();
    // Leave room for RPC framing below the native bounded resource limit.
    constexpr uint32_t transfer_kb = 63;
    if (!wire::encodePropagationGetRequestPayloadSpans(wants ? &want_view : nullptr, haves ? &have_view : nullptr,
                                                       next == Stage::Downloading, transfer_kb, data.data(), &size)) return false;
    uint8_t path[16], id[16];
    state::propagationServicePathHash(state::PropagationServicePath::Get, path);
    const auto result = transport.request(active_->propagation_hash, path, data.data(), size, utc, id);
    if (result != LinuxReticulumLinks::ServiceRequestResult::Queued && result != LinuxReticulumLinks::ServiceRequestResult::Duplicate) return false;
    state::markPropagationSyncRequestSent(runtime_, id, next);
    request_started_ms_ = now;
    return true;
}
void LinuxReticulumPropagation::tick(const chat::reticulum::LxmfPropagationClientConfig& config, bool allowed,
                                     uint32_t now, uint32_t utc, Transport& transport)
{
    now = std::max(1U, now);
    if (!sync_configured_ || !sameConfig(config, sync_config_))
    {
        if (active_) finish(transport, utc, false);
        sync_config_ = config;
        sync_configured_ = true;
        runtime_.initial_sync_pending = config.sync_on_start;
        runtime_.last_sync_s = utc;
    }
    const auto* node = select(config, allowed, now);
    if (!node || (active_ && std::memcmp(active_->propagation_hash, node->propagation_hash, 16)))
    {
        if (active_) finish(transport, utc, false);
        if (!node && allowed && config.enabled && !config.automatic_node) transport.discover(config.node_hash);
        return;
    }
    if (!active_)
    {
        if (!state::startPropagationSyncIfDue(runtime_, utc, now, config.sync_interval_s)) return;
        active_ = *node;
        runtime_.has_active_node = true;
        std::memcpy(runtime_.active_node_hash, node->propagation_hash, 16);
    }
    if (uint32_t(now - runtime_.sync_started_ms) > 3 * kRequestTtlMs)
    {
        finish(transport, utc, false);
        return;
    }
    if (runtime_.sync_stage == Stage::NeedList)
    {
        const auto result = transport.open(*active_);
        if (result == LinuxReticulumLinks::OpenResult::Active)
        {
            if (!sendGet(transport, Stage::Listing, now, utc)) finish(transport, utc, false);
        }
        else if (result == LinuxReticulumLinks::OpenResult::Invalid || result == LinuxReticulumLinks::OpenResult::Full ||
                 result == LinuxReticulumLinks::OpenResult::TxFailed || uint32_t(now - runtime_.sync_started_ms) > kRequestTtlMs)
            finish(transport, utc, false);
        return;
    }
    if (runtime_.sync_stage == Stage::Listing || runtime_.sync_stage == Stage::Downloading || runtime_.sync_stage == Stage::Acknowledging)
    {
        LinuxReticulumLinks::ServiceResponse response;
        if (!transport.take(active_->propagation_hash, runtime_.sync_request_id, response))
        {
            if (uint32_t(now - request_started_ms_) > kRequestTtlMs || !transport.pending(active_->propagation_hash, runtime_.sync_request_id))
                finish(transport, utc, false);
            return;
        }
        if (runtime_.sync_stage == Stage::Acknowledging)
        {
            // /get with nil wants must not return messages requiring acceptance.
            std::vector<std::vector<uint8_t>> unexpected;
            const bool acknowledged = response.data_is_nil ||
                                      (wire::decodePropagationMessageListPayload(response.payload.data(), response.payload.size(), &unexpected) && unexpected.empty());
            finish(transport, utc, acknowledged);
            return;
        }
        if (response.data_is_nil)
        {
            finish(transport, utc, false);
            return;
        }
        if (runtime_.sync_stage == Stage::Listing)
        {
            state::PropagationIdList ids;
            const auto collect = [](const uint8_t* bytes, size_t size, void* context)
            {
                auto& list = *static_cast<state::PropagationIdList*>(context);
                if (size != 32) return false;
                if (list.size() >= kMaxTransientHistory) return true;
                if (std::none_of(list.begin(), list.end(), [&](const auto& id)
                                 { return !std::memcmp(id.data(), bytes, 32); }))
                    list.emplace_back(bytes, bytes + size);
                return true;
            };
            if (!wire::decodePropagationIdListPayload(response.payload.data(), response.payload.size(), collect, &ids))
            {
                finish(transport, utc, false);
                return;
            }
            state::notePropagationListingResult(runtime_, ids, std::clamp<size_t>(config.max_messages_per_sync, 1, 32));
        }
        else
        {
            state::PropagationMessageList messages;
            const auto collect = [](const uint8_t* bytes, size_t size, void* context)
            {
                auto& list = *static_cast<state::PropagationMessageList*>(context);
                if (list.size() >= 32 || size > state::kMaxAdvertisedResourceBytes || size <= 16) return false;
                list.emplace_back(bytes, bytes + size);
                return true;
            };
            if (!wire::decodePropagationMessageListPayload(response.payload.data(), response.payload.size(), collect, &messages))
            {
                finish(transport, utc, false);
                return;
            }
            bool registration_failed = false;
            for (const auto& message : messages)
            {
                uint8_t transient[32];
                chat::reticulum::fullHash(message.data(), message.size(), transient);
                if (std::none_of(runtime_.sync_wants.begin(), runtime_.sync_wants.end(), [&](const auto& want)
                                 { return want.size() == 32 && !std::memcmp(want.data(), transient, 32); })) continue;
                if (std::any_of(runtime_.pending_deliveries.begin(), runtime_.pending_deliveries.end(), [&](const auto& pending)
                                { return !std::memcmp(pending.transient_id, transient, 32); })) continue;
                bool delivered = false;
                if (state::hasSeenPropagationTransient(runtime_, transient, &delivered) && delivered)
                {
                    state::rememberDeliveredPropagationTransient(runtime_, transient, utc, kMaxTransientHistory);
                    continue;
                }
                chat::MeshIncomingText incoming;
                if (!transport.decode(message.data(), message.size(), incoming) || !chat::hasReticulumLxmfMessageHash(incoming)) continue;
                if (!state::awaitPropagationDeliveryCommit(runtime_, transient, incoming.reticulum_lxmf_hash, 32))
                {
                    registration_failed = true;
                    break;
                }
                bool accepted = false;
                try
                {
                    accepted = transport.deliver(incoming);
                }
                catch (...)
                {
                }
                if (!accepted) state::notePropagationDeliveryCommit(runtime_, incoming.reticulum_lxmf_hash, false, utc, kMaxTransientHistory);
            }
            state::notePropagationDownloadResult(runtime_, registration_failed, now);
        }
    }
    if (runtime_.sync_stage == Stage::AwaitingPersistence) state::pollPropagationPersistence(runtime_, now, kRequestTtlMs);
    if (runtime_.sync_stage == Stage::NeedMessages)
    {
        if (!sendGet(transport, Stage::Downloading, now, utc)) finish(transport, utc, false);
    }
    else if (runtime_.sync_stage == Stage::NeedAcknowledge)
    {
        if (runtime_.sync_haves.empty()) finish(transport, utc, true);
        else if (!sendGet(transport, Stage::Acknowledging, now, utc)) finish(transport, utc, false);
    }
    else if (runtime_.sync_stage == Stage::Failed) finish(transport, utc, false);
}
} // namespace trailmate::linux_app
