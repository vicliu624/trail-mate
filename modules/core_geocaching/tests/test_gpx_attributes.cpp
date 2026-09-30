#include "gps/gpx/attribute_reader.h"
#include <iostream>

int main()
{
    double value = 42;
    const auto reject = [&value](const char* text)
    {
        value = 42;
        return !gps::gpx::readDoubleAttribute(text, "lat", value) && value == 42;
    };
    if (!gps::gpx::readDoubleAttribute("<trkpt lat=\"12.5\" lon=\"3\">", "lat", value) || value != 12.5 ||
        !gps::gpx::readDoubleAttribute("<wpt lon='3' lat = '-12.25' />", "lat", value) || value != -12.25 ||
        !reject("<trkpt otherlat='1'>") || !reject("<trkpt lat='1junk'>") ||
        !reject("<trkpt lat='nan'>") || !reject("<trkpt lat='inf'>") ||
        !reject("<trkpt lat='1' lat='2'>") || !reject("<trkpt lat='1'") ||
        !reject("<trkpt lat='1'lon='2'>") || !reject("<!-- lat='1' -->"))
    {
        std::cerr << "GPX attribute regression\n";
        return 1;
    }
    return 0;
}
