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
    assert(tab5->item_count >= 9);
    assert(contains(*tab5, ui::presentation::PageId::Map));
    assert(contains(*tab5, ui::presentation::PageId::Contacts));
    assert(contains(*tab5, ui::presentation::PageId::Sstv));

    const auto* t_display_p4 = ui::presentation::findPageManifest("t_display_p4_touch_manifest");
    assert(t_display_p4 != nullptr);
    assert(t_display_p4->item_count == 13);
    assert(contains(*t_display_p4, ui::presentation::PageId::Dashboard));
    assert(contains(*t_display_p4, ui::presentation::PageId::Chat));
    assert(contains(*t_display_p4, ui::presentation::PageId::Contacts));
    assert(contains(*t_display_p4, ui::presentation::PageId::Map));
    assert(contains(*t_display_p4, ui::presentation::PageId::SkyPlot));
    assert(contains(*t_display_p4, ui::presentation::PageId::Gps));
    assert(contains(*t_display_p4, ui::presentation::PageId::Team));
    assert(contains(*t_display_p4, ui::presentation::PageId::Tracker));
    assert(contains(*t_display_p4, ui::presentation::PageId::EnergySweep));
    assert(contains(*t_display_p4, ui::presentation::PageId::WalkieTalkie));
    assert(contains(*t_display_p4, ui::presentation::PageId::Sstv));
    assert(contains(*t_display_p4, ui::presentation::PageId::Extensions));
    assert(contains(*t_display_p4, ui::presentation::PageId::Settings));

    const auto* pager = ui::presentation::findPageManifest("pager_compact_manifest");
    assert(pager != nullptr);
    for (std::size_t index = 0; index < pager->item_count; ++index)
    {
        assert(contains(*t_display_p4, pager->items[index].page_id));
    }

    const auto* watch = ui::presentation::findPageManifest("watch_compact_manifest");
    assert(watch != nullptr);
    assert(contains(*watch, ui::presentation::PageId::Gps));
    assert(!contains(*watch, ui::presentation::PageId::Extensions));

    const auto* deck = ui::presentation::findPageManifest("deck_full_manifest");
    assert(deck != nullptr);
    const auto* uconsole = ui::presentation::findPageManifest("uconsole_desktop_manifest");
    assert(uconsole != nullptr);
    for (std::size_t index = 0; index < deck->item_count; ++index)
    {
        assert(contains(*uconsole, deck->items[index].page_id));
    }
    assert(contains(*uconsole, ui::presentation::PageId::SkyPlot));
    assert(contains(*uconsole, ui::presentation::PageId::EnergySweep));
    assert(contains(*uconsole, ui::presentation::PageId::WalkieTalkie));
    assert(contains(*uconsole, ui::presentation::PageId::Sstv));
    assert(contains(*uconsole, ui::presentation::PageId::Diagnostics));

    const auto* cardputer = ui::presentation::findPageManifest("cardputer_compact_manifest");
    assert(cardputer != nullptr);
    assert(cardputer->item_count == 10);
    assert(contains(*cardputer, ui::presentation::PageId::Dashboard));
    assert(contains(*cardputer, ui::presentation::PageId::Chat));
    assert(contains(*cardputer, ui::presentation::PageId::Contacts));
    assert(contains(*cardputer, ui::presentation::PageId::Map));
    assert(contains(*cardputer, ui::presentation::PageId::SkyPlot));
    assert(!contains(*cardputer, ui::presentation::PageId::Gps));
    assert(contains(*cardputer, ui::presentation::PageId::Team));
    assert(contains(*cardputer, ui::presentation::PageId::Tracker));
    assert(!contains(*cardputer, ui::presentation::PageId::Sstv));
    assert(!contains(*cardputer, ui::presentation::PageId::EnergySweep));
    assert(contains(*cardputer, ui::presentation::PageId::WalkieTalkie));
    assert(contains(*cardputer, ui::presentation::PageId::Extensions));
    assert(contains(*cardputer, ui::presentation::PageId::Settings));

    const auto* node = ui::presentation::findPageManifest("node_headless_manifest");
    assert(node != nullptr);
    assert(node->item_count == 2);
    assert(contains(*node, ui::presentation::PageId::NodeStatus));
    assert(!node->items[0].visible_in_menu);

    assert(ui::presentation::findPageManifest("unknown") == nullptr);
    return 0;
}
