#include "fake/fake_mesh_status_source.h"
#include "ui_presentation/mesh/mesh_status_model.h"

#include <cassert>
#include <cstring>

int main()
{
    ui::tests::FakeMeshStatusSource source;
    source.snapshot_value.header.valid = true;
    source.snapshot_value.header.version = 5;
    source.snapshot_value.state = ui::mesh::MeshDisplayState::Ready;
    source.snapshot_value.radio_ready = true;
    source.snapshot_value.has_peer_key = true;
    source.snapshot_value.team_linked = true;
    source.snapshot_value.known_nodes = 4;
    source.snapshot_value.unread_messages = 2;
    source.snapshot_value.last_rssi = -72;
    source.snapshot_value.last_snr = 9;
    ui::copyText(source.snapshot_value.protocol_label, "Meshtastic");
    ui::copyText(source.snapshot_value.radio_label, "LoRa direct path");
    ui::copyText(source.snapshot_value.status_line, "LoRa direct path  |  new activity");

    ui::mesh::MeshStatusModel model(source);
    const auto snapshot = model.snapshot();
    assert(snapshot.header.valid);
    assert(snapshot.header.version == 5);
    assert(snapshot.state == ui::mesh::MeshDisplayState::Ready);
    assert(snapshot.radio_ready);
    assert(snapshot.has_peer_key);
    assert(snapshot.team_linked);
    assert(snapshot.known_nodes == 4);
    assert(snapshot.unread_messages == 2);
    assert(snapshot.last_rssi == -72);
    assert(snapshot.last_snr == 9);
    assert(std::strcmp(snapshot.protocol_label.c_str(), "Meshtastic") == 0);
    assert(std::strcmp(snapshot.radio_label.c_str(), "LoRa direct path") == 0);
    assert(std::strcmp(snapshot.status_line.c_str(), "LoRa direct path  |  new activity") == 0);

    source.available = false;
    const auto invalid = model.snapshot();
    assert(!invalid.header.valid);

    return 0;
}
