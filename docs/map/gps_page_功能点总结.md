# GPS 页面功能点总结（当前代码结构与职责）

这份文档只做“功能点与代码位置”的对照，避免继续在层级/职责上改乱。

## 1) 页面入口与布局（谁创建了什么）

- 入口函数：`src/ui/ui_gps.cpp` 的 `ui_gps_enter(lv_obj_t* parent)`
- 主要职责：
  - 创建根容器与头部（TopBar）
  - 创建内容区 `content`
  - 在 `content` 下创建地图容器 `g_gps_state.map`
  - 创建右上角按钮面板 `g_gps_state.panel`
  - 在面板中创建按钮：
    - `[Z]oom`（`ZoomBtn`）
    - `[P]osition`（`PosBtn`）
    - `[H]oriz`（`PanHBtn`）
    - `[V]ert`（`PanVBtn`）
    - `[T]racker`（`TrackerBtn`）
    - `[R]oute`（`RouteBtn`）
  - 创建分辨率标签 `g_gps_state.resolution_label`
  - 绑定地图输入事件（键盘/旋钮）
  - 启动定时器（见第 3 节）

注意：
- 右上角按钮“是否可见”，高度依赖后续地图瓦片更新时的层级处理（见第 4 节）。

## 2) 状态结构（所有静态状态放哪里）

- 状态结构体：`src/ui/screens/gps/gps_state.h` 中的 `struct GPSPageState`
- 关键字段：
  - UI 引用：`map / panel / resolution_label / gps_marker / ...`
  - 地图状态：`zoom_level / lat / lng / pan_x / pan_y / has_fix`
  - 瓦片上下文：`anchor / tiles / tile_ctx`
  - 模态框：`zoom_modal / tracker_modal`
  - 轨迹叠加层数据（不是 LVGL 对象本身的层级保证）：
    - `tracker_overlay_active`
    - `tracker_file`
    - `tracker_points`
    - `tracker_screen_points`
  - 循迹叠加层数据（KML 路线）：
    - `route_overlay_active`
    - `route_file`
    - `route_points`
    - `route_screen_points`
    - `route_bbox_valid / route_min_* / route_max_*`

## 3) 定时驱动（页面如何“动起来”）

- 主定时器回调：`src/ui/ui_gps.cpp` 的 `gps_update_timer_cb`
- 逻辑顺序（概念上）：
  - 若有 `pending_refresh`：调用 `update_map_tiles(false)`
  - `tick_loader()`：推进瓦片加载
  - 若 zoom 模态框或 tracker 模态框打开：降低更新强度
  - 否则：`tick_gps_update(true)`

## 4) 地图与瓦片（地图为什么会“盖住按钮”）

地图主流程在：`src/ui/screens/gps/gps_page_map.cpp`

核心函数：
- `update_map_tiles(bool lightweight)`
  - 计算需要哪些瓦片
  - 触发 loading UI
  - 更新分辨率显示
  - 更新 GPS marker 位置
  - 最后 `lv_obj_invalidate(g_gps_state.map)`

风险点（按钮/叠加层消失的常见根因）：
- 瓦片在加载或重建时会改变对象层级与重绘顺序
- 如果没有在“瓦片更新之后”把 UI 控件提到最前，按钮可能被盖住

建议的安全做法（放在 `update_map_tiles()` 末尾，而不是 DRAW 事件里）：
- 只做层级修正，不做 layout：
  - `panel`
  - `resolution_label`
  - `gps_marker`

## 5) 输入与交互（旋钮/按键如何映射到功能）

输入处理主要在：`src/ui/screens/gps/gps_page_input.cpp`

职责：
- 将 LVGL 的键盘/旋钮/点击事件映射到：
  - 缩放
  - 定位
  - 水平/垂直平移
  - 进入 Tracker 模态框
- 这里还处理：
  - 模态框打开时屏蔽部分输入
  - 脏标记与刷新触发

## 6) 轨迹叠加层（GPX 加载与绘制）

轨迹逻辑在：`src/ui/screens/gps/gps_tracker_overlay.cpp`

功能点：
- 轨迹入口：
  - 按钮：`[T]racker`
  - 打开模态框：`gps_tracker_open_modal()`
- 文件选择：
  - 读取 `/trackers` 目录
  - 选择后加载 GPX
- GPX 读取策略（内存安全）：
  - 流式读取（逐行读）
  - reservoir sampling（上限 100 点）
  - 距离去抖（过近点跳过）
- 采样策略（与 zoom 相关）：
  - 通过 `gps::calculate_map_resolution(zoom, lat)` 计算米/像素
  - 采样距离与 zoom 相关，但总点数上限始终 100
- 绘制位置与方式：
  - 通过 `gps_screen_pos(...)` 计算屏幕坐标
  - 用线连接点 + 小圆点标记

关键约束（非常重要）：
- 不要在 `LV_EVENT_DRAW_POST` 回调中做以下操作：
  - `lv_obj_move_foreground(...)`
  - `lv_obj_invalidate(...)`
  - 改变布局/层级/尺寸
- DRAW 回调只负责“纯绘制”

## 7) 循迹（KML 路线）

循迹是“预设路线导航”模式，路线来自 KML 文件，业务入口在 Tracker 页面。

业务流程（端到端）：
- Tracker 页面 → Route 模式
  - 从 `/routes` 目录读取 `.kml` 列表
  - `Load`：写入 `AppConfig.route_enabled=true` 与 `AppConfig.route_path="/routes/xxx.kml"`
  - `Disable`：清空 `route_enabled/route_path`
- GPS 页面
  - 读取配置后显示 `[R]oute` 按钮
  - 点击 `[R]oute`：地图跳转到路线范围（自动居中 + 适配缩放）
  - 路线在地图上以点状渲染（由浅到深，体现方向）

核心文件与职责：
- Tracker 入口与写配置：`src/ui/screens/tracker/tracker_page_components.cpp`
- GPS 按钮显示/隐藏：`src/ui/ui_gps.cpp`
- 输入映射（RouteBtn / R 键）：`src/ui/screens/gps/gps_page_input.cpp`
- 路线解析与绘制：`src/ui/screens/gps/gps_route_overlay.cpp`

KML 解析策略（当前支持）：
- `gx:Track` 的 `<gx:coord>`（优先）
- `LineString` / `<coordinates>`（兼容）

## 8) 退出与清理（避免状态泄漏）

清理入口：
- `src/ui/ui_gps.cpp` 的 `ui_gps_exit(...)`
- 相关清理：
  - 关闭模态框
  - 停止定时器
  - 清理轨迹状态：`gps_tracker_cleanup()`

## 9) 这页最容易被改坏的三个点（请优先避免）

1) 在 DRAW 事件里动层级或触发重绘
- 这会直接导致抖动、撕裂、甚至卡死

2) 改动对象父子关系但没同步修正层级维护点
- 例如把 overlay / label / panel 从 `map` 挪到 `content`
- 需要同步考虑瓦片重建后谁会盖住谁

3) 轨迹绘制与瓦片重建互相抢刷新节奏
- 轨迹计算要尽量“有变化才重算”
- 轨迹绘制要尽量“轻量、纯绘制”

---

如果接下来要继续改 GPS 页，建议先做两件事：

- 先冻结结构：只允许在 `update_map_tiles()` 末尾做层级修正
- 再做优化：给轨迹点位计算加“变化检测/缓存”
