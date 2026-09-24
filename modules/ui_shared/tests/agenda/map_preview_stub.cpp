#include "ui/widgets/map/map_viewport.h"
#include <cassert>
namespace ui::widgets::map
{
// Test the real details layout and lifetime without an SD driver. Actual tile
// rendering remains covered by the shared map transport tests and device QA.
struct RuntimeImpl
{
    Widgets widgets;
    Model model;
};
Runtime::~Runtime() { destroy(*this); }
Widgets create(Runtime& runtime, lv_obj_t* parent, uint32_t)
{
    destroy(runtime);
    runtime.impl_ = new RuntimeImpl;
    runtime.impl_->widgets.root = lv_obj_create(parent);
    return runtime.impl_->widgets;
}
void destroy(Runtime& runtime)
{
    if (!runtime.impl_) return;
    assert(lv_obj_is_valid(runtime.impl_->widgets.root));
    lv_obj_delete(runtime.impl_->widgets.root);
    delete runtime.impl_;
    runtime.impl_ = nullptr;
}
void set_size(Runtime& runtime, lv_coord_t w, lv_coord_t h)
{
    lv_obj_set_size(runtime.impl_->widgets.root, w, h);
}
void set_gesture_enabled(Runtime&, bool enabled) { assert(!enabled); }
void apply_model(Runtime& runtime, const Model& model)
{
    assert(model.focus_point.valid);
    runtime.impl_->model = model;
}
Status status(const Runtime& runtime)
{
    Status result;
    result.alive = runtime.impl_ != nullptr;
    return result;
}
} // namespace ui::widgets::map
