#include "platform/ui/hostlink_runtime.h"

#include "app/app_config.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "platform/esp/arduino_common/rnode_kiss/rnode_kiss_service.h"

#include "app/app_facade_access.h"

namespace platform::ui::hostlink
{
namespace
{

bool use_rnode_bridge()
{
    return app::appFacade().getConfig().mesh_protocol == chat::MeshProtocol::RNode;
}

} // namespace

bool is_supported()
{
    return true;
}

void start()
{
    if (use_rnode_bridge())
    {
        ::rnode_kiss::start();
        return;
    }
    ::hostlink::start();
}

void stop()
{
    if (use_rnode_bridge())
    {
        ::rnode_kiss::stop();
        return;
    }
    ::hostlink::stop();
}

bool is_active()
{
    if (use_rnode_bridge())
    {
        return ::rnode_kiss::is_active();
    }
    return ::hostlink::is_active();
}

Status get_status()
{
    if (use_rnode_bridge())
    {
        return ::rnode_kiss::get_status();
    }
    return ::hostlink::get_status();
}

} // namespace platform::ui::hostlink
