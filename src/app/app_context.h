/**
 * @file app_context.h
 * @brief Application context (dependency injection)
 */

#pragma once

#include "../chat/domain/chat_model.h"
#include "../chat/usecase/chat_service.h"
#include "../chat/usecase/contact_service.h"


namespace app {
class AppContext;
}
#include "../chat/ports/i_mesh_adapter.h"
#include "../chat/ports/i_chat_store.h"
#include "../chat/ports/i_node_store.h"
#include "../chat/ports/i_contact_store.h"
#include "../chat/infra/store/ram_store.h"
#include "../chat/infra/store/log_store.h"
#include "../chat/infra/store/flash_store.h"
#include "../chat/infra/mock_mesh_adapter.h"
#include "../chat/infra/meshtastic/mt_adapter.h"
#include "../chat/infra/meshtastic/node_store.h"
#include "../chat/infra/contact_store.h"
#include "../ui/ui_controller.h"
#include "app_config.h"
#include "../board/TLoRaPagerBoard.h"
#include <memory>

namespace app {

/**
 * @brief Application context
 * Manages all dependencies and provides singleton access
 */
class AppContext {
public:
    static AppContext& getInstance() {
        static AppContext instance;
        return instance;
    }
    
    /**
     * @brief Initialize application context
     * @param board Board instance
     * @param use_mock_adapter Use mock adapter instead of real LoRa
     */
    bool init(TLoRaPagerBoard& board, bool use_mock_adapter = true, uint32_t disable_hw_init = 0);
    
    /**
     * @brief Get chat service
     */
    chat::ChatService& getChatService() {
        return *chat_service_;
    }
    
    /**
     * @brief Get contact service
     */
    chat::contacts::ContactService& getContactService() {
        return *contact_service_;
    }
    
    /**
     * @brief Get UI controller
     */
    chat::ui::UiController* getUiController() {
        return ui_controller_.get();
    }
    
    /**
     * @brief Get configuration
     */
    AppConfig& getConfig() {
        return config_;
    }

    void saveConfig() {
        config_.save(preferences_);
    }
    
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
    chat::FlashStore* flash_store_ = nullptr;
    std::unique_ptr<chat::IMeshAdapter> mesh_adapter_;
    std::unique_ptr<chat::meshtastic::NodeStore> node_store_;
    std::unique_ptr<chat::contacts::ContactStore> contact_store_;
    
    // Use case
    std::unique_ptr<chat::ChatService> chat_service_;
    std::unique_ptr<chat::contacts::ContactService> contact_service_;
    
    // UI
    std::unique_ptr<chat::ui::UiController> ui_controller_;

    // Power management

    // Config
    AppConfig config_;
    Preferences preferences_;
    
    // Board reference for hardware access (haptic feedback, etc.)
    TLoRaPagerBoard* board_ = nullptr;
};

} // namespace app
