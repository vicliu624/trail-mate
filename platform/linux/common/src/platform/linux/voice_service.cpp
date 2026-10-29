#include "platform/linux/voice_service.h"
#include "chat/infra/voice/vmp_mqtt_transport.h"
#include "platform/linux/sdl_pcm_input.h"
#include "platform/linux/vmp_radio.h"
#include "platform/linux/voice_store.h"
#include "sys/clock.h"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/rand.h>
#endif

namespace platform::linux_runtime
{
namespace
{
namespace vmp = ::chat::voice::vmp;
using Protocol = ::chat::MeshProtocol;
using Metadata = vmp::VoiceMessageMetadata;
using Result = LinuxVoiceStore::Result;
uint64_t monotonic() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Owner
{
    int fd = -1;
    ~Owner()
    {
        if (fd >= 0) ::close(fd);
    }
    bool acquire(const std::filesystem::path& database)
    {
        if (fd >= 0) return true;
        std::error_code error;
        if (!database.parent_path().empty()) std::filesystem::create_directories(database.parent_path(), error);
        if (error) return false;
        const auto path = database.string() + ".vmp-owner";
        fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0) return true;
        if (fd >= 0) ::close(fd);
        fd = -1;
        return false;
    }
};
} // namespace
struct NativeVoiceService::Impl
{
    struct Read
    {
        uint8_t protocol = 0, channel = 0;
        uint32_t peer = 0;
        bool broadcast = false;
    };
    struct MqttFrame
    {
        std::array<uint8_t, vmp::kMaxMqttEnvelopeSize> bytes{};
        size_t size = 0;
        uint64_t epoch = 0;
        uint32_t self = 0;
    };
    bool mqttCurrent(const MqttFrame& frame)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !stopping && epoch == frame.epoch && self == frame.self && protocol == Protocol::Meshtastic;
    }
    void receiveMqtt(const MqttFrame& frame, LinuxVoiceStore& store)
    {
        if (!mqttCurrent(frame)) return;
        if (mqtt_epoch != frame.epoch || monotonic() - mqtt_started >= 30000)
        {
            mqtt_transfer.clear();
            mqtt_epoch = frame.epoch;
            mqtt_started = monotonic();
        }
        struct NoContacts final : vmp::IVerifiedContactSecretProvider
        {
            bool lookupVerifiedContactSecret(uint32_t, uint8_t*) const override { return false; }
        } no_contacts;
        const auto* contacts = options.contacts ? options.contacts(Protocol::Meshtastic) : nullptr;
        const bool was_active = mqtt_transfer.active();
        const auto result = mqtt_transfer.acceptEnvelope(frame.bytes.data(), frame.size, frame.self, contacts ? *contacts : no_contacts);
        if (!was_active && mqtt_transfer.active()) mqtt_started = monotonic();
        if (result != vmp::MqttTransferResult::Complete) return;
        mqtt_clip.clear();
        const auto& control = mqtt_transfer.control();
        if (mqtt_transfer.recover(mqtt_clip.bytes.data(), mqtt_clip.bytes.size(), &mqtt_clip.size) && mqtt_clip.valid() &&
            vmp::isValidVoicePresentationBinding(vmp::VoicePresentationProtocol::Meshtastic, control.conversation_channel) && mqttCurrent(frame))
        {
            uint64_t id = 0;
            const auto saved = store.append(control, mqtt_clip, false, ::sys::epoch_seconds_now(),
                                            vmp::VoicePresentationProtocol::Meshtastic, control.conversation_channel, id);
            if (saved == Result::Ready || saved == Result::Duplicate)
            {
                publish(store);
                if (saved == Result::Ready) notify(control.conversation_channel, id, true);
            }
            else failure("MQTT voice attachment could not be committed");
        }
        mqtt_transfer.clear();
        mqtt_clip.clear();
    }
    enum class Kind
    {
        Record,
        Play,
        Incoming
    };
    struct Job
    {
        Kind kind = Kind::Record;
        uint64_t epoch = 0, local_id = 0, received = 0;
        uint32_t self = 0, peer = 0;
        Protocol protocol = Protocol::Meshtastic;
        uint8_t channel = 0;
        bool mqtt = false;
        std::array<uint8_t, vmp::kControlFrameSize> control{};
    };
    Impl(IPacketRadio& port, VoiceServiceOptions value) : radio(port), options(std::move(value)), self(options.self), protocol(options.protocol), verified(options.verified_peers), thread([this]
                                                                                                                                                                                           { run(); }) {}
    ~Impl() { stop(); }
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            cancel = true;
            wake.notify_all();
        }
        if (thread.joinable()) thread.join();
    }
    bool current(const Job& job)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !stopping && !cancel && epoch == job.epoch && self == job.self && protocol == job.protocol;
    }
    void publish(LinuxVoiceStore& store)
    {
        std::array<Metadata, vmp::kVoiceInboxCapacity> metadata;
        const auto count = store.list(metadata.data(), metadata.size());
        std::lock_guard<std::mutex> lock(mutex);
        rows = metadata;
        row_count = count;
    }
    void notify(uint8_t channel, uint64_t id, bool incoming)
    {
        // A presentation wake-up must not interrupt an already committed send.
        try
        {
            if (options.changed) options.changed(channel, id, incoming);
        }
        catch (...)
        {
        }
    }
    struct Commit final : IVmpIncomingCommit
    {
        Impl& service;
        LinuxVoiceStore& store;
        const Job& job;
        Commit(Impl& service, LinuxVoiceStore& store, const Job& job) : service(service), store(store), job(job) {}
        Reservation reservePrivateControl(const std::array<uint8_t, 32>& identifier) override
        {
            if (!service.current(job)) return Reservation::Unavailable;
            const auto saved = store.reservePrivateControl(identifier);
            return saved == Result::Ready ? Reservation::Reserved : saved == Result::Duplicate ? Reservation::Replayed
                                                                                               : Reservation::Unavailable;
        }
        bool commit(const vmp::ControlFrame& control, const VoiceClip& clip) override
        {
            if (!service.current(job)) return false;
            uint64_t id = 0;
            const auto saved = store.append(control, clip, false, ::sys::epoch_seconds_now(),
                                            static_cast<vmp::VoicePresentationProtocol>(job.protocol), control.conversation_channel, id);
            if (saved != Result::Ready && saved != Result::Duplicate) return false;
            service.publish(store);
            if (saved == Result::Ready) service.notify(control.conversation_channel, id, true);
            return true;
        }
    };
    void execute(const Job& job, LinuxVoiceStore& store)
    {
        if (!current(job)) return;
        if (job.kind == Kind::Play)
        {
            VoiceClip clip;
            if (!store.media(job.local_id, clip))
            {
                failure("Voice attachment no longer exists");
                return;
            }
            NativeVoiceAudio audio(options.pcm ? options.pcm() : nullptr);
            const auto result = audio.play(clip, vmp::Codec::Codec2_1300, 100, cancel);
            clip.clear();
            if (result != NativeVoiceAudio::Result::Complete && result != NativeVoiceAudio::Result::Cancelled) failure("Voice playback failed");
            return;
        }
        const auto* contacts = options.contacts ? options.contacts(job.protocol) : nullptr;
        NativeVmpSession session(radio, contacts, options.clock);
        if (job.kind == Kind::Incoming)
        {
            vmp::ControlFrame control;
            if (!vmp::decodeControlFrame(job.control.data(), job.control.size(), &control)) return;
            const auto budget = control.type == vmp::ControlType::Offer ? 1000u : std::min<unsigned>(control.data_start_delay_ms, 5000u);
            if (monotonic() - job.received >= budget) return;
            Commit commit(*this, store, job);
            const auto result = session.receive(job.control.data(), job.control.size(), job.self, commit, cancel);
            if (result == NativeVmpSession::Result::RadioFailure || result == NativeVmpSession::Result::RestoreFailure) failure("Voice receive radio failure");
            else if (result == NativeVmpSession::Result::StorageFailure) failure("Voice receive storage failure");
            return;
        }
        NativeVoiceAudio audio(options.pcm ? options.pcm() : nullptr);
        const auto captured = audio.capture(stop_recording, cancel);
        if (captured != NativeVoiceAudio::Result::Complete || !current(job))
        {
            if (captured != NativeVoiceAudio::Result::Cancelled && !cancel) failure("Voice capture failed");
            return;
        }
        if (job.mqtt)
        {
            sendMqtt(job, audio.captured(), contacts, store);
            return;
        }
        if (session.prepare({job.self, job.peer, job.channel, 0}, audio.captured()) != NativeVmpSession::Result::Prepared || !current(job))
        {
            failure("Voice session preparation failed");
            return;
        }
        uint64_t id = 0;
        const auto saved = store.append(session.control(), session.media(), true, ::sys::epoch_seconds_now(),
                                        static_cast<vmp::VoicePresentationProtocol>(job.protocol), job.channel, id);
        if (saved != Result::Ready)
        {
            session.discard();
            failure("Voice attachment could not be committed");
            return;
        }
        publish(store);
        notify(job.channel, id, false);
        const auto sent = session.send(cancel);
        const auto delivery = sent == NativeVmpSession::Result::Complete ? vmp::VoiceDeliveryState::Sent : vmp::VoiceDeliveryState::Failed;
        if (store.delivery(id, delivery) != Result::Ready) failure("Voice delivery state could not be committed");
        else
        {
            publish(store);
            notify(job.channel, id, false);
        }
    }
    void failure(const char* error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        view.error = error;
    }
    void sendMqtt(const Job& job, const VoiceClip& clip, const vmp::IVerifiedContactSecretProvider* contacts, LinuxVoiceStore& store)
    {
#if defined(TRAIL_MATE_HAS_OPENSSL)
        vmp::ControlFrame control;
        const bool private_mode = job.peer && job.peer != vmp::kBroadcastTargetId;
        control.type = private_mode ? vmp::ControlType::Offer : vmp::ControlType::Announce;
        control.flags = private_mode ? vmp::ControlFlagPrivate : vmp::ControlFlagBroadcast | vmp::ControlFlagPublicBroadcast;
        control.sender_id = job.self;
        control.target_id = private_mode ? job.peer : vmp::kBroadcastTargetId;
        control.conversation_channel = job.channel;
        control.phy_profile_id = 1;
        control.encoded_media_len = static_cast<uint16_t>(clip.size);
        control.total_blocks = 1;
        control.data_start_delay_ms = private_mode ? 120 : 700;
        if (RAND_bytes(reinterpret_cast<uint8_t*>(&control.session_id), sizeof(control.session_id)) != 1 ||
            RAND_bytes(control.session_nonce, sizeof(control.session_nonce)) != 1 ||
            RAND_bytes(reinterpret_cast<uint8_t*>(&control.object_fingerprint), sizeof(control.object_fingerprint)) != 1)
        {
            failure("MQTT voice randomness unavailable");
            return;
        }
        vmp::MqttTransmitTransfer transfer;
        struct ClearTransfer
        {
            vmp::MqttTransmitTransfer& transfer;
            ~ClearTransfer() { transfer.clear(); }
        } clear_transfer{transfer};
        struct Secrets
        {
            uint8_t contact[vmp::kPrivateKeySize]{};
            vmp::EphemeralKeyPair ephemeral;
            ~Secrets()
            {
                auto* bytes = reinterpret_cast<volatile uint8_t*>(this);
                for (size_t i = 0; i < sizeof(*this); ++i) bytes[i] = 0;
            }
        } secrets;
        bool prepared = false;
        if (private_mode)
        {
            if (contacts && contacts->lookupVerifiedContactSecret(job.peer, secrets.contact) && vmp::generateEphemeralKeyPair(&secrets.ephemeral))
            {
                std::copy_n(secrets.ephemeral.public_key, sizeof(control.ephemeral_public_key), control.ephemeral_public_key);
                prepared = transfer.preparePrivate(control, secrets.contact, clip.bytes.data(), clip.size);
            }
        }
        else prepared = transfer.prepareBroadcast(control, clip.bytes.data(), clip.size);
        if (!prepared || !current(job))
        {
            transfer.clear();
            failure("MQTT voice session preparation failed");
            return;
        }
        uint64_t id = 0;
        if (store.append(control, clip, true, ::sys::epoch_seconds_now(), vmp::VoicePresentationProtocol::Meshtastic, job.channel, id) != Result::Ready)
        {
            transfer.clear();
            failure("MQTT voice attachment could not be committed");
            return;
        }
        publish(store);
        notify(job.channel, id, false);
        std::array<uint8_t, vmp::kMaxMqttEnvelopeSize> envelope{};
        bool sent = true;
        while (transfer.hasNext())
        {
            size_t size = envelope.size();
            bool written = false;
            try
            {
                written = current(job) && transfer.copyNextEnvelope(envelope.data(), &size) && options.mqtt_publish &&
                          options.mqtt_publish(envelope.data(), size, cancel);
            }
            catch (...)
            {
            }
            if (!written || !transfer.commitNextEnvelope())
            {
                sent = false;
                break;
            }
        }
        transfer.clear();
        if (store.delivery(id, sent ? vmp::VoiceDeliveryState::Sent : vmp::VoiceDeliveryState::Failed) != Result::Ready)
            failure("MQTT voice delivery state could not be committed");
        else
        {
            publish(store);
            notify(job.channel, id, false);
        }
#endif
    }
    void run()
    {
        // std::thread does not run SDL's thread-exit wrapper. Release its
        // per-thread audio/error data after all worker-owned audio objects.
        struct AudioThreadCleanup
        {
            ~AudioThreadCleanup() { SDL_TLSCleanup(); }
        } audio_thread_cleanup;
        Owner owner;
        LinuxVoiceStore store(options.database);
        bool opened = false;
        try
        {
            for (;;)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (stopping) break;
                }
                if (!opened)
                {
                    opened = owner.acquire(options.database) && store.open() == Result::Ready;
                    if (opened) publish(store);
                    else failure("Voice storage unavailable or owned by another instance");
                }
                const bool capture = options.capture_available ? options.capture_available() : SdlPcmInput().available();
                Job job;
                Read read;
                MqttFrame mqtt_frame;
                bool have_job = false, have_read = false, have_mqtt = false;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    view.initialized = view.available = opened;
                    capture_ready = capture;
                    wake.wait_for(lock, std::chrono::seconds(1), [&]
                                  { return stopping || pending || read_count || mqtt_count; });
                    if (stopping) break;
                    if (pending)
                    {
                        job = pending_job;
                        pending = false;
                        have_job = true;
                    }
                    else if (read_count)
                    {
                        read = reads[read_head];
                        read_head = (read_head + 1) % reads.size();
                        --read_count;
                        have_read = true;
                    }
                    else if (mqtt_count)
                    {
                        mqtt_frame = mqtt_frames[mqtt_head];
                        mqtt_frames[mqtt_head] = {};
                        mqtt_head = (mqtt_head + 1) % mqtt_frames.size();
                        --mqtt_count;
                        have_mqtt = true;
                    }
                    if (mqtt_epoch != epoch || monotonic() - mqtt_started >= 30000) mqtt_transfer.clear();
                }
                if (have_mqtt && opened) receiveMqtt(mqtt_frame, store);
                if (have_job)
                {
                    try
                    {
                        execute(job, store);
                    }
                    catch (...)
                    {
                        failure("Voice operation failed");
                    }
                    std::lock_guard<std::mutex> lock(mutex);
                    view.busy = view.outbound = false;
                }
                if (have_read && opened)
                {
                    if (store.markRead(static_cast<vmp::VoicePresentationProtocol>(read.protocol), read.channel, read.peer, read.broadcast) == Result::Ready) publish(store);
                    else failure("Voice read state could not be committed");
                }
            }
        }
        catch (...)
        {
            failure("Voice worker stopped after an error");
        }
        mqtt_transfer.clear();
        mqtt_clip.clear();
        std::lock_guard<std::mutex> lock(mutex);
        view.available = view.initialized = view.busy = view.outbound = false;
    }
    IPacketRadio& radio;
    VoiceServiceOptions options;
    mutable std::mutex mutex;
    std::condition_variable wake;
    uint32_t self;
    Protocol protocol;
    uint64_t epoch = 1;
    std::vector<uint32_t> verified;
    bool radio_ready = false, capture_ready = false, stopping = false, pending = false;
    Snapshot view;
    Job pending_job;
    std::array<Metadata, vmp::kVoiceInboxCapacity> rows{};
    size_t row_count = 0;
    std::array<Read, vmp::kVoiceInboxCapacity> reads{};
    size_t read_head = 0, read_count = 0;
    bool mqtt_receive_ready = false;
    bool mqtt_selected = false, mqtt_send_ready = false;
    std::array<MqttFrame, 16> mqtt_frames{};
    size_t mqtt_head = 0, mqtt_count = 0;
    uint64_t mqtt_epoch = 0, mqtt_started = 0;
    vmp::MqttReceiveTransfer mqtt_transfer;
    VoiceClip mqtt_clip;
    std::atomic_bool cancel{false}, stop_recording{false};
    std::thread thread;
};
NativeVoiceService::NativeVoiceService(IPacketRadio& radio, VoiceServiceOptions options) : impl_(std::make_unique<Impl>(radio, std::move(options))) {}
NativeVoiceService::~NativeVoiceService() = default;
void NativeVoiceService::shutdown() { impl_->stop(); }
void NativeVoiceService::setContext(uint32_t self, Protocol protocol, bool ready)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->self != self || impl_->protocol != protocol)
    {
        ++impl_->epoch;
        impl_->cancel = true;
        impl_->verified.clear();
        impl_->mqtt_receive_ready = false;
        impl_->mqtt_count = impl_->mqtt_head = 0;
        impl_->mqtt_frames = {};
    }
    impl_->self = self;
    impl_->protocol = protocol;
    FskSettings profile;
    impl_->radio_ready = ready && (protocol == Protocol::Meshtastic || protocol == Protocol::MeshCore) &&
                         vmp_media_settings(0, profile) && impl_->radio.supportsFskConfiguration(profile);
}
void NativeVoiceService::invalidateContext()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->epoch;
    impl_->cancel = true;
    impl_->radio_ready = false;
    impl_->mqtt_send_ready = false;
    impl_->mqtt_receive_ready = false;
    impl_->mqtt_count = impl_->mqtt_head = 0;
    impl_->mqtt_frames = {};
}
IVoiceSource::Snapshot NativeVoiceService::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto view = impl_->view;
    view.can_record = view.available && impl_->capture_ready &&
                      (impl_->mqtt_selected ? impl_->mqtt_send_ready : impl_->radio_ready) && impl_->self && !view.busy;
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    view.can_record = false;
#endif
    return view;
}
size_t NativeVoiceService::list(Metadata* out, size_t capacity) const
{
    if (!out) return 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto count = std::min(capacity, impl_->row_count);
    std::copy_n(impl_->rows.data(), count, out);
    return count;
}
IVoiceSource::Admission NativeVoiceService::record(uint32_t peer, uint8_t protocol, uint8_t channel)
{
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    return Admission::Unsupported;
#endif
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping || !impl_->view.available || !impl_->capture_ready ||
        !(impl_->mqtt_selected ? impl_->mqtt_send_ready : impl_->radio_ready) || !impl_->self) return Admission::Unavailable;
    if (impl_->view.busy) return Admission::Busy;
    if (protocol != static_cast<uint8_t>(impl_->protocol) || !vmp::isValidVoicePresentationBinding(static_cast<vmp::VoicePresentationProtocol>(protocol), channel)) return Admission::Unsupported;
    if (peer && peer != vmp::kBroadcastTargetId)
    {
        const bool verified = impl_->options.is_verified ? impl_->options.is_verified(peer, impl_->protocol) : std::find(impl_->verified.begin(), impl_->verified.end(), peer) != impl_->verified.end();
        if (!verified) return Admission::Unverified;
    }
    impl_->pending_job = {};
    auto& job = impl_->pending_job;
    job.epoch = impl_->epoch;
    job.self = impl_->self;
    job.protocol = impl_->protocol;
    job.peer = peer;
    job.channel = channel;
    job.mqtt = impl_->mqtt_selected;
    impl_->cancel = impl_->stop_recording = false;
    impl_->view.busy = impl_->view.outbound = impl_->pending = true;
    impl_->view.error.clear();
    impl_->wake.notify_all();
    return Admission::Queued;
}
bool NativeVoiceService::stopRecording()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->view.outbound) return false;
    impl_->stop_recording = true;
    return true;
}
bool NativeVoiceService::play(uint64_t id)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping || !impl_->view.available || impl_->view.busy || !id) return false;
    if (std::none_of(impl_->rows.begin(), impl_->rows.begin() + impl_->row_count, [id](const auto& row)
                     { return row.local_id == id; })) return false;
    impl_->pending_job = {};
    auto& job = impl_->pending_job;
    job.kind = Impl::Kind::Play;
    job.local_id = id;
    job.epoch = impl_->epoch;
    job.self = impl_->self;
    job.protocol = impl_->protocol;
    impl_->cancel = false;
    impl_->view.busy = impl_->pending = true;
    impl_->wake.notify_all();
    return true;
}
bool NativeVoiceService::markRead(uint8_t protocol, uint8_t channel, uint32_t peer, bool broadcast)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping || !impl_->view.available || !vmp::isValidVoicePresentationBinding(static_cast<vmp::VoicePresentationProtocol>(protocol), channel)) return false;
    for (size_t i = 0; i < impl_->read_count; ++i)
    {
        const auto& read = impl_->reads[(impl_->read_head + i) % impl_->reads.size()];
        if (read.protocol == protocol && read.channel == channel && read.peer == peer && read.broadcast == broadcast) return true;
    }
    if (impl_->read_count == impl_->reads.size()) return false;
    impl_->reads[(impl_->read_head + impl_->read_count++) % impl_->reads.size()] = {protocol, channel, peer, broadcast};
    impl_->wake.notify_all();
    return true;
}
bool NativeVoiceService::enqueueVoiceControl(const uint8_t* bytes, size_t size, const vmp::ControlRxMetadata&, uint32_t self, Protocol protocol)
{
    if (!bytes || size != vmp::kControlFrameSize) return false;
    std::unique_lock<std::mutex> lock(impl_->mutex, std::try_to_lock);
    if (!lock || impl_->stopping || !impl_->view.available || impl_->view.busy || self != impl_->self || protocol != impl_->protocol) return false;
    impl_->pending_job = {};
    auto& job = impl_->pending_job;
    job.kind = Impl::Kind::Incoming;
    job.self = self;
    job.protocol = protocol;
    job.epoch = impl_->epoch;
    job.received = monotonic();
    std::copy_n(bytes, size, job.control.data());
    impl_->cancel = false;
    impl_->view.busy = impl_->pending = true;
    impl_->wake.notify_all();
    return true;
}
void NativeVoiceService::setMqttReceiveReady(bool ready)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->mqtt_receive_ready = ready;
}
void NativeVoiceService::setMqttSendReady(bool selected, bool ready)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->mqtt_selected = selected;
    impl_->mqtt_send_ready = selected && ready && impl_->protocol == Protocol::Meshtastic && bool(impl_->options.mqtt_publish);
}
bool NativeVoiceService::enqueueVoiceMqttEnvelope(const uint8_t* bytes, size_t size, uint32_t self, Protocol protocol)
{
    vmp::MqttEnvelopeView envelope;
    if (!vmp::parseMqttEnvelope(bytes, size, &envelope) || size > vmp::kMaxMqttEnvelopeSize) return false;
    if (envelope.kind == vmp::MqttEnvelopeKind::Control)
    {
        vmp::ControlFrame control;
        if (!vmp::decodeControlFrame(envelope.payload, envelope.payload_len, &control) || control.sender_id == self) return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping || !impl_->view.available || !impl_->mqtt_receive_ready || !self || self != impl_->self ||
        protocol != Protocol::Meshtastic || protocol != impl_->protocol || impl_->mqtt_count == impl_->mqtt_frames.size()) return false;
    auto& frame = impl_->mqtt_frames[(impl_->mqtt_head + impl_->mqtt_count++) % impl_->mqtt_frames.size()];
    frame.epoch = impl_->epoch;
    frame.self = self;
    frame.size = size;
    std::copy_n(bytes, size, frame.bytes.data());
    impl_->wake.notify_all();
    return true;
}
} // namespace platform::linux_runtime
