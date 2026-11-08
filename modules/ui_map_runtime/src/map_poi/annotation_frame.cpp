#include "ui_map_runtime/map_poi/annotation_frame.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

namespace ui::map_poi
{
AnnotationFrame::AnnotationFrame(Allocate allocate, Free release, void* context)
    : allocate_(allocate), release_(release), context_(context) {}
AnnotationFrame::~AnnotationFrame() { clear(); }

bool AnnotationFrame::reserve(std::size_t needed)
{
    if (needed <= capacity_) return true;
    const auto places = std::min(needed, AnnotationLayoutOptions::kMaxPlacements);
    auto* candidates = static_cast<AnnotationCandidate*>(allocate_(needed * sizeof(AnnotationCandidate), context_));
    auto* placements = static_cast<AnnotationPlacement*>(allocate_(places * sizeof(AnnotationPlacement), context_));
    auto* previous = static_cast<AnnotationPlacement*>(allocate_(places * sizeof(AnnotationPlacement), context_));
    auto* front = static_cast<ui::map::MapPoiItem*>(allocate_(places * sizeof(ui::map::MapPoiItem), context_));
    auto* back = static_cast<ui::map::MapPoiItem*>(allocate_(places * sizeof(ui::map::MapPoiItem), context_));
    if (!candidates || !placements || !previous || !front || !back)
    {
        release_(candidates, context_);
        release_(placements, context_);
        release_(previous, context_);
        release_(front, context_);
        release_(back, context_);
        return false;
    }
    for (std::size_t i = 0; i < needed; ++i) new (candidates + i) AnnotationCandidate{};
    for (std::size_t i = 0; i < places; ++i)
    {
        new (placements + i) AnnotationPlacement{};
        new (previous + i) AnnotationPlacement{};
        new (front + i) ui::map::MapPoiItem{};
        new (back + i) ui::map::MapPoiItem{};
    }
    if (snapshot_.item_count) std::copy_n(front_, snapshot_.item_count, front);
    if (previous_count_) std::copy_n(previous_, previous_count_, previous);
    // Publish equivalent existing contents before releasing its previous address.
    snapshot_.items = front;
    snapshot_.capacity = places;
    release_(candidates_, context_);
    release_(placements_, context_);
    release_(previous_, context_);
    release_(front_, context_);
    release_(back_, context_);
    candidates_ = candidates;
    placements_ = placements;
    previous_ = previous;
    front_ = front;
    back_ = back;
    capacity_ = needed;
    return true;
}

bool AnnotationFrame::same_view(const ui::map::MapPoiSnapshot& m) const
{
    return snapshot_.enabled && snapshot_.view_key == m.view_key && snapshot_.compatibility_key == m.compatibility_key &&
           snapshot_.width == m.width && snapshot_.height == m.height;
}

bool AnnotationFrame::begin(std::size_t needed, const ui::map::MapPoiSnapshot& metadata)
{
    needed = std::min(needed, AnnotationLayoutOptions::kMaxCandidates);
    if (!allocate_ || !release_ || (needed && !reserve(needed))) return false;
    next_ = metadata;
    next_.items = nullptr;
    next_.item_count = 0;
    count_ = 0;
    return true;
}

int AnnotationFrame::cell(const AnnotationCandidate& c) const
{
    return std::clamp<int>(c.x * 8 / std::max<int>(1, next_.width), 0, 7) +
           8 * std::clamp<int>(c.y * 4 / std::max<int>(1, next_.height), 0, 3);
}
bool AnnotationFrame::better(const AnnotationCandidate& a, const AnnotationCandidate& b) const
{
    const int ap = a.priority + (a.retained ? 12 : 0), bp = b.priority + (b.retained ? 12 : 0);
    if (ap != bp) return ap > bp;
    const int ax = (a.x - next_.width / 2) / 8, ay = (a.y - next_.height / 2) / 8;
    const int bx = (b.x - next_.width / 2) / 8, by = (b.y - next_.height / 2) / 8;
    const int ad = ax * ax + ay * ay, bd = bx * bx + by * by;
    return ad != bd ? ad < bd : a.key < b.key;
}

void AnnotationFrame::add(const AnnotationCandidate& input)
{
    if (!capacity_) return;
    // Small candidate only; record strings remain borrowed until finish copies
    // selected text into the owned frame. No source Record is copied on stack.
    AnnotationCandidate c = input;
    if (c.kind == ui::map::AnnotationKind::Road)
    {
        if (c.path_points < 2) return;
        int minx = INT16_MAX, maxx = INT16_MIN, miny = INT16_MAX, maxy = INT16_MIN;
        for (unsigned i = 0; i < std::min<unsigned>(c.path_points, 8); ++i)
        {
            minx = std::min<int>(minx, c.path[i * 2]);
            maxx = std::max<int>(maxx, c.path[i * 2]);
            miny = std::min<int>(miny, c.path[i * 2 + 1]);
            maxy = std::max<int>(maxy, c.path[i * 2 + 1]);
        }
        if (maxx < 0 || maxy < 0 || minx >= next_.width || miny >= next_.height) return;
        c.x = static_cast<int16_t>((std::max(0, minx) + std::min<int>(next_.width - 1, maxx)) / 2);
        c.y = static_cast<int16_t>((std::max(0, miny) + std::min<int>(next_.height - 1, maxy)) / 2);
    }
    else if (c.x < 0 || c.y < 0 || c.x >= next_.width || c.y >= next_.height) return;
    c.retained = false;
    if (snapshot_.compatibility_key == next_.compatibility_key)
        for (std::size_t i = 0; i < previous_count_; ++i)
            if (previous_[i].key == c.key) c.retained = true;
    std::size_t same_cell = 0, same_kind = 0, worst_cell = count_, worst_kind = count_;
    for (std::size_t i = 0; i < count_; ++i)
    {
        const auto& other = candidates_[i];
        if (other.key == c.key) return;
        if (other.kind != c.kind) continue;
        ++same_kind;
        if (worst_kind == count_ || better(candidates_[worst_kind], other)) worst_kind = i;
        if (cell(other) != cell(c)) continue;
        ++same_cell;
        if (worst_cell == count_ || better(candidates_[worst_cell], other)) worst_cell = i;
    }
    // A dense cluster cannot occupy the entire candidate pool before layout.
    const std::size_t kind_limit = c.kind == ui::map::AnnotationKind::Road ? 72 : c.kind == ui::map::AnnotationKind::Place ? 40
                                                                                                                           : 80;
    std::size_t replace = count_;
    if (same_cell >= 6) replace = worst_cell;
    else if (same_kind >= kind_limit) replace = worst_kind;
    else if (count_ == capacity_)
    {
        // Prefer replacing an over-represented class, while preserving a floor
        // of candidates for roads and places even during asynchronous arrivals.
        for (std::size_t i = 0; i < count_; ++i)
        {
            if (same_kind < 12 && candidates_[i].kind != c.kind)
            {
                replace = i;
                break;
            }
        }
        if (replace == count_) replace = worst_kind;
    }
    if (replace < count_)
    {
        if (candidates_[replace].kind != c.kind || better(c, candidates_[replace])) candidates_[replace] = c;
    }
    else if (count_ < capacity_) candidates_[count_++] = c;
}

bool AnnotationFrame::finish(const AnnotationLayoutOptions& options, AnnotationMeasure measure, void* context)
{
    const auto places = std::min(capacity_, AnnotationLayoutOptions::kMaxPlacements);
    const auto history = snapshot_.compatibility_key == next_.compatibility_key ? previous_count_ : 0;
    result_ = layout_annotations(candidates_, count_, previous_, history, placements_, places, options, measure, context);
    if (result_.metrics_pending && same_view(next_)) return false;
    for (std::size_t i = 0; i < result_.count; ++i)
    {
        const auto& placement = placements_[i];
        const auto& candidate = candidates_[placement.candidate];
        auto& item = back_[i];
        item = {};
        std::snprintf(item.id.data, sizeof(item.id.data), "%016llX", static_cast<unsigned long long>(candidate.key));
        if (placement.text_bytes)
        {
            std::memcpy(item.label.data, candidate.name, placement.text_bytes);
            std::size_t bytes = placement.text_bytes;
            if (placement.ellipsis)
            {
                std::memcpy(item.label.data + bytes, kAnnotationEllipsis, kAnnotationEllipsisBytes);
                bytes += kAnnotationEllipsisBytes;
            }
            item.label.data[bytes] = '\0';
        }
        ui::copyText(item.category, candidate.category);
        item.kind = candidate.kind;
        item.marker = placement.marker;
        item.x = placement.x;
        item.y = placement.y;
        item.priority = candidate.priority;
        item.text_x = placement.text_x;
        item.text_y = placement.text_y;
        item.text_width = placement.text_width;
        item.text_height = placement.text_height;
        item.cluster_count = placement.cluster_count;
    }
    std::swap(front_, back_);
    std::swap(placements_, previous_);
    previous_count_ = result_.count;
    snapshot_ = next_;
    snapshot_.items = front_;
    snapshot_.capacity = places;
    snapshot_.item_count = result_.count;
    snapshot_.layout_ready = true;
    return true;
}

void AnnotationFrame::clear()
{
    snapshot_ = {};
    next_ = {};
    result_ = {};
    if (release_)
    {
        release_(candidates_, context_);
        release_(placements_, context_);
        release_(previous_, context_);
        release_(front_, context_);
        release_(back_, context_);
    }
    candidates_ = nullptr;
    placements_ = previous_ = nullptr;
    front_ = back_ = nullptr;
    capacity_ = count_ = previous_count_ = 0;
}
} // namespace ui::map_poi
