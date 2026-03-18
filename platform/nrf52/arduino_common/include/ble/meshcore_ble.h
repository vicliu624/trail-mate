#pragma once

#include "app/app_facades.h"
#include "ble_manager.h"
#include "chat/domain/chat_types.h"
#include "chat/ports/i_node_store.h"
#include "chat/usecase/chat_service.h"

#include <bluefruit.h>
#include <deque>
#include <array>
#include <string>
#include <vector>

namespace ble
{

class MeshCoreBleService final : public BleService,
                                 public chat::ChatService::IncomingTextObserver
{
  public:
    MeshCoreBleService(app::IAppBleFacade& ctx, const std::string& device_name);
    ~MeshCoreBleService() override;

    void start() override;
    void stop() override;
    void update() override;
    void onIncomingText(const chat::MeshIncomingText& msg) override;

    bool handleRxFrame(const uint8_t* data, size_t len);
    bool popTxFrame(uint8_t* out, size_t* out_len);

  private:
    struct Frame
    {
        size_t len = 0;
        std::array<uint8_t, 172> buf{};
    };

    void pumpIncomingAppData();
    void handleCmdFrame(const uint8_t* data, size_t len);
    void enqueueFrame(const uint8_t* data, size_t len);
    void enqueueSentOk();
    void enqueueErr(uint8_t err);
    void sendPendingNotifications();
    void enqueueRawDataPush(const chat::MeshIncomingData& msg);
    uint32_t resolveNodeIdFromPrefix(const uint8_t* prefix, size_t len) const;
    bool buildContactFromNode(const chat::contacts::NodeEntry& entry, uint8_t code, Frame& out) const;

    app::IAppBleFacade& ctx_;
    std::string device_name_;
    ::BLEService service_;
    ::BLECharacteristic rx_char_;
    ::BLECharacteristic tx_char_;
    bool active_ = false;
    std::deque<Frame> tx_queue_;
    std::vector<uint8_t> sign_data_;
    bool sign_active_ = false;
    uint32_t stats_rx_packets_ = 0;
    uint32_t stats_tx_packets_ = 0;
    uint32_t stats_tx_flood_ = 0;
    uint32_t stats_tx_direct_ = 0;
    uint32_t stats_rx_flood_ = 0;
    uint32_t stats_rx_direct_ = 0;
};

} // namespace ble
