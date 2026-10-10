#pragma once

#include "ui_presentation/map/map_annotation_kind.h"
#include <cstddef>
#include <cstdint>

namespace ui::map_poi
{
struct AnnotationCandidate
{
    uint64_t key = 0;
    uint64_t feature_key = 0;
    const char* name = nullptr;
    const char* category = nullptr;
    int16_t x = 0, y = 0;
    int16_t path[16]{}; // Already projected screen points; no projection in layout.
    uint8_t path_points = 0;
    uint8_t priority = 0;
    ui::map::AnnotationKind kind = ui::map::AnnotationKind::Poi;
    bool processed = false;
    bool retained = false;
    uint8_t source_order = 0; // Temporary ordinal in the existing tail padding.
};
static_assert(sizeof(AnnotationCandidate) ==
                  (offsetof(AnnotationCandidate, retained) + sizeof(bool) + alignof(AnnotationCandidate) - 1) /
                      alignof(AnnotationCandidate) * alignof(AnnotationCandidate),
              "Layout ordinal must not grow candidate storage");

struct AnnotationPlacement
{
    uint64_t key = 0;
    uint64_t feature_key = 0;
    uint16_t candidate = 0;
    int16_t x = 0, y = 0;
    int16_t text_x = 0, text_y = 0, text_width = 0, text_height = 0;
    uint16_t cluster_count = 1;
    uint8_t text_bytes = 0;
    bool ellipsis = false;
    bool marker = false;
};

struct AnnotationLayoutOptions
{
    static constexpr std::size_t kMaxCandidates = 192;
    static constexpr std::size_t kMaxPlacements = 48;
    int16_t width = 0, height = 0;
    uint8_t max_labels = 32;
    uint8_t max_unnamed_markers = 6;
    uint8_t road_reservation = 8;
    uint8_t place_reservation = 2;
    uint8_t poi_reservation = 12;
    uint8_t marker_size = 12;
    uint8_t padding = 2;
    uint8_t max_text_width = 128;
    uint8_t retention_bonus = 12;
};

struct AnnotationLayoutResult
{
    std::size_t count = 0;
    uint16_t labels = 0, road_labels = 0, place_labels = 0, poi_labels = 0;
    uint16_t unnamed_markers = 0, collisions = 0, outside = 0, truncated = 0;
    bool metrics_pending = false;
};

// Measures a bounded UTF-8 prefix, optionally followed by an ellipsis.
using AnnotationMeasure = bool (*)(void*, const char*, std::size_t, bool, int16_t&, int16_t&);
using AnnotationConsumer = void (*)(void*, const AnnotationCandidate&);

// All bulk storage is caller-owned. Previous placements may alias output: only
// stable keys are read into candidate flags before any placement is overwritten.
AnnotationLayoutResult layout_annotations(AnnotationCandidate* candidates, std::size_t candidate_count,
                                          const AnnotationPlacement* previous, std::size_t previous_count,
                                          AnnotationPlacement* output, std::size_t capacity, const AnnotationLayoutOptions& options,
                                          AnnotationMeasure measure, void* measure_context);

static_assert(sizeof(AnnotationCandidate) <= 96, "Candidate geometry must stay compact");
static_assert(sizeof(AnnotationPlacement) <= 48, "Layout must not embed strings or UI objects");
} // namespace ui::map_poi
