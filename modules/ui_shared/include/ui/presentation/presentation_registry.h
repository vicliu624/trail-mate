#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::presentation
{

extern const char kBuiltinDefaultPresentationId[];
extern const char kBuiltinDirectoryStackedPresentationId[];
extern const char kOfficialDefaultPresentationId[];
extern const char kOfficialDirectoryStackedPresentationId[];

struct PresentationInfo
{
    constexpr PresentationInfo() = default;
    constexpr PresentationInfo(const char* presentation_id,
                               const char* presentation_display_name,
                               const char* presentation_base_id,
                               bool presentation_builtin)
        : id(presentation_id),
          display_name(presentation_display_name),
          base_presentation_id(presentation_base_id),
          builtin(presentation_builtin)
    {
    }

    const char* id = nullptr;
    const char* display_name = nullptr;
    const char* base_presentation_id = nullptr;
    bool builtin = false;
};

enum class MenuDashboardLayout : std::uint8_t
{
    SimpleList = 0,
    LauncherGrid,
};

enum class BootSplashLayout : std::uint8_t
{
    CenteredHeroLog = 0,
};

enum class WatchFaceLayout : std::uint8_t
{
    StandbyClock = 0,
};

enum class ChatListLayout : std::uint8_t
{
    ConversationDirectory = 0,
};

// These are builtin presentation implementations for the directory-browser
// interaction archetype. They are not firmware-owned semantics.
enum class DirectoryBrowserLayout : std::uint8_t
{
    SplitSidebar = 0,
    StackedSelectors,
};

enum class DirectoryBrowserAxis : std::uint8_t
{
    Row = 0,
    Column,
};

enum class SchemaAlign : std::uint8_t
{
    Start = 0,
    Center,
    End,
};

enum class SchemaCorner : std::uint8_t
{
    TopRight = 0,
    TopLeft,
    BottomRight,
    BottomLeft,
};

enum class SchemaFlow : std::uint8_t
{
    Column = 0,
    Row,
};

struct MenuDashboardSchema
{
    bool has_app_grid_height_pct = false;
    std::int16_t app_grid_height_pct = 70;

    bool has_app_grid_top_offset = false;
    std::int16_t app_grid_top_offset = 30;

    bool has_app_grid_pad_row = false;
    std::int16_t app_grid_pad_row = 0;

    bool has_app_grid_pad_column = false;
    std::int16_t app_grid_pad_column = 0;

    bool has_app_grid_pad_left = false;
    std::int16_t app_grid_pad_left = 0;

    bool has_app_grid_pad_right = false;
    std::int16_t app_grid_pad_right = 0;

    bool has_app_grid_align = false;
    SchemaAlign app_grid_align = SchemaAlign::Center;

    bool has_bottom_chips_height = false;
    std::int16_t bottom_chips_height = 30;

    bool has_bottom_chips_pad_left = false;
    std::int16_t bottom_chips_pad_left = 0;

    bool has_bottom_chips_pad_right = false;
    std::int16_t bottom_chips_pad_right = 0;

    bool has_bottom_chips_pad_column = false;
    std::int16_t bottom_chips_pad_column = 0;
};

struct BootSplashSchema
{
    bool has_hero_offset_x = false;
    std::int16_t hero_offset_x = 0;

    bool has_hero_offset_y = false;
    std::int16_t hero_offset_y = 0;

    bool has_log_inset_x = false;
    std::int16_t log_inset_x = 10;

    bool has_log_bottom_inset = false;
    std::int16_t log_bottom_inset = 8;

    bool has_log_align = false;
    SchemaAlign log_align = SchemaAlign::Start;
};

struct WatchFaceSchema
{
    bool has_primary_width_pct = false;
    std::int16_t primary_width_pct = 100;

    bool has_primary_height_pct = false;
    std::int16_t primary_height_pct = 100;

    bool has_primary_offset_x = false;
    std::int16_t primary_offset_x = 0;

    bool has_primary_offset_y = false;
    std::int16_t primary_offset_y = 0;
};

struct DirectoryBrowserSchema
{
    bool has_axis = false;
    DirectoryBrowserAxis axis = DirectoryBrowserAxis::Row;

    bool has_selector_first = false;
    bool selector_first = true;

    bool has_selector_width = false;
    std::int16_t selector_width = 80;

    bool has_selector_pad_all = false;
    std::int16_t selector_pad_all = 0;

    bool has_selector_pad_row = false;
    std::int16_t selector_pad_row = 0;

    bool has_selector_gap_row = false;
    std::int16_t selector_gap_row = 0;

    bool has_selector_gap_column = false;
    std::int16_t selector_gap_column = 0;

    bool has_selector_margin_after = false;
    std::int16_t selector_margin_after = 0;

    bool has_content_pad_all = false;
    std::int16_t content_pad_all = 0;

    bool has_content_pad_row = false;
    std::int16_t content_pad_row = 0;

    bool has_content_pad_left = false;
    std::int16_t content_pad_left = 0;

    bool has_content_pad_right = false;
    std::int16_t content_pad_right = 0;

    bool has_content_pad_top = false;
    std::int16_t content_pad_top = 0;

    bool has_content_pad_bottom = false;
    std::int16_t content_pad_bottom = 0;

    bool has_selector_button_height = false;
    std::int16_t selector_button_height = 28;

    bool has_selector_button_stacked_min_width = false;
    std::int16_t selector_button_stacked_min_width = 88;

    bool has_selector_button_stacked_flex_grow = false;
    bool selector_button_stacked_flex_grow = false;
};

struct ChatConversationSchema
{
    bool has_root_pad_row = false;
    std::int16_t root_pad_row = 0;

    bool has_thread_pad_row = false;
    std::int16_t thread_pad_row = 0;

    bool has_action_bar_position = false;
    bool action_bar_first = false;

    bool has_action_bar_height = false;
    std::int16_t action_bar_height = 30;

    bool has_action_bar_pad_column = false;
    std::int16_t action_bar_pad_column = 0;

    bool has_action_bar_align = false;
    SchemaAlign action_bar_align = SchemaAlign::Center;

    bool has_primary_button_width = false;
    std::int16_t primary_button_width = 120;

    bool has_primary_button_height = false;
    std::int16_t primary_button_height = 28;
};

struct ChatComposeSchema
{
    bool has_root_pad_row = false;
    std::int16_t root_pad_row = 0;

    bool has_content_pad_all = false;
    std::int16_t content_pad_all = 0;

    bool has_content_pad_row = false;
    std::int16_t content_pad_row = 0;

    bool has_action_bar_position = false;
    bool action_bar_first = false;

    bool has_action_bar_height = false;
    std::int16_t action_bar_height = 0;

    bool has_action_bar_pad_left = false;
    std::int16_t action_bar_pad_left = 0;

    bool has_action_bar_pad_right = false;
    std::int16_t action_bar_pad_right = 0;

    bool has_action_bar_pad_top = false;
    std::int16_t action_bar_pad_top = 0;

    bool has_action_bar_pad_bottom = false;
    std::int16_t action_bar_pad_bottom = 0;

    bool has_action_bar_pad_column = false;
    std::int16_t action_bar_pad_column = 0;

    bool has_action_bar_align = false;
    SchemaAlign action_bar_align = SchemaAlign::Start;

    bool has_primary_button_width = false;
    std::int16_t primary_button_width = 70;

    bool has_primary_button_height = false;
    std::int16_t primary_button_height = 28;

    bool has_secondary_button_width = false;
    std::int16_t secondary_button_width = 70;

    bool has_secondary_button_height = false;
    std::int16_t secondary_button_height = 28;
};

struct MapFocusSchema
{
    bool has_root_pad_row = false;
    std::int16_t root_pad_row = 0;

    bool has_primary_overlay_position = false;
    SchemaCorner primary_overlay_position = SchemaCorner::TopRight;

    bool has_primary_overlay_width = false;
    std::int16_t primary_overlay_width = 85;

    bool has_primary_overlay_offset_x = false;
    std::int16_t primary_overlay_offset_x = 0;

    bool has_primary_overlay_offset_y = false;
    std::int16_t primary_overlay_offset_y = 0;

    bool has_primary_overlay_gap_row = false;
    std::int16_t primary_overlay_gap_row = 0;

    bool has_primary_overlay_gap_column = false;
    std::int16_t primary_overlay_gap_column = 0;

    bool has_primary_overlay_flow = false;
    SchemaFlow primary_overlay_flow = SchemaFlow::Column;

    bool has_secondary_overlay_position = false;
    SchemaCorner secondary_overlay_position = SchemaCorner::TopLeft;

    bool has_secondary_overlay_width = false;
    std::int16_t secondary_overlay_width = 90;

    bool has_secondary_overlay_offset_x = false;
    std::int16_t secondary_overlay_offset_x = 0;

    bool has_secondary_overlay_offset_y = false;
    std::int16_t secondary_overlay_offset_y = 0;

    bool has_secondary_overlay_gap_row = false;
    std::int16_t secondary_overlay_gap_row = 0;

    bool has_secondary_overlay_gap_column = false;
    std::int16_t secondary_overlay_gap_column = 0;

    bool has_secondary_overlay_flow = false;
    SchemaFlow secondary_overlay_flow = SchemaFlow::Column;
};

struct InstrumentPanelSchema
{
    bool has_root_pad_row = false;
    std::int16_t root_pad_row = 0;

    bool has_body_flow = false;
    SchemaFlow body_flow = SchemaFlow::Row;

    bool has_body_pad_all = false;
    std::int16_t body_pad_all = 0;

    bool has_body_pad_row = false;
    std::int16_t body_pad_row = 0;

    bool has_body_pad_column = false;
    std::int16_t body_pad_column = 0;

    bool has_canvas_grow = false;
    bool canvas_grow = false;

    bool has_canvas_pad_all = false;
    std::int16_t canvas_pad_all = 0;

    bool has_canvas_pad_row = false;
    std::int16_t canvas_pad_row = 0;

    bool has_canvas_pad_column = false;
    std::int16_t canvas_pad_column = 0;

    bool has_legend_grow = false;
    bool legend_grow = false;

    bool has_legend_pad_all = false;
    std::int16_t legend_pad_all = 0;

    bool has_legend_pad_row = false;
    std::int16_t legend_pad_row = 0;

    bool has_legend_pad_column = false;
    std::int16_t legend_pad_column = 0;
};

struct ServicePanelSchema
{
    bool has_root_pad_row = false;
    std::int16_t root_pad_row = 0;

    bool has_body_pad_all = false;
    std::int16_t body_pad_all = 0;

    bool has_body_pad_row = false;
    std::int16_t body_pad_row = 0;

    bool has_body_pad_column = false;
    std::int16_t body_pad_column = 0;

    bool has_primary_panel_pad_all = false;
    std::int16_t primary_panel_pad_all = 0;

    bool has_primary_panel_pad_row = false;
    std::int16_t primary_panel_pad_row = 0;

    bool has_primary_panel_pad_column = false;
    std::int16_t primary_panel_pad_column = 0;

    bool has_footer_actions_height = false;
    std::int16_t footer_actions_height = 0;

    bool has_footer_actions_pad_all = false;
    std::int16_t footer_actions_pad_all = 0;

    bool has_footer_actions_pad_row = false;
    std::int16_t footer_actions_pad_row = 0;

    bool has_footer_actions_pad_column = false;
    std::int16_t footer_actions_pad_column = 0;
};

enum class ChatConversationLayout : std::uint8_t
{
    Standard = 0,
};

enum class ChatComposeLayout : std::uint8_t
{
    Standard = 0,
};

enum class MapFocusLayout : std::uint8_t
{
    OverlayPanels = 0,
};

enum class InstrumentPanelLayout : std::uint8_t
{
    SplitCanvasLegend = 0,
};

enum class ServicePanelLayout : std::uint8_t
{
    PrimaryPanelWithFooter = 0,
};

void reset_active_presentation();
bool set_active_presentation(const char* presentation_id);
const char* active_presentation_id();
std::uint32_t active_presentation_revision();
void reload_installed_presentations();

std::size_t builtin_presentation_count();
bool builtin_presentation_at(std::size_t index, PresentationInfo& out_info);
std::size_t presentation_count();
bool presentation_at(std::size_t index, PresentationInfo& out_info);
bool describe_presentation(const char* presentation_id, PresentationInfo& out_info);
const char* preferred_default_presentation_id();

MenuDashboardLayout active_menu_dashboard_layout();
bool resolve_active_menu_dashboard_schema(MenuDashboardSchema& out_schema);
BootSplashLayout active_boot_splash_layout();
bool resolve_active_boot_splash_schema(BootSplashSchema& out_schema);
WatchFaceLayout active_watch_face_layout();
bool resolve_active_watch_face_schema(WatchFaceSchema& out_schema);
ChatListLayout active_chat_list_layout();
DirectoryBrowserLayout active_directory_browser_layout();
bool resolve_active_directory_browser_schema(DirectoryBrowserSchema& out_schema);
bool directory_browser_uses_stacked_selectors();
ChatConversationLayout active_chat_conversation_layout();
bool resolve_active_chat_conversation_schema(ChatConversationSchema& out_schema);
ChatComposeLayout active_chat_compose_layout();
bool resolve_active_chat_compose_schema(ChatComposeSchema& out_schema);
MapFocusLayout active_map_focus_layout();
bool resolve_active_map_focus_schema(MapFocusSchema& out_schema);
InstrumentPanelLayout active_instrument_panel_layout();
bool resolve_active_instrument_panel_schema(InstrumentPanelSchema& out_schema);
ServicePanelLayout active_service_panel_layout();
bool resolve_active_service_panel_schema(ServicePanelSchema& out_schema);

} // namespace ui::presentation
