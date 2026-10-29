#include "platform/ui/reticulum_call_runtime.h"
#include <cassert>
#include <cstring>

namespace platform::ui::screen
{
void wake_for_modal() {}
void disable_sleep() {}
void enable_sleep() {}
void record_activity() {}
} // namespace platform::ui::screen
int main()
{
    namespace call = platform::ui::reticulum_call;
    call::MediaHooks media;
    media.is_supported = []
    { return true; };
    media.start = []
    { return true; };
    media.stop = [] {};
    call::set_media_hooks(media);
    call::Peer peer;
    peer.link_id[0] = 1;
    peer.destination_hash[0] = 2;
    assert(call::begin_outgoing(peer));
    call::mark_link_active(peer.link_id);
    assert(call::prepare_media(peer.link_id) && call::mark_call_active(peer.link_id));
    const uint8_t bytes[] = {1, 2, 3};
    call::AudioPacket packet;
    const auto tx = [&]
    { assert(call::enqueue_outbound_audio(peer.link_id, bytes, sizeof(bytes))); };
    const auto rx = [&]
    { assert(call::enqueue_inbound_audio(peer.link_id, bytes, sizeof(bytes))); };
    tx();
    tx();
    call::set_microphone_muted(true);
    assert(!call::dequeue_outbound_audio(&packet) && call::snapshot().tx_dropped == 2);
    call::set_microphone_muted(true);
    assert(call::snapshot().tx_dropped == 2);
    call::set_microphone_muted(false);
    tx();
    assert(call::dequeue_outbound_audio(&packet));
    rx();
    rx();
    call::set_speaker_muted(true);
    assert(!call::dequeue_inbound_audio(&packet) && call::snapshot().rx_dropped == 2);
    rx();
    assert(!call::dequeue_inbound_audio(&packet));
    call::set_speaker_muted(false);
    rx();
    assert(call::dequeue_inbound_audio(&packet));
    tx();
    assert(call::set_duplex_mode(call::DuplexMode::Half));
    assert(!call::dequeue_outbound_audio(&packet));
    call::set_ptt_pressed(true);
    tx();
    tx();
    call::set_ptt_pressed(false);
    assert(!call::dequeue_outbound_audio(&packet));
    call::set_ptt_pressed(true);
    tx();
    call::apply_remote_duplex_mode(call::DuplexMode::Half);
    assert(!call::dequeue_outbound_audio(&packet) && !call::snapshot().ptt_pressed);
    assert(!call::set_duplex_mode(static_cast<call::DuplexMode>(255)));
    call::apply_remote_duplex_mode(static_cast<call::DuplexMode>(255));
    assert(call::snapshot().duplex_mode == call::DuplexMode::Half);
    assert(call::set_duplex_mode(call::DuplexMode::Full));
    tx();
    assert(call::dequeue_outbound_audio(&packet));
    call::hangup();
    uint8_t link[16];
    assert(call::consume_hangup_request(link));
    call::notify_link_closed(peer.link_id);
    call::set_media_hooks({});
}
