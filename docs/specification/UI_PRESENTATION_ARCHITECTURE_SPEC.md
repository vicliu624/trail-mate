# UI Presentation Architecture Specification

Trail Mate supports embedded LVGL, ASCII/TUI, GTK, CLI, and headless product
targets. UI code must therefore be split by responsibility rather than by the
first toolkit that implemented a page.

## Three UI Layers

```text
App Service
  Business state and business actions.

Presentation Model
  UI-independent page/workspace state and actions.

Shell / Renderer
  LVGL / ASCII / GTK / CLI / Headless concrete rendering and input.
```

## App Service

App services own business-facing state and actions.

Examples:

```text
ChatService
ContactService
LocationService
MeshService
ConfigService
DeviceStatusService
```

App services must not know:

```text
LVGL
GTK
terminal handles
screen size
font objects
button widgets
layout widgets
framebuffers
```

## Presentation Model

Presentation models convert app-service state into displayable workspace state.

Examples:

```text
ChatWorkspaceModel
MapWorkspaceModel
SettingsWorkspaceModel
DeviceStatusModel
GpsStatusModel
MeshStatusModel
```

Presentation models may know:

```text
selected conversation
abstract list cursor
current workspace action set
field formatting policy
row/column/panel concepts
compact/desktop/terminal presentation profile
```

Presentation models must not know:

```text
lv_obj_t
GtkWidget
ncurses WINDOW
stdout
framebuffer
RadioLib
sx1262
GPS UART
board pin maps
```

## Renderer and Shell

Renderers know concrete UI technology:

```text
LVGL renderer: Presentation snapshot -> lv_obj_t tree
ASCII renderer: Presentation snapshot -> text grid / ANSI output
GTK renderer: Presentation snapshot -> GtkWidget tree
CLI renderer: command result -> stdout/stderr
Headless shell: snapshot/log/API export only
```

Renderer rules:

```text
Renderer consumes PresentationModel snapshots.
Renderer sends UI actions.
Renderer does not own business state.
Renderer does not call platform radio/GPS/storage adapters directly.
Renderer does not mutate app-service internals.
```

## UI Shell Roles

LVGL:

```text
embedded compact shell
single UI owner task/thread
small-screen layout profile
button/touch/encoder/keyboard inputs depending on target
```

ASCII/TUI:

```text
terminal shell
single terminal output owner
keyboard input
stable text canvas or panel model
```

GTK:

```text
desktop-class workbench shell
GTK main loop owner
keyboard/pointer input
wide layout profile
```

CLI:

```text
command shell
stateless or short-lived command execution
stdout/stderr output
```

Headless:

```text
no renderer
only API/log/snapshot exposure
```

## Forbidden Couplings

```text
lvgl_chat_page.cpp implements message deduplication
gtk_map_page.cpp parses GPS NMEA
ascii_mesh_view.cpp reads peer key store directly
AppService includes lv_obj_t or GtkWidget
PresentationModel stores terminal handles
Renderer writes direct to radio or GPS driver
```

## Allowed Couplings

```text
Renderer -> PresentationModel snapshot
Renderer -> PresentationModel action
PresentationModel -> AppService snapshot/action API
AppService -> UseCase/Domain/Protocol/Ports
CompositionRoot -> concrete renderer and service graph
```

