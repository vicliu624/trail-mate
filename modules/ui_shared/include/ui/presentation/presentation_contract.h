#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::presentation
{

// Public page archetypes that presentation packs are allowed to target.
enum class PageArchetype : std::uint8_t
{
    MenuDashboard = 0,
    BootSplash,
    WatchFace,
    ChatList,
    ChatConversation,
    ChatCompose,
    DirectoryBrowser,
    MapFocus,
    InstrumentPanel,
    ServicePanel,
    Count,
};

// Stable semantic regions that a declarative layout schema may place.
enum class RegionSlot : std::uint8_t
{
    ChromeTopBar = 0,
    ChromeStatusRow,
    ChromeFooter,
    MenuAppGrid,
    MenuBottomChips,
    BootHero,
    BootLog,
    WatchFacePrimary,
    ChatThread,
    ChatComposer,
    ChatActionBar,
    DirectoryHeader,
    DirectorySelector,
    DirectoryContent,
    MapViewport,
    MapOverlayPrimary,
    MapOverlaySecondary,
    InstrumentCanvas,
    InstrumentLegend,
    ServicePrimaryPanel,
    ServiceFooterActions,
    Count,
};

// Reusable components that firmware layout renderers may instantiate.
enum class ComponentSlot : std::uint8_t
{
    TopBarContainer = 0,
    TopBarBackButton,
    TopBarTitleLabel,
    TopBarRightLabel,
    StatusIconStrip,
    MenuAppButton,
    MenuBottomChip,
    ModalPanel,
    NotificationBanner,
    ToastBanner,
    DirectorySelectorButton,
    DirectoryEntryItem,
    InfoCard,
    ChatListItem,
    ChatBubbleSelf,
    ChatBubblePeer,
    ChatComposerEditor,
    ActionButtonPrimary,
    ActionButtonSecondary,
    StatusChip,
    Count,
};

// Bindings expose firmware data models to declarative presentation schemas.
enum class BindingSlot : std::uint8_t
{
    ScreenTitle = 0,
    ScreenSubtitle,
    StatusSummary,
    MenuAppEntries,
    MenuBottomSummary,
    BootLogLines,
    ChatThreadItems,
    ChatDraftText,
    DirectorySelectors,
    DirectoryEntries,
    DirectoryDetailDocument,
    MapViewportModel,
    MapOverlaySummary,
    InstrumentPrimarySeries,
    InstrumentLegendItems,
    ServiceBodyText,
    PackageCatalogEntries,
    SettingsSectionEntries,
    Count,
};

// Actions are the only user-triggerable verbs a presentation schema may bind.
enum class ActionSlot : std::uint8_t
{
    NavigateBack = 0,
    NavigateOpen,
    NavigateNext,
    NavigatePrevious,
    ActivatePrimary,
    ActivateSecondary,
    RefreshContent,
    InstallPackage,
    RemovePackage,
    ApplyPresentation,
    SendMessage,
    ToggleDirectorySelector,
    ZoomIn,
    ZoomOut,
    Count,
};

struct PageArchetypeInfo
{
    constexpr PageArchetypeInfo() = default;
    constexpr PageArchetypeInfo(const char* archetype_id, const char* archetype_summary)
        : id(archetype_id), summary(archetype_summary)
    {
    }

    const char* id = nullptr;
    const char* summary = nullptr;
};

struct RegionInfo
{
    constexpr RegionInfo() = default;
    constexpr RegionInfo(const char* region_id, const char* region_summary)
        : id(region_id), summary(region_summary)
    {
    }

    const char* id = nullptr;
    const char* summary = nullptr;
};

struct RegionSupportInfo
{
    constexpr RegionSupportInfo() = default;
    constexpr RegionSupportInfo(bool region_supported, bool region_required, bool region_repeatable)
        : supported(region_supported), required(region_required), repeatable(region_repeatable)
    {
    }

    bool supported = false;
    bool required = false;
    bool repeatable = false;
};

struct ComponentInfo
{
    constexpr ComponentInfo() = default;
    constexpr ComponentInfo(const char* component_id, const char* component_summary)
        : id(component_id), summary(component_summary)
    {
    }

    const char* id = nullptr;
    const char* summary = nullptr;
};

struct BindingInfo
{
    constexpr BindingInfo() = default;
    constexpr BindingInfo(const char* binding_id, const char* binding_summary, bool binding_collection)
        : id(binding_id), summary(binding_summary), collection(binding_collection)
    {
    }

    const char* id = nullptr;
    const char* summary = nullptr;
    bool collection = false;
};

struct ActionInfo
{
    constexpr ActionInfo() = default;
    constexpr ActionInfo(const char* action_id, const char* action_summary, bool action_user_visible)
        : id(action_id), summary(action_summary), user_visible(action_user_visible)
    {
    }

    const char* id = nullptr;
    const char* summary = nullptr;
    bool user_visible = false;
};

const char* page_archetype_id(PageArchetype archetype);
bool page_archetype_from_id(const char* archetype_id, PageArchetype& out_archetype);
std::size_t page_archetype_count();
bool describe_page_archetype(PageArchetype archetype, PageArchetypeInfo& out_info);

const char* region_slot_id(RegionSlot region);
bool region_slot_from_id(const char* region_id, RegionSlot& out_region);
std::size_t region_slot_count();
bool describe_region(RegionSlot region, RegionInfo& out_info);
bool describe_region_support(PageArchetype archetype, RegionSlot region, RegionSupportInfo& out_info);
bool archetype_supports_region(PageArchetype archetype, RegionSlot region);

const char* component_slot_id(ComponentSlot component);
bool component_slot_from_id(const char* component_id, ComponentSlot& out_component);
std::size_t component_slot_count();
bool describe_component(ComponentSlot component, ComponentInfo& out_info);

const char* binding_slot_id(BindingSlot binding);
bool binding_slot_from_id(const char* binding_id, BindingSlot& out_binding);
std::size_t binding_slot_count();
bool describe_binding(BindingSlot binding, BindingInfo& out_info);

const char* action_slot_id(ActionSlot action);
bool action_slot_from_id(const char* action_id, ActionSlot& out_action);
std::size_t action_slot_count();
bool describe_action(ActionSlot action, ActionInfo& out_info);

} // namespace ui::presentation
