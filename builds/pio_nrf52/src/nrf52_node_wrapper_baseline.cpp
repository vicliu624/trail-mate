#include "nrf52_node_app_shell.h"
#include "nrf52_pio_legacy_implementation_adapter.h"

namespace
{

bool g_wrapper_valid = false;

} // namespace

extern "C" void setup()
{
    trailmate::apps::nrf52_node::Nrf52NodeAppShell shell;
    trailmate::apps::nrf52_node::Nrf52PioLegacyImplementationDescriptor adapter{};
    g_wrapper_valid = shell.validate() &&
                      adapter.implementation_root != nullptr &&
                      adapter.board_specific_root != nullptr &&
                      adapter.app_shell != nullptr &&
                      adapter.build_wrapper != nullptr;
}

extern "C" void loop()
{
    (void)g_wrapper_valid;
}
