#include "ui_presentation/agenda/agenda_countdown.h"
#include <cassert>
#include <cstring>
int main()
{
    char text[32];
    auto check = [&](int64_t start, int64_t now, bool valid, const char* expected)
    {
        ui::agenda::formatCountdown(start, now, valid, text, sizeof(text));
        assert(std::strcmp(text, expected) == 0);
    };
    check(1, 0, true, "T-1m");
    check(60, 0, true, "T-1m");
    check(61, 0, true, "T-2m");
    check(7800, 0, true, "T-2h10m");
    check(90000, 0, true, "T-1d1h");
    check(0, 0, true, "T+0m");
    check(0, 61, true, "T+1m");
    check(0, 90000, true, "T+1d1h");
    check(100, 0, false, "--");
}
