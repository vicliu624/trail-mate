#include "ui_map_runtime/map_poi/annotation_layout.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ui::map_poi
{
namespace
{
struct Rect
{
    int x, y, w, h;
};
bool overlaps(const Rect& a, const Rect& b, int padding)
{
    return a.x < b.x + b.w + padding && a.x + a.w + padding > b.x &&
           a.y < b.y + b.h + padding && a.y + a.h + padding > b.y;
}
bool inside(const Rect& a, const AnnotationLayoutOptions& o)
{
    return a.x >= 0 && a.y >= 0 && a.x + a.w <= o.width && a.y + a.h <= o.height;
}
bool named(const AnnotationCandidate& c) { return c.name && c.name[0]; }
Rect marker_rect(int x, int y, int size) { return {x - size / 2, y - size / 2, size, size}; }
Rect text_rect(const AnnotationPlacement& p) { return {p.text_x, p.text_y, p.text_width, p.text_height}; }

bool fits(const Rect& text, const Rect* marker, const AnnotationPlacement* placed, std::size_t count, const AnnotationLayoutOptions& o)
{
    if (!inside(text, o) || (marker && !inside(*marker, o))) return false;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& p = placed[i];
        if (p.text_bytes && (overlaps(text, text_rect(p), o.padding) || (marker && overlaps(*marker, text_rect(p), o.padding)))) return false;
        if (p.marker)
        {
            const auto existing = marker_rect(p.x, p.y, o.marker_size);
            if (overlaps(text, existing, o.padding) || (marker && overlaps(*marker, existing, o.padding))) return false;
        }
    }
    return true;
}

std::size_t utf8_prefix(const char* text, std::size_t limit)
{
    auto size = std::min(std::strlen(text), limit);
    while (size && (static_cast<uint8_t>(text[size]) & 0xC0U) == 0x80U) --size;
    return size;
}

bool measure_name(const AnnotationCandidate& c, const AnnotationLayoutOptions& o, AnnotationMeasure measure, void* context,
                  uint8_t& bytes, bool& ellipsis, int16_t& width, int16_t& height, bool& pending)
{
    auto length = utf8_prefix(c.name, 76); // leave room for UTF-8 ellipsis + NUL in the 80-byte view text
    ellipsis = c.name[length] != '\0';
    const int available = std::min<int>(o.max_text_width, o.width - o.marker_size - 8);
    while (length)
    {
        if (!measure(context, c.name, length, ellipsis, width, height))
        {
            pending = true;
            return false;
        }
        if (width > 0 && height > 0 && width <= available && height <= o.height)
        {
            bytes = static_cast<uint8_t>(length);
            return true;
        }
        length = utf8_prefix(c.name, length - 1);
        ellipsis = true;
    }
    return false;
}

bool clip_segment(float& x0, float& y0, float& x1, float& y1, const AnnotationLayoutOptions& o)
{
    // Liang–Barsky clipping of already projected coordinates. No map projection here.
    const float dx = x1 - x0, dy = y1 - y0;
    float begin = 0, end = 1;
    const auto edge = [&](float p, float q)
    {
        if (p == 0) return q >= 0;
        const float t = q / p;
        if (p < 0)
        {
            if (t > end) return false;
            begin = std::max(begin, t);
        }
        else
        {
            if (t < begin) return false;
            end = std::min(end, t);
        }
        return true;
    };
    if (!edge(-dx, x0) || !edge(dx, o.width - 1 - x0) || !edge(-dy, y0) || !edge(dy, o.height - 1 - y0)) return false;
    x1 = x0 + end * dx;
    y1 = y0 + end * dy;
    x0 += begin * dx;
    y0 += begin * dy;
    return true;
}

bool road_repeats(const AnnotationCandidate& c, int x, int y, const AnnotationCandidate* candidates,
                  const AnnotationPlacement* placed, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& p = placed[i];
        const auto& other = candidates[p.candidate];
        if (other.kind != ui::map::AnnotationKind::Road) continue;
        if (c.feature_key != p.feature_key && std::strcmp(c.name, other.name) != 0) continue;
        const int dx = x - p.x, dy = y - p.y;
        if (dx * dx + dy * dy < 200 * 200) return true;
    }
    return false;
}

bool place_named(std::size_t index, AnnotationCandidate* candidates, AnnotationPlacement* output, std::size_t count,
                 const AnnotationLayoutOptions& o, AnnotationMeasure measure, void* context, AnnotationLayoutResult& result)
{
    const auto& c = candidates[index];
    auto& p = output[count];
    p = {};
    p.key = c.key;
    p.feature_key = c.feature_key;
    p.candidate = static_cast<uint16_t>(index);
    if (!measure_name(c, o, measure, context, p.text_bytes, p.ellipsis, p.text_width, p.text_height, result.metrics_pending)) return false;
    p.marker = c.kind == ui::map::AnnotationKind::Poi;
    const auto attempt = [&](int x, int y)
    {
        if (c.kind == ui::map::AnnotationKind::Road && road_repeats(c, x, y, candidates, output, count)) return false;
        const Rect marker = marker_rect(x, y, o.marker_size);
        for (int side = 0; side < (p.marker ? 4 : 1); ++side)
        {
            Rect text{x - p.text_width / 2, y - p.text_height / 2, p.text_width, p.text_height};
            if (p.marker)
            {
                if (side == 0) text.x = x + o.marker_size / 2 + o.padding;
                if (side == 1) text.x = x - o.marker_size / 2 - o.padding - text.w;
                if (side == 2) text.y = y - o.marker_size / 2 - o.padding - text.h;
                if (side == 3) text.y = y + o.marker_size / 2 + o.padding;
            }
            if (!fits(text, p.marker ? &marker : nullptr, output, count, o)) continue;
            p.x = static_cast<int16_t>(x);
            p.y = static_cast<int16_t>(y);
            p.text_x = static_cast<int16_t>(text.x);
            p.text_y = static_cast<int16_t>(text.y);
            return true;
        }
        return false;
    };
    if (c.kind != ui::map::AnnotationKind::Road) return attempt(c.x, c.y);
    // Multiple visible segments can supply a label; the original whole-road
    // midpoint is irrelevant when it is outside the viewport.
    for (unsigned i = 1; i < std::min<unsigned>(c.path_points, 8); ++i)
    {
        float x0 = c.path[(i - 1) * 2], y0 = c.path[(i - 1) * 2 + 1];
        float x1 = c.path[i * 2], y1 = c.path[i * 2 + 1];
        if (!clip_segment(x0, y0, x1, y1, o)) continue;
        for (const float fraction : {0.5f, 0.25f, 0.75f})
            if (attempt(static_cast<int>(std::lround(x0 + fraction * (x1 - x0))),
                        static_cast<int>(std::lround(y0 + fraction * (y1 - y0))))) return true;
    }
    return false;
}

bool better(const AnnotationCandidate& a, const AnnotationCandidate& b, const AnnotationLayoutOptions& o)
{
    const int as = a.priority + (a.retained ? o.retention_bonus : 0);
    const int bs = b.priority + (b.retained ? o.retention_bonus : 0);
    if (as != bs) return as > bs;
    // Quantisation reduces tie changes from sub-marker GPS jitter.
    const int ax = (a.x - o.width / 2) / 8, ay = (a.y - o.height / 2) / 8;
    const int bx = (b.x - o.width / 2) / 8, by = (b.y - o.height / 2) / 8;
    const int ad = ax * ax + ay * ay, bd = bx * bx + by * by;
    return ad != bd ? ad < bd : a.key < b.key;
}
} // namespace

AnnotationLayoutResult layout_annotations(AnnotationCandidate* candidates, std::size_t candidate_count,
                                          const AnnotationPlacement* previous, std::size_t previous_count, AnnotationPlacement* output, std::size_t capacity,
                                          const AnnotationLayoutOptions& o, AnnotationMeasure measure, void* context)
{
    AnnotationLayoutResult result;
    if (!candidates || !output || !measure || o.width <= 0 || o.height <= 0) return result;
    candidate_count = std::min(candidate_count, AnnotationLayoutOptions::kMaxCandidates);
    capacity = std::min(capacity, AnnotationLayoutOptions::kMaxPlacements);
    previous_count = previous ? std::min(previous_count, AnnotationLayoutOptions::kMaxPlacements) : 0;
    static_assert(AnnotationLayoutOptions::kMaxCandidates <= 256);
    for (std::size_t i = 0; i < candidate_count; ++i)
    {
        auto& c = candidates[i];
        c.processed = false;
        c.source_order = static_cast<uint8_t>(i);
        c.retained = false;
        for (std::size_t j = 0; j < previous_count; ++j)
            if (c.key == previous[j].key) c.retained = true;
    }
    // All preference inputs are fixed for this layout. Sort the caller's
    // candidate storage once, rather than rescanning it for every attempt.
    // Heap sort is iterative, needs no secondary array or recursive stack.
    const auto order = [&](const auto& a, const auto& b)
    {
        if (better(a, b, o)) return true;
        if (better(b, a, o)) return false;
        return a.source_order < b.source_order;
    };
    std::make_heap(candidates, candidates + candidate_count, order);
    std::sort_heap(candidates, candidates + candidate_count, order);
    const auto phase = [&](int kind, unsigned limit)
    {
        unsigned added = 0;
        for (std::size_t best = 0; best < candidate_count && added < limit &&
                                   result.count < capacity && result.labels < o.max_labels;
             ++best)
        {
            auto& c = candidates[best];
            if (c.processed || !named(c) || (kind >= 0 && static_cast<int>(c.kind) != kind)) continue;
            c.processed = true;
            bool duplicate = false;
            for (std::size_t i = 0; i < result.count; ++i)
                if (output[i].key == c.key) duplicate = true;
            if (duplicate) continue;
            if (!place_named(best, candidates, output, result.count, o, measure, context, result))
            {
                ++result.collisions;
                continue;
            }
            if (output[result.count].ellipsis) ++result.truncated;
            ++result.count;
            ++result.labels;
            ++added;
            if (c.kind == ui::map::AnnotationKind::Road) ++result.road_labels;
            else if (c.kind == ui::map::AnnotationKind::Place) ++result.place_labels;
            else ++result.poi_labels;
        }
    };
    phase(static_cast<int>(ui::map::AnnotationKind::Road), o.road_reservation);
    phase(static_cast<int>(ui::map::AnnotationKind::Place), o.place_reservation);
    phase(static_cast<int>(ui::map::AnnotationKind::Poi), o.poi_reservation);
    phase(-1, o.max_labels);

    // Keep caller indices and unnamed clustering order unchanged. The ordinal
    // uses existing padding; restoring order needs no temporary index array.
    for (std::size_t i = 0; i < result.count; ++i)
        output[i].candidate = candidates[output[i].candidate].source_order;
    const auto source_order = [](const auto& a, const auto& b)
    { return a.source_order < b.source_order; };
    std::make_heap(candidates, candidates + candidate_count, source_order);
    std::sort_heap(candidates, candidates + candidate_count, source_order);

    for (std::size_t i = 0; i < candidate_count && result.count < capacity; ++i)
    {
        const auto& c = candidates[i];
        if (named(c) || c.kind != ui::map::AnnotationKind::Poi) continue;
        bool clustered = false;
        for (std::size_t j = 0; j < result.count; ++j)
        {
            auto& p = output[j];
            const auto& other = candidates[p.candidate];
            if (p.text_bytes || !p.marker || !c.category || !other.category || std::strcmp(c.category, other.category)) continue;
            if (std::abs(c.x - p.x) < 24 && std::abs(c.y - p.y) < 24)
            {
                if (p.cluster_count < UINT16_MAX) ++p.cluster_count;
                clustered = true;
                break;
            }
        }
        if (clustered || result.unnamed_markers >= o.max_unnamed_markers) continue;
        const auto rect = marker_rect(c.x, c.y, o.marker_size);
        if (!fits(rect, nullptr, output, result.count, o))
        {
            ++result.collisions;
            continue;
        }
        auto& p = output[result.count++];
        p = {};
        p.key = c.key;
        p.feature_key = c.feature_key;
        p.candidate = static_cast<uint16_t>(i);
        p.x = c.x;
        p.y = c.y;
        p.marker = true;
        ++result.unnamed_markers;
    }
    return result;
}
} // namespace ui::map_poi
