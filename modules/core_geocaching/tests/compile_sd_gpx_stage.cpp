#include "platform/esp/arduino_common/geocaching/sd_gpx_stage.h"

static_assert(sizeof(platform::esp::arduino_common::geocaching::SdGpxStage) < 2048,
              "GPX staging job must remain a bounded worker-owned allocation");
