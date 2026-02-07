/**
 * UI Wireframe / Layout Tree (Node Info - matches provided mock)
 * --------------------------------------------------------------------
 *
 * Root (COLUMN)
 * +------------------------------------------------------------------+
 * | Header (TopBar)  [< Back]   NODE INFO                [Battery]   |
 * +------------------------------------------------------------------+
 * | TopRow (ROW, grow)                                                |
 * |  +-------------------------+  +---------------------------------+ |
 * |  | Info Card (left)         |  | Location Card (right)          | |
 * |  | +---------------------+  |  | +---------------------------+ | |
 * |  | | Avatar (S)          |  |  | | Title: Location           | | |
 * |  | | Name: ALFA-3        |  |  | +---------------------------+ | |
 * |  | | Desc: Relay Node    |  |  | | Map Placeholder           | | |
 * |  | +---------------------+  |  | | [Map image]               | | |
 * |  | | ID: !a1b2c3          |  |  | +---------------------------+ | |
 * |  | | Role: Router         |  |  | | Lat,Lon +/-Acc  Alt       | | |
 * |  | +---------------------+  |  | +---------------------------+ | |
 * |  |                         |  | | Updated: 2m ago            | | |
 * |  +-------------------------+  | +---------------------------+ | |
 * |                               +---------------------------------+ |
 * |                                                                  |
 * | Link Panel (full width)                                          |
 * |  +------------------------------------------------------------+  |
 * |  | Title: Link                                                |  |
 * |  +------------------------------------------------------------+  |
 * |  | RSSI: -112  SNR: 7.5     Ch: 478.875  SF: 7  BW: 125k      | |
 * |  +------------------------------------------------------------+  |
 * |  | Hop: 2  Last heard: 18s                                   |  |
 * |  +------------------------------------------------------------+  |
 * +------------------------------------------------------------------+
 *
 * Tree view:
 * Root(COL)
 * ├─ Header
 * └─ Content(COL)
 *    ├─ TopRow(ROW, grow=1)
 *    │  ├─ InfoCard(COL)
 *    │  │  ├─ InfoHeader(ROW)
 *    │  │  └─ InfoFooter(ROW)
 *    │  └─ LocationCard(COL, grow=1)
 *    │     ├─ LocationHeader
 *    │     ├─ LocationMap(grow=1)
 *    │     ├─ LocationCoords
 *    │     └─ LocationUpdated
 *    └─ LinkPanel(COL)
 *       ├─ LinkHeader
 *       ├─ LinkRow1
 *       └─ LinkRow2
 *
 * Preconditions:
 * - Root uses LV_FLEX_FLOW_COLUMN.
 * - TopRow uses LV_FLEX_FLOW_ROW.
 * - LinkPanel uses LV_FLEX_FLOW_COLUMN.
 */

/**
 * @file node_info_page_layout.cpp
 * @brief Node info page layout
 */

#include "node_info_page_layout.h"
#include "../../widgets/top_bar.h"

namespace node_info
{
namespace ui
{
namespace layout
{

namespace
{
constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 222;
constexpr int kTopBarHeight = 30;
constexpr int kContentY = kTopBarHeight;
constexpr int kContentHeight = 192;
constexpr int kContentPadX = 8;
constexpr int kContentPadY = 4;
constexpr int kCardGapX = 8;
constexpr int kRowGap = 8;

constexpr int kTopRowX = kContentPadX;
constexpr int kTopRowY = kContentPadY;
constexpr int kTopRowWidth = kScreenWidth - (kContentPadX * 2);
constexpr int kTopRowHeight = 118;

constexpr int kInfoCardWidth = 190;
constexpr int kInfoCardHeight = 118;
constexpr int kLocationCardWidth = 266;
constexpr int kLocationCardHeight = 118;

constexpr int kLinkPanelX = kContentPadX;
constexpr int kLinkPanelY = kTopRowY + kTopRowHeight + kRowGap;
constexpr int kLinkPanelWidth = kTopRowWidth;
constexpr int kLinkPanelHeight = 60;

constexpr int kLocationHeaderHeight = 22;
constexpr int kLocationMapHeight = 52;
constexpr int kLocationCoordsHeight = 18;
constexpr int kLocationUpdatedHeight = 18;

constexpr int kInfoHeaderHeight = 70;
constexpr int kInfoFooterHeight = 36;

constexpr int kLinkHeaderHeight = 20;
constexpr int kLinkRowHeight = 16;

void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, kScreenWidth, kScreenHeight);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_header(lv_obj_t* root)
{
    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_size(header, kScreenWidth, kTopBarHeight);
    lv_obj_set_pos(header, 0, 0);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_content(lv_obj_t* root)
{
    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_size(content, kScreenWidth, kContentHeight);
    lv_obj_set_pos(content, 0, kContentY);
    lv_obj_set_style_pad_all(content, 0, 0);
    make_non_scrollable(content);
    return content;
}

lv_obj_t* create_top_row(lv_obj_t* content)
{
    lv_obj_t* row = lv_obj_create(content);
    lv_obj_set_size(row, kTopRowWidth, kTopRowHeight);
    lv_obj_set_pos(row, kTopRowX, kTopRowY);
    lv_obj_set_style_pad_all(row, 0, 0);
    make_non_scrollable(row);
    return row;
}

lv_obj_t* create_info_card(lv_obj_t* top_row)
{
    lv_obj_t* card = lv_obj_create(top_row);
    lv_obj_set_size(card, kInfoCardWidth, kInfoCardHeight);
    lv_obj_set_pos(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    make_non_scrollable(card);
    return card;
}

lv_obj_t* create_info_header(lv_obj_t* info_card)
{
    lv_obj_t* header = lv_obj_create(info_card);
    lv_obj_set_size(header, kInfoCardWidth, kInfoHeaderHeight);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_info_footer(lv_obj_t* info_card)
{
    lv_obj_t* footer = lv_obj_create(info_card);
    lv_obj_set_size(footer, 170, kInfoFooterHeight);
    lv_obj_set_pos(footer, 10, 70);
    lv_obj_set_style_pad_all(footer, 0, 0);
    make_non_scrollable(footer);
    return footer;
}

lv_obj_t* create_location_card(lv_obj_t* top_row)
{
    lv_obj_t* card = lv_obj_create(top_row);
    lv_obj_set_size(card, kLocationCardWidth, kLocationCardHeight);
    lv_obj_set_pos(card, kInfoCardWidth + kCardGapX, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    make_non_scrollable(card);
    return card;
}

lv_obj_t* create_location_header(lv_obj_t* location_card)
{
    lv_obj_t* header = lv_obj_create(location_card);
    lv_obj_set_size(header, kLocationCardWidth, kLocationHeaderHeight);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_location_map(lv_obj_t* location_card)
{
    lv_obj_t* map = lv_obj_create(location_card);
    lv_obj_set_size(map, 246, kLocationMapHeight);
    lv_obj_set_pos(map, 10, 30);
    lv_obj_set_style_pad_all(map, 0, 0);
    make_non_scrollable(map);
    return map;
}

lv_obj_t* create_location_coords(lv_obj_t* location_card)
{
    lv_obj_t* coords = lv_obj_create(location_card);
    lv_obj_set_size(coords, 246, kLocationCoordsHeight);
    lv_obj_set_pos(coords, 10, 86);
    lv_obj_set_style_pad_all(coords, 0, 0);
    make_non_scrollable(coords);
    return coords;
}

lv_obj_t* create_location_updated(lv_obj_t* location_card)
{
    lv_obj_t* updated = lv_obj_create(location_card);
    lv_obj_set_size(updated, 246, kLocationUpdatedHeight);
    lv_obj_set_pos(updated, 10, 106);
    lv_obj_set_style_pad_all(updated, 0, 0);
    make_non_scrollable(updated);
    return updated;
}

lv_obj_t* create_link_panel(lv_obj_t* content)
{
    lv_obj_t* panel = lv_obj_create(content);
    lv_obj_set_size(panel, kLinkPanelWidth, kLinkPanelHeight);
    lv_obj_set_pos(panel, kLinkPanelX, kLinkPanelY);
    lv_obj_set_style_pad_all(panel, 0, 0);
    make_non_scrollable(panel);
    return panel;
}

lv_obj_t* create_link_header(lv_obj_t* link_panel)
{
    lv_obj_t* header = lv_obj_create(link_panel);
    lv_obj_set_size(header, kLinkPanelWidth, kLinkHeaderHeight);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_link_row(lv_obj_t* link_panel)
{
    lv_obj_t* row = lv_obj_create(link_panel);
    lv_obj_set_size(row, kLinkPanelWidth, kLinkRowHeight);
    lv_obj_set_style_pad_all(row, 0, 0);
    make_non_scrollable(row);
    return row;
}

} // namespace layout
} // namespace ui
} // namespace node_info
