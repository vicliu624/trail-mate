#include "platform/linux/oneseg_session_scan_port.h"
#include <cassert>
#include <iostream>

using namespace platform::linux_runtime;
int main()
{
    // TEST FIXTURE - NO RF. State-boundary tests do not attest receiver hardware.
    OneSegTvSession::Snapshot state;
    state.active = true;
    state.phase = OneSegTvSession::Phase::Decoding;
    state.generation = state.decoder.generation = 7;
    state.media_epoch = state.media.generation = 9;
    state.iq_age_ms = state.video_age_ms = 0;
    auto& table = state.decoder.programmes;
    table.pmt_valid = true;
    table.selected_program = 8;
    table.program_count = 1;
    table.programs[0].number = 8;
    table.selected_video_packets = 10;
    state.media.video_frames = 1;
    state.service_text.service = 8;
    state.service_text.service_valid = true;
    state.service_text.name.text = "Received station";
    assert(oneseg_scan_observation(state).confirmed_media);
    assert(oneseg_scan_observation(state).name == "Received station");
    state.service_text.service = 9;
    assert(oneseg_scan_observation(state).name.empty());
    state.service_text.service = 8;
    --state.media.generation;
    assert(!oneseg_scan_observation(state).confirmed_media);
    assert(oneseg_scan_observation(state).name.empty());
    ++state.media.generation;
    --state.decoder.generation;
    assert(!oneseg_scan_observation(state).confirmed_media);
    ++state.decoder.generation;
    state.video_age_ms = 2000;
    assert(!oneseg_scan_observation(state).confirmed_media);
    state.media.pcm_samples = 480;
    table.selected_audio_packets = 2;
    assert(oneseg_scan_observation(state).confirmed_media);
    table.pmt_valid = false;
    assert(!oneseg_scan_observation(state).confirmed_media);
    state.active = false;
    state.error = OneSegTvSession::Error::Decoder;
    state.decoder.child_exit_code = 4;
    state.receive.error = OneSegReceiveWorker::Error::SinkWrite;
    state.active = true; // Child has failed, but the owner still holds resources.
    auto cleaning = oneseg_scan_observation(state);
    assert(!cleaning.no_signal && !cleaning.failed && !cleaning.confirmed_media);
    state.active = false;
    assert(oneseg_scan_observation(state).no_signal);
    assert(!oneseg_scan_observation(state).failed);
    state.receive.error = OneSegReceiveWorker::Error::QueueFull;
    assert(!oneseg_scan_observation(state).no_signal && oneseg_scan_observation(state).failed);
    state.receive.error = OneSegReceiveWorker::Error::RadioRestore;
    assert(oneseg_scan_observation(state).restoration_failed);
    state.receive.error = OneSegReceiveWorker::Error::SinkFinish;
    state.decoder.child_signal = 15;
    assert(!oneseg_scan_observation(state).no_signal);
    state.decoder.child_signal = 0;
    state.automatic_parameters = false;
    assert(!oneseg_scan_observation(state).no_signal);
    std::cout << "Real-session scan evidence classification passed (TEST FIXTURE - NO RF)\n";
}
