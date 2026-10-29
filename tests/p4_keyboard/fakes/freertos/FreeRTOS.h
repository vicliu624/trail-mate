#pragma once
// Match the P4 IDF configuration: a short millisecond timeout can truncate to
// zero ticks, so the bus boundary must not assume milliseconds equal ticks.
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 100
#endif
#define pdMS_TO_TICKS(value) ((value)*configTICK_RATE_HZ / 1000)
