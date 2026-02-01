/**
 * @file app_context.h
 * @brief Application context (dependency injection)
 */

#pragma once

#include "../chat/domain/chat_model.h"
#include "../chat/usecase/chat_service.h"
#include "../chat/usecase/contact_service.h"
#include "../team/infra/crypto/team_crypto.h"
#include "../team/infra/event/team_event_bus_sink.h"
#include "../team/usecase/team_controller.h"
#include "../team/usecase/team_service.h"

namespace app
{
class AppContext;
}
#include "../chat/infra/contact_store.h"
#include "../chat/infra/meshtastic/node_store.h"
#include "../chat/infra/store/flash_store.h"
#include "../chat/infra/store/log_store.h"
#include "../chat/infra/store/ram_store.h"
#include "../board/BoardBase.h"
#include "../board/LoraBoard.h"
#include "../board/GpsBoard.h"
#include "../board/MotionBoard.h"
#include "../chat/ports/i_chat_store.h"
#include "../chat/ports/i_contact_store.h"
#include "../chat/ports/i_mesh_adapter.h"
#include "../chat/ports/i_node_store.h"
#include "../ui/ui_controller.h"
#include "app_config.h"
#include <memory>

class BoardBase;

namespace app
{

/**
 * @brief Application context
 * Manages all dependencies and provides singleton access
 */
class AppContext
{
  public:
    static AppContext& getInstance()
    {
        static AppContext instance;
        return instance;
    }

    /**
     * @brief Initialize application context
     * @param board Board instance
     * @param use_mock_adapter Use mock adapter instead of real LoRa
     */
    bool init(BoardBase& board, LoraBoard* lora_board = nullptr, GpsBoard* gps_board = nullptr,
              MotionBoard* motion_board = nullptr, bool use_mock_adapter = true, uint32_t disable_hw_init = 0);

    /**
     * @brief Get chat service
     */
    chat::ChatService& getChatService()
    {
        return *chat_service_;
    }

    /**
     * @brief Get contact service
     */
    chat::contacts::ContactService& getContactService()
    {
        return *contact_service_;
    }

    /**
     * @brief Get UI controller
     */
    chat::ui::UiController* getUiController()
    {
        return ui_controller_.get();
    }

    /**
     * @brief Get team controller (UI/state entry points)
     */
    team::TeamController* getTeamController()
    {
        return team_controller_.get();
    }

    /**
     * @brief Get configuration
     */
    AppConfig& getConfig()
    {
        return config_;
    }

    chat::NodeId getSelfNodeId() const
    {
        return mesh_adapter_ ? mesh_adapter_->getNodeId() : 0;
    }

    void saveConfig()
    {
        config_.save(preferences_);
    }

    void applyMeshConfig()
    {
        if (mesh_adapter_)
        {
            mesh_adapter_->applyConfig(config_.mesh_config);
        }
    }

    /**
     * @brief Reset mesh config to defaults and apply
     */
    void resetMeshConfig()
    {
        config_.mesh_config = chat::MeshConfig();
        saveConfig();
        applyMeshConfig();
    }

    /**
     * @brief Clear all stored node info
     */
    void clearNodeDb();

    /**
     * @brief Clear all stored chat messages
     */
    void clearMessageDb();

    /**
     * @brief Update (call from main loop)
     */
    void update();

  private:
    AppContext() = default;
    ~AppContext() = default;
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    // Domain
    std::unique_ptr<chat::ChatModel> chat_model_;

    // Infrastructure
    std::unique_ptr<chat::IChatStore> chat_store_;
    std::unique_ptr<chat::IMeshAdapter> mesh_adapter_;
    std::unique_ptr<chat::meshtastic::NodeStore> node_store_;
    std::unique_ptr<chat::contacts::ContactStore> contact_store_;

    // Use case
    std::unique_ptr<chat::ChatService> chat_service_;
    std::unique_ptr<chat::contacts::ContactService> contact_service_;
    std::unique_ptr<team::infra::TeamCrypto> team_crypto_;
    std::unique_ptr<team::infra::TeamEventBusSink> team_event_sink_;
    std::unique_ptr<team::TeamService> team_service_;
    std::unique_ptr<team::TeamController> team_controller_;

    // UI
    std::unique_ptr<chat::ui::UiController> ui_controller_;

    // Power management

    // Config
    AppConfig config_;
    Preferences preferences_;

    // Board reference for hardware access (haptic feedback, etc.)
    BoardBase* board_ = nullptr;
    LoraBoard* lora_board_ = nullptr;
    GpsBoard* gps_board_ = nullptr;
    MotionBoard* motion_board_ = nullptr;
};

} // namespace app
