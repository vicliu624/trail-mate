#include "app/linux_app_services.h"
#include "chat/usecase/chat_service.h"
#include "platform/linux/native_audio_service.h"
#include "sys/event_bus.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-result-dispatch-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    setenv("TRAIL_MATE_DESKTOP_NOTIFICATIONS", "0", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    trailmate::linux_app::LinuxAppServicesOptions options;
    options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::SimulatorDemo;
    chat::MessageId message;
    {
        trailmate::linux_app::LinuxAppServices services(options);
        assert(services.initialize());
        assert(services.switchMeshProtocol(chat::MeshProtocol::Meshtastic, false));
        const auto result = services.getChatService().sendTextDetailed(chat::ChannelId::PRIMARY, "background result");
        assert(result.ok && result.msg_id);
        message = result.msg_id;
        services.dispatchPendingEvents(256);
        assert(sys::EventBus::publish(new sys::ChatSendResultEvent(message, chat::MessageStatus::Delivered,
                                                                   chat::MeshProtocol::Meshtastic),
                                      0));
        assert(services.switchMeshProtocol(chat::MeshProtocol::MeshCore, false));
        services.dispatchPendingEvents(256);
        const auto* saved = services.getChatService().getMessageForProtocol(message, chat::MeshProtocol::Meshtastic);
        assert(saved && saved->status == chat::MessageStatus::Delivered);
        assert(sys::EventBus::publish(new sys::ChatSendResultEvent(message, chat::MessageStatus::Sent,
                                                                   chat::MeshProtocol::Meshtastic),
                                      0));
        services.dispatchPendingEvents(256);
        saved = services.getChatService().getMessageForProtocol(message, chat::MeshProtocol::Meshtastic);
        assert(saved && saved->status == chat::MessageStatus::Delivered);
        services.shutdown();
    }
    {
        trailmate::linux_app::LinuxAppServices services(options);
        assert(services.initialize());
        const auto* saved = services.getChatService().getMessageForProtocol(message, chat::MeshProtocol::Meshtastic);
        assert(saved && saved->status == chat::MessageStatus::Delivered);
        services.shutdown();
    }
    platform::linux_runtime::shutdown_native_audio();
    std::filesystem::remove_all(root);
}
