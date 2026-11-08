#include "ui_map_runtime/map_poi/annotation_frame.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>

using namespace ui::map_poi;
struct Heap
{
    int calls = 0, fail = -1;
    std::map<void*, std::size_t> live;
    static void* allocate(std::size_t bytes, void* context)
    {
        auto& self = *static_cast<Heap*>(context);
        if (self.calls++ == self.fail) return nullptr;
        auto* value = std::malloc(bytes);
        if (value) self.live[value] = bytes;
        return value;
    }
    static void release(void* value, void* context)
    {
        if (!value) return;
        auto& self = *static_cast<Heap*>(context);
        assert(self.live.erase(value) == 1);
        std::free(value);
    }
};
bool measure(void* context, const char*, std::size_t bytes, bool ellipsis, int16_t& width, int16_t& height)
{
    if (context) return false;
    width = static_cast<int16_t>((bytes + (ellipsis ? kAnnotationEllipsisBytes : 0)) * 6);
    height = 14;
    return true;
}
int main()
{
    {
        Heap heap;
        AnnotationFrame frame(Heap::allocate, Heap::release, &heap);
        ui::map::MapPoiSnapshot metadata;
        metadata.enabled = true;
        metadata.width = 240;
        metadata.height = 120;
        AnnotationLayoutOptions options;
        options.width = 240;
        options.height = 120;
        options.max_text_width = 40;
        AnnotationCandidate candidate;
        candidate.key = candidate.feature_key = 1;
        candidate.name = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        candidate.category = "road";
        candidate.x = candidate.y = 30;
        assert(frame.begin(1, metadata));
        frame.add(candidate);
        assert(frame.finish(options, measure, nullptr));
        assert(frame.snapshot().item_count == 1);
        assert(std::strcmp(frame.snapshot().items[0].label.c_str(), "ABC...") == 0);
    }
    for (int failure = 0; failure < 5; ++failure)
    {
        Heap heap;
        {
            AnnotationFrame frame(Heap::allocate, Heap::release, &heap);
            ui::map::MapPoiSnapshot metadata;
            metadata.enabled = true;
            metadata.header.valid = true;
            metadata.width = 240;
            metadata.height = 120;
            metadata.view_key = 42;
            metadata.compatibility_key = 3;
            AnnotationLayoutOptions options;
            options.width = 240;
            options.height = 120;
            char source[] = "Water";
            AnnotationCandidate candidate;
            candidate.key = candidate.feature_key = 1;
            candidate.name = source;
            candidate.category = "water";
            candidate.x = 30;
            candidate.y = 30;
            candidate.priority = 90;
            assert(frame.begin(1, metadata));
            frame.add(candidate);
            assert(frame.finish(options, measure, nullptr));
            assert(frame.snapshot().item_count == 1);
            source[0] = 'X'; // Published strings cannot depend on the evictable source tile.
            assert(std::strcmp(frame.snapshot().items[0].label.c_str(), "Water") == 0);
            const auto live = heap.live.size();
            heap.fail = heap.calls + failure;
            assert(!frame.begin(100, metadata));
            assert(heap.live.size() == live);
            assert(frame.same_view(metadata));
            assert(std::strcmp(frame.snapshot().items[0].label.c_str(), "Water") == 0);
            heap.fail = -1;
            assert(frame.begin(100, metadata));
            frame.add(candidate);
            assert(!frame.finish(options, measure, &heap)); // Deferred font metrics preserve the old frame.
            assert(std::strcmp(frame.snapshot().items[0].label.c_str(), "Water") == 0);
            assert(frame.begin(100, metadata));
            frame.add(candidate);
            assert(frame.finish(options, measure, nullptr));
            assert(std::strcmp(frame.snapshot().items[0].label.c_str(), "Xater") == 0);
            frame.clear();
            assert(heap.live.empty());
        }
        assert(heap.live.empty());
    }
}
