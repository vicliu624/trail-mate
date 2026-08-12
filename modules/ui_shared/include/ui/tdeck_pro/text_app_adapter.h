#pragma once

#include "ui/app_screen.h"

namespace ui::tdeck_pro
{

// The device page is a projection of an existing application capability.  It
// deliberately does not own radio, GPS, configuration, or message state.
enum class TextAppPageKind : unsigned char
{
    Map,
    SkyPlot,
    Network,
    Settings,
    Tracker,
    Walkie,
    Sstv,
    UsbStorage,
    Chat,
    Team,
    Contacts,
    Extensions,
    ProtocolProbe,
};

class TextAppAdapter final : public AppScreen
{
  public:
    // A Pro entry is a catalogue descriptor, not a wrapper around the legacy
    // page object.  This keeps legacy page callbacks out of the Pro link graph
    // while the adapter calls the typed read/action contracts directly.
    TextAppAdapter(const char* stable_id,
                   const char* name,
                   TextAppPageKind page_kind,
                   ui::AppLaunchMode launch_mode = ui::AppLaunchMode::Screen);

    const char* stable_id() const override;
    const char* name() const override;
    const lv_image_dsc_t* icon() const override;
    ui::AppLaunchMode launch_mode() const override;
    void enter(lv_obj_t* parent) override;
    void exit(lv_obj_t* parent) override;

    TextAppPageKind page_kind() const { return page_kind_; }

  private:
    const char* stable_id_ = "";
    const char* name_ = "";
    TextAppPageKind page_kind_;
    ui::AppLaunchMode launch_mode_ = ui::AppLaunchMode::Screen;
};

} // namespace ui::tdeck_pro
