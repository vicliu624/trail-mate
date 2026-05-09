#include "platform/ui/lora_runtime.h"

#include <limits>

#include "app/app_facade_access.h"
#include "board/LoraBoard.h"
#include "platform/esp/arduino_common/exclusive_lora_runtime.h"

namespace platform::ui::lora
{

namespace
{

using Session = ::platform::esp::arduino_common::exclusive_lora_runtime::Session;

Session s_session{};
LoraBoard* s_lora = nullptr;

} // namespace

bool is_supported()
{
    return true;
}

bool acquire()
{
    if (s_lora)
    {
        return true;
    }
    s_session = ::platform::esp::arduino_common::exclusive_lora_runtime::acquire();
    s_lora = s_session.lora;
    return s_lora != nullptr;
}

bool is_online()
{
    return s_lora && s_lora->isRadioOnline();
}

bool configure_receive(float freq_mhz, const ReceiveConfig& config)
{
    if (!s_lora)
    {
        return false;
    }

    s_lora->configureLoraRadio(freq_mhz,
                               config.bw_khz,
                               config.sf,
                               config.cr,
                               config.tx_power,
                               config.preamble_len,
                               config.sync_word,
                               config.crc_len);
    s_lora->startRadioReceive();
    return true;
}

float read_instant_rssi()
{
    return s_lora ? s_lora->getRadioInstantRSSI() : std::numeric_limits<float>::quiet_NaN();
}

void release()
{
    if (!s_session.lora)
    {
        s_lora = nullptr;
        return;
    }

    app::configFacade().applyMeshConfig();
    ::platform::esp::arduino_common::exclusive_lora_runtime::release(&s_session);
    s_lora = nullptr;
}

} // namespace platform::ui::lora
