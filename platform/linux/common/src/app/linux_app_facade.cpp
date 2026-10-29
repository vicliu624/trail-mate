#include "app/linux_app_facade.h"

#include <cstring>

#include "app/app_facade_access.h"
#include "platform/linux/geocaching_source.h"
#include "platform/linux/runtime_paths.h"
#include "sys/event_bus.h"
#include "ui/chat_ui_runtime.h"

namespace trailmate::cardputer_zero::linux_ui
{
namespace
{

TeamUiEventDispatcher s_team_ui_event_dispatcher = nullptr;

void copy_bounded(char* out, std::size_t out_len, const char* text)
{
    if (out == nullptr || out_len == 0)
    {
        return;
    }

    if (text == nullptr)
    {
        out[0] = '\0';
        return;
    }

    std::strncpy(out, text, out_len - 1U);
    out[out_len - 1U] = '\0';
}

bool isTeamUiEvent(const ::sys::Event& event)
{
    return event.type == ::sys::EventType::TeamKick ||
           event.type == ::sys::EventType::TeamTransferLeader ||
           event.type == ::sys::EventType::TeamKeyDist ||
           event.type == ::sys::EventType::TeamKeyRequest ||
           event.type == ::sys::EventType::TeamStatus ||
           event.type == ::sys::EventType::TeamPosition ||
           event.type == ::sys::EventType::TeamWaypoint ||
           event.type == ::sys::EventType::TeamTrack ||
           event.type == ::sys::EventType::TeamChat ||
           event.type == ::sys::EventType::TeamPairing ||
           event.type == ::sys::EventType::TeamError ||
           event.type == ::sys::EventType::SystemTick;
}

bool handleFacadeUiEvent(void* context, ::sys::Event* event)
{
    if (context == nullptr || event == nullptr)
    {
        return false;
    }

    auto& facade = *static_cast<MinimalLinuxAppFacade*>(context);

    if (isTeamUiEvent(*event) && s_team_ui_event_dispatcher != nullptr)
    {
        (void)s_team_ui_event_dispatcher(event);
        delete event;
        return true;
    }

    if (::chat::ui::IChatUiRuntime* chat_ui_runtime = facade.getChatUiRuntime())
    {
        chat_ui_runtime->onChatEvent(event);
        return true;
    }

    return false;
}

} // namespace

void setTeamUiEventDispatcher(TeamUiEventDispatcher dispatcher)
{
    s_team_ui_event_dispatcher = dispatcher;
}

MinimalLinuxAppFacade::MinimalLinuxAppFacade()
    : services_(::trailmate::linux_app::LinuxAppServicesOptions{
          .default_node_name = "Cardputer Zero",
          .default_short_name = "CZ",
          .demo_broadcast_text = "Broadcast: Cardputer Zero local mesh online.",
          .ble_supported = false,
      })
{
}

MinimalLinuxAppFacade::~MinimalLinuxAppFacade()
{
    if (geocaching_) shutdown();
}
MinimalLinuxAppFacade::MinimalLinuxAppFacade(::trailmate::linux_app::LinuxAppServicesOptions options)
    : services_(options) {}

bool MinimalLinuxAppFacade::initialize()
{
    services_.setUiEventDispatcher(handleFacadeUiEvent, this);
    if (!services_.initialize())
    {
        return false;
    }
    if (!::app::hasAppFacade())
    {
        geocaching_ = std::make_unique<::platform::linux_runtime::LinuxGeocachingSource>(
            ::platform::linux_runtime::sqlite_database_path(), ::platform::linux_runtime::resolve_paths().sd_root);
        ::app::bindAppFacade(*this);
    }
    return true;
}

void MinimalLinuxAppFacade::shutdown()
{
    if (geocaching_)
    {
        geocaching_.reset();
    }
    if (::app::hasAppFacade())
    {
        ::app::unbindAppFacade();
    }
    services_.setUiEventDispatcher(nullptr, nullptr);
    services_.shutdown();
}

::ui::geocaching::Source* MinimalLinuxAppFacade::geocachingSource() { return geocaching_.get(); }
::platform::linux_runtime::IVoiceSource* MinimalLinuxAppFacade::voiceSource() { return services_.voiceSource(); }

bool MinimalLinuxAppFacade::is_initialized() const noexcept
{
    return services_.isInitialized();
}

const ::app::AppConfig& MinimalLinuxAppFacade::getConfig() const
{
    return services_.getConfig();
}

::app::AppConfigEdit MinimalLinuxAppFacade::beginConfigEdit()
{
    return ::app::AppConfigEdit(&services_.getConfig(),
                                this,
                                &MinimalLinuxAppFacade::commitConfigEdit,
                                &MinimalLinuxAppFacade::cancelConfigEdit);
}

void MinimalLinuxAppFacade::saveConfig()
{
    services_.saveConfig();
}

void MinimalLinuxAppFacade::saveConfig(::app::AppConfigChangeSet changes)
{
    services_.saveConfig(changes);
}

void MinimalLinuxAppFacade::commitConfigEdit(void* context,
                                             ::app::AppConfigChangeSet changes)
{
    auto* self = static_cast<MinimalLinuxAppFacade*>(context);
    if (self)
    {
        self->saveConfig(changes);
    }
}

void MinimalLinuxAppFacade::cancelConfigEdit(void*) {}

void MinimalLinuxAppFacade::applyMeshConfig()
{
    services_.applyMeshConfig();
}

void MinimalLinuxAppFacade::applyUserInfo()
{
    services_.applyUserInfo();
}

void MinimalLinuxAppFacade::applyPositionConfig()
{
    services_.applyPositionConfig();
}

void MinimalLinuxAppFacade::applyNetworkLimits()
{
    services_.applyNetworkLimits();
}

void MinimalLinuxAppFacade::applyPrivacyConfig()
{
    services_.applyPrivacyConfig();
}

void MinimalLinuxAppFacade::applyChatDefaults()
{
    services_.applyChatDefaults();
}

::chat::MeshProtocol MinimalLinuxAppFacade::getMeshProtocol() const
{
    return services_.getMeshProtocol();
}

void MinimalLinuxAppFacade::getEffectiveUserInfo(char* out_long,
                                                 std::size_t long_len,
                                                 char* out_short,
                                                 std::size_t short_len) const
{
    copy_bounded(out_long, long_len, services_.getConfig().node_name);
    copy_bounded(out_short, short_len, services_.getConfig().short_name);
}

bool MinimalLinuxAppFacade::switchMeshProtocol(::chat::MeshProtocol protocol,
                                               bool persist)
{
    return services_.switchMeshProtocol(protocol, persist);
}

::chat::ChatService& MinimalLinuxAppFacade::getChatService()
{
    return services_.getChatService();
}

::chat::contacts::ContactService& MinimalLinuxAppFacade::getContactService()
{
    return services_.getContactService();
}

::chat::IMeshAdapter* MinimalLinuxAppFacade::getMeshAdapter()
{
    return services_.getMeshAdapter();
}

const ::chat::IMeshAdapter* MinimalLinuxAppFacade::getMeshAdapter() const
{
    return services_.getMeshAdapter();
}

::chat::NodeId MinimalLinuxAppFacade::getSelfNodeId() const
{
    return services_.getSelfNodeId();
}

::team::TeamController* MinimalLinuxAppFacade::getTeamController()
{
    return services_.getTeamController();
}

::team::TeamPairingService* MinimalLinuxAppFacade::getTeamPairing()
{
    return services_.getTeamPairing();
}

::team::TeamService* MinimalLinuxAppFacade::getTeamService()
{
    return services_.getTeamService();
}

const ::team::TeamService* MinimalLinuxAppFacade::getTeamService() const
{
    return services_.getTeamService();
}

::team::TeamTrackSampler* MinimalLinuxAppFacade::getTeamTrackSampler()
{
    return services_.getTeamTrackSampler();
}

void MinimalLinuxAppFacade::setTeamModeActive(bool active)
{
    services_.setTeamModeActive(active);
}

void MinimalLinuxAppFacade::broadcastNodeInfo()
{
    services_.broadcastNodeInfo();
}

void MinimalLinuxAppFacade::clearNodeDb()
{
    services_.clearNodeDb();
}

void MinimalLinuxAppFacade::clearMessageDb()
{
    services_.clearMessageDb();
}

::ble::BleManager* MinimalLinuxAppFacade::getBleManager()
{
    return nullptr;
}

const ::ble::BleManager* MinimalLinuxAppFacade::getBleManager() const
{
    return nullptr;
}

bool MinimalLinuxAppFacade::isBleEnabled() const
{
    return services_.isBleEnabled();
}

void MinimalLinuxAppFacade::setBleEnabled(bool enabled)
{
    services_.setBleEnabled(enabled);
}

void MinimalLinuxAppFacade::restartDevice()
{
    services_.restartDevice();
}
void MinimalLinuxAppFacade::setPhonePairingPresenter(std::function<bool(const ::platform::linux_runtime::PhonePairingDisplay&)> presenter)
{
    services_.setPhonePairingPresenter(std::move(presenter));
}

::chat::ui::IChatUiRuntime* MinimalLinuxAppFacade::getChatUiRuntime()
{
    return chat_ui_runtime_;
}

void MinimalLinuxAppFacade::setChatUiRuntime(::chat::ui::IChatUiRuntime* runtime)
{
    chat_ui_runtime_ = runtime;
}

::BoardBase* MinimalLinuxAppFacade::getBoard()
{
    return nullptr;
}

const ::BoardBase* MinimalLinuxAppFacade::getBoard() const
{
    return nullptr;
}

void MinimalLinuxAppFacade::updateCoreServices()
{
    services_.updateCoreServices();
}

void MinimalLinuxAppFacade::tickEventRuntime()
{
    services_.tickEventRuntime();
}

void MinimalLinuxAppFacade::dispatchPendingEvents(std::size_t max_events)
{
    services_.dispatchPendingEvents(max_events);
}

} // namespace trailmate::cardputer_zero::linux_ui
