#include "app/input_event.h"
#include "ui/app_runtime.h"
#include "ui/app_screen.h"
#include "ui/screens/geocaching/geocaching_page_shell.h"
#include "ui/shell_ui_runner.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono_literals;
lv_obj_t* label(lv_obj_t* parent, const char* text)
{
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(parent, &lv_label_class) && !std::strcmp(lv_label_get_text(parent), text)) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
        if (auto* found = label(lv_obj_get_child(parent, i), text)) return found;
    return nullptr;
}
void fields(lv_obj_t* parent, std::vector<lv_obj_t*>& out)
{
    if (lv_obj_check_type(parent, &lv_textarea_class))
    {
        out.push_back(parent);
        return;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) fields(lv_obj_get_child(parent, i), out);
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-geocaching-page-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "offline", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    trailmate::cardputer_zero::linux_ui::ShellSession shell;
    trailmate::cardputer_zero::linux_ui::CanvasLvglHost host(shell);
    assert(shell.begin());
    const auto tick = [&](auto duration)
    {
        const auto end = std::chrono::steady_clock::now() + duration;
        do
        {
            host.tick();
            std::this_thread::sleep_for(5ms);
        } while (std::chrono::steady_clock::now() < end);
    };
    tick(3300ms);
    using Key = trailmate::cardputer_zero::app::InputKey;
    const auto key = [&](Key code, char value = 0)
    {
        shell.enqueueInputs({{code, value ? std::string(1, value) : std::string{}, value}});
        tick(150ms);
    };
    for (int i = 0; i < 7; ++i) key(Key::Right);
    key(Key::Enter);
    tick(650ms);
    assert(label(lv_screen_active(), "Geocaching"));
    assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "geocaching"));
    const auto click = [&](const char* text)
    {
        auto* text_object = label(lv_screen_active(), text);
        const auto ready = std::chrono::steady_clock::now() + 5s;
        while (!text_object && std::chrono::steady_clock::now() < ready)
        {
            tick(20ms);
            text_object = label(lv_screen_active(), text);
        }
        if (!text_object) std::fprintf(stderr, "Missing Geocaching control: %s\n", text);
        assert(text_object);
        auto* button = lv_obj_get_parent(text_object);
        assert(lv_obj_check_type(button, &lv_button_class) && !lv_obj_has_state(button, LV_STATE_DISABLED));
        for (unsigned step = 0; step < 40 && lv_group_get_focused(lv_group_get_default()) != button; ++step) key(Key::Tab);
        assert(lv_group_get_focused(lv_group_get_default()) == button);
        key(Key::Enter);
        tick(650ms);
    };
    click("My caches");
    click("New");
    std::vector<lv_obj_t*> text_fields;
    fields(lv_screen_active(), text_fields);
    assert(text_fields.size() == 5);
    for (const char ch : std::string("UI draft")) key(Key::Character, ch);
    assert(!std::strcmp(lv_textarea_get_text(text_fields[0]), "UI draft"));
    key(Key::Left);
    key(Key::Character, '!');
    assert(!std::strcmp(lv_textarea_get_text(text_fields[0]), "UI draf!t"));
    key(Key::Backspace);
    key(Key::Right);
    assert(!std::strcmp(lv_textarea_get_text(text_fields[0]), "UI draft"));
    lv_textarea_set_text(text_fields[1], "Native page persistence");
    lv_group_focus_obj(text_fields[1]);
    lv_textarea_set_cursor_pos(text_fields[1], LV_TEXTAREA_CURSOR_LAST);
    trailmate::cardputer_zero::app::InputEvent committed;
    committed.key = Key::TextEdit;
    committed.edit.focus = shell.textInputState().focus;
    committed.edit.commit = " \xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x99\x82";
    committed.edit.preedit = "pending composition";
    shell.enqueueInputs({committed});
    tick(150ms);
    const std::string description = "Native page persistence" + committed.edit.commit;
    assert(description == lv_textarea_get_text(text_fields[1]));
    lv_textarea_set_text(text_fields[2], "Look here");
    click("Save");
    assert(label(lv_screen_active(), "UI draft"));
    click("UI draft");
    text_fields.clear();
    fields(lv_screen_active(), text_fields);
    assert(text_fields.size() == 5 && !std::strcmp(lv_textarea_get_text(text_fields[0]), "UI draft"));
    assert(description == lv_textarea_get_text(text_fields[1]));
    lv_textarea_set_text(text_fields[0], "Edited UI draft");
    click("Save");
    assert(label(lv_screen_active(), "Edited UI draft"));
    click("Edited UI draft");
    click("Delete local cache");
    click("Confirm local delete");
    const auto deleted_deadline = std::chrono::steady_clock::now() + 5s;
    while (label(lv_screen_active(), "Edited UI draft") && std::chrono::steady_clock::now() < deleted_deadline)
        tick(20ms);
    assert(!label(lv_screen_active(), "Edited UI draft"));
    key(Key::Home);
    tick(650ms);
    assert(!ui_get_active_app());
    key(Key::Enter);
    tick(650ms);
    assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "geocaching"));
    // Leave the page open: the host must detach it before destroying LVGL.
    std::filesystem::remove_all(root);
}
