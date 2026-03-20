#include "apps/gat562_mesh_evb_pro/startup_runtime.h"

#include <Arduino.h>

#include "apps/gat562_mesh_evb_pro/app_facade_runtime.h"
#include "apps/gat562_mesh_evb_pro/app_runtime_access.h"
#include "apps/gat562_mesh_evb_pro/debug_console.h"
#include "apps/gat562_mesh_evb_pro/ui_runtime.h"
#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "sys/clock.h"

namespace apps::gat562_mesh_evb_pro::startup_runtime
{

void run()
{
    debug_console::begin();
    debug_console::println();
    debug_console::println("[gat562] startup begin");
    auto& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
    (void)board.begin();
    sys::set_millis_provider([]() -> uint32_t
                             { return millis(); });
    sys::set_epoch_seconds_provider([]() -> uint32_t
                                    { return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().currentEpochSeconds(); });
    ui_runtime::initialize();
    ui_runtime::appendBootLog("startup begin");
    ui_runtime::appendBootLog("board/input ok");

    (void)board.bindRadioIo();
    const bool lora_ok = board.beginRadioIo();
    debug_console::println(lora_ok ? "[gat562] startup lora io ok" : "[gat562] startup lora io failed");
    ui_runtime::appendBootLog(lora_ok ? "lora io ok" : "lora io fail");

    if (app_runtime_access::initialize())
    {
        auto& cfg = AppFacadeRuntime::instance().getConfig();
        ui_runtime::bindChatObservers();
        (void)board.startGpsRuntime(cfg);
        debug_console::println("[gat562] startup app facade ok");
        ui_runtime::appendBootLog("app/gps ok");
        char freq_text[20] = {};
        if (board.formatLoraFrequencyMHz(board.activeLoraFrequencyHz(), freq_text, sizeof(freq_text)))
        {
            ui_runtime::appendBootLog(freq_text);
        }
        ui_runtime::appendBootLog(cfg.mesh_protocol == chat::MeshProtocol::MeshCore ? "proto MC" : "proto MT");
    }
    else
    {
        debug_console::println("[gat562] startup app facade failed");
        ui_runtime::appendBootLog("app init fail");
    }
}

} // namespace apps::gat562_mesh_evb_pro::startup_runtime
