#include "platform/esp/idf_common/app_runtime_support.h"

#include "app/app_facade_access.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"

namespace platform::esp::idf_common
{

void tickBoundLifecycle(std::size_t max_events)
{
    ::hostlink::process_pending_commands();

    app::IAppLifecycleFacade& lifecycle = app::lifecycleFacade();
    lifecycle.updateCoreServices();
    lifecycle.tickEventRuntime();
    lifecycle.dispatchPendingEvents(max_events);
}

} // namespace platform::esp::idf_common
