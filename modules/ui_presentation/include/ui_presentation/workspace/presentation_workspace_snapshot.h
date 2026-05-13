#pragma once

#include "ui_presentation/common/snapshot_header.h"
#include "ui_presentation/device/device_status_snapshot.h"
#include "ui_presentation/gps/gps_status_snapshot.h"
#include "ui_presentation/map/map_workspace_snapshot.h"
#include "ui_presentation/mesh/mesh_status_snapshot.h"

namespace ui::workspace
{

struct PresentationWorkspaceSnapshot
{
    ui::SnapshotHeader header;

    bool has_device = false;
    bool has_gps = false;
    bool has_mesh = false;
    bool has_settings = false;
    bool has_chat = false;
    bool has_team_chat = false;
    bool has_map = false;

    ui::device::DeviceStatusSnapshot device;
    ui::gps::GpsStatusSnapshot gps;
    ui::mesh::MeshStatusSnapshot mesh;
    ui::map::MapWorkspaceSnapshot map;
};

} // namespace ui::workspace
