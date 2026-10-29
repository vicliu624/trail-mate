/**
 * @file reticulum_call_runtime.cpp
 * @brief Shared Reticulum call state, UI facts, and audio packet queues.
 */

#include "platform/ui/reticulum_call_runtime.h"

#include "platform/ui/screen_runtime.h"
#include "sys/clock.h"
#include "sys/ringbuf.h"

#include <cstring>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <new>
#endif
#if !defined(ARDUINO_ARCH_NRF52) && !defined(NRF52840_XXAA)
#include <mutex>
#endif

namespace platform::ui::reticulum_call
{
namespace
{

constexpr std::size_t kQueueDepth = 8;

struct AudioQueues
{
    sys::RingBuffer<AudioPacket, kQueueDepth> inbound;
    sys::RingBuffer<AudioPacket, kQueueDepth> outbound;
};

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)
// nRF52 targets compile this shared translation unit but do not wire the
// Reticulum call runtime into an ISR or task. Keep that unsupported path
// buildable without pulling in unavailable C++ thread primitives. A future
// nRF52 call implementation must replace this with its platform critical
// section before it adds a caller.
class RuntimeMutex
{
  public:
    void lock() {}
    void unlock() {}
};

class RuntimeLock
{
  public:
    explicit RuntimeLock(RuntimeMutex&) {}
};
#else
using RuntimeMutex = std::mutex;
using RuntimeLock = std::lock_guard<RuntimeMutex>;
#endif

struct RuntimeState
{
    RuntimeMutex mutex;
    Snapshot snapshot;
    MediaHooks hooks;
    RealtimeHooks realtime_hooks;
    bool hangup_requested = false;
    bool closing_keep_exclusive = false;
    bool duplex_mode_request_pending = false;
    bool sleep_wake_lease = false;
    bool sleep_wake_lease_applied = false;
    uint64_t call_generation = 1;
    bool media_starting = false;
    bool configuration_change = false;
    unsigned outgoing_admissions = 0;
    uint32_t failed_dial_revision = 0;
    // Call audio is inactive for the normal device lifetime. Keep its 16 fixed
    // packet slots out of the always-resident control state and, on ESP, prefer
    // PSRAM once media actually starts. The fixed depth and drop-oldest policy
    // remain unchanged after the queues are provisioned.
#if defined(ESP_PLATFORM)
    AudioQueues* audio_queues = nullptr;
#else
    // Reticulum Call is currently unsupported on nRF, but this shared runtime
    // is compiled there. Preserve its fixed static storage model unchanged.
    AudioQueues audio_queues;
#endif
};

RuntimeState s_state;

bool hashes_equal(const uint8_t* a, const uint8_t* b)
{
    return a && b && std::memcmp(a, b, kHashSize) == 0;
}

bool hash_is_empty(const uint8_t* hash)
{
    if (!hash)
    {
        return true;
    }
    for (std::size_t index = 0; index < kHashSize; ++index)
    {
        if (hash[index] != 0)
        {
            return false;
        }
    }
    return true;
}

void copy_hash(uint8_t* out, const uint8_t* in)
{
    if (out && in)
    {
        std::memcpy(out, in, kHashSize);
    }
}

void copy_name(char* out, std::size_t out_len, const char* in)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!in)
    {
        return;
    }
    std::strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';
}

uint32_t now_ms()
{
    return sys::millis_now();
}

uint8_t clamp_volume(uint8_t volume_percent)
{
    return volume_percent > 100U ? 100U : volume_percent;
}

bool ensure_audio_queues_locked()
{
#if defined(ESP_PLATFORM)
    if (s_state.audio_queues)
    {
        return true;
    }

    void* storage = heap_caps_calloc_prefer(
        1,
        sizeof(AudioQueues),
        2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_state.audio_queues = storage ? new (storage) AudioQueues{} : nullptr;
    return s_state.audio_queues != nullptr;
#else
    return true;
#endif
}

AudioQueues* audio_queues_locked()
{
#if defined(ESP_PLATFORM)
    return s_state.audio_queues;
#else
    return &s_state.audio_queues;
#endif
}

void clear_queues_locked()
{
    AudioQueues* const queues = audio_queues_locked();
    if (!queues)
    {
        return;
    }
    queues->inbound.clear();
    queues->outbound.clear();
}

void apply_peer_locked(const Peer& peer)
{
    copy_hash(s_state.snapshot.link_id, peer.link_id);
    if (!hash_is_empty(peer.destination_hash))
    {
        copy_hash(s_state.snapshot.peer_destination_hash, peer.destination_hash);
    }
    if (!hash_is_empty(peer.identity_hash))
    {
        copy_hash(s_state.snapshot.peer_identity_hash, peer.identity_hash);
    }
    if (peer.display_name && peer.display_name[0] != '\0')
    {
        copy_name(s_state.snapshot.peer_name,
                  sizeof(s_state.snapshot.peer_name),
                  peer.display_name);
    }
    s_state.snapshot.codec2_mode = peer.codec2_mode;
    s_state.snapshot.wire_profile = peer.wire_profile;
    s_state.snapshot.updated_ms = now_ms();
}

void stop_media_outside_lock(MediaHooks hooks)
{
    if (hooks.stop)
    {
        hooks.stop();
    }
}

bool begin_soft_preempt_outside_lock(RealtimeHooks hooks,
                                     const uint8_t link_id[kHashSize])
{
    return !hooks.begin_soft_preempt || hooks.begin_soft_preempt(link_id);
}

bool begin_ringing_alert_outside_lock(RealtimeHooks hooks,
                                      const uint8_t link_id[kHashSize])
{
    return !hooks.begin_ringing_alert || hooks.begin_ringing_alert(link_id);
}

bool begin_exclusive_outside_lock(RealtimeHooks hooks,
                                  const uint8_t link_id[kHashSize])
{
    return !hooks.begin_exclusive || hooks.begin_exclusive(link_id);
}

void begin_closing_outside_lock(RealtimeHooks hooks,
                                const uint8_t link_id[kHashSize],
                                bool keep_exclusive)
{
    if (hooks.begin_closing)
    {
        hooks.begin_closing(link_id, keep_exclusive);
    }
}

void end_realtime_outside_lock(RealtimeHooks hooks,
                               const uint8_t link_id[kHashSize])
{
    if (hooks.end)
    {
        hooks.end(link_id);
    }
}

bool acquire_sleep_wake_lease_locked()
{
    if (s_state.sleep_wake_lease)
    {
        return false;
    }
    s_state.sleep_wake_lease = true;
    return true;
}

bool release_sleep_wake_lease_locked()
{
    if (!s_state.sleep_wake_lease)
    {
        return false;
    }
    s_state.sleep_wake_lease = false;
    return true;
}

void close_call(State final_state, bool request_remote_close, uint64_t expected_generation = 0);

bool start_media_if_needed()
{
    MediaHooks hooks{};
    bool should_start = false;
    bool queue_provision_failed = false;
    bool already_starting = false;
    uint64_t generation = 0;
    {
        RuntimeLock lock(s_state.mutex);
        hooks = s_state.hooks;
        generation = s_state.call_generation;
        already_starting = s_state.media_starting;
        s_state.snapshot.media_supported =
            hooks.is_supported ? hooks.is_supported() : false;
        should_start = s_state.snapshot.accepted &&
                       s_state.snapshot.link_active &&
                       !s_state.snapshot.media_active &&
                       (s_state.snapshot.realtime_phase ==
                            RealtimePhase::AcceptedStarting ||
                        s_state.snapshot.realtime_phase ==
                            RealtimePhase::ActiveCall);
        if (should_start)
        {
            if (!ensure_audio_queues_locked())
            {
                should_start = false;
                queue_provision_failed = true;
            }
            else
            {
                // Reserve media ownership before the task starts so a newly
                // scheduled task cannot observe a transient inactive state and
                // exit immediately.
                s_state.snapshot.media_active = true;
                s_state.media_starting = true;
                s_state.snapshot.updated_ms = now_ms();
            }
        }
    }
    if (queue_provision_failed)
    {
        close_call(State::Failed, true, generation);
        return false;
    }
    if (!should_start)
    {
        return !already_starting;
    }
    if (!hooks.start || !hooks.start())
    {
        close_call(State::Failed, true, generation);
        return false;
    }
    RuntimeLock lock(s_state.mutex);
    const bool current = s_state.call_generation == generation && s_state.snapshot.media_active &&
                         s_state.snapshot.accepted && s_state.snapshot.link_active;
    if (current) s_state.media_starting = false;
    return current;
}

void close_call(State final_state, bool request_remote_close, uint64_t expected_generation)
{
    MediaHooks hooks{};
    RealtimeHooks realtime_hooks{};
    uint8_t link_id[kHashSize] = {};
    bool stop_media = false;
    bool end_realtime = false;
    bool begin_closing = false;
    bool keep_exclusive = false;
    {
        RuntimeLock lock(s_state.mutex);
        if (expected_generation && s_state.call_generation != expected_generation) return;
        ++s_state.call_generation;
        s_state.media_starting = false;
        hooks = s_state.hooks;
        realtime_hooks = s_state.realtime_hooks;
        copy_hash(link_id, s_state.snapshot.link_id);
        stop_media = s_state.snapshot.media_active;
        keep_exclusive =
            s_state.snapshot.realtime_phase == RealtimePhase::AcceptedStarting ||
            s_state.snapshot.realtime_phase == RealtimePhase::ActiveCall ||
            s_state.snapshot.realtime_phase == RealtimePhase::ClosingCall;
        s_state.snapshot.media_active = false;
        s_state.snapshot.accepted = false;
        s_state.snapshot.link_active = false;
        s_state.snapshot.state = final_state;
        s_state.duplex_mode_request_pending = false;
        s_state.snapshot.updated_ms = now_ms();
        s_state.hangup_requested = request_remote_close &&
                                   !hash_is_empty(s_state.snapshot.link_id);
        if (s_state.hangup_requested)
        {
            s_state.snapshot.realtime_phase = RealtimePhase::ClosingCall;
            s_state.closing_keep_exclusive = keep_exclusive;
            begin_closing = true;
        }
        else
        {
            end_realtime =
                s_state.snapshot.realtime_phase != RealtimePhase::Idle &&
                !hash_is_empty(link_id);
            s_state.snapshot.realtime_phase = RealtimePhase::Idle;
            s_state.closing_keep_exclusive = false;
            (void)release_sleep_wake_lease_locked();
        }
        clear_queues_locked();
    }
    if (stop_media)
    {
        stop_media_outside_lock(hooks);
    }
    if (begin_closing)
    {
        begin_closing_outside_lock(realtime_hooks, link_id, keep_exclusive);
    }
    else if (end_realtime)
    {
        end_realtime_outside_lock(realtime_hooks, link_id);
    }
}

bool enqueue_packet(const uint8_t link_id[kHashSize],
                    const uint8_t* data,
                    std::size_t len,
                    bool inbound)
{
    if (!link_id || !data || len == 0 || len > kAudioPacketMaxLen)
    {
        return false;
    }
    RuntimeLock lock(s_state.mutex);
    if (!hashes_equal(link_id, s_state.snapshot.link_id) ||
        !s_state.snapshot.accepted ||
        !s_state.snapshot.link_active ||
        !s_state.snapshot.media_active)
    {
        return false;
    }

    AudioQueues* const queues = audio_queues_locked();
    if (!queues)
    {
        return false;
    }

    if ((inbound && s_state.snapshot.speaker_muted) ||
        (!inbound &&
         (s_state.snapshot.microphone_muted ||
          (s_state.snapshot.duplex_mode == DuplexMode::Half &&
           !s_state.snapshot.ptt_pressed))))
    {
        if (inbound)
        {
            ++s_state.snapshot.rx_packets;
            ++s_state.snapshot.rx_dropped;
            s_state.snapshot.rx_bytes += static_cast<uint32_t>(len);
        }
        else
        {
            ++s_state.snapshot.tx_dropped;
        }
        s_state.snapshot.updated_ms = now_ms();
        return true;
    }

    AudioPacket packet{};
    copy_hash(packet.link_id, link_id);
    std::memcpy(packet.data, data, len);
    packet.len = len;
    bool dropped = false;
    sys::RingBuffer<AudioPacket, kQueueDepth>& queue =
        inbound ? queues->inbound : queues->outbound;
    queue.pushDropOldest(packet, &dropped);
    if (inbound)
    {
        ++s_state.snapshot.rx_packets;
        s_state.snapshot.rx_bytes += static_cast<uint32_t>(len);
        if (dropped)
        {
            ++s_state.snapshot.rx_dropped;
        }
    }
    else if (dropped)
    {
        ++s_state.snapshot.tx_dropped;
    }
    s_state.snapshot.updated_ms = now_ms();
    return true;
}

} // namespace

void set_media_hooks(const MediaHooks& hooks)
{
    RuntimeLock lock(s_state.mutex);
    s_state.hooks = hooks;
    s_state.snapshot.media_supported =
        hooks.is_supported ? hooks.is_supported() : false;
}

uint8_t speaker_volume()
{
    MediaHooks hooks{};
    {
        RuntimeLock lock(s_state.mutex);
        hooks = s_state.hooks;
    }
    return hooks.speaker_volume ? hooks.speaker_volume() : 0U;
}

void set_speaker_volume(uint8_t volume_percent)
{
    MediaHooks hooks{};
    {
        RuntimeLock lock(s_state.mutex);
        hooks = s_state.hooks;
    }
    if (hooks.set_speaker_volume)
    {
        hooks.set_speaker_volume(clamp_volume(volume_percent));
    }
}

void set_microphone_muted(bool muted)
{
    RuntimeLock lock(s_state.mutex);
    s_state.snapshot.microphone_muted = muted;
    if (muted)
        if (auto* queues = audio_queues_locked())
        {
            s_state.snapshot.tx_dropped += static_cast<uint32_t>(queues->outbound.size());
            queues->outbound.clear();
        }
    s_state.snapshot.updated_ms = now_ms();
}

void set_speaker_muted(bool muted)
{
    RuntimeLock lock(s_state.mutex);
    s_state.snapshot.speaker_muted = muted;
    if (muted)
        if (auto* queues = audio_queues_locked())
        {
            s_state.snapshot.rx_dropped += static_cast<uint32_t>(queues->inbound.size());
            queues->inbound.clear();
        }
    s_state.snapshot.updated_ms = now_ms();
}

void set_ptt_pressed(bool pressed)
{
    RuntimeLock lock(s_state.mutex);
    s_state.snapshot.ptt_pressed = pressed;
    if (!pressed && s_state.snapshot.duplex_mode == DuplexMode::Half)
        if (auto* queues = audio_queues_locked())
        {
            s_state.snapshot.tx_dropped += static_cast<uint32_t>(queues->outbound.size());
            queues->outbound.clear();
        }
    s_state.snapshot.updated_ms = now_ms();
}

bool set_duplex_mode(DuplexMode mode)
{
    if (mode != DuplexMode::Full && mode != DuplexMode::Half) return false;
    RuntimeLock lock(s_state.mutex);
    if (s_state.snapshot.state != State::Active)
    {
        return false;
    }
    if (s_state.snapshot.duplex_mode == mode)
    {
        return true;
    }
    s_state.snapshot.duplex_mode = mode;
    s_state.snapshot.ptt_pressed = false;
    if (mode == DuplexMode::Half)
        if (auto* queues = audio_queues_locked())
        {
            s_state.snapshot.tx_dropped += static_cast<uint32_t>(queues->outbound.size());
            queues->outbound.clear();
        }
    s_state.snapshot.updated_ms = now_ms();
    s_state.duplex_mode_request_pending = true;
    return true;
}

void apply_remote_duplex_mode(DuplexMode mode)
{
    if (mode != DuplexMode::Full && mode != DuplexMode::Half) return;
    RuntimeLock lock(s_state.mutex);
    s_state.snapshot.duplex_mode = mode;
    s_state.snapshot.ptt_pressed = false;
    if (mode == DuplexMode::Half)
        if (auto* queues = audio_queues_locked())
        {
            s_state.snapshot.tx_dropped += static_cast<uint32_t>(queues->outbound.size());
            queues->outbound.clear();
        }
    s_state.snapshot.updated_ms = now_ms();
}

bool consume_duplex_mode_request(DuplexMode* out_mode)
{
    if (!out_mode)
    {
        return false;
    }
    RuntimeLock lock(s_state.mutex);
    if (!s_state.duplex_mode_request_pending)
    {
        return false;
    }
    *out_mode = s_state.snapshot.duplex_mode;
    s_state.duplex_mode_request_pending = false;
    return true;
}

void set_realtime_hooks(const RealtimeHooks& hooks)
{
    RuntimeLock lock(s_state.mutex);
    s_state.realtime_hooks = hooks;
}

void set_wifi_ready(bool ready)
{
    RuntimeLock lock(s_state.mutex);
    s_state.snapshot.wifi_ready = ready;
    s_state.snapshot.updated_ms = now_ms();
}

bool begin_incoming(const Peer& peer)
{
    if (!begin_incoming_identifying(peer))
    {
        return false;
    }
    update_peer(peer);
    return mark_incoming_ringing(peer.link_id);
}

bool begin_incoming_identifying(const Peer& peer)
{
    if (hash_is_empty(peer.link_id))
    {
        return false;
    }
    RealtimeHooks realtime_hooks{};
    uint8_t link_id[kHashSize] = {};
    bool start_soft_preempt = false;
    uint64_t generation = 0;
    {
        RuntimeLock lock(s_state.mutex);
        if (s_state.configuration_change) return false;
        if (s_state.snapshot.state == State::Active ||
            s_state.snapshot.state == State::Incoming ||
            s_state.snapshot.state == State::Outgoing)
        {
            return hashes_equal(s_state.snapshot.link_id, peer.link_id);
        }
        s_state.snapshot = Snapshot{};
        ++s_state.call_generation;
        generation = s_state.call_generation;
        s_state.media_starting = false;
        s_state.duplex_mode_request_pending = false;
        s_state.snapshot.supported = true;
        s_state.snapshot.incoming = true;
        s_state.snapshot.state = State::Incoming;
        s_state.snapshot.realtime_phase = RealtimePhase::IncomingIdentifying;
        s_state.snapshot.media_supported =
            s_state.hooks.is_supported ? s_state.hooks.is_supported() : false;
        apply_peer_locked(peer);
        realtime_hooks = s_state.realtime_hooks;
        copy_hash(link_id, s_state.snapshot.link_id);
        clear_queues_locked();
        s_state.hangup_requested = false;
        s_state.closing_keep_exclusive = false;
        (void)acquire_sleep_wake_lease_locked();
        start_soft_preempt = true;
    }
    if (start_soft_preempt &&
        !begin_soft_preempt_outside_lock(realtime_hooks, link_id))
    {
        close_call(State::Failed, true, generation);
        return false;
    }
    bool current;
    {
        RuntimeLock lock(s_state.mutex);
        current = s_state.call_generation == generation;
    }
    if (!current) end_realtime_outside_lock(realtime_hooks, link_id);
    return current;
}

bool mark_incoming_ringing(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return false;
    }

    RealtimeHooks realtime_hooks{};
    uint64_t generation = 0;
    {
        RuntimeLock lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id) ||
            s_state.snapshot.state != State::Incoming ||
            (s_state.snapshot.realtime_phase !=
                 RealtimePhase::IncomingIdentifying &&
             s_state.snapshot.realtime_phase !=
                 RealtimePhase::IncomingRinging))
        {
            return false;
        }
        if (s_state.snapshot.realtime_phase == RealtimePhase::IncomingRinging)
        {
            return true;
        }
        s_state.snapshot.realtime_phase = RealtimePhase::IncomingRinging;
        s_state.snapshot.updated_ms = now_ms();
        realtime_hooks = s_state.realtime_hooks;
        generation = s_state.call_generation;
    }

    if (!begin_ringing_alert_outside_lock(realtime_hooks, link_id))
    {
        close_call(State::Failed, true, generation);
        return false;
    }
    bool current;
    {
        RuntimeLock lock(s_state.mutex);
        current = s_state.call_generation == generation;
    }
    if (!current) end_realtime_outside_lock(realtime_hooks, link_id);
    return current;
}

bool begin_outgoing(const Peer& peer)
{
    if (hash_is_empty(peer.link_id) || hash_is_empty(peer.destination_hash))
    {
        return false;
    }
    RealtimeHooks realtime_hooks{};
    {
        RuntimeLock lock(s_state.mutex);
        if (s_state.configuration_change) return false;
        if (s_state.snapshot.state == State::Active ||
            s_state.snapshot.state == State::Incoming ||
            s_state.snapshot.state == State::Outgoing)
        {
            return hashes_equal(s_state.snapshot.link_id, peer.link_id);
        }
        realtime_hooks = s_state.realtime_hooks;
        ++s_state.outgoing_admissions;
    }
    if (!begin_exclusive_outside_lock(realtime_hooks, peer.link_id))
    {
        RuntimeLock lock(s_state.mutex);
        --s_state.outgoing_admissions;
        return false;
    }
    set_speaker_volume(100);
    bool started = false;
    {
        RuntimeLock lock(s_state.mutex);
        --s_state.outgoing_admissions;
        if (s_state.snapshot.state == State::Active ||
            s_state.snapshot.state == State::Incoming ||
            s_state.snapshot.state == State::Outgoing)
        {
            if (hashes_equal(s_state.snapshot.link_id, peer.link_id))
            {
                return true;
            }
            realtime_hooks = s_state.realtime_hooks;
        }
        else
        {
            s_state.snapshot = Snapshot{};
            ++s_state.call_generation;
            s_state.media_starting = false;
            s_state.duplex_mode_request_pending = false;
            s_state.snapshot.supported = true;
            s_state.snapshot.incoming = false;
            s_state.snapshot.accepted = true;
            s_state.snapshot.state = State::Outgoing;
            s_state.snapshot.realtime_phase = RealtimePhase::AcceptedStarting;
            s_state.snapshot.media_supported =
                s_state.hooks.is_supported ? s_state.hooks.is_supported() : false;
            apply_peer_locked(peer);
            clear_queues_locked();
            s_state.hangup_requested = false;
            s_state.closing_keep_exclusive = false;
            (void)acquire_sleep_wake_lease_locked();
            started = true;
        }
    }
    if (started)
    {
        return true;
    }
    end_realtime_outside_lock(realtime_hooks, peer.link_id);
    return false;
}

bool try_begin_configuration_change()
{
    RuntimeLock lock(s_state.mutex);
    if (s_state.configuration_change || s_state.outgoing_admissions ||
        s_state.snapshot.realtime_phase != RealtimePhase::Idle || s_state.media_starting ||
        s_state.snapshot.media_active || s_state.hangup_requested ||
        s_state.snapshot.state == State::Incoming || s_state.snapshot.state == State::Outgoing ||
        s_state.snapshot.state == State::Active) return false;
    s_state.configuration_change = true;
    return true;
}

void end_configuration_change()
{
    RuntimeLock lock(s_state.mutex);
    s_state.configuration_change = false;
}

bool report_dial_failure(const Peer& peer)
{
    RuntimeLock lock(s_state.mutex);
    if (s_state.snapshot.realtime_phase != RealtimePhase::Idle ||
        s_state.snapshot.state == State::Incoming || s_state.snapshot.state == State::Outgoing || s_state.snapshot.state == State::Active) return false;
    s_state.snapshot = Snapshot{};
    s_state.snapshot.state = State::Failed;
    apply_peer_locked(peer);
    if (++s_state.failed_dial_revision == 0) ++s_state.failed_dial_revision;
    s_state.snapshot.failed_dial_revision = s_state.failed_dial_revision;
    return true;
}
void update_peer(const Peer& peer)
{
    if (hash_is_empty(peer.link_id))
    {
        return;
    }
    RuntimeLock lock(s_state.mutex);
    if (!hashes_equal(s_state.snapshot.link_id, peer.link_id))
    {
        return;
    }
    apply_peer_locked(peer);
}

void mark_link_active(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return;
    }
    RuntimeLock lock(s_state.mutex);
    if (!hashes_equal(s_state.snapshot.link_id, link_id))
    {
        return;
    }
    if (s_state.snapshot.state != State::Incoming &&
        s_state.snapshot.state != State::Outgoing &&
        s_state.snapshot.state != State::Active)
    {
        return;
    }
    s_state.snapshot.link_active = true;
    s_state.snapshot.updated_ms = now_ms();
}

bool prepare_media(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return false;
    }
    {
        RuntimeLock lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id) ||
            !s_state.snapshot.accepted ||
            !s_state.snapshot.link_active ||
            (s_state.snapshot.realtime_phase !=
                 RealtimePhase::AcceptedStarting &&
             s_state.snapshot.realtime_phase != RealtimePhase::ActiveCall))
        {
            return false;
        }
        if (s_state.snapshot.media_active)
        {
            return !s_state.media_starting;
        }
    }
    if (!start_media_if_needed())
    {
        return false;
    }
    RuntimeLock lock(s_state.mutex);
    return hashes_equal(s_state.snapshot.link_id, link_id) &&
           s_state.snapshot.accepted && s_state.snapshot.link_active &&
           s_state.snapshot.media_active && !s_state.media_starting;
}

bool mark_call_active(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return false;
    }
    RuntimeLock lock(s_state.mutex);
    if (!hashes_equal(s_state.snapshot.link_id, link_id) ||
        !s_state.snapshot.accepted ||
        !s_state.snapshot.link_active ||
        !s_state.snapshot.media_active || s_state.media_starting ||
        (s_state.snapshot.state != State::Incoming &&
         s_state.snapshot.state != State::Outgoing &&
         s_state.snapshot.state != State::Active))
    {
        return false;
    }
    s_state.snapshot.state = State::Active;
    s_state.snapshot.realtime_phase = RealtimePhase::ActiveCall;
    s_state.snapshot.updated_ms = now_ms();
    if (s_state.snapshot.active_since_ms == 0)
    {
        s_state.snapshot.active_since_ms = s_state.snapshot.updated_ms;
    }
    return true;
}

void notify_link_closed(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return;
    }
    uint64_t generation = 0;
    {
        RuntimeLock lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id))
        {
            return;
        }
        generation = s_state.call_generation;
    }
    close_call(State::Ended, false, generation);
}

void notify_media_failed()
{
    bool should_close = false;
    uint64_t generation = 0;
    {
        RuntimeLock lock(s_state.mutex);
        should_close = s_state.snapshot.accepted &&
                       s_state.snapshot.media_active;
        generation = s_state.call_generation;
    }
    if (should_close)
    {
        close_call(State::Failed, true, generation);
    }
}

void notify_media_failed_for_link(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id)) return;
    uint64_t generation = 0;
    {
        RuntimeLock lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id) || !s_state.snapshot.accepted || !s_state.snapshot.media_active) return;
        generation = s_state.call_generation;
    }
    close_call(State::Failed, true, generation);
}

void service_ui_runtime()
{
    bool apply_lease = false;
    bool release_lease = false;
    {
        RuntimeLock lock(s_state.mutex);
        if (s_state.sleep_wake_lease_applied != s_state.sleep_wake_lease)
        {
            s_state.sleep_wake_lease_applied = s_state.sleep_wake_lease;
            apply_lease = s_state.sleep_wake_lease;
            release_lease = !s_state.sleep_wake_lease;
        }
    }

    if (apply_lease)
    {
        screen::wake_for_modal();
        screen::disable_sleep();
        screen::record_activity();
    }
    else if (release_lease)
    {
        screen::enable_sleep();
    }
}

bool accept()
{
    RealtimeHooks realtime_hooks{};
    uint8_t link_id[kHashSize] = {};
    {
        RuntimeLock lock(s_state.mutex);
        if (s_state.snapshot.state != State::Incoming ||
            s_state.snapshot.realtime_phase !=
                RealtimePhase::IncomingRinging)
        {
            return false;
        }
        realtime_hooks = s_state.realtime_hooks;
        copy_hash(link_id, s_state.snapshot.link_id);
    }
    if (!begin_exclusive_outside_lock(realtime_hooks, link_id))
    {
        close_call(State::Failed, true);
        return false;
    }
    set_speaker_volume(100);
    bool accepted = false;
    {
        RuntimeLock lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id) ||
            s_state.snapshot.state != State::Incoming ||
            s_state.snapshot.realtime_phase !=
                RealtimePhase::IncomingRinging)
        {
            realtime_hooks = s_state.realtime_hooks;
        }
        else
        {
            s_state.snapshot.accepted = true;
            s_state.snapshot.realtime_phase =
                RealtimePhase::AcceptedStarting;
            s_state.snapshot.updated_ms = now_ms();
            (void)acquire_sleep_wake_lease_locked();
            accepted = true;
        }
    }
    if (accepted)
    {
        return true;
    }
    end_realtime_outside_lock(realtime_hooks, link_id);
    return false;
}

void reject()
{
    close_call(State::Ended, true);
}

void hangup()
{
    close_call(State::Ended, true);
}

bool consume_hangup_request(uint8_t out_link_id[kHashSize])
{
    if (!out_link_id)
    {
        return false;
    }
    RuntimeLock lock(s_state.mutex);
    if (!s_state.hangup_requested ||
        hash_is_empty(s_state.snapshot.link_id))
    {
        return false;
    }
    copy_hash(out_link_id, s_state.snapshot.link_id);
    s_state.hangup_requested = false;
    return true;
}

Snapshot snapshot()
{
    RuntimeLock lock(s_state.mutex);
    Snapshot copy = s_state.snapshot;
    copy.media_supported =
        s_state.hooks.is_supported ? s_state.hooks.is_supported() : false;
    return copy;
}

State state()
{
    RuntimeLock lock(s_state.mutex);
    return s_state.snapshot.state;
}

RealtimePhase realtime_phase()
{
    RuntimeLock lock(s_state.mutex);
    return s_state.snapshot.realtime_phase;
}

bool media_should_run()
{
    RuntimeLock lock(s_state.mutex);
    return s_state.snapshot.accepted &&
           s_state.snapshot.link_active &&
           s_state.snapshot.media_active &&
           (s_state.snapshot.realtime_phase ==
                RealtimePhase::AcceptedStarting ||
            s_state.snapshot.realtime_phase == RealtimePhase::ActiveCall);
}

bool realtime_mode_active()
{
    return resource_preempt_active();
}

bool modal_active()
{
    RuntimeLock lock(s_state.mutex);
    return s_state.snapshot.realtime_phase != RealtimePhase::Idle;
}

bool resource_preempt_active()
{
    RuntimeLock lock(s_state.mutex);
    return s_state.hangup_requested ||
           s_state.snapshot.realtime_phase != RealtimePhase::Idle;
}

bool wifi_exclusive_active()
{
    RuntimeLock lock(s_state.mutex);
    return s_state.snapshot.realtime_phase == RealtimePhase::AcceptedStarting ||
           s_state.snapshot.realtime_phase == RealtimePhase::ActiveCall ||
           (s_state.snapshot.realtime_phase == RealtimePhase::ClosingCall &&
            s_state.closing_keep_exclusive);
}

bool current_link_id(uint8_t out_link_id[kHashSize])
{
    if (!out_link_id)
    {
        return false;
    }
    RuntimeLock lock(s_state.mutex);
    if (hash_is_empty(s_state.snapshot.link_id))
    {
        return false;
    }
    copy_hash(out_link_id, s_state.snapshot.link_id);
    return true;
}

bool enqueue_inbound_audio(const uint8_t link_id[kHashSize],
                           const uint8_t* data,
                           std::size_t len)
{
    return enqueue_packet(link_id, data, len, true);
}

bool dequeue_inbound_audio(AudioPacket* out)
{
    if (!out)
    {
        return false;
    }
    RuntimeLock lock(s_state.mutex);
    AudioQueues* const queues = audio_queues_locked();
    return queues && queues->inbound.popOldest(out);
}

bool enqueue_outbound_audio(const uint8_t link_id[kHashSize],
                            const uint8_t* data,
                            std::size_t len)
{
    return enqueue_packet(link_id, data, len, false);
}

bool dequeue_outbound_audio(AudioPacket* out)
{
    if (!out)
    {
        return false;
    }
    RuntimeLock lock(s_state.mutex);
    AudioQueues* const queues = audio_queues_locked();
    return queues && queues->outbound.popOldest(out);
}

void note_tx_sent(std::size_t bytes)
{
    RuntimeLock lock(s_state.mutex);
    ++s_state.snapshot.tx_packets;
    s_state.snapshot.tx_bytes += static_cast<uint32_t>(bytes);
    s_state.snapshot.updated_ms = now_ms();
}

} // namespace platform::ui::reticulum_call
