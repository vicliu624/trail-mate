#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::size_t positionOf(const std::string& source, const std::string& needle)
{
    const std::size_t position = source.find(needle);
    assert(position != std::string::npos);
    return position;
}

std::size_t positionOfAfter(const std::string& source,
                            const std::string& needle,
                            std::size_t offset)
{
    const std::size_t position = source.find(needle, offset);
    assert(position != std::string::npos);
    return position;
}

} // namespace

// This is deliberately a source-level boundary test. The ESP attachment
// adapter depends on SdFat/Arduino, while its most important regressions are
// architectural: bypassing text-storage hydration, exposing an object before
// a durable commit, or adding a bearer-side escape to local attachment data.
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path root = argv[1];
    const std::string session = readFile(
        root / "platform/esp/arduino_common/src/voice/vmp_pager_session.cpp");
    const std::string app = readFile(
        root / "platform/esp/arduino_common/src/app_context.cpp");
    const std::string bindings = readFile(
        root / "platform/esp/arduino_common/src/app_context_platform_bindings.cpp");
    const std::string attachment = readFile(
        root / "platform/esp/arduino_common/src/chat/infra/store/"
               "message_attachment_store.cpp");
    const std::string attachment_header = readFile(
        root / "platform/esp/arduino_common/include/platform/esp/arduino_common/"
               "chat/infra/store/message_attachment_store.h");
    const std::string pager_header = readFile(
        root / "platform/esp/arduino_common/include/platform/esp/arduino_common/"
               "voice/vmp_pager_session.h");
    const std::string pager_audio = readFile(
        root / "platform/esp/arduino_common/src/voice/vmp_pager_audio.cpp");
    const std::string pager_audio_header = readFile(
        root / "platform/esp/arduino_common/include/platform/esp/arduino_common/"
               "voice/vmp_pager_audio.h");
    const std::string event_bus = readFile(
        root / "platform/esp/arduino_common/include/sys/event_bus.h");
    const std::string event_runtime = readFile(
        root / "platform/esp/arduino_common/src/app_event_runtime_support.cpp");
    const std::string chat_event_pump = readFile(
        root / "modules/ui_chat_runtime/src/chat_page_runtime_event_pump.cpp");
    const std::string codec2 = readFile(root / "third_party/codec2/src/codec2.c");
    const std::string codec2_internal =
        readFile(root / "third_party/codec2/src/codec2_internal.h");
    const std::string codec2_fft =
        readFile(root / "third_party/codec2/src/codec2_fft.c");
    const std::string codec2_fft_header =
        readFile(root / "third_party/codec2/src/codec2_fft.h");
    const std::string codec2_nlp = readFile(root / "third_party/codec2/src/nlp.c");
    const std::string codec2_memtools =
        readFile(root / "third_party/codec2/src/memtools.c");
    const std::string codec2_sine = readFile(root / "third_party/codec2/src/sine.c");
    const std::string codec2_sine_header =
        readFile(root / "third_party/codec2/src/sine.h");
    const std::string codec2_quantise =
        readFile(root / "third_party/codec2/src/quantise.c");
    const std::string codec2_quantise_header =
        readFile(root / "third_party/codec2/src/quantise.h");
    const std::string chat_controller = readFile(
        root / "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp");
    const std::string chat_compose = readFile(
        root / "modules/ui_shared/src/ui/screens/chat/chat_compose_components.cpp");
    const std::string contacts_components = readFile(
        root / "modules/ui_shared/src/ui/screens/contacts/contacts_page_components.cpp");
    const std::string chat_page_runtime = readFile(
        root / "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp");
    const std::string storage_runtime = readFile(
        root / "platform/esp/arduino_common/src/storage/storage_runtime.cpp");
    const std::string chat_voice_runtime = readFile(
        root / "modules/ui_shared/src/ui/chat_voice_runtime.cpp");
    const std::string pager_board = readFile(
        root / "boards/tlora_pager/src/tlora_pager_board.cpp");

    const std::size_t store_completed = positionOf(session, "bool storeCompletedVoice(");
    const std::size_t outbound_store = positionOf(session, "bool storeOutboundVoice()");
    const std::string inbound_store_body =
        session.substr(store_completed, outbound_store - store_completed);
    const std::size_t durable_commit = positionOf(inbound_store_body, "persistVoiceInbox(");
    const std::size_t rollback = positionOf(inbound_store_body, "media_->inbox.erase(local_id)");
    assert(durable_commit < rollback);

    // VMP is a local attachment-message extension, not a receive-only
    // scratch inbox. The sender must durably create `Sending` before choosing
    // one immutable carrier. Direct RF/LXMF commit a terminal state in the
    // worker; MQTT remains Sending until the final socket-write acknowledgement.
    const std::size_t outbound_run = positionOf(session, "void runOutbound()");
    const std::size_t outbound_store_call = positionOfAfter(
        session, "storeOutboundVoice()", outbound_run);
    const std::size_t carrier_select = positionOfAfter(
        session, "switch (outbound_carrier_)", outbound_store_call);
    const std::size_t terminal_commit = positionOfAfter(
        session, "commitDelivery(outbound_local_id_", carrier_select);
    assert(outbound_store_call < carrier_select);
    assert(carrier_select < terminal_commit);
    const std::size_t outbound_store_commit = positionOfAfter(
        session, "persistVoiceInbox(", outbound_store);
    const std::size_t outbound_local_id = positionOfAfter(
        session, "outbound_local_id_ = local_id", outbound_store_commit);
    assert(outbound_store_commit < outbound_local_id);
    assert(session.find("VoiceDeliveryState::Sending") != std::string::npos);
    assert(session.find("VoiceDeliveryState::Sent") != std::string::npos);
    assert(session.find("VoiceDeliveryState::Failed") != std::string::npos);
    assert(session.find("local delivery awaiting_mqtt_socket") !=
           std::string::npos);
    assert(session.find("commitMqttDeliveryLocked(vmp::VoiceDeliveryState::Sent)") !=
           std::string::npos);
    assert(session.find("requires_durable_attachment_store_") !=
           std::string::npos);
    assert(session.find("inbox_ready_") != std::string::npos);
    assert(session.find("servicePersistentInbox") != std::string::npos);

    assert(app.find("getSelfNodeId(), deferred_storage_store_context_ != nullptr") !=
           std::string::npos);
    assert(app.find("vmp_session::servicePersistentInbox()") !=
           std::string::npos);
    // The VMP runtime projects a chat-message timeline in both directions.
    // Do not regress to the original receive-only inbox API: the sender's
    // durable Sending/Sent/Failed object must be visible to the controller.
    assert(app.find("std::size_t listMessages(") != std::string::npos);
    assert(chat_controller.find("::ui::chat_voice::listMessages(") !=
           std::string::npos);
    assert(chat_controller.find("::ui::chat_voice::listReceivedMessages(") ==
           std::string::npos);
    // Durable attachment recovery is intentionally a send gate, never a
    // reason to hide the supported Pager's only voice affordance.
    assert(chat_voice_runtime.find("bool isRuntimeBound()") != std::string::npos);
    assert(chat_controller.find(
               "setVoiceButton(::ui::i18n::tr(\"Voice\"), voice_runtime_bound)") !=
           std::string::npos);
    assert(chat_controller.find("Hold to talk") == std::string::npos);
    assert(chat_controller.find("setVoiceButton(::ui::i18n::tr(\"Release\"), true)") !=
           std::string::npos);
    assert(chat_controller.find("Release %lu.%lus") == std::string::npos);
    // Typed attachments have the same conversation boundary as text: a peer
    // number alone is insufficient because channels and mesh backends can
    // legitimately reuse it. The logical chat channel is part of VMP control
    // and the local protocol/channel binding is persisted/projected.
    assert(chat_controller.find("summary.presentation_protocol !=") !=
           std::string::npos);
    assert(chat_controller.find("appendVoiceConversationsToControllerList") !=
           std::string::npos);
    assert(chat_controller.find("::ui::chat_voice::markConversationRead(") !=
           std::string::npos);
    assert(chat_controller.find("current_conv_.peer == 0U ? chat::voice::vmp::kBroadcastTargetId") !=
           std::string::npos);
    assert(session.find("outgoing_control_.conversation_channel =") !=
           std::string::npos);
    assert(session.find("control.conversation_channel") != std::string::npos);
    assert(app.find("metadata.presentation_protocol") != std::string::npos);
    assert(app.find("metadata.presentation_channel") != std::string::npos);
    // Press-to-talk begins on PRESSED, but release must leave the compose
    // event stack before it can destroy that view. The conversation then
    // polls the local attachment commit quickly enough to expose Sending.
    assert(chat_compose.find("LV_EVENT_PRESSED") != std::string::npos);
    assert(chat_compose.find("schedule_action_async(ActionIntent::VoiceStop)") !=
           std::string::npos);
    assert(chat_controller.find("kVoiceProjectionBusyPollMs = 150U") !=
           std::string::npos);
    assert(chat_controller.find("voice release stop_requested=%u; return to timeline") !=
           std::string::npos);
    assert(app.find("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") !=
           std::string::npos);
    assert(app.find("UI metadata scratch unavailable in PSRAM") !=
           std::string::npos);
    assert(bindings.find("vmp_session::onPersistentStorageReady()") !=
           std::string::npos);
    assert(storage_runtime.find("bool initial_hydration_ready()") !=
           std::string::npos);
    assert(session.find("initial_hydration_ready()") != std::string::npos);
    assert(session.find("[VMP][Store] durable hydration observed after edge") !=
           std::string::npos);

    // Contacts chooses a destination but must not create a competing Compose
    // tree. The Chat app owns push-to-talk, durable projections, and the
    // post-send return to the conversation for every entry path.
    assert(contacts_components.find("chat::ui::runtime::requestCompose(conv)") !=
           std::string::npos);
    assert(contacts_components.find("launchAppByStableId(\"chat\")") !=
           std::string::npos);
    assert(contacts_components.find("new chat::ui::ChatComposeScreen") ==
           std::string::npos);
    assert(chat_page_runtime.find("PendingComposeRoute") != std::string::npos);
    assert(chat_page_runtime.find("openComposeForConversation(requested)") !=
           std::string::npos);

    // Rotary focus follows the visible compact layout, rather than append
    // timing: IME, Symbols, Emoji, Send, optional Voice/Position, Cancel,
    // then the top-bar Back button. The hidden IME proxy and text area are
    // intentionally excluded from that cycle.
    assert(chat_compose.find("void ChatComposeScreen::syncFocusOrder") !=
           std::string::npos);
    const std::size_t ime_focus = chat_compose.find("add_visible(ime_toggle)");
    const std::size_t sym_focus = chat_compose.find("add_visible(impl_->sym_btn)");
    const std::size_t emoji_focus = chat_compose.find("add_visible(impl_->emoji_btn)");
    const std::size_t zero_focus_branch = chat_compose.find("#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)",
                                                            chat_compose.find("void ChatComposeScreen::syncFocusOrder"));
    const std::size_t zero_text_focus = chat_compose.find("add_visible(impl_->w.textarea)", zero_focus_branch);
    const std::size_t zero_send_focus = chat_compose.find("add_visible(impl_->w.send_btn)", zero_text_focus);
    const std::size_t zero_focus_end = chat_compose.find("#endif", zero_focus_branch);
    assert(zero_focus_branch < zero_text_focus && zero_text_focus < zero_send_focus && zero_send_focus < zero_focus_end);
    const std::size_t rotary_focus_branch = chat_compose.find("#if !defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)", emoji_focus);
    const std::size_t send_focus = chat_compose.find("add_visible(impl_->w.send_btn)", rotary_focus_branch);
    const std::size_t auxiliary_focus =
        chat_compose.find("add_visible(impl_->w.position_btn)");
    const std::size_t cancel_focus =
        chat_compose.find("add_visible(impl_->w.cancel_btn)");
    const std::size_t back_focus =
        chat_compose.find("add_visible(impl_->w.top_bar.back_btn)");
    assert(ime_focus < sym_focus && sym_focus < emoji_focus &&
           emoji_focus < send_focus && send_focus < auxiliary_focus &&
           auxiliary_focus < cancel_focus && cancel_focus < back_focus);

    assert(attachment.find("/data/v2/attachments/voice/inbox.v1") !=
           std::string::npos);
    assert(attachment.find("inbox.v1.tmp") != std::string::npos);
    assert(attachment.find("inbox.v1.bak") != std::string::npos);
    assert(attachment.find("payload_crc32") != std::string::npos);
    assert(attachment.find("kSnapshotSchemaVersion = 2U") != std::string::npos);
    assert(attachment.find("record.reserved[1] = static_cast<uint8_t>(metadata.presentation_protocol)") !=
           std::string::npos);
    assert(attachment.find("record.reserved[2] = metadata.presentation_channel") !=
           std::string::npos);
    assert(attachment.find("kVoiceReadFlag") != std::string::npos);
    assert(attachment.find("restoreVoiceInboxSnapshot") != std::string::npos);
    assert(attachment.find("header.record_count - index") != std::string::npos);
    // Snapshot records and restore sequencing are a matched on-disk contract:
    // listMetadata() is newest-first, serialization must retain that order,
    // and hydrate assigns the first record the highest sequence.  Reversing
    // only one side silently inverts the chat timeline after reboot.
    const std::size_t persist = positionOf(attachment, "bool persistVoiceInbox(");
    const std::string persist_body = attachment.substr(persist);
    assert(persist_body.find("for (std::size_t index = 0U; wrote && index < count; ++index)") !=
           std::string::npos);
    assert(persist_body.find("metadata_scratch[index]") != std::string::npos);
    assert(persist_body.find("metadata_scratch[remaining - 1U]") == std::string::npos);
    assert(attachment.find("const VoiceInboxLoadResult backup_result") !=
           std::string::npos);
    assert(attachment.find("return backup_result;") != std::string::npos);
    // The prior snapshot must survive a successful temporary-to-primary
    // rename, otherwise a detected primary corruption has nothing to restore.
    assert(attachment.find("if (moved_current)\n        {\n            (void)storage::sd_remove(kVoiceSnapshotBackupPath);") ==
           std::string::npos);
    assert(attachment.find("AttachmentKind::Voice") != std::string::npos);
    assert(attachment_header.find("Image = 2U") != std::string::npos);
    assert(attachment_header.find("Location = 3U") != std::string::npos);
    assert(attachment.find("radio::") == std::string::npos);
    assert(attachment.find("mqtt_") == std::string::npos);
    assert(attachment.find("lxmf_") == std::string::npos);

    // The Pager's SX1262 variant can only create the isolated MQTT plan after
    // a live broker session exists. It must never become a hidden direct-RF or
    // LXMF fallback merely because the shared VMP session is compiled for both
    // Pager radio variants. LR1121 prefers MQTT when it is live and does not
    // append a second MQTT copy after an RF transfer.
    assert(session.find("#if defined(ARDUINO_T_LORA_PAGER)") !=
           std::string::npos);
    assert(session.find("direct_rf_voice_supported_") != std::string::npos);
    assert(session.find("mqtt_queued = queueMqttPublication();") !=
           std::string::npos);
    assert(session.find("mqtt_uplink_enabled_ && mqtt_uplink_online_") !=
           std::string::npos);
    assert(session.find("rf_suppressed=%u") != std::string::npos);
    assert(session.find("if (sent && !used_lxmf)") == std::string::npos);
    assert(session.find("OutboundCarrier selectOutboundCarrierLocked") !=
           std::string::npos);
    assert(session.find("return direct_rf_voice_supported_ && source_id != 0U") !=
           std::string::npos);
    assert(pager_header.find("SX1262 never has a") != std::string::npos);
    assert(pager_audio.find("#if defined(ARDUINO_T_LORA_PAGER)") !=
           std::string::npos);

    // An RF capture must be diagnosable from *both* Pagers. Keep the ingress,
    // readiness, shard/FEC, and timeout spine explicit so an on-device report
    // can distinguish a missing offer, 2.4 GHz setup failure, ready-probe
    // failure, no-first-media condition, or durable-store failure.
    assert(session.find("[VMP][RX] private offer accepted") != std::string::npos);
    assert(session.find("[VMP][RX] private accept sent; switch_2g_rx") !=
           std::string::npos);
    assert(session.find("[VMP][RX] private 2g rx ready window_ms=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] private ready sent; wait_first_voice") !=
           std::string::npos);
    assert(session.find("[VMP][RX] private media timeout ready=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] broadcast announce accepted") !=
           std::string::npos);
    assert(session.find("[VMP][RX] broadcast 2g rx ready window_ms=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] broadcast media timeout unique_shards=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] shard quorum reached unique_shards=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] inbox durable_commit") != std::string::npos);
    // A received clip becomes visible only after its inbox transaction has
    // committed.  Its event transports the local ID, not a copied PCM/Codec2
    // frame, and is queued non-blocking after the receive mutex is released.
    assert(event_bus.find("ChatVoiceMessageEvent") != std::string::npos);
    assert(event_bus.find("ChatMessageContentKind") != std::string::npos);
    assert(event_bus.find("uint64_t local_id") != std::string::npos);
    assert(session.find("publishIncomingVoiceMessage(completed_conversation_channel,") !=
           std::string::npos);
    assert(session.find("const uint8_t completed_conversation_channel = incoming_control_.conversation_channel") !=
           std::string::npos);
    assert(session.find("sys::EventBus::publish(event, 0U)") !=
           std::string::npos);
    assert(event_runtime.find("ChatMessageContentKind::Voice") !=
           std::string::npos);
    assert(event_runtime.find("show_notice(notice, 3000)") !=
           std::string::npos);
    assert(chat_event_pump.find("ChatMessageContentKind::Voice") !=
           std::string::npos);

    // Codec2 and the Pager I2S/codec/I2C call chain execute on the short-lived
    // VMP worker, not the UI task. ESP-IDF interprets xTaskCreatePinnedToCore
    // stack depth as bytes: do not regress the overflow fix to the old 4 KiB
    // TX or 3 KiB playback task, and keep runtime proof of the high-water mark.
    // The Pager's Wi-Fi-loaded internal heap cannot reliably satisfy the former
    // 10/16 KiB request. Codec2's non-DMA workspaces are PSRAM-backed, leaving
    // a bounded 8 KiB internal stack for the I2S/codec call chain.
    assert(session.find("kOutboundTaskStackBytes = 8U * 1024U") !=
           std::string::npos);
    assert(session.find("kPlaybackTaskStackBytes = 8U * 1024U") !=
           std::string::npos);
    assert(session.find("kOutboundTaskStackWords") == std::string::npos);
    assert(session.find("kPlaybackTaskStackWords") == std::string::npos);
    assert(session.find("logCurrentTaskStack(\"vmp_tx\", \"after_capture\")") !=
           std::string::npos);
    assert(session.find("logCurrentTaskStack(\"vmp_play\", \"after_playback\")") !=
           std::string::npos);
    assert(session.find("stack_budget_bytes=%u") != std::string::npos);
    assert(session.find("logTaskCreateFailure(\"vmp_tx\", kOutboundTaskStackBytes)") !=
           std::string::npos);
    // Voice-message playback is speech, not a notification tone. Its former
    // hard-coded 70% value maps to -15 dB in the ES8311 default volume curve.
    // Keep it at full output gain unless a future dedicated voice-volume
    // setting supplies an explicit user value.
    assert(session.find("playback_codec_,\n                                        100U)") !=
           std::string::npos);
    assert(pager_header.find("volume_percent = 100U") != std::string::npos);
    assert(pager_audio_header.find("volume_percent = 100U") !=
           std::string::npos);
    const std::size_t capture_audio =
        positionOf(pager_audio, "bool beginCaptureAudio(");
    const std::size_t playback_audio =
        positionOfAfter(pager_audio, "bool beginPlaybackAudio(", capture_audio);
    const std::size_t pcm_level =
        positionOfAfter(pager_audio, "struct PcmLevel", playback_audio);
    const std::string capture_audio_body =
        pager_audio.substr(capture_audio, playback_audio - capture_audio);
    const std::string playback_audio_body =
        pager_audio.substr(playback_audio, pcm_level - playback_audio);
    assert(capture_audio_body.find("audioSetGain(kOwner, kCaptureGainDb)") !=
           std::string::npos);
    assert(capture_audio_body.find("audioSetMute(kOwner, false)") !=
           std::string::npos);
    assert(capture_audio_body.find("audioSetOutMute") == std::string::npos);
    assert(playback_audio_body.find("audioSetOutMute(kOwner, false)") !=
           std::string::npos);
    assert(playback_audio_body.find("audioSetGain") == std::string::npos);
    assert(playback_audio_body.find("audioSetMute") == std::string::npos);
    // Peak/RMS inspection must run on the existing PCM frame. It must not add
    // a retained frame, queue, or other RAM-backed telemetry buffer.
    assert(pager_audio.find("kAudioLevelLogIntervalFrames = 25U") !=
           std::string::npos);
    assert(pager_audio.find("[VMP][AUDIO] level capture") !=
           std::string::npos);
    assert(pager_audio.find("[VMP][AUDIO] level playback") !=
           std::string::npos);
    // VMP keeps its calibrated gain policy local to the voice adapter.  Do not
    // alter the shared codec volume curve, and do not add a second PCM buffer
    // merely to meter it: the gain/clip telemetry operates in FrameScratch.
    assert(pager_audio.find("kCaptureGainDb = 30.0F") != std::string::npos);
    assert(pager_audio.find("kPlaybackPcmGainQ8 = 512U") != std::string::npos);
    assert(pager_audio.find("kCapturePcmGainQ8") == std::string::npos);
    assert(pager_audio.find("applyBoundedPcmGain(frame_scratch_->mono") !=
           std::string::npos);
    assert(pager_audio.find("[VMP][AUDIO] capture summary") !=
           std::string::npos);
    assert(pager_audio.find("[VMP][AUDIO] playback summary") !=
           std::string::npos);
    assert(pager_audio.find("source=left_mono") != std::string::npos);
    assert(pager_audio.find("input_raw_clip=") != std::string::npos);
    const std::size_t mix_capture =
        positionOf(pager_audio, "void PagerCodec2Audio::mixCaptureToMono()");
    const std::size_t duplicate_playback =
        positionOfAfter(pager_audio, "void PagerCodec2Audio::duplicatePlaybackToStereo()",
                        mix_capture);
    const std::string mix_capture_body =
        pager_audio.substr(mix_capture, duplicate_playback - mix_capture);
    assert(mix_capture_body.find("level.input = measurePcmLevel") !=
           std::string::npos);
    assert(mix_capture_body.find("frame_scratch_->stereo[index * kHardwareChannels]") !=
           std::string::npos);
    assert(mix_capture_body.find("use_left") == std::string::npos);
    assert(mix_capture_body.find("use_right") == std::string::npos);
    assert(pager_audio.find("struct PagerCodec2Audio::FrameScratch\n{\n    int16_t stereo[") !=
           std::string::npos);
    assert(pager_audio.find("PcmLevel stereo[") == std::string::npos);
    assert(pager_audio.find("PcmLevel mono[") == std::string::npos);
    assert(codec2_internal.find("CODEC2_1300_SCRATCH") != std::string::npos);
    assert(codec2_internal.find("COMP         *fft_inplace_scratch") !=
           std::string::npos);
    assert(codec2.find("c2->analysis_spectrum = (COMP*)MALLOC") !=
           std::string::npos);
    assert(codec2.find("c2->mode_1300_scratch") != std::string::npos);
    assert(codec2.find("c2->fft_inplace_scratch = (COMP*)MALLOC") !=
           std::string::npos);
    assert(codec2.find("FREE(c2->fft_inplace_scratch);") != std::string::npos);
    const std::size_t decoder_1300_declaration =
        positionOf(codec2, "\nvoid codec2_decode_1300(");
    const std::size_t decoder_1300 = positionOfAfter(
        codec2, "\nvoid codec2_decode_1300(", decoder_1300_declaration + 1);
    const std::size_t encoder_1200 =
        positionOfAfter(codec2, "void codec2_encode_1200(", decoder_1300);
    const std::string decoder_1300_body =
        codec2.substr(decoder_1300, encoder_1200 - decoder_1300);
    const std::size_t analyse_frame = positionOfAfter(
        codec2, "\nvoid analyse_one_frame(", decoder_1300);
    const std::size_t ear_protection =
        positionOfAfter(codec2, "static void ear_protection(", analyse_frame);
    const std::string analyse_frame_body =
        codec2.substr(analyse_frame, ear_protection - analyse_frame);
    assert(analyse_frame_body.find("COMP    Sw[FFT_ENC]") == std::string::npos);
    assert(decoder_1300_body.find("COMP    Aw[FFT_ENC]") == std::string::npos);
    assert(codec2_internal.find("CODEC2_LPC_SCRATCH decoder_lpc_scratch") !=
           std::string::npos);
    assert(decoder_1300_body.find("aks_to_M2_with_scratch") !=
           std::string::npos);
    assert(decoder_1300_body.find("&scratch->decoder_lpc_scratch") !=
           std::string::npos);

    // Codec2-1300 decode reaches aks_to_M2() and lpc_post_filter() beneath
    // the bounded vmp_play task. Their combined 6 KiB FFT/spectral arrays
    // must stay in the decoder's PSRAM scratch, not reappear as automatic
    // arrays in the nested call chain.
    const std::size_t lpc_post_filter = positionOf(
        codec2_quantise, "static void lpc_post_filter_with_scratch(");
    const std::size_t aks_with_scratch = positionOfAfter(
        codec2_quantise, "void aks_to_M2_with_scratch(", lpc_post_filter);
    const std::size_t legacy_aks = positionOfAfter(
        codec2_quantise, "void aks_to_M2(", aks_with_scratch);
    const std::string lpc_post_filter_body =
        codec2_quantise.substr(lpc_post_filter, aks_with_scratch - lpc_post_filter);
    const std::string aks_with_scratch_body =
        codec2_quantise.substr(aks_with_scratch, legacy_aks - aks_with_scratch);
    assert(lpc_post_filter_body.find("float x[FFT_ENC]") == std::string::npos);
    assert(lpc_post_filter_body.find("COMP  Ww[FFT_ENC/2+1]") ==
           std::string::npos);
    assert(lpc_post_filter_body.find("float Rw[FFT_ENC/2+1]") ==
           std::string::npos);
    assert(lpc_post_filter_body.find("scratch->fft_input") != std::string::npos);
    assert(lpc_post_filter_body.find("scratch->post_filter_weight") !=
           std::string::npos);
    assert(lpc_post_filter_body.find("scratch->post_filter_response") !=
           std::string::npos);
    assert(aks_with_scratch_body.find("float a[FFT_ENC]") ==
           std::string::npos);
    assert(aks_with_scratch_body.find("float Pw[FFT_ENC/2]") ==
           std::string::npos);
    assert(aks_with_scratch_body.find("scratch->power_spectrum") !=
           std::string::npos);
    assert(codec2_quantise_header.find("CODEC2_LPC_SCRATCH") !=
           std::string::npos);
    assert(codec2_nlp.find("COMP   Fw[PE_FFT_SIZE]") == std::string::npos);
    assert(codec2_nlp.find("nlp->Fw = (COMP*)MALLOC") != std::string::npos);
    assert(codec2_nlp.find(
               "codec2_fft_inplace_with_scratch(nlp->fft_cfg, Fw, fft_scratch)") !=
           std::string::npos);
    assert(codec2_memtools.find("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") !=
           std::string::npos);
    // Codec2 creation happens after the Pager has acquired the I2S session.
    // Its analysis-window setup must not put two FFT_ENC complex buffers on
    // the bounded VMP task stack; the scratch belongs to the codec allocator
    // only for the duration of initialization and is then released.
    const std::size_t make_analysis_window =
        positionOf(codec2_sine, "void make_analysis_window(");
    const std::size_t hpf = positionOfAfter(
        codec2_sine, "float hpf(", make_analysis_window);
    const std::string make_analysis_window_body =
        codec2_sine.substr(make_analysis_window, hpf - make_analysis_window);
    assert(make_analysis_window_body.find("COMP  wshift[FFT_ENC]") ==
           std::string::npos);
    assert(make_analysis_window_body.find("COMP temp[FFT_ENC]") ==
           std::string::npos);
    assert(make_analysis_window_body.find("COMP  *wshift = scratch") !=
           std::string::npos);
    assert(make_analysis_window_body.find("COMP  *temp = scratch + FFT_ENC") !=
           std::string::npos);
    assert(codec2_sine_header.find("COMP scratch[]") != std::string::npos);
    assert(codec2.find("analysis_window_scratch = (COMP*)MALLOC") !=
           std::string::npos);
    assert(codec2.find("FREE(analysis_window_scratch);") != std::string::npos);

    // A VMP encoder runs both the analysis and NLP KISS FFTs while its audio
    // task is live. The generic legacy helper retains its stack fallback for
    // non-Codec2 callers, but the Codec2 path must pass instance PSRAM scratch
    // to the no-local-buffer variant.
    const std::size_t scratch_fft =
        positionOf(codec2_fft, "void codec2_fft_inplace_with_scratch(");
    const std::size_t legacy_fft = positionOfAfter(
        codec2_fft, "void codec2_fft_inplace(", scratch_fft);
    const std::string scratch_fft_body =
        codec2_fft.substr(scratch_fft, legacy_fft - scratch_fft);
    assert(scratch_fft_body.find("kiss_fft_cpx in[512]") == std::string::npos);
    assert(scratch_fft_body.find("kiss_fft(cfg, (kiss_fft_cpx*)scratch") !=
           std::string::npos);
    assert(codec2_fft_header.find("codec2_fft_inplace_with_scratch") !=
           std::string::npos);
    assert(codec2_sine.find(
               "codec2_fft_inplace_with_scratch(fft_fwd_cfg, Sw, fft_scratch)") !=
           std::string::npos);
    assert(analyse_frame_body.find("c2->fft_inplace_scratch") !=
           std::string::npos);

    // Pager's physical rotary button is an input level, not a one-shot click.
    // A VoiceStart press must remain pressed until the user actually releases
    // it; otherwise LVGL cannot dispatch a reliable VoiceStop event.
    const std::size_t button_reader = positionOf(pager_board, "static bool getButtonState()");
    const std::size_t pager_constructor =
        positionOfAfter(pager_board, "TLoRaPagerBoard::TLoRaPagerBoard()", button_reader);
    const std::string button_reader_body =
        pager_board.substr(button_reader, pager_constructor - button_reader);
    assert(button_reader_body.find("return buttonState == LOW;") != std::string::npos);
    assert(button_reader_body.find("rotaryTaskFlag") == std::string::npos);

    const std::size_t rotary_reader = positionOf(pager_board, "RotaryMsg_t TLoRaPagerBoard::getRotary()");
    const std::size_t rotary_feedback =
        positionOfAfter(pager_board, "void TLoRaPagerBoard::feedback", rotary_reader);
    const std::string rotary_reader_body =
        pager_board.substr(rotary_reader, rotary_feedback - rotary_reader);
    const std::size_t queue_receive =
        positionOf(rotary_reader_body, "if (xQueueReceive(rotaryMsg");
    const std::string idle_rotary_body = rotary_reader_body.substr(queue_receive);
    assert(idle_rotary_body.find("msg.centerBtnPressed = false;") == std::string::npos);
    return 0;
}
