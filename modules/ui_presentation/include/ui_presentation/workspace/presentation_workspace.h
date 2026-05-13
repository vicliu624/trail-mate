#pragma once

#include "ui_presentation/chat/chat_workspace_model.h"
#include "ui_presentation/device/device_status_model.h"
#include "ui_presentation/gps/gps_status_model.h"
#include "ui_presentation/map/map_workspace_model.h"
#include "ui_presentation/mesh/mesh_status_model.h"
#include "ui_presentation/settings/settings_model.h"

namespace ui::workspace
{

// PresentationWorkspace is a typed graph of already-constructed presentation
// models. Target composition owns model/source/sink lifetimes.
struct PresentationWorkspace
{
    ui::device::DeviceStatusModel* device = nullptr;
    ui::gps::GpsStatusModel* gps = nullptr;
    ui::mesh::MeshStatusModel* mesh = nullptr;
    ui::settings::SettingsModel* settings = nullptr;
    ui::chat::ChatWorkspaceModel* chat = nullptr;
    ui::chat::ChatWorkspaceModel* team_chat = nullptr;
    ui::map::MapWorkspaceModel* map = nullptr;

    bool hasDevice() const { return device != nullptr; }
    bool hasGps() const { return gps != nullptr; }
    bool hasMesh() const { return mesh != nullptr; }
    bool hasSettings() const { return settings != nullptr; }
    bool hasChat() const { return chat != nullptr; }
    bool hasTeamChat() const { return team_chat != nullptr; }
    bool hasMap() const { return map != nullptr; }
};

inline bool hasCoreStatusModels(const PresentationWorkspace& workspace)
{
    return workspace.device != nullptr &&
           workspace.gps != nullptr &&
           workspace.mesh != nullptr;
}

inline bool hasInteractiveWorkspaceModels(
    const PresentationWorkspace& workspace)
{
    return workspace.settings != nullptr ||
           workspace.chat != nullptr ||
           workspace.team_chat != nullptr ||
           workspace.map != nullptr;
}

} // namespace ui::workspace
