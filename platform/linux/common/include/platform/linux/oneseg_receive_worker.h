#pragma once

#include "platform/linux/oneseg_iq_archive.h"
#include "platform/linux/oneseg_iq_stream.h"
#include <atomic>
#include <memory>

namespace platform::linux_runtime
{
class IPacketRadio;

// One decoder connection per generation. Methods run off the UI thread;
// open/finish run on the owner thread, write on its consumer thread.
// Open and write must be bounded and honor cancellation. Finish must release
// partial resources even after a failed/throwing open, and be idempotent.
class OneSegIqSink
{
  public:
    virtual ~OneSegIqSink() = default;
    virtual bool open(unsigned channel, uint64_t generation, const std::atomic_bool& cancel) = 0;
    // Owner-thread operation, after previous finish and before the next open.
    virtual bool configureReception(const OneSegReceptionSettings&) { return true; }
    // Called off the UI before opening an archive replay. Implementations must
    // explicitly support its rate/source, otherwise replay is rejected.
    virtual bool configureRecordedIq(uint32_t) { return false; }
    virtual bool write(const uint8_t* iq, size_t bytes, uint64_t first_pair,
                       uint64_t generation, const std::atomic_bool& cancel) = 0;
    virtual bool finish() = 0;
    // Called from the FIFO cancellation hot path, even with no incoming IQ.
    // Must be lock-free, nonblocking and perform no I/O. A false result and its
    // immutable lastError must be published with release/acquire ordering.
    virtual bool healthy() const { return true; }
    virtual const char* lastError() const { return ""; }
};

class OneSegReceiveWorker
{
  public:
    struct Source
    {
        std::string archive_path; // Empty selects the live receiver.
        bool loop = false;
        size_t recording_capacity = 0; // Zero disables recording; max32 MiB.
        bool synthetic = false;        // Session-owned teaching child, never this RF worker.
    };
    // Automatic parameter probing can briefly pause the decoder pipe. Reserve
    // about four seconds at 1 MB/s without dropping samples or blocking capture.
    static constexpr uint32_t queue_slots = 2048;
    static constexpr size_t max_block_bytes = 2048;
    enum class Phase : uint8_t
    {
        Idle,
        Starting,
        ReceivingIq,
        Stopping,
        Stopped,
        Error
    };
    enum class Error : uint8_t
    {
        None,
        InvalidChannel,
        Unavailable,
        ReceiveBusy,
        SinkOpen,
        RadioAcquire,
        InvalidBlock,
        QueueFull,
        SinkWrite,
        RadioStream,
        RadioRestore,
        SinkFinish,
        Internal
    };
    struct Snapshot
    {
        Phase phase = Phase::Idle;
        Error error = Error::None;
        unsigned channel = 13;
        uint64_t generation = 0, forwarded_pairs = 0;
        uint32_t queue_high_water = 0;
        uint64_t recorded_bytes = 0;
        bool recording_full = false, recorded_source = false;
        bool lease_restore_failed = false;
        OneSegIqResult radio;
        char message[160]{};
    };
    // The sink and radio must outlive this object. The handoff callback waits
    // for physical packet-receive cleanup, not merely an Idle UI snapshot.
    OneSegReceiveWorker(IPacketRadio& radio, OneSegIqSink& sink, bool (*handoff)());
    ~OneSegReceiveWorker();
    OneSegReceiveWorker(const OneSegReceiveWorker&) = delete;
    OneSegReceiveWorker& operator=(const OneSegReceiveWorker&) = delete;
    bool start(unsigned channel, OneSegReceptionSettings reception = {});
    bool start(unsigned channel, OneSegReceptionSettings reception, Source source);
    // Background caller only, after the generation has stopped. Exclusive save.
    bool exportRecording(const std::string& path, std::string& error);
    void requestStop();
    Snapshot snapshot() const;
    // Worker/shutdown/test only. Never block LVGL while awaiting RF recovery.
    void waitUntilFinished();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Real Linux composition, using the foreground coordinator and physical receive
// handoff barrier. Returns null for simulated modes. No hardware opened here.
std::unique_ptr<OneSegReceiveWorker> make_oneseg_receive_worker(OneSegIqSink& sink);
} // namespace platform::linux_runtime
