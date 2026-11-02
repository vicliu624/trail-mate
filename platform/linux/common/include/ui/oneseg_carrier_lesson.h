#pragma once
#include "platform/linux/oneseg_ofdm_carrier_observations.h"
#include "platform/linux/oneseg_tv_session.h"
#include "ui/ui_theme.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <lvgl.h>
#include <string>
#include <utility>
#include <vector>

namespace platform::linux_runtime
{
// A view of immutable observations plus a separately labelled four-tone probe.
// None of its controls mutate receiver settings or the qualified factory preset.
class OneSegCarrierLesson
{
  public:
    OneSegCarrierLesson(lv_obj_t* analysis, lv_obj_t* waves, lv_obj_t* selected, lv_group_t* group) : group_(group)
    {
        parents_ = {analysis, waves, selected};
        for (unsigned i = 0; i < roots_.size(); ++i)
        {
            roots_[i] = lv_obj_create(parents_[i]);
            lv_obj_remove_style_all(roots_[i]);
            lv_obj_set_size(roots_[i], lv_pct(100), lv_pct(100));
            lv_obj_add_flag(roots_[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_add_flag(roots_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(roots_[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(roots_[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_flex_flow(roots_[i], LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(roots_[i], i == 0 ? 2 : 3, 0);
        }
        auto* bar = row(roots_[0], 25);
        title_ = label(bar, "Subcarrier grid");
        lv_obj_set_flex_grow(title_, 1);
        lv_obj_set_width(title_, 0);
        lv_label_set_long_mode(title_, LV_LABEL_LONG_CLIP);
        palette_control_ = lv_dropdown_create(bar);
        lv_obj_set_size(palette_control_, 102, 24);
        lv_obj_set_style_text_font(palette_control_, &lv_font_montserrat_12, 0);
        lv_dropdown_set_options(palette_control_, "Roles\nH amplitude\nH phase");
        lv_group_add_obj(group_, palette_control_);
        lv_obj_add_event_cb(
            palette_control_, [](lv_event_t* e)
            { auto& s = owner(e); s.palette_ = lv_dropdown_get_selected(s.palette_control_); s.dirty_ = true; },
            LV_EVENT_VALUE_CHANGED, this);
        hold_button_ = lv_button_create(bar);
        lv_obj_set_size(hold_button_, 49, 24);
        lv_obj_set_style_pad_all(hold_button_, 2, 0);
        lv_obj_center(label(hold_button_, "Hold"));
        lv_group_add_obj(group_, hold_button_);
        lv_obj_add_event_cb(
            hold_button_, [](lv_event_t* e)
            { auto& s = owner(e); s.hold_ = !s.hold_; s.dirty_ = true; },
            LV_EVENT_CLICKED, this);
        geometry_label_ = label(roots_[0], "");
        legend_ = label(roots_[0], "");
        canvas_ = lv_canvas_create(roots_[0]);
        lv_canvas_set_buffer(canvas_, pixels_.data(), 432, grid_height_, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_size(canvas_, lv_pct(100), grid_height_);
        lv_image_set_inner_align(canvas_, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_add_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(
            canvas_, [](lv_event_t* e)
            {
            auto& s = owner(e);
            auto* input = lv_indev_active();
            if (!input || !s.geometry_.valid()) return;
            lv_point_t p; lv_indev_get_point(input, &p);
            lv_area_t area; lv_obj_get_coords(s.canvas_, &area);
            const int width = std::max(1, int(lv_obj_get_width(s.canvas_)));
            s.values_[0] = std::clamp<int>((p.x-area.x1)*s.geometry_.active_carriers/width, 0, s.geometry_.active_carriers-1);
            s.values_[1] = std::clamp<int>((p.y-area.y1)/grid_row_height_, 0, 3);
            s.dirty_ = true; },
            LV_EVENT_CLICKED, this);
        selected_label_ = label(roots_[0], "");
        context_ = label(roots_[0], "13-segment context: [------ CENTER ------]\nOnly the center segment is observed; other segments are not captured.");
        lv_obj_set_style_text_opa(context_, LV_OPA_70, 0);
        flow_ = label(roots_[0], "TX: bits -> QAM -> IFFT -> CP -> channel\nRX: CP removal -> FFT -> pilots/H -> EQ -> demap/FEC");
        capacity_ = label(roots_[0], "CAPACITY PREVIEW / not measured programme bitrate");
        lv_obj_set_style_text_opa(capacity_, LV_OPA_70, 0);

        model_title_ = label(roots_[1], "MODEL PREVIEW / four-tone probe");
        auto* charts = row(roots_[1], 0);
        lv_obj_set_flex_grow(charts, 1);
        for (unsigned plot = 0; plot < 2; ++plot)
        {
            auto* column = lv_obj_create(charts);
            lv_obj_remove_style_all(column);
            lv_obj_set_size(column, 0, lv_pct(100));
            lv_obj_set_flex_grow(column, 1);
            lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
            plot_titles_[plot] = label(column, plot ? "Frequency / relative FFT bins" : "Time / useful-symbol fraction");
            plots_[plot] = box(column);
            lv_obj_set_width(plots_[plot], lv_pct(100));
            lv_obj_set_flex_grow(plots_[plot], 1);
            if (!plot)
                for (auto& shade : time_shades_)
                {
                    shade = lv_obj_create(plots_[plot]);
                    lv_obj_remove_style_all(shade);
                    lv_obj_set_style_bg_color(shade, ::ui::theme::accent(), 0);
                    lv_obj_set_style_bg_opa(shade, LV_OPA_20, 0);
                    lv_obj_remove_flag(shade, LV_OBJ_FLAG_CLICKABLE);
                }
            if (plot)
                for (unsigned i = 0; i < frequency_grid_.size(); ++i)
                {
                    frequency_grid_[i] = lv_line_create(plots_[plot]);
                    lv_obj_set_style_line_color(frequency_grid_[i], ::ui::theme::border(), 0);
                    lv_obj_set_style_line_width(frequency_grid_[i], 1, 0);
                    frequency_ticks_[i] = label(plots_[plot], "");
                    lv_obj_set_width(frequency_ticks_[i], 24);
                }
            for (unsigned line = 0; line < 4; ++line)
            {
                lines_[plot][line] = lv_line_create(plots_[plot]);
                lv_obj_set_style_line_color(lines_[plot][line], lv_color_hex(colors_[line]), 0);
                lv_obj_set_style_line_width(lines_[plot][line], 1, 0);
            }
        }
        model_note_ = label(roots_[1], "");
        selected_title_ = label(roots_[2], "Selected complex symbol");
        selected_body_ = row(roots_[2], 0);
        lv_obj_set_flex_grow(selected_body_, 1);
        iq_plot_ = box(selected_body_);
        lv_obj_set_size(iq_plot_, 112, 112);
        axis_points_[0] = {{{4, 54}, {105, 54}}};
        axis_points_[1] = {{{54, 4}, {54, 105}}};
        for (auto& points : axis_points_)
        {
            auto* axis = lv_line_create(iq_plot_);
            axes_[&points - axis_points_.data()] = axis;
            lv_obj_set_style_line_color(axis, ::ui::theme::border(), 0);
            lv_line_set_points(axis, points.data(), 2);
        }
        for (unsigned i = 0; i < vectors_.size(); ++i)
        {
            vectors_[i] = lv_line_create(iq_plot_);
            lv_obj_set_style_line_color(vectors_[i], lv_color_hex(colors_[i]), 0);
            lv_obj_set_style_line_width(vectors_[i], 1, 0);
        }
        for (unsigned i = 0; i < dots_.size(); ++i)
        {
            dots_[i] = lv_obj_create(iq_plot_);
            lv_obj_set_size(dots_[i], 7, 7);
            lv_obj_set_style_bg_color(dots_[i], lv_color_hex(colors_[i]), 0);
            lv_obj_set_style_border_width(dots_[i], 0, 0);
            lv_obj_set_style_radius(dots_[i], LV_RADIUS_CIRCLE, 0);
        }
        auto* details = lv_obj_create(selected_body_);
        lv_obj_remove_style_all(details);
        lv_obj_set_size(details, 0, lv_pct(100));
        lv_obj_set_flex_grow(details, 1);
        lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
        iq_text_ = label(details, "");
        phase_text_ = label(details, "");
        selected_note_ = label(roots_[2], "");
    }

    void clearControls()
    {
        widgets_.fill(nullptr);
        for (auto& buttons : step_buttons_) buttons.fill(nullptr);
    }
    void reset()
    {
        values_ = {217, 0, 0, 0, 100, 16, 50, 2, 2};
        hold_ = false;
        palette_ = 0;
        lv_dropdown_set_selected(palette_control_, 0);
        dirty_ = true;
    }
    void controls(lv_obj_t* parent)
    {
        clearControls();
        constexpr const char* names[]{"Carrier index", "Symbol row", "Dataset", "Lesson", "CFO model Hz", "Delay / shift", "Echo % amp", "FFT / mode", "GI model"};
        constexpr const char* options[]{nullptr, "Row 0\nRow 1\nRow 2\nRow 3", "Actual burst\nISDBT model\nGeneric model",
                                        "Orthogonality\nCFO / ICI\nEcho / CP\nTiming offset", nullptr, nullptr, nullptr,
                                        "Mode 1 / 256\nMode 2 / 512\nMode 3 / 576\nMode 3 / 1024", "1/4\n1/8\n1/16\n1/32"};
        constexpr int low[]{0, 0, 0, 0, -4000, -512, 0, 0, 0}, high[]{431, 3, 2, 3, 4000, 512, 150, 3, 3};
        for (unsigned i = 0; i < widgets_.size(); ++i)
        {
            bindings_[i] = {this, i};
            auto* cell = lv_obj_create(parent);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, lv_pct(32), 47);
            label(cell, names[i]);
            auto* control = options[i] ? lv_dropdown_create(cell) : lv_spinbox_create(cell);
            widgets_[i] = control;
            lv_obj_set_style_text_font(control, &lv_font_montserrat_12, 0);
            lv_obj_set_style_pad_all(control, 3, 0);
            lv_obj_set_size(control, options[i] ? lv_pct(100) : 66, 28);
            lv_obj_align(control, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            if (options[i])
            {
                lv_dropdown_set_options(control, options[i]);
                lv_dropdown_set_selected(control, values_[i]);
            }
            else
            {
                lv_spinbox_set_digit_format(control, i == 4 || i == 5 ? 5 : 4, 0);
                lv_spinbox_set_range(control, low[i], high[i]);
                lv_spinbox_set_step(control, i == 4 ? 10 : 1);
                lv_spinbox_set_value(control, values_[i]);
                for (int direction : {-1, 1})
                {
                    auto* b = lv_button_create(cell);
                    step_buttons_[i][direction > 0 ? 1 : 0] = b;
                    lv_obj_set_size(b, 17, 28);
                    lv_obj_set_style_pad_all(b, 0, 0);
                    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, direction < 0 ? -18 : 0, 0);
                    lv_obj_center(label(b, direction < 0 ? "-" : "+"));
                    if (direction < 0) lv_obj_add_event_cb(
                        b, [](lv_event_t* e)
                        {
                        auto* spin = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
                        if (lv_obj_has_state(spin, LV_STATE_DISABLED)) return;
                        lv_spinbox_decrement(spin); lv_obj_send_event(spin, LV_EVENT_VALUE_CHANGED, nullptr); },
                        LV_EVENT_CLICKED, control);
                    if (direction > 0) lv_obj_add_event_cb(
                        b, [](lv_event_t* e)
                        {
                        auto* spin = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
                        if (lv_obj_has_state(spin, LV_STATE_DISABLED)) return;
                        lv_spinbox_increment(spin); lv_obj_send_event(spin, LV_EVENT_VALUE_CHANGED, nullptr); },
                        LV_EVENT_CLICKED, control);
                    lv_group_add_obj(group_, b);
                }
            }
            lv_group_add_obj(group_, control);
            lv_obj_add_event_cb(
                control, [](lv_event_t* e)
                {
                auto& b = *static_cast<Binding*>(lv_event_get_user_data(e));
                auto& s = *b.self;
                if (s.syncing_) return;
                auto* object = static_cast<lv_obj_t*>(lv_event_get_target(e));
                s.values_[b.index] = b.index == 0 || b.index == 4 || b.index == 5 || b.index == 6 ? lv_spinbox_get_value(object) : lv_dropdown_get_selected(object);
                s.dirty_ = true; },
                LV_EVENT_VALUE_CHANGED, &bindings_[i]);
        }
        dirty_ = true;
    }
    void show(bool enabled)
    {
        if (visible_ == enabled) return;
        if (enabled)
        {
            hidden_.clear();
            for (unsigned i = 0; i < parents_.size(); ++i)
                for (unsigned j = 0; j < lv_obj_get_child_count(parents_[i]); ++j)
                {
                    auto* child = lv_obj_get_child(parents_[i], j);
                    if (child == roots_[i]) continue;
                    hidden_.push_back({child, lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)});
                    lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                }
        }
        else
            for (const auto& item : hidden_)
                if (!item.second) lv_obj_remove_flag(item.first, LV_OBJ_FLAG_HIDDEN);
        for (auto* root : roots_)
            if (enabled) lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
        visible_ = enabled;
        dirty_ = true;
    }
    void update(const OneSegTvSession::Snapshot& state, const OneSegOfdmExperiment& preview)
    {
        // Session and native receive-worker generations are distinct counters.
        const bool new_generation = generation_ != state.generation || decoder_generation_ != state.decoder.generation;
        const bool new_source = source_initialized_ && (source_path_ != state.source.archive_path || source_synthetic_ != state.source.synthetic);
        if (new_generation || new_source)
        {
            blocked_generation_ = new_source && !new_generation ? state.generation : uint64_t(-1);
            generation_ = state.generation;
            decoder_generation_ = state.decoder.generation;
            hold_ = false;
            burst_.reset();
            phase_mask_ = 0;
            dirty_ = true;
        }
        source_path_ = state.source.archive_path;
        source_synthetic_ = state.source.synthetic;
        source_initialized_ = true;
        auto expected_source = state.source.synthetic ? OneSegLabSource::Synthetic : state.source.archive_path.empty() ? OneSegLabSource::LiveReceiver
                                                                                                                       : OneSegLabSource::RecordedIq;
        // Offline radio fixtures can explicitly tag the decoder as RecordedIq.
        // Preserve that trusted producer provenance rather than calling it live.
        if (!state.source.synthetic && state.source.archive_path.empty() &&
            (state.decoder.ofdm_phase.source == OneSegLabSource::RecordedIq ||
             (state.decoder.native_configuration.available && state.decoder.native_configuration.source == OneSegLabSource::RecordedIq))) expected_source = OneSegLabSource::RecordedIq;
        auto incoming = state.decoder.ofdm_carriers;
        if (blocked_generation_ == generation_ || (incoming && incoming->source != expected_source)) incoming.reset();
        if ((!hold_ || !burst_) && burst_ != incoming)
        {
            burst_ = incoming;
            phase_mask_ = 0;
            dirty_ = true;
        }
        if (burst_ && state.decoder.ofdm_phase.source == burst_->source)
            for (unsigned row = 0; row < 4; ++row)
                for (size_t i = 0; i < state.decoder.ofdm_phase.size; ++i)
                {
                    const auto& p = state.decoder.ofdm_phase.chronological(i);
                    if (p.symbol == burst_->symbols[row].metadata.sequence)
                    {
                        if (!(phase_mask_ & (1u << row))) dirty_ = true;
                        phases_[row] = p;
                        phase_mask_ |= 1u << row;
                    }
                }
        const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        const uint64_t age = burst_ && now >= burst_->updated_ms ? (now - burst_->updated_ms) / 1000 : 0;
        const bool fresh = burst_ && burst_->fresh(now) && state.active;
        if (model_qam_ != preview.constellation || model_rate_ != preview.code_rate_index)
        {
            model_qam_ = preview.constellation;
            model_rate_ = preview.code_rate_index;
            dirty_ = true;
        }
        if (fresh != fresh_ || preview_floor_ != preview.equalizer_floor)
        {
            fresh_ = fresh;
            preview_floor_ = preview.equalizer_floor;
            dirty_ = true;
        }
        if (age != age_)
        {
            age_ = age;
            dirty_ = true;
        }
        if (!visible_ || !dirty_) return;
        dirty_ = false;
        actual_ = values_[2] == 0 && burst_ && burst_->source != OneSegLabSource::Unavailable && burst_->symbols[0].metadata.valid();
        generic_ = actual_ ? burst_->symbols[0].metadata.layout == 1 : values_[2] == 2 || (values_[2] == 0 && state.source.synthetic);
        if (actual_)
        {
            const auto& m = burst_->symbols[0].metadata;
            geometry_ = {m.mode, m.fft, m.cp, m.count, m.sample_rate};
        }
        else geometry_ = OneSegCarrierGeometry::centerSegment(values_[7] < 2 ? values_[7] + 1 : 3, 1u << (values_[8] + 2), values_[7] == 2);
        values_[0] = std::clamp<int>(values_[0], 0, geometry_.active_carriers - 1);
        syncing_ = true;
        if (widgets_[0])
        {
            lv_spinbox_set_range(widgets_[0], 0, geometry_.active_carriers - 1);
            lv_spinbox_set_value(widgets_[0], values_[0]);
        }
        if (widgets_[1]) lv_dropdown_set_selected(widgets_[1], values_[1]);
        if (widgets_[7]) lv_dropdown_set_selected(widgets_[7], actual_ ? geometry_.mode < 3 ? geometry_.mode - 1 : geometry_.fft_length == 576 ? 2
                                                                                                                                               : 3
                                                                       : values_[7]);
        if (widgets_[8]) lv_dropdown_set_selected(widgets_[8], actual_ ? unsigned(std::lround(std::log2(double(geometry_.fft_length) / geometry_.cp_length))) - 2 : values_[8]);
        if (widgets_[5])
        {
            if (values_[3] == 2) values_[5] = std::max(0, values_[5]);
            lv_spinbox_set_range(widgets_[5], values_[3] == 2 ? 0 : -512, 512);
            lv_spinbox_set_value(widgets_[5], values_[5]);
        }
        for (unsigned i : {4u, 5u, 6u, 7u, 8u})
            if (widgets_[i])
            {
                const bool disabled = (i == 4 && values_[3] != 1) || (i == 5 && values_[3] < 2) || (i == 6 && values_[3] != 2) || (i >= 7 && actual_);
                if (disabled) lv_obj_add_state(widgets_[i], LV_STATE_DISABLED);
                else lv_obj_remove_state(widgets_[i], LV_STATE_DISABLED);
                for (auto* b : step_buttons_[i])
                    if (b)
                    {
                        if (disabled) lv_obj_add_state(b, LV_STATE_DISABLED);
                        else lv_obj_remove_state(b, LV_STATE_DISABLED);
                    }
            }
        syncing_ = false;
        if (hold_) lv_obj_add_state(hold_button_, LV_STATE_CHECKED);
        else lv_obj_remove_state(hold_button_, LV_STATE_CHECKED);
        paint(preview_floor_);
    }

  private:
    struct Binding
    {
        OneSegCarrierLesson* self = nullptr;
        unsigned index = 0;
    };
    static OneSegCarrierLesson& owner(lv_event_t* e) { return *static_cast<OneSegCarrierLesson*>(lv_event_get_user_data(e)); }
    static lv_obj_t* label(lv_obj_t* parent, const char* text)
    {
        auto* object = lv_label_create(parent);
        lv_label_set_text(object, text);
        lv_obj_set_style_text_font(object, &lv_font_montserrat_12, 0);
        lv_obj_set_width(object, lv_pct(100));
        return object;
    }
    static void text(lv_obj_t* object, const char* value)
    {
        if (std::strcmp(lv_label_get_text(object), value)) lv_label_set_text(object, value);
    }
    static lv_obj_t* row(lv_obj_t* parent, int height)
    {
        auto* object = lv_obj_create(parent);
        lv_obj_remove_style_all(object);
        lv_obj_set_size(object, lv_pct(100), height);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(object, 5, 0);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        return object;
    }
    static lv_obj_t* box(lv_obj_t* parent)
    {
        auto* object = lv_obj_create(parent);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_border_width(object, 1, 0);
        lv_obj_set_style_border_color(object, ::ui::theme::border(), 0);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        return object;
    }
    static const char* roleName(OneSegCarrierRole role)
    {
        constexpr const char* names[]{"unused", "data", "SP", "TMCC", "AC", "reference pilot"};
        return names[unsigned(role)];
    }
    int bin(unsigned carrier) const { return generic_ ? geometry_.genericSignedBin(carrier) : geometry_.signedBin(carrier); }
    OneSegCarrierRole role(unsigned carrier, unsigned row) const
    {
        const unsigned phase = actual_ ? burst_->symbols[row].metadata.phase : row;
        return generic_ ? geometry_.genericRole(carrier) : geometry_.role(carrier, phase);
    }
    OneSegFourToneProbe probe() const
    {
        OneSegFourToneProbe p;
        p.geometry = geometry_;
        p.selected_bin = bin(values_[0]);
        p.symbol_index = values_[1];
        p.epsilon = values_[3] == 1 ? values_[4] / geometry_.spacingHz() : 0;
        p.delay = values_[3] == 2 ? std::max(0, values_[5]) : 0;
        p.echo_gain = values_[3] == 2 ? values_[6] / 100. : 0;
        p.window_shift = values_[3] == 3 ? values_[5] : 0;
        return p;
    }
    std::complex<double> channel(unsigned carrier, unsigned row) const
    {
        if (actual_)
        {
            const auto& v = burst_->symbols[row].values[carrier].values;
            return {v[4], v[5]};
        }
        auto p = probe();
        p.selected_bin = bin(carrier);
        return p.diagonalChannel();
    }
    static uint16_t rgb(uint32_t c) { return uint16_t(((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31)); }
    void paint(double floor)
    {
        char line[640];
        const char* origin = actual_ ? burst_->source == OneSegLabSource::Synthetic ? "Synthetic" : burst_->source == OneSegLabSource::RecordedIq ? "Recorded IQ"
                                                                                                                                                  : "Live"
                                     : "layout";
        std::snprintf(line, sizeof(line), "%s / %s / %s", actual_ ? "ACTUAL" : "MODEL", origin, actual_           ? hold_ ? "held" : fresh_ ? "updating"
                                                                                                                                            : "retained/stale"
                                                                                                : values_[2] == 0 ? "no observed burst"
                                                                                                                  : "preview");
        if (actual_) std::snprintf(line, sizeof(line), "ACTUAL / %s / %s / %llus", origin, hold_ ? "held" : fresh_ ? "updating"
                                                                                                                   : "retained/stale",
                                   (unsigned long long)age_);
        text(title_, line);
        std::snprintf(line, sizeof(line), "Mode %u / receiver FFT %u / active %u / %s\nFsFFT %.3fkHz; df=Fs/N %.3fHz; Tu=N/Fs %.1fus; CP %.1fus (%u samples)",
                      geometry_.mode, geometry_.fft_length, geometry_.active_carriers, generic_ ? "generic DC-null" : "ISDB-T center segment",
                      geometry_.sample_rate / 1000, geometry_.spacingHz(), geometry_.usefulSeconds() * 1e6, geometry_.guardSeconds() * 1e6, geometry_.cp_length);
        text(geometry_label_, line);
        text(legend_, palette_ == 1 ? "H amplitude: -40..+20 dB relative (clipped color scale)" : palette_ == 2 ? "arg(H): -pi..+pi wrapped; deep fades uncoloured"
                                                                                              : generic_        ? "Data blue / fixed reference pilots amber / DC unused"
                                                                                                                : "Data blue / SP amber / TMCC purple / AC teal");
        if (!palette_)
        {
            const unsigned factor = geometry_.active_carriers / 108;
            if (generic_) std::snprintf(line, sizeof(line), "Data %u (blue) / reference pilots %u (amber) / DC unused", 99 * factor, 9 * factor);
            else std::snprintf(line, sizeof(line), "Data %u (blue) / SP %u (amber) / TMCC %u (purple) / AC %u (teal)", 96 * factor, 9 * factor, factor, 2 * factor);
            text(legend_, line);
        }
        constexpr uint32_t role_colors[]{0xd7d0c0, 0x5b80a4, 0xeba341, 0x9c6ade, 0x0a8b7a, 0xeba341};
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned x = 0; x < 432; ++x)
            {
                const unsigned carrier = x * geometry_.active_carriers / 432;
                uint32_t color = role_colors[unsigned(role(carrier, row))];
                if (palette_)
                {
                    const auto h = channel(carrier, row);
                    if (palette_ == 2 && std::norm(h) < 1e-12) color = 0xd7d0c0;
                    else
                    {
                        const double value = palette_ == 1 ? (20 * std::log10(std::max(1e-12, std::abs(h))) + 40) / 60 : (std::arg(h) + 3.141592653589793) / 6.283185307179586;
                        const unsigned level = unsigned(std::clamp(value, 0., 1.) * 255);
                        color = ((255 - level) << 16) | (level << 8) | 0x70;
                    }
                }
                for (unsigned y = 0; y < grid_row_height_; ++y)
                    pixels_[(row * grid_row_height_ + y) * 432 + x] = rgb(carrier == unsigned(values_[0]) || (row == unsigned(values_[1]) && (y == 0 || y == grid_row_height_ - 1)) ? 0x3a2a1a : color);
            }
        lv_obj_invalidate(canvas_);
        const auto selected_role = role(values_[0], values_[1]);
        const unsigned phase = actual_ ? burst_->symbols[values_[1]].metadata.phase : values_[1];
        std::snprintf(line, sizeof(line), "Row %d / active index %d / signed k=%d / %+.2f Hz\n%s / %s phase %u%s; grid covers active carriers only",
                      values_[1], values_[0], bin(values_[0]), bin(values_[0]) * geometry_.spacingHz(), roleName(selected_role), generic_ ? "fixed pilot layout; virtual" : "SP", phase,
                      actual_ && !generic_ && !(burst_->symbols[values_[1]].metadata.flags & 1) ? " (unverified)" : "");
        text(selected_label_, line);
        if (generic_) std::snprintf(line, sizeof(line), "Generic reference OFDM: no native TMCC/AC or TV.\n%u unused FFT bins, including DC (not observed here).", geometry_.fft_length - geometry_.active_carriers);
        else std::snprintf(line, sizeof(line), "ISDB-T MODEL: 6MHz / 13 used segments; center nominal %.2fkHz.\nBroadcast FFT %u; %u unused Rx FFT bins not captured.", geometry_.active_carriers * geometry_.spacingHz() / 1000, geometry_.broadcastFftLength(), geometry_.fft_length - geometry_.active_carriers);
        text(context_, line);
        const double ts = geometry_.usefulSeconds() + geometry_.guardSeconds();
        if (generic_) std::snprintf(line, sizeof(line), "TX: known QAM/pilots -> IFFT -> CP -> channel\nRX: remove CP -> FFT -> H/EQ -> uncoded bits; CP %.2f%%", 100 * geometry_.guardSeconds() / ts);
        else std::snprintf(line, sizeof(line), "TX: bits/FEC -> QAM -> interleave/pilots -> IFFT -> CP\nRX: remove CP -> FFT -> H/EQ -> demap/FEC; CP %.2f%%", 100 * geometry_.guardSeconds() / ts);
        text(flow_, line);
        if ((model_qam_ == 4 || model_qam_ == 16 || model_qam_ == 64) && model_rate_ < 5)
        {
            constexpr double rates[]{.5, 2. / 3, .75, 5. / 6, 7. / 8};
            const unsigned data = generic_ ? geometry_.active_carriers * 11 / 12 : geometry_.active_carriers * 8 / 9;
            const double bits = std::log2(double(model_qam_));
            const double gross = data * bits / ts / 1000;
            std::snprintf(line, sizeof(line), "CAPACITY PREVIEW: M=%u; %u data * %.0f bits / Ts %.1fus\nGross %.1fkb/s; %s", model_qam_, data, bits, ts * 1e6, gross, generic_ ? "generic source has no FEC or TV." : "coding factors below; not measured bitrate.");
            if (!generic_) std::snprintf(line, sizeof(line), "CAPACITY PREVIEW: M=%u; %u * %.0f / Ts %.1fus = %.1fkb/s\nR=%.3g and RS188/204 -> %.1fkb/s (not measured bitrate).", model_qam_, data, bits, ts * 1e6, gross, rates[model_rate_], gross * rates[model_rate_] * 188 / 204);
            text(capacity_, line);
        }
        else text(capacity_, "CAPACITY PREVIEW: unsupported modulation/code parameters.");
        auto p = probe();
        const bool beyond = p.delay > geometry_.cp_length || p.window_shift > 0 || p.window_shift < -int(geometry_.cp_length);
        if (values_[3] == 0) std::snprintf(line, sizeof(line), "MODEL PREVIEW: x[n]=sum X[k] exp(j2pi*k*n/N); selected k=%d", p.selected_bin);
        else if (values_[3] == 1) std::snprintf(line, sizeof(line), "MODEL PREVIEW: D_N(x)=sum exp(j2pi*x*n/N)/N; epsilon=%.4f", p.epsilon);
        else if (values_[3] == 2) std::snprintf(line, sizeof(line), "MODEL PREVIEW: y[n]=x[n]+a*x[n-delay]; delay %.0f / CP %u", p.delay, geometry_.cp_length);
        else std::snprintf(line, sizeof(line), "MODEL PREVIEW: FFT window uses y[n+shift]; shift %.0f / CP %u", p.window_shift, geometry_.cp_length);
        text(model_title_, line);
        lv_obj_update_layout(roots_[1]);
        double time_extent = 1;
        std::array<double, 129> wave{};
        for (unsigned i = 0; i < wave.size(); ++i)
        {
            const double time = -double(geometry_.cp_length) + double(i) / 128 * (geometry_.fft_length + geometry_.cp_length - 1);
            wave[i] = p.relativeSample(time).real();
            time_extent = std::max(time_extent, std::abs(wave[i]));
        }
        std::snprintf(line, sizeof(line), "Re(y) +/-%.2g / time -Tg..Tu / shaded CP", time_extent);
        text(plot_titles_[0], line);
        const double xmin = std::min(-3.5, p.epsilon - 3.5), xmax = std::max(3.5, p.epsilon + 3.5);
        std::array<double, 144> frequencies{};
        size_t frequency_count = 0;
        for (unsigned i = 0; i < 129; ++i) frequencies[frequency_count++] = xmin + (xmax - xmin) * i / 128;
        // Include exact receiver-bin centers so orthogonal zeros are not hidden
        // by interpolation between uniformly sampled display points.
        for (int k = int(std::ceil(xmin)); k <= int(std::floor(xmax)) && frequency_count < frequencies.size(); ++k) frequencies[frequency_count++] = k;
        std::sort(frequencies.begin(), frequencies.begin() + frequency_count);
        std::snprintf(line, sizeof(line), "Basis / bins %.1f..%+.1f; integer guides", xmin, xmax);
        text(plot_titles_[1], line);
        for (unsigned plot = 0; plot < 2; ++plot)
        {
            const int w = std::max(2, int(lv_obj_get_content_width(plots_[plot]))), h = std::max(2, int(lv_obj_get_content_height(plots_[plot])));
            const int graph_h = plot ? std::max(2, h - 14) : h;
            if (plot)
                for (unsigned i = 0; i < frequency_grid_.size(); ++i)
                {
                    const int k = int(std::ceil(xmin)) + int(i);
                    const bool used = k <= int(std::floor(xmax));
                    for (auto* object : {frequency_grid_[i], frequency_ticks_[i]})
                        if (used) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
                        else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
                    if (!used) continue;
                    const lv_value_precise_t x = (k - xmin) / (xmax - xmin) * (w - 1);
                    frequency_grid_points_[i] = {{{x, 0}, {x, lv_value_precise_t(graph_h - 1)}}};
                    lv_line_set_points(frequency_grid_[i], frequency_grid_points_[i].data(), 2);
                    std::snprintf(line, sizeof(line), "%d", k);
                    text(frequency_ticks_[i], line);
                    lv_obj_set_pos(frequency_ticks_[i], int(x) - 6, graph_h);
                    lv_obj_set_style_line_color(frequency_grid_[i], k == 0 ? ::ui::theme::text_muted() : ::ui::theme::border(), 0);
                }
            if (!plot)
            {
                const int prefix = std::max(1, int(double(w) * geometry_.cp_length / (geometry_.fft_length + geometry_.cp_length)));
                lv_obj_set_pos(time_shades_[0], 0, 0);
                lv_obj_set_size(time_shades_[0], prefix, h);
                lv_obj_set_pos(time_shades_[1], w - prefix, 0);
                lv_obj_set_size(time_shades_[1], prefix, h);
            }
            for (unsigned tone = 0; tone < 4; ++tone)
            {
                if (!plot && tone)
                {
                    lv_obj_add_flag(lines_[plot][tone], LV_OBJ_FLAG_HIDDEN);
                    continue;
                }
                const size_t count = plot ? frequency_count : 129;
                for (size_t i = 0; i < count; ++i)
                {
                    const double x = plot ? (frequencies[i] - xmin) / (xmax - xmin) : double(i) / 128;
                    const double v = plot ? std::abs(oneseg_carrier_projection((int(tone) - 1) + p.epsilon - frequencies[i], geometry_.fft_length))
                                          : wave[i] / time_extent;
                    points_[plot][tone][i] = {lv_value_precise_t(x * (w - 1)), lv_value_precise_t((plot ? 1 - std::clamp(v, 0., 1.) : .5 - .45 * std::clamp(v, -1., 1.)) * (graph_h - 1))};
                }
                if (!plot && tone) lv_obj_add_flag(lines_[plot][tone], LV_OBJ_FLAG_HIDDEN);
                else
                {
                    lv_obj_remove_flag(lines_[plot][tone], LV_OBJ_FLAG_HIDDEN);
                    lv_line_set_points(lines_[plot][tone], points_[plot][tone].data(), count);
                }
            }
        }
        const char* guard_note = beyond ? "CP violated: adjacent symbols enter FFT; selected sample includes ISI." : "Cyclic extension protects the chosen delay; per-symbol Y=HX is sufficient.";
        if (values_[3] == 0) std::snprintf(line, sizeof(line), "sum exp(j2pi(k-l)n/N)/N=delta[k,l]; orthogonality over the whole Tu.\nShaded prefix repeats the tail; the FFT integrates only the next N samples.");
        else if (values_[3] == 1)
        {
            const double integer = std::round(p.epsilon), fraction = p.epsilon - integer;
            std::snprintf(line, sizeof(line), "eps=CFO/df: integer %+.0f bins, fraction %+.4f; fractional ICI %.2f%%.\nInteger CFO permutes bins; fractional CFO spreads energy between them.", integer, fraction, 100 * (1 - std::norm(oneseg_carrier_projection(fraction, geometry_.fft_length))));
        }
        else std::snprintf(line, sizeof(line), "Linear symbol-boundary probe; right plot shows the FFT basis, not received PSD.\n%s", guard_note);
        text(model_note_, line);
        std::complex<double> y{}, h{}, reference{}, x{};
        bool known = true;
        if (actual_)
        {
            const auto& frame = burst_->symbols[values_[1]];
            const auto& v = frame.values[values_[0]].values;
            y = {v[0], v[1]};
            x = {v[2], v[3]};
            h = {v[4], v[5]};
            reference = {v[6], v[7]};
            known = generic_ || (selected_role == OneSegCarrierRole::ScatteredPilot && (frame.metadata.flags & 1));
        }
        else
        {
            y = p.receivedBin();
            h = p.diagonalChannel();
            reference = OneSegFourToneProbe::weight(p.symbol_index, 0);
            if (std::norm(h) + floor > 1e-12) x = y * std::conj(h) / (std::norm(h) + floor);
        }
        std::snprintf(line, sizeof(line), "%s / raw amber, equalized blue, reference green", actual_ ? "SELECTED OBSERVATION" : "SELECTED FOUR-TONE PROBE");
        text(selected_title_, line);
        lv_obj_update_layout(roots_[2]);
        const int side = std::clamp<int>(lv_obj_get_content_height(selected_body_), 48, 112);
        lv_obj_set_size(iq_plot_, side, side);
        const int center = (side - 4) / 2;
        const double span = (side - 14) / 2.;
        axis_points_[0] = {{{4, lv_value_precise_t(center)}, {lv_value_precise_t(side - 7), lv_value_precise_t(center)}}};
        axis_points_[1] = {{{lv_value_precise_t(center), 4}, {lv_value_precise_t(center), lv_value_precise_t(side - 7)}}};
        for (unsigned i = 0; i < 2; ++i) lv_line_set_points(axes_[i], axis_points_[i].data(), 2);
        const double extent = std::max({1.5, std::abs(y.real()), std::abs(y.imag()), std::abs(x.real()), std::abs(x.imag()), known ? std::abs(reference.real()) : 0., known ? std::abs(reference.imag()) : 0.}) * 1.1;
        const std::complex<double> values[]{y, x, reference};
        for (unsigned i = 0; i < 3; ++i)
        {
            if (i == 2 && !known)
            {
                lv_obj_add_flag(dots_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(vectors_[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_remove_flag(dots_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(vectors_[i], LV_OBJ_FLAG_HIDDEN);
            const lv_point_precise_t end{lv_value_precise_t(center + span * values[i].real() / extent), lv_value_precise_t(center - span * values[i].imag() / extent)};
            vector_points_[i][0] = i == 2 ? lv_point_precise_t{lv_value_precise_t(center + span * x.real() / extent), lv_value_precise_t(center - span * x.imag() / extent)} : lv_point_precise_t{lv_value_precise_t(center), lv_value_precise_t(center)};
            vector_points_[i][1] = end;
            lv_line_set_points(vectors_[i], vector_points_[i].data(), 2);
            lv_obj_set_pos(dots_[i], int(end.x) - 3, int(end.y) - 3);
        }
        const bool phase_defined = std::norm(h) > 1e-12;
        char phase_label[48];
        if (phase_defined) std::snprintf(phase_label, sizeof(phase_label), "%.4g rad", std::arg(h));
        else std::snprintf(phase_label, sizeof(phase_label), "undefined (deep fade)");
        char reference_label[120];
        if (known && std::abs(reference) > 1e-12) std::snprintf(reference_label, sizeof(reference_label), "Ref=(%.3g,%+.3g); single-sample error %.2f%%", reference.real(), reference.imag(), 100 * std::abs(x - reference) / std::abs(reference));
        else std::snprintf(reference_label, sizeof(reference_label), "Payload reference unknown; no true error/BER.");
        std::snprintf(line, sizeof(line), "I/Q +/-%.3g (equal scale)\nY=(%.3g,%+.3g)  Xeq=(%.3g,%+.3g)\n|H|=%.3g; arg=%s; lambda=%.3g\n%s", extent, y.real(), y.imag(), x.real(), x.imag(), std::abs(h), phase_label, actual_ ? burst_->symbols[values_[1]].metadata.equalizer_floor : floor, reference_label);
        text(iq_text_, line);
        if (actual_ && (phase_mask_ & (1u << values_[1])))
        {
            const auto& q = phases_[values_[1]];
            std::snprintf(line, sizeof(line), "Same output %llu / phase advance [rad/Tu]:\nforecast %.4g; corrected %.4g; residual %.4g", (unsigned long long)q.symbol, q.prediction, q.corrected, q.residual);
        }
        else std::snprintf(line, sizeof(line), "%s", actual_ ? "Matching phase update unavailable (e.g. moved CP)." : "Model probe, not a receiver loop observation.");
        text(phase_text_, line);
        text(selected_note_, actual_ && known && (selected_role == OneSegCarrierRole::ScatteredPilot || selected_role == OneSegCarrierRole::ReferencePilot) ? "Pilot fit is self-referenced: a small residual does not prove good decoding." : actual_ ? "Y before equalization; H estimated; raw/eq/reference belong to the same symbol."
                                                                                                                                                                                                                                                       : "Known four-tone probe with adjacent symbols; not an ISDB-T payload encoder.");
    }
    lv_group_t* group_;
    std::array<lv_obj_t*, 3> parents_{}, roots_{};
    lv_obj_t *title_, *geometry_label_, *legend_, *canvas_, *selected_label_, *context_, *palette_control_, *hold_button_, *flow_, *capacity_;
    lv_obj_t *model_title_, *model_note_, *selected_title_, *iq_plot_, *iq_text_, *phase_text_, *selected_note_, *selected_body_;
    std::array<lv_obj_t*, 2> plots_{}, plot_titles_{};
    std::array<lv_obj_t*, 2> time_shades_{};
    std::array<lv_obj_t*, 15> frequency_grid_{}, frequency_ticks_{};
    std::array<std::array<lv_point_precise_t, 2>, 15> frequency_grid_points_{};
    std::array<std::array<lv_obj_t*, 4>, 2> lines_{};
    std::array<std::array<std::array<lv_point_precise_t, 144>, 4>, 2> points_{};
    std::array<lv_obj_t*, 3> dots_{};
    std::array<lv_obj_t*, 3> vectors_{};
    std::array<std::array<lv_point_precise_t, 2>, 3> vector_points_{};
    std::array<std::array<lv_point_precise_t, 2>, 2> axis_points_{};
    std::array<lv_obj_t*, 2> axes_{};
    static constexpr unsigned grid_row_height_ = 13, grid_height_ = 4 * grid_row_height_;
    alignas(64) std::array<uint16_t, 432 * grid_height_> pixels_{};
    std::array<lv_obj_t*, 9> widgets_{};
    std::array<Binding, 9> bindings_{};
    std::array<std::array<lv_obj_t*, 2>, 9> step_buttons_{};
    std::array<int, 9> values_{217, 0, 0, 0, 100, 16, 50, 2, 2};
    std::vector<std::pair<lv_obj_t*, bool>> hidden_;
    std::shared_ptr<const OneSegCarrierBurst> burst_;
    std::array<OneSegPhaseObservation, 4> phases_{};
    OneSegCarrierGeometry geometry_;
    uint64_t generation_ = uint64_t(-1), age_ = 0;
    uint64_t decoder_generation_ = uint64_t(-1);
    uint64_t blocked_generation_ = uint64_t(-1);
    std::string source_path_;
    bool source_initialized_ = false, source_synthetic_ = false;
    unsigned phase_mask_ = 0, palette_ = 0;
    unsigned model_qam_ = 4, model_rate_ = 1;
    bool visible_ = false, dirty_ = true, hold_ = false, actual_ = false, generic_ = false, syncing_ = false, fresh_ = false;
    double preview_floor_ = -1;
    static constexpr std::array<uint32_t, 4> colors_{0xd7932d, 0x2f7ddb, 0x087f70, 0x9c6ade};
};
} // namespace platform::linux_runtime
