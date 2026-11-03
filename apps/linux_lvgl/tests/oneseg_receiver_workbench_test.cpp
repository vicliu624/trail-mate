#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/oneseg_receiver_model.h"
#include "ui/oneseg_ofdm_workbench.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>

using namespace platform::linux_runtime;

static lv_obj_t* visible_label(lv_obj_t* root, const char* text, bool prefix = false)
{
    if (lv_obj_check_type(root, &lv_label_class) && lv_obj_is_visible(root))
    {
        const auto* value = lv_label_get_text(root);
        if (prefix ? !std::strncmp(value, text, std::strlen(text)) : !std::strcmp(value, text)) return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* found = visible_label(lv_obj_get_child(root, i), text, prefix)) return found;
    return nullptr;
}

static lv_obj_t* dropdown(lv_obj_t* root, const char* prefix)
{
    if (lv_obj_check_type(root, &lv_dropdown_class) && lv_obj_is_visible(root) &&
        !std::strncmp(lv_dropdown_get_options(root), prefix, std::strlen(prefix))) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* found = dropdown(lv_obj_get_child(root, i), prefix)) return found;
    return nullptr;
}

static void click(const char* text)
{
    auto* object = visible_label(lv_screen_active(), text);
    assert(object);
    lv_obj_send_event(lv_obj_get_parent(object), LV_EVENT_CLICKED, nullptr);
}

static void pump()
{
    lv_tick_inc(101);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
}

int main()
{
    lv_init();
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        options.width = 1232;
        options.height = 568;
        platform::desktop::SdlLvglHost host(options);
        auto* parent = lv_obj_create(lv_screen_active());
        lv_obj_set_size(parent, lv_pct(100), lv_pct(100));
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_border_width(parent, 0, 0);
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        auto* group = lv_group_create();
        {
            unsigned applies = 0, saves = 0, loads = 0, defaults = 0;
            OneSegOfdmExperiment applied;
            OneSegReceptionSettings applied_reception;
            OneSegOfdmWorkbench::Callbacks callbacks;
            callbacks.apply = [&](const auto& reception, const auto& experiment)
            { ++applies; applied = experiment; applied_reception = reception; };
            callbacks.save = [&](const auto&, const auto&)
            { ++saves; };
            callbacks.load = [&]
            { ++loads; };
            callbacks.factory_reset = [&]
            { ++defaults; };
            OneSegOfdmWorkbench workbench(parent, group, callbacks);
            auto* video = lv_obj_create(workbench.videoHost());
            lv_obj_set_size(video, 320, 240);
            OneSegOfdmExperiment settings;
            settings.digital_gain = 1.125;
            OneSegReceptionSettings reception;
            workbench.setWorking(reception, settings);
            OneSegTvSession::Snapshot empty;
            empty.generation = 13;
            for (const auto& view : oneseg_receiver_views())
            {
                if (view.view == OneSegReceiverView::Actual) continue;
                workbench.selectStage(view.stage);
                pump();
                auto* selector = dropdown(parent, "Actual");
                assert(selector);
                unsigned selected = 0;
                bool found = false;
                std::istringstream options(lv_dropdown_get_options(selector));
                for (std::string option; std::getline(options, option); ++selected)
                    if (option == view.name)
                    {
                        found = true;
                        break;
                    }
                assert(found);
                lv_dropdown_set_selected(selector, selected);
                lv_obj_send_event(selector, LV_EVENT_VALUE_CHANGED, nullptr);
                pump();
                workbench.update(empty);
                pump();
                assert(visible_label(parent, "MODEL / ", true));
                assert(lv_obj_get_width(video) == 320 && lv_obj_get_height(video) == 240);
                auto* rate = dropdown(parent, "450 kpair/s");
                assert(rate && lv_obj_has_state(rate, LV_STATE_DISABLED));
                assert(lv_dropdown_get_selected(rate) == 0);
                click("Apply experiment");
                click("Save");
                click("Load");
                click("Default");
                assert(applies == 0 && saves == 0 && loads == 0 && defaults == 0);
                click("?");
                pump();
                const std::string title = std::string("Formula / details / ") + view.name;
                assert(visible_label(parent, title.c_str()));
                click("Close");
                assert(!visible_label(parent, title.c_str()));
                std::cout << "PASS workbench " << view.name << '\n';
            }
            workbench.selectStage(OneSegOfdmStage::Input);
            pump();
            assert(!visible_label(parent, "MODEL / ", true));
            workbench.setRecordedSource(true);
            assert(lv_obj_has_state(dropdown(parent, "450 kpair/s"), LV_STATE_DISABLED));
            workbench.setRecordedSource(false);
            assert(!lv_obj_has_state(dropdown(parent, "450 kpair/s"), LV_STATE_DISABLED));
            click("Apply experiment");
            assert(applies == 1 && applied.digital_gain == settings.digital_gain);
            assert(applied_reception.sample_rate_hz == 450000 && applied_reception.spi_clock_hz == 8700000 && applied_reception.gain_step == 9);
            click("?");
            pump();
            assert(visible_label(parent, "Formula / details"));
            workbench.fullscreen(true);
            pump();
            assert(!visible_label(parent, "Formula / details"));
            assert(lv_obj_get_width(video) == 320 && lv_obj_get_height(video) == 240);
            workbench.fullscreen(false);
        }
        lv_obj_delete(parent);
        lv_group_delete(group);
    }
    lv_deinit();
    std::cout << "PASS model/receiver isolation, preset invariants and fullscreen\n";
}
