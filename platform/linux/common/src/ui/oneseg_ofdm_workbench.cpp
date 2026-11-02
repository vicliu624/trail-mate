#include "ui/oneseg_ofdm_workbench.h"
#include "platform/linux/oneseg_ofdm_lessons.h"
#include "ui/oneseg_carrier_lesson.h"
#include "ui/ui_theme.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace platform::linux_runtime
{
namespace
{
lv_obj_t* label(lv_obj_t* parent, const char* value)
{
    auto* object = lv_label_create(parent);
    lv_label_set_text(object, value);
    lv_obj_set_style_text_font(object, &lv_font_montserrat_12, 0);
    return object;
}
void text(lv_obj_t* object, const char* value)
{
    if (std::strcmp(lv_label_get_text(object), value)) lv_label_set_text(object, value);
}
void hide(lv_obj_t* object, bool value)
{
    if (value) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}
lv_obj_t* panel(lv_obj_t* parent)
{
    auto* object = lv_obj_create(parent);
    lv_obj_set_style_pad_all(object, 6, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, ::ui::theme::border(), 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::surface(), 0);
    lv_obj_set_style_radius(object, 4, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
} // namespace
struct OneSegOfdmWorkbench::Impl
{
    Callbacks callbacks;
    lv_group_t* group;
    lv_obj_t *root, *ribbon, *grid, *video, *parameters, *fields, *analysis, *equation, *explanation, *quality, *message;
    lv_obj_t *rate, *clock, *gain;
    lv_obj_t *phase_panel, *residual_panel, *phase_graph, *residual_graph, *phase_empty, *residual_empty;
    lv_obj_t *phase_axis, *residual_axis, *source_label;
    lv_obj_t* prediction_legend = nullptr;
    lv_obj_t *phase_title = nullptr, *reference_line = nullptr, *reference_legend = nullptr;
    std::array<lv_obj_t*, 3> phase_legends{};
    std::array<lv_point_precise_t, 160> reference_points{};
    lv_obj_t *prediction_error_line = nullptr, *prediction_error_legend = nullptr;
    std::array<lv_point_precise_t, 160> prediction_error_points{};
    lv_obj_t *phase_high, *phase_low, *residual_high, *residual_low, *residual_zero;
    std::array<lv_point_precise_t, 2> residual_zero_points{};
    lv_obj_t *signal_title, *signal_graph, *signal_empty, *signal_line;
    lv_obj_t* signal_selector = nullptr;
    std::array<lv_obj_t*, 64> signal_dots{};
    std::array<lv_point_precise_t, 64> signal_points{};
    std::array<lv_obj_t*, 3> phase_lines{};
    lv_obj_t* residual_line = nullptr;
    std::array<std::array<lv_point_precise_t, 160>, 3> phase_points{};
    std::array<lv_point_precise_t, 160> residual_points{};
    std::array<lv_obj_t*, 7> stage_buttons{};
    OneSegOfdmStage stage = OneSegOfdmStage::Subcarriers;
    std::unique_ptr<OneSegCarrierLesson> carrier_lesson;
    OneSegReceptionSettings reception;
    OneSegOfdmExperiment experiment;
    bool synthetic = false;
    bool syncing_synthetic_rf = false;
    lv_obj_t *radio_rf = nullptr, *synthetic_rf = nullptr;
    double processing_rate = 0;
    OneSegNativeConfiguration native_configuration;
    const OneSegOfdmControl* focused_control = nullptr;
    uint32_t last_draw = uint32_t(-100);
    struct Binding
    {
        Impl* owner = nullptr;
        const OneSegOfdmControl* control = nullptr;
        int scale = 1;
        std::array<double, 8> choices{};
        size_t choice_count = 0;
        lv_obj_t* widget = nullptr;
    };
    std::array<Binding, 8> bindings{};
    std::array<Binding, 3> synthetic_bindings{};
    std::array<int32_t, 4> columns{320, 350, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    std::array<int32_t, 3> rows{270, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    std::array<int32_t, 2> full{LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_t* button(lv_obj_t* parent, const char* caption, lv_event_cb_t action, void* data)
    {
        auto* object = lv_button_create(parent);
        lv_obj_set_size(object, LV_SIZE_CONTENT, 28);
        lv_obj_set_style_pad_hor(object, 7, 0);
        lv_obj_set_style_radius(object, 4, 0);
        lv_obj_set_style_bg_color(object, ::ui::theme::surface_alt(), 0);
        lv_obj_set_style_text_color(object, ::ui::theme::text(), 0);
        auto* caption_label = label(object, caption);
        lv_obj_center(caption_label);
        lv_obj_add_event_cb(object, action, LV_EVENT_CLICKED, data);
        lv_group_add_obj(group, object);
        return object;
    }
    void showLesson(const OneSegOfdmControl& control)
    {
        last_draw = lv_tick_get() - 100;
        focused_control = &control;
        const auto lesson = oneseg_ofdm_lesson(control, experiment, synthetic ? 64000000. / 63 : processing_rate, synthetic);
        text(equation, lesson.equation);
        auto detail = lesson.explanation;
        if (native_configuration.available && !synthetic)
        {
            using P = OneSegOfdmExperiment;
            const auto& n = native_configuration;
            char actual[160]{};
            if (control.real == &P::carrier_bandwidth || control.real == &P::damping)
                std::snprintf(actual, sizeof(actual), " ACTUAL native alpha=%.6g beta=%.6g.", n.alpha, n.beta);
            else if (control.real == &P::timing_bandwidth)
                std::snprintf(actual, sizeof(actual), " ACTUAL timing alpha=%.6g beta=%.6g.", n.timing_alpha, n.timing_beta);
            else if (control.integer == &P::integer_search)
                std::snprintf(actual, sizeof(actual), " ACTUAL search=%.0f bins.", n.integer_search);
            else if (control.integer == &P::offset_confirmation)
                std::snprintf(actual, sizeof(actual), " ACTUAL confirmations=%.0f.", n.offset_confirmation);
            else if (control.integer == &P::channel_average)
                std::snprintf(actual, sizeof(actual), " ACTUAL averaging=%.0f.", n.channel_average);
            else if (control.real == &P::equalizer_floor)
                std::snprintf(actual, sizeof(actual), " ACTUAL lambda=%.5g.", n.equalizer_floor);
            else if (control.real == &P::sample_clock_ppm)
                std::snprintf(actual, sizeof(actual), " ACTUAL first clock readback %.5g ppm (already updated).", n.sampling_ppm);
            else if (control.integer == &P::mode || control.integer == &P::guard_denominator || control.boolean == &P::automatic_waveform)
                std::snprintf(actual, sizeof(actual), " ACTUAL Mode %.0f, FFT %.0f, CP %.0f samples.", n.mode, n.fft_length, n.cp_length);
            detail += actual;
        }
        text(explanation, detail.c_str());
    }
    void manualAvailability()
    {
        using P = OneSegOfdmExperiment;
        for (const auto& binding : bindings)
        {
            if (!binding.widget || !binding.control) continue;
            const auto member = binding.control->integer;
            if (binding.control->boolean == &P::automatic_waveform)
            {
                if (synthetic) lv_obj_add_state(binding.widget, LV_STATE_DISABLED);
                else lv_obj_remove_state(binding.widget, LV_STATE_DISABLED);
                continue;
            }
            if (member != &P::mode && member != &P::guard_denominator && member != &P::constellation &&
                member != &P::code_rate_index && member != &P::time_interleaving) continue;
            if ((synthetic && (member == &P::code_rate_index || member == &P::time_interleaving)) || (!synthetic && experiment.automatic_waveform)) lv_obj_add_state(binding.widget, LV_STATE_DISABLED);
            else lv_obj_remove_state(binding.widget, LV_STATE_DISABLED);
        }
    }
    void syncSyntheticRF()
    {
        syncing_synthetic_rf = true;
        for (auto& binding : synthetic_bindings)
            if (binding.widget) lv_spinbox_set_value(binding.widget, static_cast<int32_t>(binding.control->value(experiment) * binding.scale));
        syncing_synthetic_rf = false;
    }
    void rebuild()
    {
        focused_control = nullptr;
        // Focus events during removal must never dereference an old binding.
        if (carrier_lesson) carrier_lesson->clearControls();
        for (auto& binding : bindings) binding = {};
        lv_obj_clean(fields);
        if (carrier_lesson)
        {
            carrier_lesson->show(stage == OneSegOfdmStage::Subcarriers);
            if (stage == OneSegOfdmStage::Subcarriers) carrier_lesson->controls(fields);
        }
        size_t slot = 0;
        for (const auto& control : oneseg_ofdm_controls())
        {
            if (control.stage != stage || control.synthetic_rf || (control.synthetic_only && !synthetic) || slot == bindings.size()) continue;
            auto& binding = bindings[slot++];
            binding = {this, &control, control.real ? control.high <= 1 ? 1000000 : control.high <= 16 ? 10000
                                                                                : control.high <= 500  ? 100
                                                                                                       : 1
                                                    : 1};
            auto* cell = lv_obj_create(fields);
            lv_obj_set_size(cell, lv_pct(49), 49);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            auto* heading = label(cell, control.label);
            lv_obj_set_width(heading, lv_pct(100));
            lv_label_set_long_mode(heading, LV_LABEL_LONG_CLIP);
            using P = OneSegOfdmExperiment;
            std::string options;
            const auto choose = [&](double value, const char* caption)
            {
                if (!options.empty()) options += '\n';
                options += caption;
                binding.choices[binding.choice_count++] = value;
            };
            if (control.boolean)
            {
                choose(0, control.boolean == &P::automatic_waveform ? "Manual" : "Off");
                choose(1, control.boolean == &P::automatic_waveform ? "Automatic" : "On");
            }
            else if (control.integer == &P::mode)
            {
                choose(1, "Mode 1");
                choose(2, "Mode 2");
                choose(3, "Mode 3");
            }
            else if (control.integer == &P::guard_denominator)
            {
                choose(4, "1/4");
                choose(8, "1/8");
                choose(16, "1/16");
                choose(32, "1/32");
            }
            else if (control.integer == &P::constellation)
            {
                choose(4, "QPSK");
                choose(16, "16-QAM");
                choose(64, "64-QAM");
            }
            else if (control.integer == &P::code_rate_index)
            {
                choose(0, "1/2");
                choose(1, "2/3");
                choose(2, "3/4");
                choose(3, "5/6");
                choose(4, "7/8");
            }
            else if (control.integer == &P::time_interleaving)
            {
                const unsigned factor = 1u << (3 - experiment.mode);
                for (unsigned value : {0u, factor, factor * 2, factor * 4})
                {
                    char item[24];
                    std::snprintf(item, sizeof(item), "%u", value);
                    choose(value, item);
                }
            }
            if (binding.choice_count)
            {
                auto* dropdown = lv_dropdown_create(cell);
                binding.widget = dropdown;
                lv_obj_set_size(dropdown, lv_pct(100), 29);
                lv_obj_align(dropdown, LV_ALIGN_BOTTOM_LEFT, 0, 0);
                lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_12, 0);
                lv_dropdown_set_options(dropdown, options.c_str());
                bool found = false;
                for (size_t index = 0; index < binding.choice_count; ++index)
                    if (binding.choices[index] == control.value(experiment))
                    {
                        lv_dropdown_set_selected(dropdown, index);
                        found = true;
                    }
                if (!found && binding.choice_count < binding.choices.size())
                {
                    const auto index = binding.choice_count;
                    char invalid[48];
                    std::snprintf(invalid, sizeof(invalid), "Invalid: %.5g", control.value(experiment));
                    choose(control.value(experiment), invalid);
                    lv_dropdown_set_options(dropdown, options.c_str());
                    lv_dropdown_set_selected(dropdown, index);
                }
                lv_group_add_obj(group, dropdown);
                lv_obj_add_event_cb(
                    dropdown, [](lv_event_t* event)
                    {
                    auto& binding = *static_cast<Binding*>(lv_event_get_user_data(event));
                    const auto index = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event)));
                    if (!binding.owner || !binding.control) return;
                    if (index < binding.choice_count) binding.control->set(binding.owner->experiment, binding.choices[index]);
                    binding.owner->manualAvailability();
                    binding.owner->showLesson(*binding.control); },
                    LV_EVENT_VALUE_CHANGED, &binding);
                lv_obj_add_event_cb(
                    dropdown, [](lv_event_t* event)
                    { auto& b = *static_cast<Binding*>(lv_event_get_user_data(event)); if (b.owner && b.control) b.owner->showLesson(*b.control); },
                    LV_EVENT_FOCUSED, &binding);
                continue;
            }
            auto* spin = lv_spinbox_create(cell);
            binding.widget = spin;
            lv_obj_set_size(spin, lv_pct(100), 29);
            lv_obj_align(spin, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            unsigned decimals = 0;
            for (int factor = binding.scale; factor > 1; factor /= 10) ++decimals;
            unsigned digits = decimals + 1;
            for (double maximum = std::max(std::abs(control.low), std::abs(control.high)); maximum >= 10; maximum /= 10) ++digits;
            lv_spinbox_set_digit_format(spin, digits, decimals ? digits - decimals : 0);
            lv_spinbox_set_range(spin, static_cast<int32_t>(control.low * binding.scale), static_cast<int32_t>(control.high * binding.scale));
            lv_spinbox_set_step(spin, std::max<int32_t>(1, static_cast<int32_t>(control.step * binding.scale)));
            lv_spinbox_set_value(spin, static_cast<int32_t>(std::llround(control.value(experiment) * binding.scale)));
            lv_obj_set_style_text_font(spin, &lv_font_montserrat_12, 0);
            lv_obj_set_style_pad_right(spin, 86, 0);
            auto* unit = label(cell, control.unit);
            lv_obj_set_width(unit, lv_pct(45));
            lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_opa(unit, LV_OPA_70, 0);
            lv_label_set_long_mode(unit, LV_LABEL_LONG_CLIP);
            lv_obj_align(unit, LV_ALIGN_BOTTOM_RIGHT, -8, -7);
            lv_group_add_obj(group, spin);
            lv_obj_add_event_cb(
                spin, [](lv_event_t* event)
                {
                auto& binding = *static_cast<Binding*>(lv_event_get_user_data(event));
                if (!binding.owner || !binding.control) return;
                binding.control->set(binding.owner->experiment, double(lv_spinbox_get_value(static_cast<lv_obj_t*>(lv_event_get_target(event))))/binding.scale);
                binding.owner->showLesson(*binding.control); },
                LV_EVENT_VALUE_CHANGED, &binding);
            lv_obj_add_event_cb(
                spin, [](lv_event_t* event)
                { auto& b = *static_cast<Binding*>(lv_event_get_user_data(event)); if (b.owner && b.control) b.owner->showLesson(*b.control); },
                LV_EVENT_FOCUSED, &binding);
        }
        manualAvailability();
        syncSyntheticRF();
        focused_control = nullptr;
        text(equation, oneseg_ofdm_equation(stage));
        text(explanation, synthetic ? oneseg_synthetic_stage_note(stage) : oneseg_ofdm_explanation(stage));
        if (signal_selector)
        {
            lv_dropdown_set_options(signal_selector, stage == OneSegOfdmStage::Channel ? "H amplitude\nH phase" : synthetic ? "I/Q\nReference BER"
                                                                                                                            : "I/Q\nRS words");
            lv_dropdown_set_selected(signal_selector, 0);
            hide(signal_selector, stage != OneSegOfdmStage::Channel && stage != OneSegOfdmStage::Decoder);
        }
        for (size_t index = 0; index < stage_buttons.size(); ++index)
            if (index == static_cast<unsigned>(stage)) lv_obj_add_state(stage_buttons[index], LV_STATE_CHECKED);
            else lv_obj_remove_state(stage_buttons[index], LV_STATE_CHECKED);
    }
    OneSegReceptionSettings workingReception() const
    {
        OneSegReceptionSettings result;
        result.enabled = true;
        result.sample_rate_hz = lv_dropdown_get_selected(rate) ? 500000 : 450000;
        constexpr uint32_t clocks[]{8350000, 8700000, 16000000};
        result.spi_clock_hz = clocks[std::min<unsigned>(2, lv_dropdown_get_selected(clock))];
        result.gain_step = lv_dropdown_get_selected(gain);
        return result;
    }
    Impl(lv_obj_t* parent, lv_group_t* input_group, Callbacks actions) : callbacks(std::move(actions)), group(input_group)
    {
        root = lv_obj_create(parent);
        lv_obj_set_size(root, lv_pct(100), 0);
        lv_obj_set_flex_grow(root, 1);
        lv_obj_set_style_pad_all(root, 0, 0);
        lv_obj_set_style_border_width(root, 0, 0);
        lv_obj_set_style_pad_row(root, 4, 0);
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        ribbon = lv_obj_create(root);
        lv_obj_set_size(ribbon, lv_pct(100), 30);
        lv_obj_set_style_pad_all(ribbon, 0, 0);
        lv_obj_set_style_border_width(ribbon, 0, 0);
        lv_obj_set_style_pad_column(ribbon, 4, 0);
        lv_obj_set_flex_flow(ribbon, LV_FLEX_FLOW_ROW);
        constexpr OneSegOfdmStage order[]{OneSegOfdmStage::Input, OneSegOfdmStage::Filter, OneSegOfdmStage::Symbol,
                                          OneSegOfdmStage::Subcarriers, OneSegOfdmStage::Tracking, OneSegOfdmStage::Channel, OneSegOfdmStage::Decoder};
        for (auto entry : order)
        {
            const auto index = static_cast<size_t>(entry);
            stage_buttons[index] = button(
                ribbon, oneseg_ofdm_stage_name(static_cast<OneSegOfdmStage>(index)), [](lv_event_t* event)
                {
                auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
                auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
                for (size_t i = 0; i < self.stage_buttons.size(); ++i)
                    if (self.stage_buttons[i] == target) { self.stage = static_cast<OneSegOfdmStage>(i); self.rebuild(); break; } },
                this);
            lv_obj_set_flex_grow(stage_buttons[index], 1);
            lv_obj_set_style_bg_color(stage_buttons[index], ::ui::theme::accent(), LV_STATE_CHECKED);
        }
        grid = lv_obj_create(root);
        lv_obj_set_size(grid, lv_pct(100), 0);
        lv_obj_set_flex_grow(grid, 1);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_pad_row(grid, 6, 0);
        lv_obj_set_style_pad_column(grid, 6, 0);
        lv_obj_set_grid_dsc_array(grid, columns.data(), rows.data());
        lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        video = panel(grid);
        lv_obj_set_style_pad_all(video, 0, 0);
        lv_obj_set_style_border_width(video, 0, 0);
        lv_obj_set_grid_cell(video, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        parameters = panel(grid);
        lv_obj_set_grid_cell(parameters, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_flex_flow(parameters, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parameters, 4, 0);
        fields = lv_obj_create(parameters);
        lv_obj_set_size(fields, lv_pct(100), 154);
        lv_obj_set_style_pad_all(fields, 0, 0);
        lv_obj_set_style_border_width(fields, 0, 0);
        lv_obj_set_style_bg_opa(fields, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_column(fields, 3, 0);
        lv_obj_set_style_pad_row(fields, 2, 0);
        lv_obj_set_flex_flow(fields, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_remove_flag(fields, LV_OBJ_FLAG_SCROLLABLE);
        auto* row = lv_obj_create(parameters);
        lv_obj_set_size(row, lv_pct(100), 30);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        button(
            row, "Apply experiment", [](lv_event_t* event)
            {
            auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
            const auto hardware = self.workingReception();
            if (!self.experiment.valid(self.synthetic ? 64000000/63 : hardware.sample_rate_hz, self.synthetic)) { text(self.message, "Invalid parameter combination / check limits"); return; }
            if (self.callbacks.apply) self.callbacks.apply(hardware, self.experiment); },
            this);
        button(
            row, "Default", [](lv_event_t* event)
            {
            auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
            self.experiment = {};
            if (self.carrier_lesson) self.carrier_lesson->reset();
            lv_dropdown_set_selected(self.rate, 0);
            lv_dropdown_set_selected(self.clock, 1);
            lv_dropdown_set_selected(self.gain, 9);
            self.rebuild();
            if (self.callbacks.factory_reset) self.callbacks.factory_reset();
            else if (self.callbacks.apply) self.callbacks.apply(self.workingReception(), self.experiment); },
            this);
        button(
            row, "Save", [](lv_event_t* event)
            {
            auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
            if (self.callbacks.save) self.callbacks.save(self.workingReception(), self.experiment); },
            this);
        button(
            row, "Load", [](lv_event_t* event)
            {
            auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
            if (self.callbacks.load) self.callbacks.load(); },
            this);
        auto* rf = lv_obj_create(parameters);
        radio_rf = rf;
        lv_obj_set_size(rf, lv_pct(100), 32);
        lv_obj_set_style_pad_all(rf, 0, 0);
        lv_obj_set_style_border_width(rf, 0, 0);
        lv_obj_set_flex_flow(rf, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(rf, 3, 0);
        lv_obj_remove_flag(rf, LV_OBJ_FLAG_SCROLLABLE);
        const auto dropdown = [&](const char* options)
        {
            auto* object = lv_dropdown_create(rf);
            lv_obj_set_size(object, 105, 29);
            lv_obj_set_style_text_font(object, &lv_font_montserrat_12, 0);
            lv_dropdown_set_options(object, options);
            lv_group_add_obj(group, object);
            return object;
        };
        rate = dropdown("450 kpair/s\n500 kpair/s");
        clock = dropdown("8.35 MHz\n8.7 MHz\n16 MHz");
        gain = dropdown("AGC\nGain 1\nGain 2\nGain 3\nGain 4\nGain 5\nGain 6\nGain 7\nGain 8\nGain 9\nGain 10\nGain 11\nGain 12\nGain 13");
        lv_dropdown_set_selected(clock, 1);
        lv_dropdown_set_selected(gain, 9);
        synthetic_rf = lv_obj_create(parameters);
        lv_obj_set_size(synthetic_rf, lv_pct(100), 45);
        lv_obj_set_style_pad_all(synthetic_rf, 0, 0);
        lv_obj_set_style_border_width(synthetic_rf, 0, 0);
        lv_obj_set_style_pad_column(synthetic_rf, 3, 0);
        lv_obj_set_flex_flow(synthetic_rf, LV_FLEX_FLOW_ROW);
        lv_obj_remove_flag(synthetic_rf, LV_OBJ_FLAG_SCROLLABLE);
        size_t source_slot = 0;
        for (const auto& control : oneseg_ofdm_controls())
        {
            if (!control.synthetic_rf) continue;
            auto& binding = synthetic_bindings[source_slot++];
            binding = {this, &control, control.high <= 100 ? 10 : 1};
            auto* cell = lv_obj_create(synthetic_rf);
            lv_obj_set_size(cell, 105, 45);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            char title[48];
            std::snprintf(title, sizeof(title), "%s [%s]", control.label, control.unit);
            label(cell, title);
            auto* spin = lv_spinbox_create(cell);
            binding.widget = spin;
            lv_obj_set_size(spin, lv_pct(100), 29);
            lv_obj_align(spin, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            lv_obj_set_style_text_font(spin, &lv_font_montserrat_12, 0);
            lv_spinbox_set_digit_format(spin, control.high <= 100 ? 4 : control.high <= 2000 ? 4
                                                                                             : 5,
                                        control.high <= 100 ? 3 : 0);
            lv_spinbox_set_range(spin, static_cast<int32_t>(control.low * binding.scale), static_cast<int32_t>(control.high * binding.scale));
            lv_spinbox_set_step(spin, static_cast<int32_t>(control.step * binding.scale));
            lv_spinbox_set_value(spin, static_cast<int32_t>(control.value(experiment) * binding.scale));
            lv_group_add_obj(group, spin);
            lv_obj_add_event_cb(
                spin, [](lv_event_t* event)
                { auto& b = *static_cast<Binding*>(lv_event_get_user_data(event)); if (b.owner->syncing_synthetic_rf) return; b.control->set(b.owner->experiment, double(lv_spinbox_get_value(b.widget))/b.scale); b.owner->showLesson(*b.control); },
                LV_EVENT_VALUE_CHANGED, &binding);
            lv_obj_add_event_cb(
                spin, [](lv_event_t* event)
                { auto& b = *static_cast<Binding*>(lv_event_get_user_data(event)); b.owner->showLesson(*b.control); },
                LV_EVENT_FOCUSED, &binding);
        }
        hide(synthetic_rf, true);
        message = label(parameters, "Factory: 450 kpair/s / 8.7 MHz / gain 9");
        lv_obj_set_width(message, lv_pct(100));
        lv_label_set_long_mode(message, LV_LABEL_LONG_CLIP);
        analysis = panel(grid);
        lv_obj_set_grid_cell(analysis, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_flex_flow(analysis, LV_FLEX_FLOW_COLUMN);
        source_label = label(analysis, "OFDM learning / native tracking data");
        lv_obj_set_width(source_label, lv_pct(100));
        lv_obj_set_style_pad_right(source_label, 118, 0);
        signal_selector = lv_dropdown_create(analysis);
        lv_obj_set_size(signal_selector, 112, 22);
        lv_obj_add_flag(signal_selector, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(signal_selector, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_style_text_font(signal_selector, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_ver(signal_selector, 2, 0);
        lv_group_add_obj(group, signal_selector);
        lv_obj_add_event_cb(
            signal_selector, [](lv_event_t* event)
            {
                auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
                self.focused_control = nullptr; self.last_draw = lv_tick_get()-100;
                const bool selected = lv_dropdown_get_selected(self.signal_selector) == 1;
                if (selected && self.stage == OneSegOfdmStage::Channel)
                {
                    text(self.equation, "phase[k]=unwrap(arg(H[k])); phase is unreliable where |H| approaches zero");
                    text(self.explanation, "Actual complex channel estimates, unwrapped before display decimation. Residual carrier phase, timing and equalizer reference are included; this is not absolute propagation phase. Compare amplitude at deep fades before interpreting phase jumps.");
                }
                else if (selected && self.stage == OneSegOfdmStage::Decoder)
                {
                    text(self.equation, self.synthetic ? "Uncoded BER=known wrong Gray-labelled bits / known data bits" : "RS(204,188): 16 parity symbols; t=8 byte-symbol errors");
                    text(self.explanation, self.synthetic ? "Known-reference teaching data only, without TV or FEC. This is the reference window's hard-decision BER, not native RF BER." : "Actual RS outcomes, including startup. Reported corrected symbols may include shortened padding; they are not bit BER. Graph: uncorrectable / attempted words per report interval. Cumulative reports may lag by up to63 words.");
                }
                else
                {
                    text(self.equation, oneseg_ofdm_equation(self.stage));
                    text(self.explanation, self.synthetic ? oneseg_synthetic_stage_note(self.stage) : oneseg_ofdm_explanation(self.stage));
                } },
            LV_EVENT_VALUE_CHANGED, this);
        equation = label(analysis, "");
        explanation = label(analysis, "");
        quality = label(analysis, "");
        for (auto* object : {equation, explanation, quality}) lv_obj_set_width(object, lv_pct(100));
        signal_title = label(analysis, "Measured signal unavailable");
        lv_obj_set_width(signal_title, lv_pct(100));
        signal_graph = lv_obj_create(analysis);
        lv_obj_set_width(signal_graph, lv_pct(100));
        lv_obj_set_flex_grow(signal_graph, 1);
        lv_obj_set_style_min_height(signal_graph, 40, 0);
        lv_obj_set_style_pad_all(signal_graph, 0, 0);
        lv_obj_set_style_border_width(signal_graph, 1, 0);
        lv_obj_set_style_border_color(signal_graph, ::ui::theme::border(), 0);
        lv_obj_remove_flag(signal_graph, LV_OBJ_FLAG_SCROLLABLE);
        signal_line = lv_line_create(signal_graph);
        lv_obj_set_style_line_color(signal_line, lv_color_hex(0x2f7ddb), 0);
        lv_obj_set_style_line_width(signal_line, 1, 0);
        for (auto& dot : signal_dots)
        {
            dot = lv_obj_create(signal_graph);
            lv_obj_set_size(dot, 3, 3);
            lv_obj_set_style_border_width(dot, 0, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x087f70), 0);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            hide(dot, true);
        }
        signal_empty = label(signal_graph, "No measured samples");
        lv_obj_center(signal_empty);
        hide(signal_line, true);
        phase_panel = panel(grid);
        lv_obj_set_grid_cell(phase_panel, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
        phase_title = label(phase_panel, "Native phase advance [rad/useful symbol]");
        const char* legends[]{"observed", "prior (legacy)", "corrected"};
        constexpr uint32_t legend_colors[]{0x687480, 0xd7932d, 0x2f7ddb};
        constexpr int legend_right[]{-205, -92, 0};
        for (unsigned i = 0; i < 3; ++i)
        {
            auto* legend = label(phase_panel, legends[i]);
            phase_legends[i] = legend;
            if (i == 1) prediction_legend = legend;
            lv_obj_set_style_text_color(legend, lv_color_hex(legend_colors[i]), 0);
            lv_obj_align(legend, LV_ALIGN_TOP_RIGHT, legend_right[i], 0);
        }
        residual_panel = panel(grid);
        lv_obj_set_grid_cell(residual_panel, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        label(residual_panel, "Error [rad/useful symbol]");
        prediction_error_legend = label(residual_panel, "forecast");
        lv_obj_set_style_text_color(prediction_error_legend, lv_color_hex(0xd7932d), 0);
        lv_obj_align(prediction_error_legend, LV_ALIGN_TOP_RIGHT, -95, 0);
        auto* corrected_legend = label(residual_panel, "corrected");
        lv_obj_set_style_text_color(corrected_legend, lv_color_hex(0x087f70), 0);
        lv_obj_align(corrected_legend, LV_ALIGN_TOP_RIGHT, 0, 0);
        const auto graph = [&](lv_obj_t* parent)
        {
            auto* object = lv_obj_create(parent);
            lv_obj_set_size(object, lv_pct(100), lv_pct(100));
            lv_obj_set_style_pad_all(object, 0, 0);
            lv_obj_set_style_pad_top(object, 18, 0);
            lv_obj_set_style_pad_bottom(object, 17, 0);
            lv_obj_set_style_border_width(object, 0, 0);
            lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
            lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
            return object;
        };
        phase_graph = graph(phase_panel);
        reference_legend = label(phase_panel, "truth");
        lv_obj_set_style_text_color(reference_legend, lv_color_hex(0x9a44bb), 0);
        lv_obj_align(reference_legend, LV_ALIGN_TOP_RIGHT, 0, 0);
        hide(reference_legend, true);
        residual_graph = graph(residual_panel);
        residual_zero = lv_line_create(residual_graph);
        lv_obj_set_style_line_color(residual_zero, lv_color_hex(0x808080), 0);
        lv_obj_set_style_line_opa(residual_zero, LV_OPA_50, 0);
        lv_obj_set_style_line_width(residual_zero, 1, 0);
        constexpr uint32_t colors[]{0x687480, 0xd7932d, 0x2f7ddb};
        for (size_t i = 0; i < phase_lines.size(); ++i)
        {
            phase_lines[i] = lv_line_create(phase_graph);
            lv_obj_set_style_line_color(phase_lines[i], lv_color_hex(colors[i]), 0);
            lv_obj_set_style_line_width(phase_lines[i], i == 2 ? 2 : 1, 0);
        }
        reference_line = lv_line_create(phase_graph);
        lv_obj_set_style_line_color(reference_line, lv_color_hex(0x9a44bb), 0);
        lv_obj_set_style_line_width(reference_line, 1, 0);
        hide(reference_line, true);
        residual_line = lv_line_create(residual_graph);
        prediction_error_line = lv_line_create(residual_graph);
        lv_obj_set_style_line_color(prediction_error_line, lv_color_hex(0xd7932d), 0);
        lv_obj_set_style_line_width(prediction_error_line, 1, 0);
        lv_obj_set_style_line_color(residual_line, lv_color_hex(0x087f70), 0);
        lv_obj_set_style_line_width(residual_line, 2, 0);
        phase_empty = label(phase_graph, "No native tracking observations");
        residual_empty = label(residual_graph, "No residual observations");
        lv_obj_center(phase_empty);
        lv_obj_center(residual_empty);
        phase_high = label(phase_graph, "");
        phase_low = label(phase_graph, "");
        residual_high = label(residual_graph, "");
        residual_low = label(residual_graph, "");
        for (auto* object : {phase_high, residual_high}) lv_obj_align(object, LV_ALIGN_TOP_LEFT, 0, 0);
        for (auto* object : {phase_low, residual_low}) lv_obj_align(object, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        phase_axis = label(phase_panel, "Native output-symbol index / unavailable");
        residual_axis = label(residual_panel, "RMS unavailable");
        lv_obj_align(phase_axis, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_align(residual_axis, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        carrier_lesson = std::make_unique<OneSegCarrierLesson>(analysis, phase_panel, residual_panel, group);
        rebuild();
    }
    void update(const OneSegTvSession::Snapshot& state)
    {
        if (state.decoder.ofdm_signal[0].count) processing_rate = state.decoder.ofdm_signal[0].sample_rate;
        const auto tick = lv_tick_get();
        if (uint32_t(tick - last_draw) < 100) return;
        last_draw = tick;
        const bool readback_changed = native_configuration.available != state.decoder.native_configuration.available || native_configuration.received_ms != state.decoder.native_configuration.received_ms;
        native_configuration = state.decoder.native_configuration;
        if (readback_changed && focused_control) showLesson(*focused_control);
        if (stage == OneSegOfdmStage::Subcarriers)
        {
            if (lv_obj_is_visible(analysis)) carrier_lesson->update(state, experiment);
            return;
        }
        const auto& history = state.decoder.ofdm_phase;
        const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        const bool available = history.size >= 2;
        bool forecast = available;
        for (size_t i = 0; i < history.size; ++i) forecast = forecast && history.chronological(i).has_prediction;
        bool reference = forecast && history.source == OneSegLabSource::Synthetic;
        for (size_t i = 0; i < history.size; ++i) reference = reference && history.chronological(i).has_reference;
        text(phase_title, state.source.synthetic ? "Synthetic phase advance [rad/useful symbol]" : "Native phase advance [rad/useful symbol]");
        hide(reference_line, !reference);
        hide(reference_legend, !reference);
        const int positions[]{reference ? -260 : -205, reference ? -170 : -92, reference ? -70 : 0};
        for (size_t i = 0; i < phase_legends.size(); ++i) lv_obj_align(phase_legends[i], LV_ALIGN_TOP_RIGHT, positions[i], 0);
        text(prediction_legend, forecast ? "prediction" : "prior (legacy)");
        hide(prediction_error_line, !forecast);
        hide(prediction_error_legend, !forecast);
        for (auto* line : phase_lines) hide(line, !available);
        hide(residual_line, !available);
        hide(residual_zero, !available);
        for (auto* label : {phase_high, phase_low, residual_high, residual_low}) hide(label, !available);
        hide(phase_empty, available);
        hide(residual_empty, available);
        auto origin = history.source;
        bool fresh_source = history.fresh(now);
        if (origin == OneSegLabSource::Unavailable)
            for (const auto& signal : state.decoder.ofdm_signal)
                if (signal.count)
                {
                    origin = signal.source;
                    fresh_source = fresh_source || signal.fresh(now);
                }
        const char* source = origin == OneSegLabSource::LiveReceiver ? "LIVE receiver" : origin == OneSegLabSource::RecordedIq ? "RECORDED IQ"
                                                                                     : origin == OneSegLabSource::Synthetic    ? "SYNTHETIC experiment"
                                                                                                                               : "No measured source";
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "%s / %s", source, fresh_source && state.active ? "updating" : origin != OneSegLabSource::Unavailable ? "retained / stale"
                                                                                                                                                    : "unavailable");
        if (state.input_rate_hz)
        {
            const auto used = std::strlen(buffer);
            std::snprintf(buffer + used, sizeof(buffer) - used, " / input %.1f kpair/s", state.input_rate_hz / 1000.);
        }
        text(source_label, buffer);
        std::snprintf(buffer, sizeof(buffer), "Service %u / Frames %llu / TS %.1f kB\nCapture %d / CC %llu / PSI CRC %llu / Decode %llu / Track %zu / rejected %llu",
                      state.decoder.programmes.selected_program, (unsigned long long)state.presented_frames, double(state.decoder.ts_bytes) / 1000,
                      state.receive.radio.capture_error, (unsigned long long)state.decoder.programmes.continuity_errors,
                      (unsigned long long)state.decoder.programmes.crc_errors, (unsigned long long)state.media.decode_errors,
                      history.size, (unsigned long long)state.decoder.rejected_ofdm_records);
        const auto used = std::strlen(buffer);
        if (native_configuration.available)
            std::snprintf(buffer + used, sizeof(buffer) - used, "\nNative M%.0f / FFT %.0f / CP %.0f / avg %.0f / lambda %.3g", native_configuration.mode, native_configuration.fft_length, native_configuration.cp_length, native_configuration.channel_average, native_configuration.equalizer_floor);
        else
            std::snprintf(buffer + used, sizeof(buffer) - used, "\nNative loop readback unavailable");
        if (stage == OneSegOfdmStage::Decoder && !state.source.synthetic)
        {
            const auto length = std::strlen(buffer);
            if (state.decoder.fec.size)
            {
                const auto& f = state.decoder.fec.newest();
                std::snprintf(buffer + length, sizeof(buffer) - length, "\nRS clean %llu / corrected %llu / drop %llu / symbols %llu", (unsigned long long)f.clean, (unsigned long long)f.corrected, (unsigned long long)f.uncorrectable, (unsigned long long)f.symbols);
            }
            else std::snprintf(buffer + length, sizeof(buffer) - length, "\nNative RS outcomes unavailable (not zero errors)");
        }
        if (state.source.synthetic)
        {
            if (history.size && history.newest().has_reference)
            {
                const auto& r = history.newest();
                std::snprintf(buffer, sizeof(buffer), "SYNTHETIC / known framing / no TV or FEC\nReference EVM %.2f%% / uncoded BER %.3g / %.0f bits\nVirtual time %.3f s (not wall time)", r.reference_evm * 100, r.reference_ber, r.reference_bits, r.virtual_time);
            }
            else std::snprintf(buffer, sizeof(buffer), "SYNTHETIC OFDM / waiting for generated IQ\nKnown framing; no RF, TV, TMCC or FEC");
        }
        text(quality, buffer);
        drawSignal(state, now);
        if (!available) return;
        lv_obj_update_layout(grid);
        double low = forecast ? history.chronological(0).prediction : history.chronological(0).prior, high = low, residual = .001, forecast_energy = 0;
        for (size_t i = 0; i < history.size; ++i)
        {
            const auto& value = history.chronological(i);
            const double predicted = forecast ? value.prediction : value.prior;
            low = std::min({low, predicted, value.observed, value.corrected});
            high = std::max({high, predicted, value.observed, value.corrected});
            if (reference)
            {
                low = std::min(low, value.reference_phase);
                high = std::max(high, value.reference_phase);
            }
            residual = std::max(residual, std::abs(value.residual));
            if (forecast)
            {
                const auto error = value.observed - value.prediction;
                residual = std::max(residual, std::abs(error));
                forecast_energy += error * error;
            }
        }
        const double pad = std::max(.01, (high - low) * .1);
        low -= pad;
        high += pad;
        residual *= 1.1;
        constexpr int axis_width = 44;
        const double width = std::max<int32_t>(1, lv_obj_get_content_width(phase_graph) - axis_width - 2);
        const double height = std::max<int32_t>(1, lv_obj_get_content_height(phase_graph) - 2);
        const double rw = std::max<int32_t>(1, lv_obj_get_content_width(residual_graph) - axis_width - 2);
        const double rh = std::max<int32_t>(1, lv_obj_get_content_height(residual_graph) - 2);
        for (size_t i = 0; i < history.size; ++i)
        {
            const auto& value = history.chronological(i);
            const double values[]{value.observed, forecast ? value.prediction : value.prior, value.corrected};
            for (size_t j = 0; j < 3; ++j)
                phase_points[j][i] = {static_cast<lv_value_precise_t>(axis_width + i * width / (history.size - 1)), static_cast<lv_value_precise_t>((high - values[j]) / (high - low) * height)};
            residual_points[i] = {static_cast<lv_value_precise_t>(axis_width + i * rw / (history.size - 1)), static_cast<lv_value_precise_t>((residual - value.residual) / (2 * residual) * rh)};
            if (forecast) prediction_error_points[i] = {residual_points[i].x, static_cast<lv_value_precise_t>((residual - (value.observed - value.prediction)) / (2 * residual) * rh)};
            if (reference) reference_points[i] = {phase_points[0][i].x, static_cast<lv_value_precise_t>((high - value.reference_phase) / (high - low) * height)};
        }
        residual_zero_points = {{{axis_width, static_cast<lv_value_precise_t>(rh / 2)}, {static_cast<lv_value_precise_t>(axis_width + rw), static_cast<lv_value_precise_t>(rh / 2)}}};
        lv_line_set_points(residual_zero, residual_zero_points.data(), residual_zero_points.size());
        std::snprintf(buffer, sizeof(buffer), "%.3g", high);
        text(phase_high, buffer);
        std::snprintf(buffer, sizeof(buffer), "%.3g", low);
        text(phase_low, buffer);
        std::snprintf(buffer, sizeof(buffer), "+%.2g", residual);
        text(residual_high, buffer);
        std::snprintf(buffer, sizeof(buffer), "-%.2g", residual);
        text(residual_low, buffer);
        for (size_t i = 0; i < 3; ++i) lv_line_set_points(phase_lines[i], phase_points[i].data(), history.size);
        lv_line_set_points(residual_line, residual_points.data(), history.size);
        if (forecast) lv_line_set_points(prediction_error_line, prediction_error_points.data(), history.size);
        if (reference) lv_line_set_points(reference_line, reference_points.data(), history.size);
        if (forecast)
            std::snprintf(buffer, sizeof(buffer), "Symbols %llu..%llu / %s alpha %.5g beta %.5g", (unsigned long long)history.chronological(0).symbol, (unsigned long long)history.newest().symbol, state.source.synthetic ? "model" : "native", history.newest().alpha, history.newest().beta);
        else
            std::snprintf(buffer, sizeof(buffer), "Symbols %llu..%llu / legacy prior; forecast unavailable", (unsigned long long)history.chronological(0).symbol, (unsigned long long)history.newest().symbol);
        text(phase_axis, buffer);
        if (forecast)
            std::snprintf(buffer, sizeof(buffer), "RMS forecast %.5g -> corrected %.5g rad", std::sqrt(forecast_energy / history.size), history.residualRms());
        else
            std::snprintf(buffer, sizeof(buffer), "RMS corrected %.5g rad / forecast unavailable", history.residualRms());
        text(residual_axis, buffer);
    }
    void drawSignal(const OneSegTvSession::Snapshot& state, uint64_t now)
    {
        const bool clock = focused_control && (focused_control->real == &OneSegOfdmExperiment::sample_clock_ppm || focused_control->real == &OneSegOfdmExperiment::timing_bandwidth);
        const bool alternate = lv_dropdown_get_selected(signal_selector) == 1;
        const bool fec_view = stage == OneSegOfdmStage::Decoder && alternate && !state.source.synthetic;
        const bool reference_view = stage == OneSegOfdmStage::Decoder && alternate && state.source.synthetic;
        const unsigned kind = stage == OneSegOfdmStage::Symbol ? 4 : stage == OneSegOfdmStage::Channel                                     ? alternate ? 5 : 3
                                                                 : stage == OneSegOfdmStage::Decoder || stage == OneSegOfdmStage::Tracking ? 2
                                                                                                                                           : 1;
        // UI projection of the actual native phase history, not an IPC record.
        OneSegSignalMeasurement clock_view;
        if (clock)
        {
            const auto& history = state.decoder.ofdm_phase;
            clock_view.count = std::min(history.size, clock_view.points.size());
            clock_view.source = history.source;
            clock_view.updated_ms = history.last_update_ms;
            for (size_t i = 0; i < clock_view.count; ++i)
            {
                const auto index = clock_view.count > 1 ? i * (history.size - 1) / (clock_view.count - 1) : 0;
                const auto& observation = history.chronological(index);
                clock_view.points[i] = {static_cast<double>(observation.symbol), observation.sampling_ppm};
            }
        }
        else if (fec_view)
        {
            const auto& history = state.decoder.fec;
            clock_view.count = history.size;
            clock_view.source = history.source;
            if (history.size) clock_view.updated_ms = history.newest().received_ms;
            for (size_t i = 0; i < history.size; ++i)
            {
                const auto& current = history.chronological(i);
                const auto previous_words = i ? history.chronological(i - 1).words : 0;
                const auto previous_errors = i ? history.chronological(i - 1).uncorrectable : 0;
                clock_view.points[i] = {static_cast<double>(current.words), double(current.uncorrectable - previous_errors) / double(current.words - previous_words)};
            }
        }
        else if (reference_view)
        {
            const auto& history = state.decoder.ofdm_phase;
            clock_view.count = std::min(history.size, clock_view.points.size());
            clock_view.source = history.source;
            clock_view.updated_ms = history.last_update_ms;
            for (size_t i = 0; i < clock_view.count; ++i)
            {
                const auto index = clock_view.count > 1 ? i * (history.size - 1) / (clock_view.count - 1) : 0;
                const auto& sample = history.chronological(index);
                if (!sample.has_reference)
                {
                    clock_view.count = 0;
                    break;
                }
                clock_view.points[i] = {static_cast<double>(sample.symbol), sample.reference_ber};
            }
        }
        const auto& value = clock || fec_view || reference_view ? clock_view : state.decoder.ofdm_signal[kind - 1];
        const bool available = value.count >= 2;
        const bool scatter = !clock && !fec_view && !reference_view && kind == 2;
        hide(signal_empty, available);
        hide(signal_line, !available || scatter);
        for (auto* dot : signal_dots) hide(dot, true);
        if (!available)
        {
            text(signal_title, "Measured signal unavailable");
            return;
        }
        lv_obj_update_layout(analysis);
        double xmin = value.points[0].x, xmax = xmin, ymin = value.points[0].y, ymax = ymin;
        for (size_t i = 0; i < value.count; ++i)
        {
            xmin = std::min(xmin, value.points[i].x);
            xmax = std::max(xmax, value.points[i].x);
            ymin = std::min(ymin, value.points[i].y);
            ymax = std::max(ymax, value.points[i].y);
        }
        if (scatter)
        {
            xmin = ymin = -2;
            xmax = ymax = 2;
        }
        else if (fec_view || reference_view || (!clock && kind == 4))
        {
            ymin = 0;
            ymax = 1;
        }
        else
        {
            const double padding = std::max(1., (ymax - ymin) * .1);
            ymin -= padding;
            ymax += padding;
        }
        const double width = std::max<int32_t>(1, lv_obj_get_content_width(signal_graph) - 4);
        const double height = std::max<int32_t>(1, lv_obj_get_content_height(signal_graph) - 4);
        const double plot_width = scatter ? std::min(width, height) : width;
        const double plot_height = scatter ? std::min(width, height) : height;
        const double left = (width - plot_width) / 2, top = (height - plot_height) / 2;
        for (size_t i = 0; i < value.count; ++i)
        {
            const auto x = static_cast<lv_value_precise_t>(1 + left + (value.points[i].x - xmin) / std::max(1e-9, xmax - xmin) * plot_width);
            const auto y = static_cast<lv_value_precise_t>(1 + top + (ymax - value.points[i].y) / std::max(1e-9, ymax - ymin) * plot_height);
            signal_points[i] = {x, y};
            if (scatter)
            {
                lv_obj_set_pos(signal_dots[i], x, y);
                hide(signal_dots[i], value.points[i].x < xmin || value.points[i].x > xmax || value.points[i].y < ymin || value.points[i].y > ymax);
            }
        }
        if (!scatter) lv_line_set_points(signal_line, signal_points.data(), value.count);
        char buffer[256];
        const char* freshness = state.active && value.fresh(now) ? "updating" : "retained/stale";
        if (scatter && state.source.synthetic && state.decoder.ofdm_phase.size && state.decoder.ofdm_phase.newest().has_reference)
            std::snprintf(buffer, sizeof(buffer), "Known-reference data I/Q / EVM %.2f%% / %s", value.metric * 100, freshness);
        else if (scatter)
            std::snprintf(buffer, sizeof(buffer), "Equalized I/Q [-2,+2] / DD EVM %.2f%% / up to %u symbols (pilots) / %s", value.metric * 100, state.experiment.evm_window, freshness);
        else
            std::snprintf(buffer, sizeof(buffer), "%s / x %.3g..%.3g / y %.3g..%.3g / %s",
                          clock ? "Clock loop: symbol / ppm" : fec_view     ? "RS uncorrectable-word fraction (not BER)"
                                                           : reference_view ? "Known-reference uncoded BER"
                                                           : kind == 5      ? "Channel unwrapped phase / rad (fades unreliable)"
                                                           : kind == 1      ? "Post-filter Hz / dBFS/bin"
                                                           : kind == 3      ? "Channel carrier / dB"
                                                                            : "CP offset / normalized correlation",
                          xmin, xmax, ymin, ymax, freshness);
        text(signal_title, buffer);
    }
};
OneSegOfdmWorkbench::OneSegOfdmWorkbench(lv_obj_t* parent, lv_group_t* group, Callbacks callbacks) : impl_(std::make_unique<Impl>(parent, group, std::move(callbacks))) {}
OneSegOfdmWorkbench::~OneSegOfdmWorkbench()
{
    if (impl_->root) lv_obj_delete(impl_->root);
}
lv_obj_t* OneSegOfdmWorkbench::videoHost() const { return impl_->video; }
lv_obj_t* OneSegOfdmWorkbench::object() const { return impl_->root; }
void OneSegOfdmWorkbench::update(const OneSegTvSession::Snapshot& state) { impl_->update(state); }
void OneSegOfdmWorkbench::notice(const char* value) { text(impl_->message, value); }
void OneSegOfdmWorkbench::selectStage(OneSegOfdmStage stage)
{
    if (static_cast<unsigned>(stage) >= impl_->stage_buttons.size()) return;
    impl_->stage = stage;
    impl_->rebuild();
    lv_group_focus_obj(impl_->stage_buttons[static_cast<unsigned>(stage)]);
}
void OneSegOfdmWorkbench::setRecordedSource(bool recorded)
{
    for (auto* object : {impl_->rate, impl_->clock, impl_->gain})
        if (recorded) lv_obj_add_state(object, LV_STATE_DISABLED);
        else lv_obj_remove_state(object, LV_STATE_DISABLED);
}
void OneSegOfdmWorkbench::setWorking(const OneSegReceptionSettings& reception, const OneSegOfdmExperiment& experiment)
{
    auto& self = *impl_;
    self.reception = reception;
    self.experiment = experiment;
    lv_dropdown_set_selected(self.rate, reception.sample_rate_hz == 500000 ? 1 : 0);
    lv_dropdown_set_selected(self.clock, reception.spi_clock_hz == 8350000 ? 0 : reception.spi_clock_hz == 8700000 ? 1
                                                                                                                   : 2);
    lv_dropdown_set_selected(self.gain, reception.gain_step);
    self.rebuild();
}
void OneSegOfdmWorkbench::setSyntheticSource(bool enabled)
{
    auto& self = *impl_;
    if (self.synthetic == enabled) return;
    self.synthetic = enabled;
    hide(self.radio_rf, enabled);
    hide(self.synthetic_rf, !enabled);
    self.rebuild();
}
void OneSegOfdmWorkbench::fullscreen(bool enabled)
{
    auto& self = *impl_;
    for (auto* object : {self.ribbon, self.parameters, self.analysis, self.phase_panel, self.residual_panel}) hide(object, enabled);
    lv_obj_set_grid_dsc_array(self.grid, enabled ? self.full.data() : self.columns.data(), enabled ? self.full.data() : self.rows.data());
    lv_obj_set_grid_cell(self.video, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
}
} // namespace platform::linux_runtime
