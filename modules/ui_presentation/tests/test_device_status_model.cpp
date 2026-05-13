#include "fake/fake_device_status_source.h"
#include "ui_presentation/device/device_status_model.h"

#include <cassert>
#include <cstring>

int main()
{
    ui::tests::FakeDeviceStatusSource source;
    source.snapshot_value.header.valid = true;
    source.snapshot_value.header.version = 7;
    source.snapshot_value.lora = ui::device::CapabilityDisplayState::Ready;
    source.snapshot_value.gps = ui::device::CapabilityDisplayState::Degraded;
    source.snapshot_value.time_synced = true;
    source.snapshot_value.epoch_seconds = 1700000000ULL;
    ui::copyText(source.snapshot_value.active_protocol, "Meshtastic");
    ui::copyText(source.snapshot_value.region, "915.000MHz");
    ui::copyText(source.snapshot_value.modem_preset, "BW125k SF11 CR5");
    ui::copyText(source.snapshot_value.status_line, "Meshtastic  915.000MHz");

    ui::device::DeviceStatusModel model(source);
    auto snapshot = model.snapshot();
    assert(snapshot.header.valid);
    assert(snapshot.header.version == 7);
    assert(snapshot.lora == ui::device::CapabilityDisplayState::Ready);
    assert(snapshot.gps == ui::device::CapabilityDisplayState::Degraded);
    assert(snapshot.time_synced);
    assert(snapshot.epoch_seconds == 1700000000ULL);
    assert(std::strcmp(snapshot.active_protocol.c_str(), "Meshtastic") == 0);
    assert(std::strcmp(snapshot.region.c_str(), "915.000MHz") == 0);
    assert(std::strcmp(snapshot.modem_preset.c_str(), "BW125k SF11 CR5") == 0);
    assert(std::strcmp(snapshot.status_line.c_str(), "Meshtastic  915.000MHz") == 0);

    source.available = false;
    auto invalid = model.snapshot();
    assert(!invalid.header.valid);

    return 0;
}
