#include "platform/esp/arduino_common/geocaching/sd_journal.h"

static_assert(sizeof(platform::esp::arduino_common::geocaching::SdGeocachingJournal) <= 768,
              "Journal owner must have bounded storage");
