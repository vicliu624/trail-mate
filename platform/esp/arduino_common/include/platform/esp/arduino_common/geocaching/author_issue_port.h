#pragma once
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/geocaching/sd_author_issue_port.h"

namespace platform::esp::arduino_common::geocaching
{
// Device and host verification share one reservation/signing implementation.
using DeviceAuthorIssuePort = SdAuthorIssuePort<chat::MeshAdapterRouter>;
} // namespace platform::esp::arduino_common::geocaching
