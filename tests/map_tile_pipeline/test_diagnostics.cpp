#include "ui/widgets/map/map_diagnostics.h"
#include <cassert>

#if TRAIL_MATE_MAP_DIAGNOSTICS
#include <Arduino.h>
#include <string>
#endif

int main()
{
    int calls = 0;
    MAP_DIAG("request=%d result=%s\n", ++calls, "ready");
#if TRAIL_MATE_MAP_DIAGNOSTICS
    assert(calls == 1);
    assert(Serial.output == "request=1 result=ready\n");
    Serial.output.clear();
    const std::string long_record(1024, 'x');
    MAP_DIAG("%s", long_record.c_str());
    assert(Serial.output == long_record);
#else
    assert(calls == 0);
#endif
}
