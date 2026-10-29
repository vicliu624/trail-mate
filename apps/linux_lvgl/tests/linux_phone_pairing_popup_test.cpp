#include "platform/desktop/sdl_lvgl_host.h"
#include "ui/phone_pairing_binding.h"
#include "ui/widgets/ble_pairing_popup.h"
#include <cassert>
#include <cstring>
#include <filesystem>

namespace
{
lv_obj_t* label(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* found = label(lv_obj_get_child(root, i), text)) return found;
    return nullptr;
}
void fits(lv_obj_t* object, lv_obj_t* container)
{
    lv_area_t child, parent;
    lv_obj_get_coords(object, &child);
    lv_obj_get_coords(container, &parent);
    assert(child.x1 >= parent.x1 && child.y1 >= parent.y1 && child.x2 <= parent.x2 && child.y2 <= parent.y2);
}
} // namespace
int main(int argc, char** argv)
{
    lv_init();
    for (const auto dimensions : {std::pair{1232, 568}, std::pair{320, 170}, std::pair{320, 240}})
    {
        {
            platform::desktop::DeviceOptions options;
            options.width = dimensions.first;
            options.height = dimensions.second;
            options.fullscreen = false;
            platform::desktop::SdlLvglHost host(options);
            const platform::linux_runtime::PhonePairingDisplay state{true, 0, false, "Trail Mate Display Number"};
            assert(trailmate::linux_ui::presentPhonePairing(state));
            lv_obj_update_layout(lv_layer_top());
            auto* pin = label(lv_layer_top(), "000 000");
            assert(pin && ui::BlePairingPopup::isVisible());
            auto* panel = lv_obj_get_parent(pin);
            fits(panel, lv_layer_top());
            for (uint32_t i = 0; i < lv_obj_get_child_count(panel); ++i)
            {
                auto* item = lv_obj_get_child(panel, i);
                if (!lv_obj_has_flag(item, LV_OBJ_FLAG_HIDDEN)) fits(item, panel);
            }
            ui::BlePairingPopup::update(0, false, "legacy hide");
            assert(!ui::BlePairingPopup::isVisible());
            ui::BlePairingPopup::setState(true, 123456, true, "Trail Mate Display Number");
            lv_obj_update_layout(lv_layer_top());
            assert(label(lv_layer_top(), "123 456") && label(lv_layer_top(), "Fixed PIN"));
            lv_refr_now(nullptr);
            host.present();
            if (argc == 2)
            {
                std::filesystem::create_directories(argv[1]);
                host.saveScreenshot(std::string(argv[1]) + "/pairing-" + std::to_string(options.width) + "x" + std::to_string(options.height) + ".bmp");
            }
        }
        assert(!ui::BlePairingPopup::isVisible()); // Display deletion must reset static widget pointers.
    }
    lv_deinit();
}
