#pragma once

#include "platform/linux/oneseg_iq_stream.h"
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace platform::linux_runtime
{
// A separate control pipe prevents result bytes from being mistaken for IQ.
// Fixed-width little-endian fields avoid native padding or time_t ABI coupling.
using OneSegCaptureReceipt = std::array<uint8_t, 56>;
inline OneSegCaptureReceipt encode_oneseg_capture_result(const OneSegIqResult& result)
{
    OneSegCaptureReceipt wire{};
    const auto put = [&](size_t offset, uint64_t value, size_t bytes)
    {
        for (size_t i = 0; i < bytes; ++i) wire[offset + i] = static_cast<uint8_t>(value >> (8 * i));
    };
    put(0, 0x43514d54, 4);
    put(4, 1, 4);
    put(8, static_cast<uint32_t>(result.capture_error), 4);
    put(12, static_cast<uint32_t>(result.recovery_error), 4);
    put(16, result.nominal_rate_hz, 4);
    put(20, result.max_fifo_level, 4);
    put(24, result.fifo_flags | (unsigned(result.cancelled) << 8) | (unsigned(result.sink_rejected) << 9), 4);
    put(32, result.validated_bytes, 8);
    put(40, result.elapsed_us, 8);
    put(48, result.max_service_gap_us, 8);
    return wire;
}
inline bool decode_oneseg_capture_result(const OneSegCaptureReceipt& wire, OneSegIqResult& result)
{
    const auto get = [&](size_t offset, size_t bytes)
    {
        uint64_t value = 0;
        for (size_t i = 0; i < bytes; ++i) value |= uint64_t(wire[offset + i]) << (8 * i);
        return value;
    };
    if (get(0, 4) != 0x43514d54 || get(4, 4) != 1 || get(28, 4) || (get(24, 4) & ~uint64_t{0x3ff})) return false;
    result = {};
    result.capture_error = static_cast<int32_t>(get(8, 4));
    result.recovery_error = static_cast<int32_t>(get(12, 4));
    result.nominal_rate_hz = static_cast<uint32_t>(get(16, 4));
    result.max_fifo_level = static_cast<uint32_t>(get(20, 4));
    result.fifo_flags = static_cast<uint8_t>(get(24, 4));
    result.cancelled = get(24, 4) & 0x100;
    result.sink_rejected = get(24, 4) & 0x200;
    result.validated_bytes = get(32, 8);
    result.elapsed_us = get(40, 8);
    result.max_service_gap_us = get(48, 8);
    return !(result.validated_bytes & 1);
}

namespace capture_process_detail
{
struct Resources
{
    std::array<int, 5> descriptors{{-1, -1, -1, -1, -1}};
    pid_t child = -1;
    ~Resources()
    {
        if (child > 0)
        {
            (void)kill(-child, SIGKILL);
            int status = 0;
            while (waitpid(child, &status, 0) < 0 && errno == EINTR)
            {
            }
        }
        for (int fd : descriptors)
            if (fd >= 0) close(fd);
    }
    void closeAt(size_t index)
    {
        if (descriptors[index] >= 0) close(descriptors[index]);
        descriptors[index] = -1;
    }
};
} // namespace capture_process_detail

// The caller retains its coordinator lease but releases physical transport
// ownership before invoking this. The helper receives profile/channel options,
// raw IQ stdout, and a final receipt on fd3. It must exec, not fork into GUI C++ state.
inline bool run_oneseg_capture_process(const char* executable, const char* profile, const OneSegIqRequest& request,
                                       uint32_t sample_rate, OneSegIqResult& result)
{
    constexpr int io_error = -5;
    result = {};
    if (!executable || executable[0] != '/' || !profile || profile[0] != '/' || !request.staging || request.staging_bytes < 2 ||
        !request.accept || !request.cancelled)
    {
        result.capture_error = io_error;
        return false;
    }
    if (request.cancelled(request.context))
    {
        result.cancelled = true;
        return true;
    }
    capture_process_detail::Resources resources;
    int data[2], control[2];
    if (pipe2(data, O_CLOEXEC))
    {
        result.capture_error = io_error;
        return false;
    }
    resources.descriptors[0] = data[0];
    resources.descriptors[1] = data[1];
    if (pipe2(control, O_CLOEXEC))
    {
        result.capture_error = io_error;
        return false;
    }
    resources.descriptors[2] = control[0];
    resources.descriptors[3] = control[1];
    // Keep dup sources clear of fixed child fds, regardless of parent fd layout.
    for (size_t index : {size_t{1}, size_t{3}})
    {
        const int duplicate = fcntl(resources.descriptors[index], F_DUPFD_CLOEXEC, 10);
        if (duplicate < 0)
        {
            result.capture_error = io_error;
            return false;
        }
        resources.closeAt(index);
        resources.descriptors[index] = duplicate;
    }
    if (fcntl(data[0], F_SETFL, O_NONBLOCK) || fcntl(control[0], F_SETFL, O_NONBLOCK))
    {
        result.capture_error = io_error;
        return false;
    }
    // A privileged prototype may deliberately supply a register capability.
    // Duplicate it only for the capture helper, never the decoder child.
    const char* memory = std::getenv("CM0_MMIO_FD");
    if (memory && !std::strcmp(memory, "9") && fcntl(9, F_GETFD) >= 0)
    {
        resources.descriptors[4] = fcntl(9, F_DUPFD_CLOEXEC, 10);
        if (resources.descriptors[4] < 0)
        {
            result.capture_error = io_error;
            return false;
        }
    }
    std::vector<char*> environment;
    for (char** entry = ::environ; *entry; ++entry)
        if (std::strncmp(*entry, "TRAIL_MATE_ONESEG_CAPTURE_CHILD=", sizeof("TRAIL_MATE_ONESEG_CAPTURE_CHILD=") - 1)) environment.push_back(*entry);
    char marker[] = "TRAIL_MATE_ONESEG_CAPTURE_CHILD=1";
    environment.push_back(marker);
    environment.push_back(nullptr);
    char channel[16];
    std::snprintf(channel, sizeof(channel), "%u", request.channel);
    char profile_option[] = "--profile", channel_option[] = "--channel", result_option[] = "--capture-result-fd", result_fd[] = "3";
    char* arguments[] = {const_cast<char*>(executable), profile_option, const_cast<char*>(profile),
                         channel_option, channel, result_option, result_fd, nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int error = posix_spawn_file_actions_init(&actions);
    if (error)
    {
        result.capture_error = io_error;
        return false;
    }
    error = posix_spawnattr_init(&attributes);
    if (error)
    {
        posix_spawn_file_actions_destroy(&actions);
        result.capture_error = io_error;
        return false;
    }
    sigset_t empty, defaults;
    sigemptyset(&empty);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    const auto check = [&](int code)
    { if (!error) error = code; };
    check(posix_spawnattr_setsigmask(&attributes, &empty));
    check(posix_spawnattr_setsigdefault(&attributes, &defaults));
    check(posix_spawnattr_setpgroup(&attributes, 0));
    check(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP));
    check(posix_spawn_file_actions_addclose(&actions, data[0]));
    check(posix_spawn_file_actions_addclose(&actions, control[0]));
    check(posix_spawn_file_actions_adddup2(&actions, resources.descriptors[1], STDOUT_FILENO));
    check(posix_spawn_file_actions_adddup2(&actions, resources.descriptors[3], 3));
    check(posix_spawn_file_actions_addclose(&actions, resources.descriptors[1]));
    check(posix_spawn_file_actions_addclose(&actions, resources.descriptors[3]));
    if (resources.descriptors[4] >= 0)
    {
        check(posix_spawn_file_actions_adddup2(&actions, resources.descriptors[4], 9));
        check(posix_spawn_file_actions_addclose(&actions, resources.descriptors[4]));
    }
    if (!error) error = posix_spawn(&resources.child, executable, &actions, &attributes, arguments, environment.data());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    resources.closeAt(1);
    resources.closeAt(3);
    resources.closeAt(4);
    if (error)
    {
        resources.child = -1;
        result.capture_error = io_error;
        return false;
    }
    std::fprintf(stderr, "[oneseg_capture_process] child=%ld channel=%u\n", static_cast<long>(resources.child), request.channel);
    using Clock = std::chrono::steady_clock;
    auto progress = Clock::now(), stop_at = Clock::time_point::max(), drain_at = Clock::time_point::max();
    bool stopping = false, killed = false, exited = false, cancelled = false, failed = false, rejected = false;
    int status = 0;
    size_t pending = 0, receipt_bytes = 0;
    uint64_t published = 0;
    OneSegCaptureReceipt receipt{};
    const pid_t child = resources.child;
    while (!exited || resources.descriptors[0] >= 0 || resources.descriptors[2] >= 0)
    {
        const auto now = Clock::now();
        cancelled = cancelled || request.cancelled(request.context);
        if (!exited && now - progress > std::chrono::seconds(5)) failed = true;
        if ((cancelled || failed || rejected) && !stopping && !exited)
        {
            (void)kill(-child, SIGTERM);
            stopping = true;
            stop_at = now + std::chrono::seconds(2);
        }
        if (stopping && !exited && !killed && now >= stop_at)
        {
            (void)kill(-child, SIGKILL);
            killed = true;
        }
        if (!exited)
        {
            const pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child || (waited < 0 && errno != EINTR))
            {
                failed = failed || waited < 0;
                exited = true;
                resources.child = -1;
                drain_at = now + std::chrono::milliseconds(250);
            }
        }
        if (exited && now >= drain_at)
        {
            failed = failed || resources.descriptors[0] >= 0 || resources.descriptors[2] >= 0;
            if (failed) (void)kill(-child, SIGKILL);
            break;
        }
        pollfd descriptors[] = {{resources.descriptors[0], POLLIN | POLLHUP, 0},
                                {resources.descriptors[2], POLLIN | POLLHUP, 0}};
        const int ready = poll(descriptors, 2, 25);
        if (ready < 0 && errno != EINTR) failed = true;
        if (descriptors[0].revents)
        {
            const ssize_t count = read(resources.descriptors[0], request.staging + pending, request.staging_bytes - pending);
            if (count > 0)
            {
                progress = Clock::now();
                const size_t available = pending + static_cast<size_t>(count), even = available & ~size_t{1};
                const uint8_t tail = available != even ? request.staging[even] : 0;
                if (even && !cancelled && !failed && !rejected)
                {
                    if (request.accept(request.context, request.staging, even, published / 2)) published += even;
                    else if (!request.cancelled(request.context)) rejected = true;
                    else cancelled = true;
                }
                pending = available - even;
                if (pending) request.staging[0] = tail;
            }
            else if (!count) resources.closeAt(0);
            else if (errno != EAGAIN && errno != EINTR)
            {
                resources.closeAt(0);
                failed = true;
            }
        }
        if (descriptors[1].revents)
        {
            uint8_t bytes[64];
            const ssize_t count = read(resources.descriptors[2], bytes, sizeof(bytes));
            if (count > 0)
            {
                if (receipt_bytes + static_cast<size_t>(count) > receipt.size()) failed = true;
                else std::memcpy(receipt.data() + receipt_bytes, bytes, static_cast<size_t>(count));
                receipt_bytes += static_cast<size_t>(count);
            }
            else if (!count) resources.closeAt(2);
            else if (errno != EAGAIN && errno != EINTR)
            {
                resources.closeAt(2);
                failed = true;
            }
        }
    }
    const bool received = receipt_bytes == receipt.size() && decode_oneseg_capture_result(receipt, result);
    if (!received || pending || (!cancelled && (!WIFEXITED(status) || WEXITSTATUS(status))) ||
        (received && ((!cancelled && result.nominal_rate_hz != sample_rate) ||
                      (result.nominal_rate_hz && result.nominal_rate_hz != sample_rate) ||
                      (!cancelled && !rejected && result.validated_bytes != published)))) failed = true;
    // A cancellation during exec/startup may terminate before the receipt exists.
    if (cancelled && !received && !published && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM && !killed) failed = false;
    result.validated_bytes = published;
    result.cancelled = result.cancelled || cancelled;
    result.sink_rejected = result.sink_rejected || rejected;
    if (rejected && !result.capture_error) result.capture_error = -4;
    if ((failed || killed) && !result.capture_error) result.capture_error = io_error;
    return !result.capture_error && !result.recovery_error;
}
} // namespace platform::linux_runtime
