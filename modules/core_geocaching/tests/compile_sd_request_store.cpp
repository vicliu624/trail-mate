#include "platform/esp/arduino_common/geocaching/sd_request_store.h"
static_assert(sizeof(platform::esp::arduino_common::geocaching::SdRequestStore) < 2048,
              "Request store must not regain transaction or Outgoing payload buffers");
