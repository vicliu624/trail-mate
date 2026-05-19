#include "nrf52_historical_source_descriptor.h"
#include "nrf52_node_app_shell.h"

namespace
{

bool g_wrapper_valid = false;

} // namespace

extern "C" void setup()
{
    trailmate::apps::nrf52_node::Nrf52NodeAppShell shell;
    const auto& historical_source =
        trailmate::apps::nrf52_node::nrf52HistoricalSourceDescriptor();
    g_wrapper_valid = shell.validate() &&
                      historical_source.historical_generic_root_name != nullptr &&
                      historical_source.historical_board_root_name != nullptr &&
                      historical_source.historical_role != nullptr &&
                      historical_source.replacement_owner != nullptr;
}

extern "C" void loop()
{
    (void)g_wrapper_valid;
}
