#pragma once

#include <cstdint>

namespace device
{

enum class RuntimeSchedulerKind : std::uint8_t
{
    None,
    FreeRtos,
    ZephyrWorkQueue,
    LinuxEventLoop,
    SingleThreadTick,
    TestFake,
};

enum class QueuePolicyKind : std::uint8_t
{
    None,
    EventQueue,
    CommandQueue,
    Snapshot,
    DirectCallForbidden,
};

struct RuntimePolicy
{
    RuntimeSchedulerKind scheduler = RuntimeSchedulerKind::None;
    QueuePolicyKind app_to_ui = QueuePolicyKind::Snapshot;
    QueuePolicyKind radio_to_app = QueuePolicyKind::EventQueue;
    QueuePolicyKind gps_to_app = QueuePolicyKind::EventQueue;
    QueuePolicyKind ble_to_app = QueuePolicyKind::CommandQueue;
    bool isr_defer_only = true;
    bool ui_thread_only = true;
};

} // namespace device
