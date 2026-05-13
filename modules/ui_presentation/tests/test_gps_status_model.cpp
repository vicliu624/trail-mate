#include "fake/fake_gps_status_source.h"
#include "ui_presentation/gps/gps_status_model.h"

#include <cassert>
#include <cstring>

int main()
{
    ui::tests::FakeGpsStatusSource source;
    source.snapshot_value.header.valid = true;
    source.snapshot_value.header.version = 3;
    source.snapshot_value.receiver_enabled = true;
    source.snapshot_value.receiver_powered = true;
    source.snapshot_value.fix_valid = true;
    source.snapshot_value.latitude = 31.2304;
    source.snapshot_value.longitude = 121.4737;
    source.snapshot_value.altitude_m = 12.5f;
    source.snapshot_value.speed_mps = 1.25f;
    source.snapshot_value.course_deg = 48.0f;
    source.snapshot_value.satellites = 8;
    source.snapshot_value.hdop = 0.9f;
    source.snapshot_value.time_synced = true;
    source.snapshot_value.epoch_seconds = 1700000000ULL;
    ui::copyText(source.snapshot_value.fix_label, "FIX");
    ui::copyText(source.snapshot_value.coordinate_label, "31.23040, 121.47370");
    ui::copyText(source.snapshot_value.satellite_label, "8 SAT");
    ui::copyText(source.snapshot_value.time_label, "SYNCED");

    ui::gps::GpsStatusModel model(source);
    const auto snapshot = model.snapshot();
    assert(snapshot.header.valid);
    assert(snapshot.header.version == 3);
    assert(snapshot.receiver_enabled);
    assert(snapshot.receiver_powered);
    assert(snapshot.fix_valid);
    assert(snapshot.latitude == 31.2304);
    assert(snapshot.longitude == 121.4737);
    assert(snapshot.satellites == 8);
    assert(snapshot.time_synced);
    assert(snapshot.epoch_seconds == 1700000000ULL);
    assert(std::strcmp(snapshot.fix_label.c_str(), "FIX") == 0);
    assert(std::strcmp(snapshot.coordinate_label.c_str(), "31.23040, 121.47370") == 0);
    assert(std::strcmp(snapshot.satellite_label.c_str(), "8 SAT") == 0);
    assert(std::strcmp(snapshot.time_label.c_str(), "SYNCED") == 0);

    source.available = false;
    const auto invalid = model.snapshot();
    assert(!invalid.header.valid);

    return 0;
}
