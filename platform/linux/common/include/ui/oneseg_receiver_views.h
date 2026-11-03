#pragma once

#include "platform/linux/oneseg_receiver_model.h"
#include "ui/ui_theme.h"
#include <lvgl.h>
#include <utility>

namespace platform::linux_runtime
{
// A projection of receiver-only models into the existing three analysis panels.
// Actual observations and radio/application callbacks belong to the workbench.
class OneSegReceiverViews
{
  public:
    OneSegReceiverViews(lv_obj_t* analysis, lv_obj_t* phase, lv_obj_t* residual, lv_group_t* group) : group_(group)
    {
        parents_ = {analysis, phase, residual};
        for (unsigned i = 0; i < 3; ++i)
        {
            roots_[i] = lv_obj_create(parents_[i]);
            lv_obj_remove_style_all(roots_[i]);
            lv_obj_set_size(roots_[i], lv_pct(100), lv_pct(100));
            lv_obj_add_flag(roots_[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_add_flag(roots_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(roots_[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(roots_[i], LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(roots_[i], 2, 0);
            titles_[i] = label(roots_[i], "");
            lv_obj_set_height(titles_[i], 16);
            frames_[i] = lv_obj_create(roots_[i]);
            lv_obj_set_size(frames_[i], lv_pct(100), 0);
            lv_obj_set_flex_grow(frames_[i], 1);
            lv_obj_set_style_pad_all(frames_[i], 0, 0);
            lv_obj_set_style_border_width(frames_[i], 1, 0);
            lv_obj_set_style_border_color(frames_[i], ::ui::theme::border(), 0);
            lv_obj_set_style_bg_opa(frames_[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_radius(frames_[i], 0, 0);
            lv_obj_remove_flag(frames_[i], LV_OBJ_FLAG_SCROLLABLE);
            axes_[i] = label(roots_[i], "");
            legends_[i] = label(roots_[i], "");
            lv_obj_set_style_text_opa(legends_[i], LV_OPA_70, 0);
        }
    }
    bool active() const { return selected_ != OneSegReceiverView::Actual; }
    OneSegReceiverView selected() const { return selected_; }
    const OneSegReceiverModel& model() const { return model_; }
    void select(OneSegReceiverView view)
    {
        if (active())
        {
            parameters_[static_cast<unsigned>(selected_)] = model_.values;
            initialized_[static_cast<unsigned>(selected_)] = true;
        }
        selected_ = view;
        if (active())
        {
            model_.reset(view);
            if (initialized_[static_cast<unsigned>(view)]) model_.values = parameters_[static_cast<unsigned>(view)];
        }
        dirty_ = true;
    }
    void clearControls() { widgets_.fill(nullptr); }
    void show(bool visible)
    {
        if (visible == visible_) return;
        if (visible)
        {
            hidden_.clear();
            for (unsigned i = 0; i < 3; ++i)
                for (unsigned j = 0; j < lv_obj_get_child_count(parents_[i]); ++j)
                {
                    auto* child = lv_obj_get_child(parents_[i], j);
                    if (child == roots_[i]) continue;
                    hidden_.push_back({child, lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)});
                    lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                }
        }
        else
        {
            for (const auto& item : hidden_)
                if (!item.second) lv_obj_remove_flag(item.first, LV_OBJ_FLAG_HIDDEN);
            hidden_.clear();
        }
        for (auto* root : roots_)
            if (visible) lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
        visible_ = visible;
        dirty_ = true;
    }
    void controls(lv_obj_t* parent)
    {
        clearControls();
        const auto& controls = oneseg_receiver_view(selected_).parameters;
        for (size_t i = 0; i < controls.size(); ++i)
        {
            const auto& p = controls[i];
            auto& b = bindings_[i];
            b = {this, i, 1};
            auto* cell = lv_obj_create(parent);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, lv_pct(49), 48);
            std::string caption = p.name;
            if (*p.unit) caption += " [" + std::string(p.unit) + "]";
            label(cell, caption.c_str());
            if (p.choices)
            {
                auto* choice = lv_dropdown_create(cell);
                widgets_[i] = choice;
                lv_dropdown_set_options(choice, p.choices);
                lv_dropdown_set_selected(choice, unsigned(model_.values[i]));
            }
            else
            {
                auto* spin = lv_spinbox_create(cell);
                widgets_[i] = spin;
                while (b.scale < 1000000 && std::abs(p.step * b.scale - std::round(p.step * b.scale)) > 1e-8) b.scale *= 10;
                unsigned decimals = 0;
                for (int factor = b.scale; factor > 1; factor /= 10) ++decimals;
                unsigned digits = decimals + 1;
                for (double maximum = std::max(std::abs(p.low), std::abs(p.high)); maximum >= 10; maximum /= 10) ++digits;
                lv_spinbox_set_digit_format(spin, digits, decimals ? digits - decimals : 0);
                lv_spinbox_set_range(spin, int32_t(std::llround(p.low * b.scale)), int32_t(std::llround(p.high * b.scale)));
                lv_spinbox_set_step(spin, std::max<int32_t>(1, int32_t(std::llround(p.step * b.scale))));
                lv_spinbox_set_value(spin, int32_t(std::llround(model_.values[i] * b.scale)));
            }
            auto* widget = widgets_[i];
            lv_obj_set_size(widget, lv_pct(100), 28);
            lv_obj_align(widget, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            lv_obj_set_style_text_font(widget, &lv_font_montserrat_12, 0);
            lv_obj_set_style_pad_ver(widget, 2, 0);
            lv_group_add_obj(group_, widget);
            lv_obj_add_event_cb(
                widget, [](lv_event_t* e)
                {
                auto& binding=*static_cast<Binding*>(lv_event_get_user_data(e));auto& self=*binding.owner;
                if(self.syncing_)return;
                const auto& p=oneseg_receiver_view(self.selected_).parameters[binding.index];
                const double value=p.choices?lv_dropdown_get_selected(self.widgets_[binding.index]):double(lv_spinbox_get_value(self.widgets_[binding.index]))/binding.scale;
                if(self.model_.set(binding.index,value)){self.dirty_=true;self.refresh();} },
                LV_EVENT_VALUE_CHANGED, &b);
        }
        dirty_ = true;
        refresh();
    }
    void reset()
    {
        if (!active()) return;
        model_.reset(selected_);
        dirty_ = true;
        refresh();
    }
    void save()
    {
        saved_ = model_.values;
        saved_view_ = selected_;
        has_saved_ = active();
    }
    bool load()
    {
        if (!has_saved_ || saved_view_ != selected_) return false;
        model_.values = saved_;
        dirty_ = true;
        refresh();
        return true;
    }
    void refresh()
    {
        if (!visible_ || !active()) return;
        lv_obj_update_layout(roots_[0]);
        bool resized = false;
        for (unsigned i = 0; i < 3; ++i)
        {
            const auto size = std::make_pair(lv_obj_get_content_width(frames_[i]), lv_obj_get_content_height(frames_[i]));
            resized = resized || size != sizes_[i];
            sizes_[i] = size;
        }
        if (!dirty_ && !resized) return;
        model_.render();
        syncing_ = true;
        for (size_t i = 0; i < oneseg_receiver_view(selected_).parameters.size(); ++i)
            if (widgets_[i])
                if (oneseg_receiver_view(selected_).parameters[i].choices) lv_dropdown_set_selected(widgets_[i], unsigned(model_.values[i]));
                else lv_spinbox_set_value(widgets_[i], int32_t(std::llround(model_.values[i] * bindings_[i].scale)));
        syncing_ = false;
        for (unsigned i = 0; i < 3; ++i) draw(i);
        dirty_ = false;
    }
    std::string helpText() const
    {
        const auto& info = oneseg_receiver_view(selected_);
        return std::string(info.equations) + "\n\n" + info.variables + "\n\n" + info.relationship + "\n\nConditions\n" + info.conditions + "\n\nModel result\n" + model_.result.status;
    }

  private:
    struct Binding
    {
        OneSegReceiverViews* owner = nullptr;
        size_t index = 0;
        int scale = 1;
    };
    lv_group_t* group_;
    std::array<lv_obj_t*, 3> parents_{}, roots_{}, titles_{}, frames_{}, axes_{}, legends_{};
    std::array<lv_obj_t*, 6> widgets_{};
    std::array<Binding, 6> bindings_{};
    std::array<std::pair<int32_t, int32_t>, 3> sizes_{};
    std::vector<std::pair<lv_obj_t*, bool>> hidden_;
    std::array<std::vector<std::vector<lv_point_precise_t>>, 3> line_points_;
    OneSegReceiverModel model_;
    OneSegReceiverView selected_ = OneSegReceiverView::Actual, saved_view_ = OneSegReceiverView::Actual;
    std::array<std::array<double, 6>, static_cast<unsigned>(OneSegReceiverView::Count)> parameters_{};
    std::array<bool, static_cast<unsigned>(OneSegReceiverView::Count)> initialized_{};
    std::array<double, 6> saved_{};
    bool visible_ = false, dirty_ = true, syncing_ = false, has_saved_ = false;
    static lv_obj_t* label(lv_obj_t* parent, const char* text)
    {
        auto* object = lv_label_create(parent);
        lv_label_set_text(object, text);
        lv_obj_set_width(object, lv_pct(100));
        lv_obj_set_style_text_font(object, &lv_font_montserrat_12, 0);
        return object;
    }
    static lv_color_t color(OneSegReceiverModel::Color value)
    {
        using C = OneSegReceiverModel::Color;
        switch (value)
        {
        case C::Blue:
            return lv_color_hex(0x2f7ddb);
        case C::Gold:
            return lv_color_hex(0xd7932d);
        case C::Teal:
            return lv_color_hex(0x087f70);
        case C::Red:
            return lv_color_hex(0xc84e4e);
        case C::Purple:
            return lv_color_hex(0x8850ad);
        case C::Muted:
            return ::ui::theme::text_muted();
        }
        return ::ui::theme::text();
    }
    void draw(unsigned index)
    {
        const auto& plot = model_.result.plots[index];
        const std::string title = (index == 0 ? "MODEL / " : "") + plot.title;
        lv_label_set_text(titles_[index], title.c_str());
        lv_obj_clean(frames_[index]);
        line_points_[index].clear();
        const double width = std::max<int32_t>(10, sizes_[index].first), height = std::max<int32_t>(10, sizes_[index].second);
        double xmin = std::numeric_limits<double>::infinity(), xmax = -xmin, ymin = xmin, ymax = -xmin;
        for (const auto& s : plot.series)
            for (const auto& p : s.points)
            {
                xmin = std::min(xmin, p.x);
                xmax = std::max(xmax, p.x);
                ymin = std::min(ymin, p.y);
                ymax = std::max(ymax, p.y);
            }
        if (!std::isfinite(xmin))
        {
            xmin = ymin = -1;
            xmax = ymax = 1;
        }
        if (xmax - xmin < 1e-9)
        {
            xmin -= 1;
            xmax += 1;
        }
        if (ymax - ymin < 1e-9)
        {
            ymin -= 1;
            ymax += 1;
        }
        const double xpad = (xmax - xmin) * .08, ypad = (ymax - ymin) * .12;
        xmin -= xpad;
        xmax += xpad;
        ymin -= ypad;
        ymax += ypad;
        const double left = 8, right = 8, top = 5, bottom = 5;
        if (plot.equal_axes)
        {
            const double scale = std::max((xmax - xmin) / std::max(1., width - left - right), (ymax - ymin) / std::max(1., height - top - bottom)), cx = (xmax + xmin) / 2, cy = (ymax + ymin) / 2;
            xmin = cx - scale * (width - left - right) / 2;
            xmax = cx + scale * (width - left - right) / 2;
            ymin = cy - scale * (height - top - bottom) / 2;
            ymax = cy + scale * (height - top - bottom) / 2;
        }
        const auto project = [&](const OneSegReceiverModel::Point& p)
        {
            return lv_point_precise_t{lv_value_precise_t(left + (p.x - xmin) / (xmax - xmin) * (width - left - right)), lv_value_precise_t(top + (ymax - p.y) / (ymax - ymin) * (height - top - bottom))};
        };
        auto& storage = line_points_[index];
        storage.reserve(plot.series.size() + plot.vertical_guides.size() + plot.horizontal_guides.size() + 2);
        const auto line = [&](const std::vector<lv_point_precise_t>& points, lv_color_t stroke)
        {
            storage.push_back(points);
            auto* object = lv_line_create(frames_[index]);
            lv_obj_set_style_line_color(object, stroke, 0);
            lv_obj_set_style_line_width(object, 1, 0);
            lv_line_set_points(object, storage.back().data(), storage.back().size());
        };
        if (xmin <= 0 && xmax >= 0) line({project({0, ymin}), project({0, ymax})}, ::ui::theme::border());
        if (ymin <= 0 && ymax >= 0) line({project({xmin, 0}), project({xmax, 0})}, ::ui::theme::border());
        for (const auto x : plot.vertical_guides)
            if (x >= xmin && x <= xmax) line({project({x, ymin}), project({x, ymax})}, ::ui::theme::border());
        for (const auto y : plot.horizontal_guides)
            if (y >= ymin && y <= ymax) line({project({xmin, y}), project({xmax, y})}, ::ui::theme::border());
        std::string legend;
        for (const auto& s : plot.series)
        {
            if (!legend.empty()) legend += " / ";
            legend += s.name;
            if (s.scatter)
            {
                for (const auto& p : s.points)
                {
                    const auto point = project(p);
                    auto* dot = lv_obj_create(frames_[index]);
                    lv_obj_remove_style_all(dot);
                    lv_obj_set_size(dot, 3, 3);
                    lv_obj_set_pos(dot, int32_t(point.x) - 1, int32_t(point.y) - 1);
                    lv_obj_set_style_bg_color(dot, color(s.color), 0);
                    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
                }
            }
            else
            {
                std::vector<lv_point_precise_t> points;
                points.reserve(s.points.size());
                for (const auto& p : s.points) points.push_back(project(p));
                if (points.size() > 1) line(points, color(s.color));
            }
        }
        const std::string range = OneSegReceiverModel::number(xmin) + ".." + OneSegReceiverModel::number(xmax) + " " + plot.x_unit + " | " + OneSegReceiverModel::number(ymin) + ".." + OneSegReceiverModel::number(ymax) + " " + plot.y_unit;
        lv_label_set_text(axes_[index], range.c_str());
        lv_label_set_text(legends_[index], legend.c_str());
    }
};

class OneSegReceiverHelp
{
  public:
    OneSegReceiverHelp(lv_obj_t* parent, lv_group_t* group) : group_(group)
    {
        root_ = lv_obj_create(parent);
        lv_obj_set_size(root_, lv_pct(74), lv_pct(90));
        lv_obj_add_flag(root_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(root_);
        lv_obj_set_style_bg_color(root_, ::ui::theme::surface(), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(root_, ::ui::theme::accent(), 0);
        lv_obj_set_style_border_width(root_, 1, 0);
        lv_obj_set_style_pad_all(root_, 12, 0);
        lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
        auto* bar = lv_obj_create(root_);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, lv_pct(100), 30);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
        title_ = lv_label_create(bar);
        lv_obj_set_flex_grow(title_, 1);
        lv_obj_set_style_text_font(title_, &lv_font_montserrat_14, 0);
        close_ = lv_button_create(bar);
        lv_obj_set_size(close_, 70, 28);
        auto* caption = lv_label_create(close_);
        lv_label_set_text(caption, "Close");
        lv_obj_center(caption);
        lv_group_add_obj(group, close_);
        lv_obj_add_event_cb(
            close_, [](lv_event_t* e)
            { static_cast<OneSegReceiverHelp*>(lv_event_get_user_data(e))->close(); },
            LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(
            close_, [](lv_event_t* e)
            {if(lv_event_get_key(e)==LV_KEY_ESC)static_cast<OneSegReceiverHelp*>(lv_event_get_user_data(e))->close(); },
            LV_EVENT_KEY, this);
        body_ = lv_obj_create(root_);
        lv_obj_remove_style_all(body_);
        lv_obj_set_size(body_, lv_pct(100), 0);
        lv_obj_set_flex_grow(body_, 1);
        lv_obj_set_scroll_dir(body_, LV_DIR_VER);
        text_ = lv_label_create(body_);
        lv_obj_set_width(text_, lv_pct(100));
        lv_obj_set_style_text_font(text_, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(text_, LV_LABEL_LONG_WRAP);
    }
    void open(const std::string& title, const std::string& content, lv_obj_t* return_focus)
    {
        return_focus_ = return_focus;
        lv_label_set_text(title_, title.c_str());
        lv_label_set_text(text_, content.c_str());
        lv_obj_scroll_to_y(body_, 0, LV_ANIM_OFF);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        lv_group_focus_obj(close_);
    }
    void close()
    {
        const bool was_visible = visible();
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        if (was_visible && return_focus_) lv_group_focus_obj(return_focus_);
        return_focus_ = nullptr;
    }
    bool visible() const { return !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN); }
    lv_obj_t* object() const { return root_; }

  private:
    lv_group_t* group_;
    lv_obj_t *root_ = nullptr, *title_ = nullptr, *body_ = nullptr, *text_ = nullptr, *close_ = nullptr, *return_focus_ = nullptr;
};
} // namespace platform::linux_runtime
