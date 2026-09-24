#include "ui_map_runtime/map_geo_coordinates.h"

#include <cassert>
#include <limits>

int main()
{
    namespace geo = ui::map_geo;
    const double points[][2] = {{24.8731, 118.0}, {39.9, 116.4}, {31.23, 121.47}, {51.5, -0.12}, {-33.86, 151.2}, {0.0, 0.0}, {20.0, 179.999}, {-20.0, -179.999}, {85.0, 0.0}};
    for (uint8_t system = 0; system <= 2; ++system)
        for (const auto& point : points)
        {
            double lat, lon, restored_lat, restored_lon;
            assert(geo::transform(point[0], point[1], system, lat, lon));
            assert(geo::inverse(lat, geo::wrap_longitude(lon), system, restored_lat, restored_lon));
            assert(std::fabs(restored_lat - point[0]) < 1e-7);
            assert(std::fabs(geo::wrap_longitude(restored_lon - point[1])) < 1e-7);
        }
    // Known forward output pins the existing formula, independently of inverse.
    double lat, lon;
    assert(geo::transform(39.9, 116.4, 1, lat, lon));
    assert(std::fabs(lat - 39.9014035298494) < 1e-10);
    assert(std::fabs(lon - 116.406242784911) < 1e-10);
    assert(geo::transform(51.5, -0.12, 1, lat, lon));
    assert(lat == 51.5 && lon == -0.12);
    lat = 123;
    lon = 456;
    assert(!geo::inverse(std::numeric_limits<double>::quiet_NaN(), 0, 0, lat, lon));
    assert(!geo::inverse(91, 0, 0, lat, lon));
    assert(!geo::inverse(0, 181, 0, lat, lon));
    assert(!geo::inverse(0, 0, 3, lat, lon));
    assert(lat == 123 && lon == 456);
}
