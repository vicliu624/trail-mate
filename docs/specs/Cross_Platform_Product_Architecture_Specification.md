# Cross-Platform Product Architecture Specification

```text
docs/specification/CROSS_PLATFORM_PRODUCT_ARCHITECTURE_SPEC.md
```

# 1. 核心问题重新定义

Trail Mate 不是单一固件，也不是单一 Linux app。它是一个跨平台户外通信/导航产品族。

它面对的变体不是一维的。

```text
ESP32
  - T-LoRa Pager
  - T-Deck
  - T-Deck Pro
  - T-Watch S3
  - Cardputer Zero
  - 其他 SX1262 / SX127x / GPS / 屏幕组合

nRF52
  - GAT562
  - RNode-like endpoint
  - BLE phone host
  - LoRa coprocessor
  - 独立低功耗 Mesh 节点

Linux
  - uConsole AIO2
  - RPi
  - Cardputer Zero Linux shell
  - desktop simulator
  - headless daemon
```

同时 UI 也不是一种：

```text
- LVGL embedded UI
- ASCII / TUI UI
- GTK desktop-class UI
- CLI
- Headless daemon
- Web / remote control future shell
```

所以规格的目标不是“把代码分成 core/platform/app”这么简单，而是要保证：

```text
同一个业务语义不因板子、芯片、OS、UI 技术栈、调度模型而漂移。
```

------

# 2. 总分层模型

完整架构应该是：

```text
Target Manifest
    ↓
Product Composition
    ↓
App Services
    ↓
Presentation Models
    ↓
Shell / Renderer
```

并行支撑层：

```text
Board Package
Platform Runtime
Capability Drivers
Protocol Cores
Storage Backends
Concurrency Policy
```

更具体：

```text
docs/targets/*.yaml
    声明产品目标

boards/*
platform/*/boards/*
    声明硬件事实

platform/*
    提供平台能力适配

modules/core_*
    提供共享领域、协议、用例、表现模型

apps/*
    组合目标产品

shells 或 apps/*/ui/*
    具体 UI 技术栈
```

------

# 3. 必须引入 Target / Board / Platform 三重区分

之前我们混用了：

```text
ESP32 target
ESP32 platform
ESP32 board
```

这三个不能再混。

------

## 3.1 Platform

Platform 是芯片/OS/运行环境。

例如：

```text
esp32
nrf52
linux
test
simulator
```

Platform 决定：

```text
- 可用 SDK
- 线程/task 模型
- ISR 模型
- 存储 API
- 随机数 API
- 时间 API
- BLE stack
- filesystem 能力
- 构建工具链
```

Platform 不决定：

```text
- 哪个 LoRa 芯片接在哪个 pin
- 哪个屏幕分辨率
- 产品 UI 形态
- 用户看到哪些功能
```

------

## 3.2 Board Variant

Board 是具体硬件变体。

例如：

```text
t_lora_pager
t_deck
t_deck_pro
t_watch_s3
gat562_mesh_evb_pro
uconsole_aio2
cardputer_zero
```

Board 决定硬件事实：

```text
- LoRa chip 型号
- LoRa SPI bus / reset / busy / dio1 / tx_en
- GPS UART / power pin / PPS
- display driver / resolution / rotation
- input device
- battery / charger / PMU
- SD card / filesystem
- BLE antenna / coexistence constraints
```

Board 不决定：

```text
- direct message 怎么发
- Meshtastic BLE 怎么响应
- MeshCore command 怎么解释
- ChatService 怎么存消息
- UI 页面怎么布局
```

------

## 3.3 Target

Target 是产品构成。

例如：

```text
trailmate-tpager-esp32
trailmate-tdeck-esp32
trailmate-gat562-nrf52-node
trailmate-uconsole-aio2-linux
trailmate-uconsole-nrf52-radio-proxy
trailmate-linux-headless
trailmate-linux-ascii
trailmate-linux-gtk
trailmate-esp32-lvgl
```

Target 决定：

```text
- 用哪个 platform
- 用哪个 board
- 哪些 capability 启用
- 状态权威在哪里
- 使用哪个 UI shell
- 使用哪个 storage backend
- 使用哪个 concurrency policy
- 使用哪个 protocol profile
```

Target 必须有 manifest。

------

# 4. Target Manifest 必须升级

之前的 manifest 只覆盖 capability/authority。现在要覆盖：

```text
platform
board
runtime
concurrency
ui_shell
renderer
presentation_model
capabilities
protocols
authorities
storage
build features
race policy
```

建议模板：

```yaml
target: "trailmate-tpager-esp32"
status: "draft"
updated: "2026-05-12"

product:
  family: "trail-mate"
  form_factor: "handheld | watch | linux_handheld | headless | simulator"
  interaction_class: "embedded_lvgl | ascii_tui | desktop_gtk | cli | headless"

platform:
  kind: "esp32 | nrf52 | linux | test"
  sdk: "arduino_esp32 | esp_idf | zephyr | arduino_nrf52 | linux_posix"
  language_profile: "embedded_cpp | linux_cpp"
  memory_tier: "tiny | small | medium | large"
  filesystem: "none | littlefs | sd | posix"
  dynamic_allocation: "forbidden | limited | allowed"

board:
  id: "t_lora_pager"
  package: "platform/esp/boards/t_lora_pager"
  display:
    present: true
    width: 222
    height: 480
    rotation: 90
    driver: "..."
  input:
    keyboard: false
    buttons: true
    touch: false
    encoder: false
    trackball: false
  lora:
    present: true
    chip: "sx1262"
    owner: "board_facts_only"
  gps:
    present: true
    owner: "board_facts_only"
  power:
    battery: true
    charger: true
    deep_sleep: true

runtime:
  scheduler: "freertos | zephyr_workqueue | linux_event_loop | single_thread_tick | test_fake"
  ui_thread: "main_task | dedicated_task | gtk_main | terminal_loop | none"
  radio_thread: "task | workqueue | event_loop | poll_tick"
  gps_thread: "task | workqueue | event_loop | poll_tick"
  ble_thread: "stack_callback | task | event_loop | none"
  isr_policy: "defer_to_queue"
  lock_policy: "single_owner | mutex | message_passing | immutable_snapshot"

ui:
  shell: "lvgl | ascii | gtk | cli | headless"
  renderer: "lvgl | ascii_canvas | gtk4 | stdout | none"
  presentation_model_host: "esp32 | nrf52 | linux | none"
  layout_profile: "compact_handheld | watch | desktop_workbench | terminal_panel | none"
  input_model: "buttons | keyboard | touch | mixed | none"

execution:
  app_host: "esp32 | nrf52 | linux | none"
  mesh_core_host: "esp32 | nrf52 | linux | none"
  gps_core_host: "esp32 | nrf52 | linux | none"
  phone_core_host: "esp32 | nrf52 | linux | none"
  hostlink_core_host: "esp32 | nrf52 | linux | none"
  ui_host: "esp32 | nrf52 | linux | none"

authorities:
  identity: "esp32 | nrf52 | linux | none"
  peer_key_store: "esp32 | nrf52 | linux | none"
  node_store: "esp32 | nrf52 | linux | none"
  message_store: "esp32 | nrf52 | linux | none"
  location: "esp32 | nrf52 | linux | none"
  time: "esp32 | nrf52 | linux | none"
  config: "esp32 | nrf52 | linux | none"
  device_status: "esp32 | nrf52 | linux | none"
  ui_state: "esp32 | nrf52 | linux | none"

capabilities:
  lora:
    state: "unsupported | absent | present | unbound | ready | degraded | simulated | error"
    endpoint_host: "esp32 | nrf52 | linux | external | none"
    mode: "local_radio | packet_proxy | command_proxy | none"
  gps:
    state: "unsupported | absent | present | unbound | ready | degraded | simulated | error"
    endpoint_host: "esp32 | nrf52 | linux | external | none"
    mode: "local_uart | raw_stream_proxy | fix_proxy | command_proxy | simulated | none"
  ble:
    state: "unsupported | absent | present | unbound | ready | degraded | simulated | error"
    endpoint_host: "esp32 | nrf52 | linux | none"
    stack: "nimble | bluefruit | bluez | none"
    roles:
      meshtastic_phone: "server | client | disabled | unsupported"
      meshcore_phone: "server | client | disabled | unsupported"
      trailmate_control: "server | client | disabled | unsupported"

protocols:
  meshtastic:
    enabled: true
    radio_profile_source: "shared_core"
    ble_phone_core: "shared_core | disabled"
  meshcore:
    enabled: true
    radio_profile_source: "shared_core"
    ble_phone_core: "shared_core | disabled"
  hostlink:
    enabled: true
    frame_core: "shared_core"

storage:
  backend: "esp_nvs | nrf52_flash | sqlite | file | fake | none"
  identity: "..."
  peer_keys: "..."
  config: "..."
  messages: "..."
  node_store: "..."

race_policy:
  app_to_ui: "snapshot"
  radio_to_app: "event_queue"
  gps_to_app: "event_queue"
  ble_to_app: "command_queue"
  isr_to_driver: "defer_only"
  store_access: "single_owner_or_mutex"
  ui_update: "ui_thread_only"

build:
  app: "apps/..."
  board_package: "platform/.../boards/..."
  enabled_modules:
    - "core_chat"
    - "core_mesh"
    - "core_gps"
    - "core_phone"
    - "core_device"
    - "ui_presentation"
  disabled_modules: []
```

------

# 5. Board Package 规格

必须新增一份板级规格：

```text
docs/specification/BOARD_PACKAGE_SPEC.md
```

## 5.1 Board Package 职责

Board package 只拥有硬件事实和板级 bring-up。

允许：

```text
- pin map
- bus map
- power rails
- display facts
- input facts
- battery facts
- radio hardware facts
- gps hardware facts
- board init
- board sleep/wake hooks
```

禁止：

```text
- ChatService
- ContactService
- DirectMessageService
- MeshtasticPhoneCore
- MeshCorePhoneCore
- ConfigService 业务解释
- UI 页面状态
- BLE phone protocol
- GPS 业务策略
- LoRa packet 语义
```

## 5.2 Board Package 结构

```text
platform/
  esp/
    boards/
      t_lora_pager/
        board_manifest.yaml
        include/board/t_lora_pager_board.h
        src/t_lora_pager_board.cpp
        include/board/t_lora_pager_pins.h

      t_deck/
      t_watch_s3/

  nrf52/
    boards/
      gat562_mesh_evb_pro/
        board_manifest.yaml
        include/board/gat562_board.h
        src/gat562_board.cpp

  linux/
    boards/
      uconsole_aio2/
        board_manifest.yaml
        include/board/uconsole_aio2_facts.h
        src/uconsole_aio2_facts.cpp
```

## 5.3 Board Manifest

每个板子必须有：

```yaml
board: "t_lora_pager"
platform: "esp32"

facts:
  display:
    present: true
    width: 222
    height: 480
    rotation: 90
    driver: "st7789"
  lora:
    present: true
    chip: "sx1262"
    spi_bus: "spi2"
    reset_pin: 8
    busy_pin: 7
    dio1_pin: 33
    power_enable_pin: null
    dio2_as_rf_switch: true
    dio3_tcxo_voltage: 1.8
  gps:
    present: true
    uart: "uart1"
    power_enable_pin: 21
    pps_pin: null
  input:
    buttons: true
    keyboard: false
    touch: false
  storage:
    sd: true
    internal_flash: true
  power:
    battery: true
    charger: true
```

这和 target manifest 不同：

```text
board_manifest.yaml 说明硬件有什么。
target_manifest.yaml 说明产品怎么使用这些硬件。
```

------

# 6. Runtime / Concurrency 规格

这是你刚才指出的重点：不同 ESP32 板子、nRF52、Linux 都会面对不同竞态。

必须新增：

```text
docs/specification/RUNTIME_CONCURRENCY_SPEC.md
```

## 6.1 竞态来源

Trail Mate 至少有这些并发源：

```text
- Radio IRQ
- Radio RX task / poll loop
- Radio TX completion
- GPS UART RX
- GPS parser task
- BLE stack callback
- BLE notify queue
- UI event loop
- LVGL tick / input
- GTK main loop
- ASCII terminal input loop
- HostLink USB/serial RX
- Storage write
- Config update
- Power/sleep/wake
- Timer / retry / ACK timeout
```

这些如果不规格化，会导致：

```text
- UI 在非 UI 线程更新
- BLE callback 直接改 ChatService
- radio IRQ 直接触发业务
- GPS parser 和 UI 同时读写 location
- config 更新和 radio apply 并发
- sleep 时 radio/gps/ble 还在访问硬件
- SQLite/NVS/flash 并发写
```

------

## 6.2 总规则：外部输入全部转成事件

禁止：

```text
ISR -> 直接业务
BLE callback -> 直接业务
UART callback -> 直接业务
UI callback -> 直接改底层 driver
```

必须：

```text
ISR / callback / transport
    -> event queue / command queue
        -> app service owner
            -> state update
                -> presentation snapshot
                    -> UI thread render
```

------

## 6.3 单 owner 规则

每个 mutable service 必须有 owner context。

```text
ChatService owner: app service context
MeshSession owner: mesh context
GpsService owner: gps context
ConfigService owner: app service context
DeviceStatus owner: device service context
UI State owner: UI context
```

其他线程/task 不能直接改，只能投递 command/event。

------

## 6.4 Snapshot 规则

UI 不应该直接读 mutable service 内部对象。

应该：

```text
App Service
    -> immutable snapshot / projection
        -> Presentation Model
            -> Shell Renderer
```

例如：

```cpp
struct ChatListSnapshot {
    ConversationRow rows[32];
    size_t count;
    uint32_t version;
};
```

UI 获取 snapshot 后渲染。
后台继续更新不会破坏 UI。

------

## 6.5 UI Thread Only 规则

不同 UI 技术栈都要遵守“UI 只能在自己的 UI context 更新”。

### LVGL

```text
- lv_obj_* 只能在 LVGL owner task/thread 调用
- 其他 task 只能投递 UI command
- 不能从 BLE/radio/GPS callback 直接改 LVGL object
```

### GTK

```text
- GTK widget 只能在 GTK main loop 更新
- 后台线程通过 idle/source/channel 投递更新
```

### ASCII / TUI

```text
- terminal buffer 有单一 renderer owner
- 输入线程和刷新线程不能同时写 stdout
- 后台事件只更新 model 或提交 render request
```

### Headless

```text
- 不存在 UI thread
- 只导出 snapshot / logs / API
```

------

## 6.6 ISR 规则

ISR 只能：

```text
- 清中断
- 记录最小标志
- 投递 lightweight event
```

ISR 禁止：

```text
- malloc/free
- storage write
- BLE notify
- UI update
- protobuf encode/decode
- direct message send
- GPS parse
```

------

## 6.7 Storage 并发规则

Storage backend 必须声明：

```text
- single writer
- multi reader
- transaction support
- async write support
- erase/write blocking behavior
```

ESP32 NVS、nRF52 flash、SQLite 完全不同：

```text
ESP32 NVS:
  blocking, limited write endurance, usually mutex protected

nRF52 flash:
  erase/write expensive, often async, must avoid callback/context conflict

SQLite:
  transaction, file lock, can support stronger consistency but must avoid UI thread blocking
```

所以 store port 不能只是：

```cpp
bool save(...)
```

至少要有：

```cpp
StoreResult
StoreCapabilities
StoreConcurrencyPolicy
```

------

# 7. UI 架构必须拆成 Presentation Model + Renderer

你说的 ASCII/LVGL/GTK 是关键。
不能让业务层为了 LVGL 组织状态，也不能让 GTK 复用 LVGL 页面。

必须新增：

```text
docs/specification/UI_PRESENTATION_ARCHITECTURE_SPEC.md
```

------

## 7.1 三层 UI

```text
App Service
    业务状态与动作

Presentation Model
    UI 无关的页面/工作区状态与动作

Shell / Renderer
    LVGL / ASCII / GTK / CLI / Headless
```

------

## 7.2 App Service

例如：

```text
ChatService
ContactService
TeamService
LocationService
MeshService
ConfigService
DeviceStatusService
```

不允许知道：

```text
LVGL
GTK
terminal
screen size
font
button
layout
```

------

## 7.3 Presentation Model

Presentation Model 负责把 App Service 变成“可展示状态”。

例如：

```text
ChatWorkspaceModel
MapWorkspaceModel
SettingsWorkspaceModel
DeviceStatusModel
GpsStatusModel
MeshStatusModel
```

它可以知道：

```text
- 当前选中哪个 conversation
- 当前列表滚动位置的抽象 cursor
- 当前 workspace 的 actions
- 字段格式化
- 行/列/面板概念
- compact / desktop / terminal 的 presentation profile
```

但不能知道：

```text
lv_obj_t
GtkWidget
ncurses WINDOW
stdout
framebuffer
```

------

## 7.4 Renderer / Shell

Renderer 才知道具体技术。

```text
LVGL renderer:
  ChatWorkspaceModel -> lv_obj_t tree

ASCII renderer:
  ChatWorkspaceModel -> text grid / ANSI escape

GTK renderer:
  ChatWorkspaceModel -> GtkWidget tree

CLI renderer:
  ChatWorkspaceModel -> command output
```

------

## 7.5 UI 技术栈不允许拥有业务

禁止：

```text
lvgl_chat_page.cpp 里直接实现 message dedup
gtk_map_page.cpp 里直接解析 GPS NMEA
ascii_mesh_view.cpp 里直接读 peer key store
```

允许：

```text
renderer 调用 presentation model action:
  sendMessage()
  selectContact()
  toggleLayer()
  applyConfigPatch()
```

------

# 8. UI Target Profile

不同 UI 应该有 profile，而不是靠 ifdef 到处判断。

```yaml
ui:
  shell: "lvgl"
  renderer: "lvgl"
  layout_profile: "compact_handheld"
  screen:
    width: 222
    height: 480
    density: "small"
  input_model:
    primary: "buttons"
    text_input: "limited"
```

ASCII：

```yaml
ui:
  shell: "ascii"
  renderer: "ascii_canvas"
  layout_profile: "terminal_panel"
  terminal:
    min_columns: 80
    min_rows: 24
    color: "ansi"
  input_model:
    primary: "keyboard"
```

GTK：

```yaml
ui:
  shell: "gtk"
  renderer: "gtk4"
  layout_profile: "desktop_workbench"
  window:
    default_width: 1280
    default_height: 720
  input_model:
    primary: "keyboard_pointer"
```

Headless：

```yaml
ui:
  shell: "headless"
  renderer: "none"
  layout_profile: "none"
```

------

# 9. Board Variant 与 UI 的关系

Board 可以告诉你：

```text
屏幕尺寸、输入设备、旋转方向
```

但 Board 不能决定：

```text
使用哪个页面布局
```

Target 决定：

```text
这个板子上的产品用哪个 shell/profile
```

例如同一个 Linux：

```text
linux_uconsole_gtk
linux_uconsole_ascii
linux_headless
```

可以共享同一套 AppService 和 PresentationModel，但 Shell 不同。

------

# 10. Capability 规格需要增加 Board Binding

之前 capability 只说 endpoint_host。现在要增加：

```text
capability binding
```

因为同样是 ESP32，不同板子的 LoRa/GPS 接线不同。

```yaml
capability_bindings:
  lora:
    board_provider: "t_lora_pager.radio"
    platform_driver: "esp_sx1262_packet_radio"
    protocol_binding: "meshtastic_or_meshcore"
    runtime_owner: "mesh_task"

  gps:
    board_provider: "t_lora_pager.gps"
    platform_driver: "esp_uart_gnss"
    parser: "core_gps_nmea"
    runtime_owner: "gps_task"

  display:
    board_provider: "t_lora_pager.display"
    platform_driver: "esp_lcd_panel"
    renderer: "lvgl"
    runtime_owner: "ui_task"

  input:
    board_provider: "t_lora_pager.input"
    platform_driver: "esp_input_driver"
    event_target: "ui_command_queue"
```

这样能避免：

```text
AppContext 直接知道 T-LoRa Pager 的 GPS pin
BLE service 直接知道 GAT562 board
UI 直接读 board display rotation
```

------

# 11. 产品组合层 Product Composition

Apps 层不能无限膨胀成上帝对象。
应该定义 Product Composition Root。

```text
apps/<target>/
  main.cpp
  target_manifest.yaml
  product_composition.cpp
  product_composition.h
  ui_composition.cpp
  runtime_composition.cpp
  capability_composition.cpp
```

职责：

```text
- 读取/引用 target manifest
- 创建 board facts provider
- 创建 platform drivers
- 创建 stores
- 创建 protocol cores
- 创建 app services
- 创建 presentation models
- 创建 shell renderer
- 连接 event queues
```

禁止：

```text
- 在 composition root 里写 direct message 业务
- 在 composition root 里解析 BLE phone protocol
- 在 composition root 里解析 NMEA
- 在 composition root 里拼 UI 业务数据
```

------

# 12. Event Bus / Command Bus 规格

跨平台共享时，事件模型必须明确。

建议新增：

```text
modules/core_runtime
```

或者先作为规格定义。

## 12.1 事件类型分层

```text
HardwareEvent
  RadioIrq
  GpsBytesAvailable
  BleConnected
  ButtonPressed

CapabilityEvent
  RadioPacketReceived
  GpsFixUpdated
  BleWriteReceived

ProtocolEvent
  MeshtasticPacketReceived
  MeshCoreFrameReceived
  PhoneCommandReceived

AppEvent
  MessageReceived
  ContactUpdated
  LocationUpdated
  ConfigChanged

PresentationEvent
  ChatListChanged
  MapViewportChanged
  DeviceStatusChanged

ShellEvent
  KeyPressed
  TouchClicked
  WindowResized
```

禁止低层事件越级：

```text
HardwareEvent -> UI
BLE callback -> ChatModel
Radio IRQ -> ContactService
```

------

## 12.2 Command 类型分层

```text
UiCommand
  SendMessageClicked
  SelectContact
  ToggleMapLayer

AppCommand
  SendDirectMessage
  ApplyConfigPatch
  MarkConversationRead

CapabilityCommand
  ConfigureRadio
  StartGps
  StartBleAdvertising

DriverCommand
  WriteSpi
  SetGpio
```

UI 只能发 `UiCommand` 或调用 presentation action。
PresentationModel 转成 AppCommand。
AppService 再转 CapabilityCommand。

------

# 13. 配置系统必须区分三类配置

很多跨平台漂移来自 config 混在一起。

必须区分：

## 13.1 Product Config

用户意义配置：

```text
- 当前协议 Meshtastic/MeshCore
- region
- modem preset
- screen timeout
- map layer preference
- team mode
```

归属：

```text
core_config + AppService
```

## 13.2 Platform Config

平台运行配置：

```text
- Linux data root
- SQLite path
- serial device path
- framebuffer path
- GTK window size
```

归属：

```text
platform/app target
```

## 13.3 Board Facts

硬件事实：

```text
- pins
- bus
- display resolution
- GPS UART
```

归属：

```text
board package
```

禁止把 Board Facts 放进 Product Config。
禁止让用户 UI 修改 board pin。
禁止让 Meshtastic BLE config 写 Linux path。
禁止让 GTK settings 直接改 radio driver without ConfigService。

------

# 14. Shared Core 代码规则要进一步严格

因为要支持 nRF52 + ESP32 + Linux + 多 UI，共享模块要分等级。

## 14.1 Core Portable Level

```text
core_portable
```

允许：

```text
- fixed buffer
- no exception
- no RTTI
- limited STL or no STL
- no heap hidden allocation
- explicit capacity
```

用于：

```text
core_mesh
core_gps parser
core_phone codec
core_config schema
```

## 14.2 Core Rich Level

```text
core_rich
```

允许更丰富结构，但不能进 MCU 必需路径：

```text
- std::vector
- std::string
- indexes
- search
- Linux-friendly projection
```

用于：

```text
Linux search
GTK presentation model maybe
map package index
diagnostics
```

Target manifest 要声明能用哪个 level。

------

# 15. Presentation Model 也要分 portable/rich

嵌入式和 Linux UI 不同，不应该强行共用一个巨大的 presentation model。

建议：

```text
modules/ui_presentation/
  include/ui_presentation/
    chat/
      chat_summary_model.h        # portable
      chat_workspace_model.h      # richer
    map/
      map_compact_model.h
      map_workbench_model.h
    device/
      device_status_model.h
```

规则：

```text
- compact model 可用于 LVGL/ASCII/小屏
- workbench model 可用于 GTK/uConsole
- 两者共享 AppService，不共享布局对象
```

例子：

```text
ChatSummaryModel:
  conversation rows
  selected row
  unread count
  simple actions

ChatWorkbenchModel:
  conversation rail
  transcript
  node inspector
  compose state
  search/filter
```

LVGL 不必吃 GTK 的 workspace model。
GTK 不必复用 LVGL 页面。

------

# 16. ASCII UI 特别规则

ASCII/TUI 不是“简单日志输出”，它也是 Shell。

需要约束：

```text
ASCII renderer 只能消费 presentation model。
不能直接读 service/store。
不能直接访问 radio/GPS/BLE。
```

ASCII renderer 输出应通过：

```text
AsciiCanvas
AsciiLayout
AsciiTheme
TerminalInputAdapter
```

而不是业务代码里到处 `printf()`。

```cpp
class AsciiCanvas {
public:
    void drawText(int x, int y, TextStyle style, const char* text);
    void drawBox(int x, int y, int w, int h);
    void flush();
};
```

这样以后 CLI/TUI 也能保持架构边界。

------

# 17. LVGL 特别规则

LVGL 页面必须变薄。

禁止：

```text
lvgl page 直接:
- 读写 store
- 解析协议
- 访问 board
- 访问 radio driver
- 访问 gps driver
- 修改 config backend
```

允许：

```text
lvgl page:
- 绑定 presentation model
- 渲染 snapshot
- 发送 UI action
```

推荐结构：

```text
platform/esp/.../ui/lvgl_shell/
  lvgl_chat_page.cpp
  lvgl_map_page.cpp
  lvgl_settings_page.cpp

modules/ui_presentation/
  chat_model.h
  map_model.h
  settings_model.h
```

------

# 18. GTK 特别规则

GTK 是 desktop shell，不是 Linux 业务层。

禁止：

```text
gtk page:
- 自己维护 ContactService
- 自己解析 Meshtastic node info
- 自己拼 device status
- 自己读 SQLite 表绕过 AppService
```

允许：

```text
gtk page:
- 显示 richer presentation model
- 发 action
- 处理 keyboard/mouse/window
```

GTK 主线程规则必须写死：

```text
All GtkWidget mutations must happen on GTK main loop.
Background services publish snapshots/events only.
```

------

# 19. Headless / CLI 规则

Linux 可能有 daemon 或 CLI。
它不能变成“绕过架构的调试捷径”。

CLI 命令也必须走 AppService：

```text
trailmate send --to ...
    -> CliShell
        -> AppCommand
            -> DirectMessageService
```

禁止：

```text
CLI 直接打开 SQLite 改 peer key
CLI 直接调用 radio driver 发包
```

除非命令明确是低层诊断命令，并放在 diagnostics capability 下。

------

# 20. 多 ESP32 板子类的具体约束

你特别指出不同 ESP32 有不同板子类。这里必须具体化。

## 20.1 不允许 `#ifdef BOARD_X` 扩散

禁止：

```cpp
#ifdef BOARD_T_DECK
  ...
#elif BOARD_T_LORA_PAGER
  ...
#endif
```

散落在业务、UI、协议代码中。

允许它集中在：

```text
board package
target composition
build config
```

------

## 20.2 Board Facade

每个 board 实现统一接口：

```cpp
class IBoardPackage {
public:
    virtual BoardId id() const = 0;
    virtual bool radioFacts(RadioHardwareFacts& out) const = 0;
    virtual bool gpsFacts(GpsHardwareFacts& out) const = 0;
    virtual bool displayFacts(DisplayHardwareFacts& out) const = 0;
    virtual bool inputFacts(InputHardwareFacts& out) const = 0;
    virtual bool powerFacts(PowerHardwareFacts& out) const = 0;
};
```

Board-specific 类可以很多：

```text
TLoraPagerBoard
TDeckBoard
TWatchS3Board
Gat562Board
UConsoleAio2Board
```

但上层只看 `IBoardPackage`。

------

## 20.3 Board Runtime Hooks

板子可以有 runtime hook：

```cpp
class IBoardRuntimeHooks {
public:
    virtual void beforeSleep() = 0;
    virtual void afterWake() = 0;
    virtual void onLowPowerModeChanged(PowerMode mode) = 0;
};
```

但 hook 不允许调用业务服务，只能处理硬件状态。

------

# 21. Race Policy Manifest

每个 target 必须声明竞态策略：

```yaml
race_policy:
  ownership:
    chat_service: "app_task"
    mesh_session: "mesh_task"
    gps_service: "gps_task"
    config_service: "app_task"
    ui_state: "ui_task"
    device_status: "app_task"

  event_paths:
    radio_rx: "radio_irq -> radio_task -> mesh_event_queue -> app_task"
    gps_rx: "uart_irq -> gps_task -> location_event_queue -> app_task"
    ble_write: "ble_callback -> phone_command_queue -> app_task"
    ui_input: "ui_thread -> presentation_action -> app_command_queue"

  forbidden_direct_calls:
    - "ble_callback -> ChatService"
    - "radio_irq -> MeshSession"
    - "gps_task -> lvgl"
    - "gtk_worker -> GtkWidget"
    - "ui_thread -> blocking storage write"
```

这个比抽象讲“注意线程安全”有用得多。

------

# 22. 新增规格文档清单

最终建议新增这些：

```text
docs/specification/
  CROSS_PLATFORM_PRODUCT_ARCHITECTURE_SPEC.md
  TARGET_MANIFEST_SPEC.md
  BOARD_PACKAGE_SPEC.md
  RUNTIME_CONCURRENCY_SPEC.md
  UI_PRESENTATION_ARCHITECTURE_SPEC.md
  CAPABILITY_AUTHORITY_GLOSSARY.md
  STORAGE_AUTHORITY_SPEC.md
  EVENT_COMMAND_FLOW_SPEC.md
```

不是一次全写到极致，但 Phase 1 至少要把文件和核心规则立起来。

------

# 23. 目录结构建议

```text
modules/
  core_device/
    capability / authority / target vocabulary

  core_runtime/
    event / command / scheduler abstractions

  core_config/
    config schema / patch / validation

  core_mesh/
    LoRa / Meshtastic / MeshCore radio-side business

  core_gps/
    GNSS parser / location / time facts

  core_phone/
    Meshtastic BLE phone core
    MeshCore BLE/control core

  core_chat/
    chat/contact/conversation domain

  ui_presentation/
    UI toolkit independent presentation models

  ui_ascii/
    optional reusable ASCII canvas/render helpers

platform/
  esp/
    boards/
    drivers/
    runtime/
    storage/
    ble/
    ui/lvgl/

  nrf52/
    boards/
    drivers/
    runtime/
    storage/
    ble/

  linux/
    boards/
    drivers/
    runtime/
    storage/
    ui/gtk/
    ui/ascii/

apps/
  esp_tpager/
  esp_tdeck/
  nrf52_gat562/
  linux_uconsole_gtk/
  linux_uconsole_ascii/
  linux_headless/
  linux_sim/
```



------

# 附录1:实现细则

```text
业务稳定以后，跨平台改造的目标不是重新设计业务，
而是把已经稳定的业务语义从平台、板级、UI、协议通道、运行时细节中剥离出来，
让它成为可复用、可测试、可替换宿主的应用核心。
```

对应工程原则：

```text
Domain 定义事实；
UseCase 编排业务；
ProtocolCore 解释协议；
CapabilityPort 描述所需能力；
PlatformAdapter 提供能力；
BoardPackage 描述硬件事实；
RuntimeContext 处理调度和并发；
PresentationModel 投影界面状态；
Renderer 只负责绘制；
CompositionRoot 负责组装。
```

------

# 1. 分层总览与设计模式对应

先给全局映射。

| 层                            | 责任                 | 主要设计模式                                        | 目的                                      |
| ----------------------------- | -------------------- | --------------------------------------------------- | ----------------------------------------- |
| Domain                        | 定义业务事实和值对象 | Value Object、Entity                                | 去平台化，统一语义                        |
| UseCase / Application Service | 编排业务流程         | Application Service、Command Handler、State Machine | 稳定业务唯一实现                          |
| Protocol Core                 | 解释外部协议         | Strategy、Codec、State Machine、Mapper              | Meshtastic/MeshCore/BLE/HostLink 协议复用 |
| Capability Ports              | 定义所需能力         | Ports and Adapters、Repository Interface            | 隔离平台能力                              |
| Platform Adapters             | 实现能力             | Adapter、Repository、Proxy、Null Object             | ESP32/nRF52/Linux 可替换                  |
| Board Package                 | 描述硬件事实         | Abstract Factory、Provider                          | 隔离不同板子                              |
| Runtime Context               | 处理并发调度         | Active Object、Reactor、Command Queue、Event Queue  | 消解竞态                                  |
| Config Core                   | 管理配置语义         | Schema、Patch、Validator、Repository                | 多入口统一改配置                          |
| Device Core                   | 聚合能力状态         | Snapshot、Projection、Facade                        | UI/BLE/HostLink 共享设备状态              |
| Presentation Model            | UI 无关状态          | MVVM、Presenter、CQRS Read Model                    | LVGL/ASCII/GTK 复用                       |
| Renderer / Shell              | 绘制与输入           | Renderer、Adapter、Command                          | UI 技术栈隔离                             |
| Composition Root              | 组装对象             | Dependency Injection、Abstract Factory、Builder     | 控制依赖方向                              |

这不是“模式堆砌”。每个模式对应一个具体结构性问题。

------

# 2. Domain 层：稳定业务语义的原子层

## 2.1 目标

Domain 层只定义业务中的“事实”和“值”。

例如：

```text
NodeId
Peer
Contact
Conversation
Message
LocationFix
TimeFact
RadioPacket
MeshProtocol
DeviceCapability
ConfigValue
```

它不做 I/O，不调平台 API，不知道 UI。

## 2.2 使用模式

### Value Object

用于不可变业务值：

```cpp
namespace domain {

struct NodeId {
    uint32_t value = 0;

    bool isValid() const {
        return value != 0;
    }

    bool operator==(const NodeId& other) const {
        return value == other.value;
    }
};

struct MessageId {
    uint64_t value = 0;
};

struct Timestamp {
    uint64_t epoch_seconds = 0;
    bool valid = false;
};

}
```

目的：

```text
把“节点 ID”“消息 ID”“时间戳”这些语义从 uint32_t / uint64_t 中解放出来。
```

避免：

```cpp
uint32_t node;
uint32_t peer;
uint32_t id;
uint32_t from;
```

到处混。

------

### Entity

用于有身份、有生命周期的业务对象：

```cpp
namespace chat {

struct Message {
    domain::MessageId id;
    domain::NodeId from;
    domain::NodeId to;
    domain::Timestamp timestamp;
    MessageDirection direction;
    MessageStatus status;
    FixedString<240> text;
};

struct Contact {
    domain::NodeId node_id;
    FixedString<32> short_name;
    FixedString<64> long_name;
    ContactSource source;
    TrustState trust;
};

}
```

目的：

```text
让消息、联系人、节点等业务对象在所有平台上语义一致。
```

------

## 2.3 Domain 层禁止事项

Domain 禁止 include：

```cpp
Arduino.h
Preferences.h
sqlite3.h
freertos/...
zephyr/...
nrf_...
esp_...
lvgl.h
gtk/...
```

Domain 禁止：

```text
- 访问 NVS / SQLite / Flash
- 发 LoRa packet
- 解析 BLE characteristic
- 访问 GPS UART
- 更新 UI
- 加锁
- 起线程
```

------

# 3. UseCase 层：稳定业务的唯一实现

业务已经稳定，所以 UseCase 层要做的是：

```text
把稳定业务流程固化为唯一实现。
```

例如：

```text
发送直连消息
收到 Mesh packet
更新联系人
GPS fix 更新
BLE phone command 处理
配置变更
设备状态聚合
```

## 3.1 使用模式：Application Service

例如 DirectMessageService：

```cpp
class DirectMessageService {
public:
    DirectMessageService(
        MeshProtocolStrategy& protocol,
        PeerIdentityService& identity,
        IPacketRadio& radio,
        IClock& clock,
        IMeshEventSink& events
    )
        : protocol_(protocol),
          identity_(identity),
          radio_(radio),
          clock_(clock),
          events_(events) {}

    SendResult sendDirect(const DirectMessageCommand& cmd) {
        if (!cmd.isValid()) {
            return SendResult::fail(SendFailure::InvalidInput);
        }

        LocalIdentity local;
        auto local_result = identity_.ensureLocalIdentity(local);
        if (!local_result.ok) {
            return SendResult::fail(SendFailure::LocalIdentityMissing);
        }

        PeerPublicKey peer_key;
        auto peer_result = identity_.findPeerKey(cmd.to, peer_key);
        if (!peer_result.ok) {
            return SendResult::fail(SendFailure::PeerKeyMissing);
        }

        EncodedPacket packet;
        auto build_result = protocol_.buildDirectMessage(
            local,
            peer_key,
            cmd,
            packet
        );

        if (!build_result.ok) {
            return SendResult::fail(SendFailure::PacketBuildFailed);
        }

        auto tx_result = radio_.send(packet.bytes());
        if (!tx_result.ok) {
            return SendResult::fail(SendFailure::RadioSendFailed);
        }

        events_.emit(MeshEvent::messageSent(cmd.to));
        return SendResult::ok();
    }

private:
    MeshProtocolStrategy& protocol_;
    PeerIdentityService& identity_;
    IPacketRadio& radio_;
    IClock& clock_;
    IMeshEventSink& events_;
};
```

目的：

```text
ESP32、nRF52、Linux 发直连消息时，走同一个 DirectMessageService。
```

平台差异只在：

```text
IPacketRadio
ILocalIdentityStore
IPeerKeyStore
IClock
IRandom
```

------

## 3.2 使用模式：Command Handler

对于 UI、BLE、HostLink、CLI 进来的操作，不应该直接调用底层服务，而应该统一成 Command。

```cpp
struct SendDirectMessageCommand {
    domain::NodeId to;
    FixedBytes<240> text;
};

struct ApplyConfigPatchCommand {
    ConfigPatch patch;
};

using AppCommand = Variant<
    SendDirectMessageCommand,
    ApplyConfigPatchCommand,
    MarkConversationReadCommand,
    StartGpsCommand,
    SetMeshProtocolCommand
>;
```

统一处理：

```cpp
class AppCommandHandler {
public:
    CommandResult handle(const AppCommand& command) {
        return visit(command, [this](const auto& cmd) {
            return handleTyped(cmd);
        });
    }

private:
    CommandResult handleTyped(const SendDirectMessageCommand& cmd) {
        DirectMessageCommand direct;
        direct.to = cmd.to;
        direct.payload = cmd.text.view();
        return direct_message_.sendDirect(direct).toCommandResult();
    }

    CommandResult handleTyped(const ApplyConfigPatchCommand& cmd) {
        return config_service_.applyPatch(cmd.patch);
    }

private:
    DirectMessageService& direct_message_;
    ConfigService& config_service_;
};
```

目的：

```text
UI / BLE / HostLink / CLI 都只发命令，不直接操纵业务内部对象。
```

------

## 3.3 使用模式：State Machine

MeshSession、BLE phone session、GPS runtime、HostLink session 都应该是显式状态机。

例如 MeshSession：

```cpp
enum class MeshSessionState {
    Stopped,
    Starting,
    Ready,
    Degraded,
    Error,
};

class MeshSession {
public:
    void start();
    void stop();
    void tick(uint32_t now_ms);
    void onRadioPacket(const RadioRxPacket& packet);
    SendResult sendDirect(const DirectMessageCommand& cmd);

private:
    MeshSessionState state_ = MeshSessionState::Stopped;
};
```

目的：

```text
避免一堆 bool：
radioReady
meshStarted
gpsSynced
bleConnected
configApplied
```

状态机可以精确表达：

```text
Stopped -> Starting -> Ready -> Degraded -> Error
```

------

# 4. Protocol Core 层：协议语义和 transport 分离

这是 BLE/Meshtastic/MeshCore 里最容易混的地方。

## 4.1 协议核心分四类

```text
core_mesh/protocol/meshtastic
core_mesh/protocol/meshcore
core_phone/protocol/meshtastic
core_phone/protocol/meshcore
core_hostlink
```

区别：

```text
Meshtastic radio protocol:
  LoRa packet / protobuf / PKI / channel / direct packet

Meshtastic phone protocol:
  BLE ToRadio / FromRadio / admin / config

MeshCore radio protocol:
  frame / sign / encrypt / payload

MeshCore phone/control protocol:
  BLE/NUS command / device status / contact / telemetry

HostLink protocol:
  USB/Serial frame / command / event
```

这些不应该混在 BLE host 或 radio adapter 里。

------

## 4.2 使用模式：Strategy

Meshtastic 和 MeshCore 是不同协议策略。

```cpp
class MeshProtocolStrategy {
public:
    virtual ~MeshProtocolStrategy() = default;

    virtual ProtocolKind kind() const = 0;

    virtual PacketBuildResult buildDirectMessage(
        const LocalIdentity& local,
        const PeerPublicKey& peer,
        const DirectMessageCommand& cmd,
        EncodedPacket& out
    ) = 0;

    virtual PacketParseResult parseRadioPacket(
        const RadioRxPacket& packet,
        MeshProtocolEvent& out
    ) = 0;

    virtual RadioConfig deriveRadioConfig(
        const MeshConfig& config
    ) = 0;
};
```

实现：

```cpp
class MeshtasticProtocolStrategy final : public MeshProtocolStrategy {
    ...
};

class MeshCoreProtocolStrategy final : public MeshProtocolStrategy {
    ...
};
```

目的：

```text
上层 DirectMessageService 不关心当前是 Meshtastic 还是 MeshCore。
切协议只替换 strategy。
```

------

## 4.3 使用模式：Codec

Codec 只做编码/解码，不做业务决策。

```cpp
class MeshtasticPacketCodec {
public:
    DecodeResult decode(ByteView bytes, MeshtasticPacket& out);
    EncodeResult encode(const MeshtasticPacket& packet, FixedBuffer& out);
};
```

Codec 禁止：

```text
- 查 peer key
- 决定是否发送
- 决定是否更新 contact
- 访问 radio
- 访问 storage
```

------

## 4.4 使用模式：Mapper / Translator

BLE phone protocol 与 AppCommand 之间要用 Mapper。

```cpp
class MeshtasticPhoneCommandMapper {
public:
    MapResult toAppCommand(
        const MeshtasticToRadio& input,
        AppCommand& out
    );

    MapResult fromAppEvent(
        const AppEvent& event,
        MeshtasticFromRadio& out
    );
};
```

目的：

```text
Meshtastic BLE 的 ToRadio/FromRadio 是外部协议对象。
AppCommand/AppEvent 是内部业务对象。
两者不能互相污染。
```

------

# 5. Capability Ports：能力接口

这一层是 Hexagonal Architecture / Ports and Adapters 的核心。

## 5.1 使用模式：Port Interface

业务需要什么能力，就定义 port。

### Radio

```cpp
class IPacketRadio {
public:
    virtual ~IPacketRadio() = default;

    virtual RadioResult configure(const RadioConfig& config) = 0;
    virtual RadioResult send(ByteView packet) = 0;
    virtual RadioResult poll(RadioRxPacket& out) = 0;
    virtual RadioStatus status() const = 0;
};
```

### GPS

```cpp
class IGnssByteStream {
public:
    virtual ~IGnssByteStream() = default;
    virtual IoResult read(uint8_t* out, size_t cap, size_t& len) = 0;
};

class ILocationSource {
public:
    virtual ~ILocationSource() = default;
    virtual bool latest(LocationFix& out) = 0;
};

class ITimeAuthority {
public:
    virtual ~ITimeAuthority() = default;
    virtual bool nowEpoch(uint64_t& out) = 0;
    virtual uint32_t nowMonotonicMs() = 0;
    virtual TimeSyncStatus syncStatus() = 0;
};
```

### BLE Transport

```cpp
class IBleGattHost {
public:
    virtual ~IBleGattHost() = default;

    virtual BleResult startAdvertising(const BleAdvertisement& adv) = 0;
    virtual BleResult registerService(const BleServiceDefinition& service) = 0;
    virtual BleResult notify(BleCharacteristicId id, ByteView payload) = 0;
};
```

### Storage

```cpp
class IPeerKeyStore {
public:
    virtual ~IPeerKeyStore() = default;

    virtual StoreResult get(domain::NodeId node, PeerPublicKey& out) = 0;
    virtual StoreResult put(const PeerPublicKey& key) = 0;
    virtual StoreResult remove(domain::NodeId node) = 0;
    virtual StoreCapabilities capabilities() const = 0;
};
```

目的：

```text
UseCase 不知道 NVS / SQLite / nRF52 flash。
ProtocolCore 不知道 Bluefruit / NimBLE / BlueZ。
LocationService 不知道 UART / Linux fd。
```

------

## 5.2 使用模式：Repository

Store port 用 Repository 模式。

```cpp
class IMessageRepository {
public:
    virtual StoreResult append(const chat::Message& message) = 0;
    virtual StoreResult listConversation(
        domain::NodeId peer,
        MessageCursor cursor,
        MessagePage& out
    ) = 0;
    virtual StoreResult markRead(domain::MessageId id) = 0;
};
```

实现：

```text
EspNvsPeerKeyStore
Nrf52FlashPeerKeyStore
SqlitePeerKeyStore
FakePeerKeyStore
```

目的：

```text
同一业务服务可以在不同存储后端运行。
```

------

## 5.3 使用模式：Null Object

某些 target 没有某能力，例如 headless 没有 display，Linux 当前不支持 BLE。

不要到处写：

```cpp
if (ble != nullptr) ...
```

使用 Null Object：

```cpp
class NullBleGattHost final : public IBleGattHost {
public:
    BleResult startAdvertising(const BleAdvertisement&) override {
        return BleResult::unsupported();
    }

    BleResult registerService(const BleServiceDefinition&) override {
        return BleResult::unsupported();
    }

    BleResult notify(BleCharacteristicId, ByteView) override {
        return BleResult::unsupported();
    }
};
```

目的：

```text
能力不存在也是一种明确对象，而不是 null 分支。
```

------

# 6. Platform Adapter 层：把平台 API 包起来

## 6.1 使用模式：Adapter

ESP32：

```cpp
class EspSx1262PacketRadio final : public IPacketRadio {
public:
    explicit EspSx1262PacketRadio(const RadioHardwareFacts& facts);

    RadioResult configure(const RadioConfig& config) override {
        // RadioLib / ESP SPI / GPIO
    }

    RadioResult send(ByteView packet) override {
        // platform-specific TX
    }

    RadioResult poll(RadioRxPacket& out) override {
        // platform-specific RX
    }
};
```

nRF52：

```cpp
class Nrf52Sx1262PacketRadio final : public IPacketRadio {
    // Bluefruit / Zephyr / Arduino nRF52 / Nordic SDK specific
};
```

Linux：

```cpp
class LinuxAio2Sx1262PacketRadio final : public IPacketRadio {
    // spidev / gpiochip / epoll etc.
};
```

目的：

```text
平台 API 的不一致性在 Adapter 内收敛。
```

Adapter 禁止：

```text
- direct message 业务
- peer key 逻辑
- BLE phone protocol
- GPS 业务策略
- UI 状态
```

------

## 6.2 使用模式：Proxy

Linux + nRF52 radio endpoint 时，Linux 的 `IPacketRadio` 实现不是本机 radio，而是 proxy。

```cpp
class SerialPacketRadioProxy final : public IPacketRadio {
public:
    SerialPacketRadioProxy(ISerialPort& serial);

    RadioResult configure(const RadioConfig& config) override {
        return protocol_.sendConfigure(serial_, config);
    }

    RadioResult send(ByteView packet) override {
        return protocol_.sendPacket(serial_, packet);
    }

    RadioResult poll(RadioRxPacket& out) override {
        return protocol_.readPacket(serial_, out);
    }

private:
    ISerialPort& serial_;
    PacketRadioProxyProtocol protocol_;
};
```

目的：

```text
Linux 仍然运行 Mesh 业务核心；
nRF52 只是 packet radio endpoint。
```

这和 command proxy 不同。

------

## 6.3 Command Proxy

如果 nRF52 是 smart coprocessor，则 Linux 不运行 DirectMessageService，而是发命令。

```cpp
class RemoteMeshCommandClient {
public:
    SendResult sendDirect(const DirectMessageCommand& cmd) {
        return link_.sendCommand(HostCommand::sendDirect(cmd));
    }
};
```

这时：

```text
DirectMessageService 在 nRF52；
Linux 只有 RemoteMeshCommandClient。
```

目的：

```text
防止同一个用户动作被 Linux 和 nRF52 两个业务核心同时处理。
```

------

# 7. Board Package 层：多 ESP32 板子变体

## 7.1 使用模式：Provider / Abstract Factory

每个板子提供 `IBoardPackage`。

```cpp
class IBoardPackage {
public:
    virtual ~IBoardPackage() = default;

    virtual BoardId id() const = 0;

    virtual bool radioFacts(RadioHardwareFacts& out) const = 0;
    virtual bool gpsFacts(GpsHardwareFacts& out) const = 0;
    virtual bool displayFacts(DisplayHardwareFacts& out) const = 0;
    virtual bool inputFacts(InputHardwareFacts& out) const = 0;
    virtual bool powerFacts(PowerHardwareFacts& out) const = 0;
};
```

T-LoRa Pager：

```cpp
class TLoraPagerBoard final : public IBoardPackage {
public:
    BoardId id() const override {
        return BoardId::TLoraPager;
    }

    bool radioFacts(RadioHardwareFacts& out) const override {
        out.chip = RadioChip::Sx1262;
        out.spi_bus = SpiBusId::Spi2;
        out.reset = GpioPin{8};
        out.busy = GpioPin{7};
        out.dio1 = GpioPin{33};
        out.dio2_as_rf_switch = true;
        out.dio3_tcxo_voltage = 1.8f;
        return true;
    }

    bool displayFacts(DisplayHardwareFacts& out) const override {
        out.width = 222;
        out.height = 480;
        out.rotation = DisplayRotation::Landscape;
        return true;
    }
};
```

T-Deck：

```cpp
class TDeckBoard final : public IBoardPackage {
    // different display/input/radio/gps facts
};
```

目的：

```text
不同 ESP32 板子的差异只在 board package 和 composition root。
业务、协议、UI model 不出现 BOARD_T_DECK / BOARD_TPAGER 分支。
```

------

## 7.2 使用模式：Abstract Factory

Platform 根据 board facts 创建 driver。

```cpp
class EspCapabilityFactory {
public:
    explicit EspCapabilityFactory(const IBoardPackage& board)
        : board_(board) {}

    UniquePtr<IPacketRadio> createPacketRadio() {
        RadioHardwareFacts facts;
        if (!board_.radioFacts(facts)) {
            return makeUnique<NullPacketRadio>();
        }

        if (facts.chip == RadioChip::Sx1262) {
            return makeUnique<EspSx1262PacketRadio>(facts);
        }

        return makeUnique<NullPacketRadio>();
    }

    UniquePtr<IGnssByteStream> createGnssByteStream() {
        GpsHardwareFacts facts;
        if (!board_.gpsFacts(facts)) {
            return makeUnique<NullGnssByteStream>();
        }

        return makeUnique<EspUartGnssByteStream>(facts);
    }

private:
    const IBoardPackage& board_;
};
```

目的：

```text
board 只描述事实；
factory 才根据事实创建平台驱动；
业务层完全无感。
```

------

# 8. Runtime / Concurrency 层：竞态问题结构化

这是嵌入式和 Linux 差异最大的地方。

不能靠“注意线程安全”。必须设计通信模式。

## 8.1 使用模式：Active Object

每个 mutable service 有 owner context。

```text
RadioContext
GpsContext
BleContext
AppContext
UiContext
StorageContext
```

每个 context 有自己的队列：

```cpp
class IRuntimeQueue {
public:
    virtual bool post(RuntimeCommand command) = 0;
    virtual bool poll(RuntimeCommand& out) = 0;
};
```

例如：

```text
Radio IRQ
  -> RadioContext queue
    -> MeshSession.onRadioPacket
      -> AppContext queue
        -> ChatService.onMessageReceived
          -> UiContext snapshot event
```

目的：

```text
跨 task / thread / callback 不直接共享 mutable state。
```

------

## 8.2 使用模式：Command Queue

UI / BLE / HostLink 都不能直接改业务，只投递命令。

```cpp
class AppCommandQueue {
public:
    bool post(const AppCommand& command);
    bool poll(AppCommand& out);
};
```

BLE callback：

```cpp
void MeshtasticBleHost::onWrite(ByteView bytes) {
    PhoneInputEvent event;
    event.source = PhoneInputSource::Ble;
    event.bytes.assign(bytes);

    phone_queue_.post(event);
}
```

PhoneCore context：

```cpp
void PhoneRuntime::tick() {
    PhoneInputEvent event;
    while (queue_.poll(event)) {
        auto result = meshtastic_phone_core_.handleInput(event.bytes);
        dispatchPhoneOutputs(result);
    }
}
```

目的：

```text
BLE stack callback 不直接进入 ChatService / ConfigService / GPS。
```

------

## 8.3 使用模式：Event Queue

业务结果通过事件发布。

```cpp
struct AppEvent {
    AppEventKind kind;
    AppEventPayload payload;
    uint32_t version;
};
```

例如：

```text
MessageReceived
ContactUpdated
LocationUpdated
ConfigChanged
DeviceStatusChanged
```

UI 收到后不直接读内部对象，而是请求 snapshot。

------

## 8.4 使用模式：Immutable Snapshot

UI 不直接读 mutable service。

```cpp
struct DeviceStatusSnapshot {
    CapabilityState lora;
    CapabilityState gps;
    CapabilityState ble;
    TimeSyncStatus time;
    uint32_t version;
};

struct ChatListSnapshot {
    ConversationRow rows[32];
    size_t count = 0;
    uint32_t selected_index = 0;
    uint32_t version = 0;
};
```

AppService 生成 snapshot：

```cpp
class ChatProjectionService {
public:
    ChatListSnapshot buildChatListSnapshot() const;
};
```

目的：

```text
LVGL / GTK / ASCII 都渲染快照，不持有业务内部引用。
```

------

## 8.5 ISR 规则

ISR 只做 defer。

```cpp
void IRAM_ATTR radioDio1Isr() {
    radio_irq_queue.postFromIsr(RadioIrqEvent::Dio1);
}
```

禁止：

```text
ISR 内：
- protobuf decode
- packet decrypt
- storage write
- BLE notify
- UI update
- DirectMessageService
```

------

## 8.6 UI Thread Only

### LVGL

```cpp
void UiRuntime::tick() {
    UiCommand cmd;
    while (ui_queue_.poll(cmd)) {
        lvgl_renderer_.apply(cmd);
    }

    lv_timer_handler();
}
```

所有 `lv_obj_*` 只在 UI runtime。

### GTK

```cpp
void GtkShell::onAppSnapshot(DeviceStatusSnapshot snapshot) {
    g_main_context_invoke(nullptr, [](gpointer data) {
        auto* self = static_cast<GtkShell*>(data);
        self->renderOnGtkThread();
        return G_SOURCE_REMOVE;
    }, this);
}
```

### ASCII

```cpp
void AsciiShell::render(const AppSnapshot& snapshot) {
    canvas_.clear();
    renderer_.draw(snapshot, canvas_);
    canvas_.flushSingleWriter();
}
```

目的：

```text
每种 UI 技术栈都有自己的线程/循环约束，但业务层不需要知道。
```

------

# 9. Config Core：多入口统一配置

配置会从这些入口修改：

```text
LVGL Settings
GTK Settings
ASCII/CLI
Meshtastic BLE Admin
MeshCore BLE Command
HostLink
配置文件
```

如果各自直接写存储，必然漂移。

## 9.1 使用模式：Schema + Patch + Validator

```cpp
struct ConfigPatch {
    Optional<MeshProtocolKind> mesh_protocol;
    Optional<RegionCode> region;
    Optional<ModemPreset> modem_preset;
    Optional<uint32_t> screen_timeout_sec;
    Optional<bool> gps_enabled;
};

class ConfigValidator {
public:
    ConfigValidationResult validate(
        const AppConfig& current,
        const ConfigPatch& patch,
        const TargetCapabilitySnapshot& target
    );
};
```

ConfigService：

```cpp
class ConfigService {
public:
    ConfigResult applyPatch(const ConfigPatch& patch) {
        auto validation = validator_.validate(current_, patch, target_);
        if (!validation.ok) {
            return ConfigResult::fail(validation.failure);
        }

        AppConfig next = current_;
        patch.applyTo(next);

        auto store_result = store_.save(next);
        if (!store_result.ok) {
            return ConfigResult::storeFailed(store_result.failure);
        }

        current_ = next;
        events_.emit(AppEvent::configChanged());
        return ConfigResult::ok();
    }

private:
    AppConfig current_;
    IConfigStore& store_;
    ConfigValidator& validator_;
    IAppEventSink& events_;
    TargetCapabilitySnapshot target_;
};
```

目的：

```text
所有入口都走同一个 ConfigService。
Meshtastic BLE Admin 不直接改 NVS。
GTK Settings 不直接改 SQLite。
HostLink 不直接改 AppContext 内部字段。
```

------

# 10. Device Core：能力状态聚合

BLE/HostLink/UI 都需要设备状态。不能各自拼。

## 10.1 使用模式：Facade + Projection

```cpp
class DeviceStatusService {
public:
    DeviceStatusSnapshot snapshot() const {
        DeviceStatusSnapshot out;
        out.lora = radio_monitor_.state();
        out.gps = location_monitor_.state();
        out.ble = ble_monitor_.state();
        out.time = time_authority_.syncStatus();
        out.battery = power_monitor_.snapshot();
        return out;
    }

private:
    IRadioMonitor& radio_monitor_;
    ILocationMonitor& location_monitor_;
    IBleMonitor& ble_monitor_;
    ITimeAuthority& time_authority_;
    IPowerMonitor& power_monitor_;
};
```

目的：

```text
MeshtasticPhoneCore、MeshCorePhoneCore、HostLink、LVGL、GTK、ASCII 都消费同一份 DeviceStatusSnapshot。
```

------

# 11. BLE 架构：Transport Host + Phone Core

这是必须拆清楚的一块。

## 11.1 BLE Host

```cpp
class MeshtasticBleHost {
public:
    void start() {
        ble_.registerService(makeMeshtasticServiceDefinition());
        ble_.startAdvertising(makeAdvertisement());
    }

    void onToRadioWrite(ByteView bytes) {
        phone_input_queue_.post(PhoneInputEvent::meshtastic(bytes));
    }

    void notifyFromRadio(ByteView bytes) {
        ble_.notify(from_radio_char_, bytes);
    }

private:
    IBleGattHost& ble_;
    IPhoneInputQueue& phone_input_queue_;
};
```

BLE Host 只做：

```text
advertising
service
characteristic
write callback
notify
connection
MTU/chunking
```

------

## 11.2 MeshtasticPhoneCore

```cpp
class MeshtasticPhoneCore {
public:
    PhoneProcessResult handleToRadio(ByteView bytes) {
        MeshtasticToRadio msg;
        auto decode = codec_.decodeToRadio(bytes, msg);
        if (!decode.ok) {
            return PhoneProcessResult::protocolError();
        }

        AppCommand command;
        auto mapped = mapper_.toAppCommand(msg, command);
        if (mapped.ok) {
            app_commands_.post(command);
        }

        return buildResponses(msg);
    }

    bool nextNotification(MeshtasticFromRadio& out);

private:
    MeshtasticPhoneCodec codec_;
    MeshtasticPhoneCommandMapper mapper_;
    IPhoneAppFacade& app_;
    IAppCommandSink& app_commands_;
};
```

目的：

```text
Meshtastic phone API 语义在 shared core。
ESP32/nRF52 BLE 文件只负责 BLE transport。
```

------

## 11.3 MeshCorePhoneCore

```cpp
class MeshCorePhoneCore {
public:
    PhoneProcessResult handleFrame(ByteView bytes) {
        MeshCorePhoneCommand cmd;
        auto decoded = codec_.decode(bytes, cmd);
        if (!decoded.ok) {
            return PhoneProcessResult::protocolError();
        }

        return dispatcher_.dispatch(cmd);
    }

private:
    MeshCorePhoneCodec codec_;
    MeshCorePhoneCommandDispatcher dispatcher_;
    IMeshCorePhoneFacade& app_;
};
```

目的：

```text
MeshCore BLE/NUS 命令解释在 shared core。
BLE host 不拼 contact/status/device info。
```

------

# 12. GPS 架构：ByteStream + Parser + LocationService + TimeAuthority

## 12.1 GnssByteStream

平台相关：

```cpp
class EspUartGnssByteStream final : public IGnssByteStream {
    IoResult read(uint8_t* out, size_t cap, size_t& len) override;
};
```

Linux：

```cpp
class LinuxSerialGnssByteStream final : public IGnssByteStream {
    ...
};
```

nRF52：

```cpp
class Nrf52UartGnssByteStream final : public IGnssByteStream {
    ...
};
```

------

## 12.2 NMEA Parser

共享：

```cpp
class NmeaParser {
public:
    NmeaParseResult feed(ByteView bytes);
    bool latestFix(LocationFix& out) const;
    bool latestTime(TimeSyncFact& out) const;
};
```

------

## 12.3 LocationService

```cpp
class LocationService {
public:
    void onGnssBytes(ByteView bytes) {
        auto result = parser_.feed(bytes);

        LocationFix fix;
        if (parser_.latestFix(fix)) {
            auto filtered = jitter_filter_.apply(fix);
            latest_ = filtered;
            events_.emit(AppEvent::locationUpdated());
        }

        TimeSyncFact time;
        if (parser_.latestTime(time)) {
            time_authority_.observe(time);
        }
    }

    bool latest(LocationFix& out) const {
        if (!latest_.valid) return false;
        out = latest_;
        return true;
    }

private:
    NmeaParser parser_;
    GpsJitterFilter jitter_filter_;
    ITimeAuthorityUpdater& time_authority_;
    IAppEventSink& events_;
    LocationFix latest_;
};
```

目的：

```text
BLE / Mesh / HostLink / UI 不再直接读 GPS driver。
```

------

# 13. UI 架构：Presentation Model + Renderer

## 13.1 AppService 不知道 UI

```cpp
class ChatService {
public:
    SendResult sendMessage(...);
    void onMessageReceived(...);
    ChatListSnapshot buildListSnapshot(...);
};
```

不要出现：

```cpp
lv_obj_t*
GtkWidget*
printf
```

------

## 13.2 Presentation Model

```cpp
class ChatWorkspaceModel {
public:
    ChatWorkspaceSnapshot snapshot() const {
        ChatWorkspaceSnapshot out;
        out.conversations = chat_.buildConversationRows();
        out.active_thread = chat_.buildActiveThread();
        out.device_status = device_.snapshot();
        return out;
    }

    CommandResult sendMessage(const UiSendMessageAction& action) {
        AppCommand cmd = AppCommand::sendDirect(action.peer, action.text);
        return commands_.post(cmd);
    }

    void selectConversation(domain::NodeId peer) {
        selected_peer_ = peer;
    }

private:
    ChatService& chat_;
    DeviceStatusService& device_;
    IAppCommandSink& commands_;
    domain::NodeId selected_peer_;
};
```

目的：

```text
LVGL / ASCII / GTK 都可以消费 ChatWorkspaceModel，
但不共享布局实现。
```

------

## 13.3 LVGL Renderer

```cpp
class LvglChatRenderer {
public:
    void render(const ChatWorkspaceSnapshot& snapshot) {
        // only lv_obj_* operations here
    }

    void onButtonSendClicked() {
        UiSendMessageAction action;
        action.peer = selectedPeerFromUi();
        action.text = readTextInput();
        model_.sendMessage(action);
    }

private:
    ChatWorkspaceModel& model_;
};
```

------

## 13.4 ASCII Renderer

```cpp
class AsciiChatRenderer {
public:
    void render(const ChatWorkspaceSnapshot& snapshot, AsciiCanvas& canvas) {
        canvas.drawBox(0, 0, 30, 20);
        canvas.drawText(1, 1, "Conversations");

        for (size_t i = 0; i < snapshot.conversations.count; ++i) {
            canvas.drawText(1, 2 + i, snapshot.conversations.rows[i].title.c_str());
        }
    }

private:
    ChatWorkspaceModel& model_;
};
```

------

## 13.5 GTK Renderer

```cpp
class GtkChatWorkspace {
public:
    void applySnapshot(const ChatWorkspaceSnapshot& snapshot) {
        // must run on GTK main loop
        updateConversationList(snapshot.conversations);
        updateTranscript(snapshot.active_thread);
        updateInspector(snapshot.device_status);
    }

private:
    ChatWorkspaceModel& model_;
};
```

目的：

```text
UI 技术栈之间复用 presentation model，不复用 widget/page。
```

------

# 14. App Composition Root：依赖注入

最终每个 app 只做组装。

## 14.1 ESP32 T-Pager LVGL

```cpp
class EspTPagerComposition {
public:
    void build() {
        board_ = makeUnique<TLoraPagerBoard>();

        EspCapabilityFactory factory(*board_);

        radio_ = factory.createPacketRadio();
        gnss_ = factory.createGnssByteStream();
        ble_ = factory.createBleGattHost();
        display_ = factory.createDisplayHost();

        identity_store_ = makeUnique<EspNvsLocalIdentityStore>();
        peer_key_store_ = makeUnique<EspNvsPeerKeyStore>();
        message_store_ = makeUnique<EspMessageStore>();

        protocol_ = makeUnique<MeshtasticProtocolStrategy>();

        identity_service_ = makeUnique<PeerIdentityService>(
            *identity_store_,
            *peer_key_store_,
            random_,
            clock_
        );

        direct_message_ = makeUnique<DirectMessageService>(
            *protocol_,
            *identity_service_,
            *radio_,
            clock_,
            app_events_
        );

        chat_model_ = makeUnique<ChatWorkspaceModel>(
            chat_service_,
            device_status_,
            app_commands_
        );

        shell_ = makeUnique<LvglShell>(*chat_model_, ...);
    }
};
```

## 14.2 Linux uConsole GTK

```cpp
class LinuxUConsoleGtkComposition {
public:
    void build() {
        board_ = makeUnique<UConsoleAio2Board>();

        LinuxCapabilityFactory factory(*board_);

        radio_ = factory.createPacketRadio();
        gnss_ = factory.createGnssByteStream();

        identity_store_ = makeUnique<SqliteLocalIdentityStore>(db_);
        peer_key_store_ = makeUnique<SqlitePeerKeyStore>(db_);
        message_store_ = makeUnique<SqliteMessageStore>(db_);

        protocol_ = makeUnique<MeshtasticProtocolStrategy>();

        identity_service_ = makeUnique<PeerIdentityService>(
            *identity_store_,
            *peer_key_store_,
            random_,
            clock_
        );

        direct_message_ = makeUnique<DirectMessageService>(
            *protocol_,
            *identity_service_,
            *radio_,
            clock_,
            app_events_
        );

        chat_model_ = makeUnique<ChatWorkspaceModel>(
            chat_service_,
            device_status_,
            app_commands_
        );

        shell_ = makeUnique<GtkShell>(*chat_model_, ...);
    }
};
```

业务对象相同。
替换的是 adapter、store、runtime、renderer。

------

# 15. 自证方式：这套架构如何证明自己正确

这点很重要。架构不能只靠解释，要能被测试证明。

## 15.1 Contract Test

每个 port 都有 contract test。

```cpp
void runPeerKeyStoreContract(IPeerKeyStore& store) {
    PeerPublicKey key = makeTestKey();

    REQUIRE(store.put(key).ok());

    PeerPublicKey loaded;
    REQUIRE(store.get(key.node_id, loaded).ok());
    REQUIRE(loaded == key);

    REQUIRE(store.remove(key.node_id).ok());
    REQUIRE(store.get(key.node_id, loaded).isNotFound());
}
```

同一测试跑：

```text
FakePeerKeyStore
EspNvsPeerKeyStore
Nrf52FlashPeerKeyStore
SqlitePeerKeyStore
```

如果都通过，说明业务层真的不关心存储后端。

------

## 15.2 UseCase Behavior Test

```cpp
TEST("DirectMessageService sends via any packet radio") {
    FakePeerKeyStore keys;
    FakeLocalIdentityStore identity;
    FakePacketRadio radio;
    FakeClock clock;

    MeshtasticProtocolStrategy protocol;
    PeerIdentityService peer_identity(identity, keys, random, clock);
    DirectMessageService service(protocol, peer_identity, radio, clock, events);

    keys.put(makePeerKey());
    identity.save(makeLocalIdentity());

    auto result = service.sendDirect(makeDirectMessage());

    REQUIRE(result.ok);
    REQUIRE(radio.sentPacketCount() == 1);
}
```

这个测试不需要 ESP32，不需要 Linux，不需要 nRF52。
说明业务核心可独立运行。

------

## 15.3 Renderer Test

同一个 snapshot：

```cpp
ChatWorkspaceSnapshot snapshot = makeChatSnapshot();
```

分别测试：

```text
LvglChatRenderer 不改业务
AsciiChatRenderer 输出稳定文本
GtkChatWorkspace 只更新 widget model
```

目的：

```text
UI 渲染不拥有业务状态。
```

------

## 15.4 Race Test

用 fake runtime 模拟事件顺序：

```text
BLE write
Radio RX
GPS fix
Config patch
UI refresh
```

验证：

```text
不会直接跨线程改 service
所有变化通过 queue/snapshot
```

------

# 16. 最终依赖方向

必须始终满足：

```text
Renderer
  -> PresentationModel
    -> AppService
      -> UseCase
        -> Domain
        -> Ports
          <- PlatformAdapter
```

Board 只被 Factory / CompositionRoot 使用：

```text
CompositionRoot
  -> BoardPackage
  -> PlatformFactory
```

协议：

```text
UseCase
  -> ProtocolStrategy
    -> Codec / CryptoFlow / Mapper
```

BLE：

```text
BleHost
  -> PhoneCore
    -> AppFacade / AppCommandSink
```

GPS：

```text
GnssDriver
  -> GnssParser
    -> LocationService
      -> TimeAuthority / AppEvent
```

UI：

```text
AppService
  -> Snapshot
    -> PresentationModel
      -> Renderer
```

------

# 17. 架构核心模式总结

最后把模式和目的再压缩一下。

| 模式                      | 用在哪里                      | 解决什么                        |
| ------------------------- | ----------------------------- | ------------------------------- |
| Hexagonal Architecture    | 整体结构                      | 业务和平台分离                  |
| Ports and Adapters        | Capability 接口               | ESP32/nRF52/Linux 替换实现      |
| Repository                | Store                         | NVS/Flash/SQLite 统一           |
| Strategy                  | Meshtastic/MeshCore           | 协议切换                        |
| Codec                     | 协议编解码                    | 协议字节和业务分离              |
| Mapper                    | 外部协议 ↔ 内部命令           | 防止 ToRadio/FromRadio 污染业务 |
| Application Service       | UseCase                       | 稳定业务唯一实现                |
| Command Handler           | UI/BLE/HostLink 输入          | 多入口统一动作                  |
| State Machine             | Mesh/BLE/GPS/HostLink session | 状态显式化                      |
| Facade                    | Phone/App/DeviceStatus        | 降低跨模块耦合                  |
| Adapter                   | 平台驱动                      | 包装 SDK/API                    |
| Proxy                     | Linux+nRF52 endpoint          | 远端能力本地化                  |
| Null Object               | 不支持能力                    | 消除 null 分支                  |
| Abstract Factory          | Board → Driver                | 板级变体隔离                    |
| Active Object             | Runtime context               | 消解竞态                        |
| Event Queue               | 异步事件                      | 跨线程安全                      |
| Command Queue             | 用户/协议命令                 | 多入口统一                      |
| Immutable Snapshot        | UI 状态                       | 防止 UI 读 mutable service      |
| MVVM / Presentation Model | UI 复用                       | LVGL/ASCII/GTK 分离             |
| Renderer                  | UI 绘制                       | 技术栈隔离                      |
| Composition Root          | apps                          | 集中依赖注入                    |
| ------------------------- | ----------------------------- | ------------------------------- |
