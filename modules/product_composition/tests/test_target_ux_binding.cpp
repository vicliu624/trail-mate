#include "product_composition/target_ux_binding.h"

#include <cassert>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const auto* bindings = product_composition::allTargetUxBindings(&count);
    assert(bindings != nullptr);
    assert(count == 12);

    const auto* tab5 = product_composition::findTargetUxBinding("tab5");
    assert(tab5 != nullptr);
    assert(std::strcmp(tab5->desired_ux_pack_id, "tab5_touch") == 0);
    assert(std::strcmp(tab5->active_ux_pack_id, "compatibility") == 0);
    assert(!tab5->final_ux_pack_available);

    const auto* tft = product_composition::findTargetUxBinding("t_display_p4_tft");
    assert(tft != nullptr);
    assert(std::strcmp(tft->desired_ux_pack_id, "t_display_p4_touch") == 0);
    assert(std::strcmp(tft->active_ux_pack_id, "t_display_p4_touch") == 0);
    assert(tft->fallback_ux_pack_id == nullptr);
    assert(tft->final_ux_pack_available);

    const auto* amoled = product_composition::findTargetUxBinding("t_display_p4_amoled");
    assert(amoled != nullptr);
    assert(std::strcmp(amoled->desired_ux_pack_id, "t_display_p4_touch") == 0);
    assert(std::strcmp(amoled->active_ux_pack_id, "t_display_p4_touch") == 0);
    assert(amoled->fallback_ux_pack_id == nullptr);
    assert(amoled->final_ux_pack_available);

    const auto* uconsole = product_composition::findTargetUxBinding("uconsole");
    assert(uconsole != nullptr);
    assert(std::strcmp(uconsole->desired_ux_pack_id, "uconsole_desktop") == 0);
    assert(std::strcmp(uconsole->active_ux_pack_id, "uconsole_desktop") == 0);
    assert(uconsole->fallback_ux_pack_id == nullptr);
    assert(uconsole->final_ux_pack_available);

    const auto* linux_sim = product_composition::findTargetUxBinding("linux_sim");
    assert(linux_sim != nullptr);
    assert(std::strcmp(linux_sim->desired_ux_pack_id, "simulator_full") == 0);
    assert(std::strcmp(linux_sim->active_ux_pack_id, "simulator_full") == 0);
    assert(linux_sim->fallback_ux_pack_id == nullptr);
    assert(linux_sim->final_ux_pack_available);

    const auto* cardputer = product_composition::findTargetUxBinding("cardputerzero");
    assert(cardputer != nullptr);
    assert(std::strcmp(cardputer->desired_ux_pack_id, "cardputer_compact") == 0);
    assert(std::strcmp(cardputer->active_ux_pack_id, "cardputer_compact") == 0);
    assert(cardputer->fallback_ux_pack_id == nullptr);
    assert(cardputer->final_ux_pack_available);

    const auto* gat562 = product_composition::findTargetUxBinding("gat562_mesh_evb_pro");
    assert(gat562 != nullptr);
    assert(std::strcmp(gat562->desired_ux_pack_id, "node_headless") == 0);
    assert(std::strcmp(gat562->active_ux_pack_id, "tiny_node_status") == 0);

    const auto* impulse = product_composition::findTargetUxBinding("t-impulse-plus");
    assert(impulse != nullptr);
    assert(std::strcmp(impulse->desired_ux_pack_id, "node_headless") == 0);
    assert(std::strcmp(impulse->active_ux_pack_id, "tiny_node_status_64x32") == 0);
    assert(!impulse->final_ux_pack_available);

    assert(product_composition::findTargetUxBinding("unknown") == nullptr);
    return 0;
}
