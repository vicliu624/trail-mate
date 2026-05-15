#include "apps/gat562_mesh_evb_pro/app_runtime_access.h"

#include <Arduino.h>
#include <cstdlib>

#include "app/app_facade_access.h"
#include "apps/gat562_mesh_evb_pro/app_facade_runtime.h"
#include "apps/gat562_mesh_evb_pro/debug_console.h"
#include "apps/gat562_mesh_evb_pro/ui_runtime.h"
#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "chat/ports/i_mesh_adapter.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"

namespace apps::gat562_mesh_evb_pro::app_runtime_access
{
namespace
{

Status s_status{};
uint32_t s_rx_packet_count = 0;
uint32_t s_last_rx_log_ms = 0;

int decimalDigit(int value)
{
    return value < 0 ? -value : value;
}

} // namespace

bool initialize()
{
    if (s_status.initialized)
    {
        return s_status.app_facade_bound;
    }

    s_status = Status{};
    s_status.initialized = true;

    AppFacadeRuntime& runtime = AppFacadeRuntime::instance();
    s_status.app_facade_bound = runtime.initialize() && app::hasAppFacade();
    if (!s_status.app_facade_bound)
    {
        debug_console::println("[gat562] app runtime init failed");
    }
    return s_status.app_facade_bound;
}

void tick()
{
    auto& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
    board.tickGps();
    ::boards::gat562_mesh_evb_pro::BoardInputEvent input_event{};
    (void)board.pollInputEvent(&input_event);
    AppFacadeRuntime& runtime = AppFacadeRuntime::instance();
    if (chat::IMeshAdapter* adapter = runtime.getMeshAdapter())
    {
        adapter->processSendQueue();

        platform::nrf52::arduino_common::chat::infra::RadioPacket packet{};
        auto* io = platform::nrf52::arduino_common::chat::infra::radioPacketIo();
        while (io && io->pollReceive(&packet))
        {
            ++s_rx_packet_count;
            adapter->setLastRxStats(packet.rx_meta.rssi_dbm_x10 / 10.0f,
                                    packet.rx_meta.snr_db_x10 / 10.0f);
            const uint32_t now_ms = millis();
            if (s_rx_packet_count <= 4 || (now_ms - s_last_rx_log_ms) >= 2000U)
            {
                s_last_rx_log_ms = now_ms;
                debug_console::printf("[gat562] rx raw #%lu len=%u rssi=%d.%01d snr=%d.%01d\n",
                                      static_cast<unsigned long>(s_rx_packet_count),
                                      static_cast<unsigned>(packet.size),
                                      packet.rx_meta.rssi_dbm_x10 / 10,
                                      decimalDigit(packet.rx_meta.rssi_dbm_x10 % 10),
                                      packet.rx_meta.snr_db_x10 / 10,
                                      decimalDigit(packet.rx_meta.snr_db_x10 % 10));
            }
            adapter->handleRawPacket(packet.data, packet.size);
        }
    }

    runtime.updateCoreServices();
    runtime.tickEventRuntime();
    runtime.dispatchPendingEvents();
    ui_runtime::tick(&input_event);
}

const Status& status()
{
    return s_status;
}

} // namespace apps::gat562_mesh_evb_pro::app_runtime_access
