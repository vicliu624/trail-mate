#include "ui_presentation/page/page_manifest.h"

#include <cassert>
#include <cstring>

namespace
{

bool contains(const ui::presentation::PageManifest& manifest,
              ui::presentation::PageId page_id)
{
    for (std::size_t index = 0; index < manifest.item_count; ++index)
    {
        if (manifest.items[index].page_id == page_id)
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    const auto* tab5 = ui::presentation::findPageManifest("tab5_touch_manifest");
    assert(tab5 != nullptr);
    assert(tab5->item_count >= 8);
    assert(contains(*tab5, ui::presentation::PageId::Map));
    assert(contains(*tab5, ui::presentation::PageId::Contacts));

    const auto* watch = ui::presentation::findPageManifest("watch_compact_manifest");
    assert(watch != nullptr);
    assert(contains(*watch, ui::presentation::PageId::Gps));
    assert(!contains(*watch, ui::presentation::PageId::Extensions));

    const auto* uconsole = ui::presentation::findPageManifest("uconsole_desktop_manifest");
    assert(uconsole != nullptr);
    assert(contains(*uconsole, ui::presentation::PageId::Diagnostics));

    const auto* node = ui::presentation::findPageManifest("node_headless_manifest");
    assert(node != nullptr);
    assert(node->item_count == 2);
    assert(contains(*node, ui::presentation::PageId::NodeStatus));
    assert(!node->items[0].visible_in_menu);

    assert(ui::presentation::findPageManifest("unknown") == nullptr);
    return 0;
}
