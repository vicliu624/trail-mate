#pragma once
namespace platform::linux_runtime
{
// Application-owner shutdown: cancel native transfers and join their worker.
void stop_route_image_tasks();
}
