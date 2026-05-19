# Linux 适配现状评估与开发指南

评估日期：2026-05-08

适用范围：本文评估并指导 Trail Mate 仓库中的 Linux 线，重点覆盖
`apps/linux_sim`、`apps/linux_rpi`、`apps/linux_unoq`、`platform/linux/*`、
`modules/*` 与 `modules/core_sys/include/platform/ui/*` 之间的关系。

本文不是单个功能的任务清单。它的目标是回答三个问题：

1. 当前 Linux 适配到底做到哪种程度。
2. 接下来应该先做什么，后做什么。
3. 怎样优雅地适配 Linux，而不是把 Linux 变成另一个长期分叉。

## 0. 总结

当前 Linux 适配已经从“结构成型、模拟器可验证、真实设备尚未闭环”，推进到了“真机闭环结构正在搭建，但当前工作区还没有重新 build-closed”的阶段。

更具体地说：

- 仓库结构已经把 Linux 当成一等目标在推进，而不是临时实验目录。
- `apps/linux_sim` 仍然是最快的共享 UI 验证壳，并且 source list 已经开始通过 `cmake/TrailMateLinuxSources.cmake` 收口。
- `platform/linux/common` 已经实现了一批 Linux-safe 平台合约，包括 settings、time、screen、device、GPS NMEA/env、hostlink TCP、route/tracker 文件存储、pack repository、SSTV/walkie/LoRa 的模拟运行时，以及 Wi-Fi/USB/FOTA 等显式 unsupported 桩。本轮又新增了 runtime path、env helper、capability status、runtime mode、demo world 抽取等结构；最新推进中，`CapabilityStatus` 已经上移为 `platform::ui` 合约种子并进入 LoRa/SSTV/Walkie public header，hostlink/team/SSTV/map tiles 也开始更多复用 `runtime_paths`。
- `apps/linux_rpi` 仍有两条设备路径：一条是 repo-local CMake framebuffer shell，一条是 `M5Stack_Linux_Libs` SCons SDK shell。custom framebuffer path 已经接入 evdev 源文件和输入 drain，但仍没有完成 build-closed 与真机验证；SDK path 已从很薄的占位 main 推进到 fbdev 自动探测、LVGL evdev 初始化和启动日志齐备的 bring-up shell，但仍尚未接上 Trail Mate shared shell。
- 边界检查已经存在并通过。当前 `modules/ui_shared` 和 `modules/core_*` 没有命中 `Arduino.h`、`Preferences`、`freertos/*`、`platform/esp/*` 这类关键平台污染规则。
- 真机完成度仍低于模拟器完成度。显示、输入、能力模型、真实硬件运行时、SDK 主路径、安装发布、设备 CI 都还没有形成完整闭环；其中能力模型已经有合约种子和部分 public API，但还没有进入 contract inventory 和 UI 呈现。
- 本轮代码有明显架构进展，上一轮列出的主要 compile/runtime gate 已基本修掉；当前最大风险已经从“源码明显不一致”转为“尚未完整构建、测试和真机验证”。不能因为结构文件已经出现，就把 P1/P2/P3/P4/P5/P6 直接标记为完成。

一句话判断：

> 现在不是“Linux 还没开始”，也不是“Linux 已经适配好了”。本轮回归后，Linux 线已经进入 L4 结构施工阶段；输入路径、SDK bring-up、路径安全和能力合约的结构明显前进，并且一部分上轮 P0 项已经从“待修”降级为“待验证”。但当前工作区仍需要先完成剩余编译验证和真机验证，才能把 L4 从“结构存在”推进到“可构建、可运行、可验证”。

### 0.1 2026-05-07 回归结论（历史记录）

本次回归基于 `LINUX_ADAPTATION_GUIDE.md` 之后的代码改动重新评估，结论如下：

注意：本节保留 2026-05-07 当天的判断，用于观察演进；当前状态以 2026-05-08 的 `0.6` 小节为准。

| 指南项 | 本轮代码变化 | 当前判断 |
| --- | --- | --- |
| P1 CMake source list 收口 | 新增 `cmake/TrailMateLinuxSources.cmake`，`linux_sim`/`linux_rpi` 的 CMake 明显变薄 | 方向正确，结构基本落地；但必须通过 sim/rpi 两套构建后才能标记 done |
| P2 LVGL ownership 拆分 | 新增 `ShellSession`、`CanvasLvglHost`、`NativeLvglHost` | 方向正确，已从“单一 runner”进入“双 host”结构；但当前 header/callback 细节仍会阻断或破坏输入 |
| P3 真机输入 | 新增 `EvdevInput`，`LinuxFramebufferPlatform::drainInput()` 改为从 evdev 读取 | 历史判断：当日还没闭环；该行的 source wiring / key map / 目录探测问题已在 2026-05-08 回归中更新 |
| P4 runtime path/env | 新增 `platform/linux/runtime_paths.*`、`env_config.*`，route/tracker 删除路径开始限制在根目录下 | 明显进展；但 settings/sstv/hostlink/team 等还没有完全统一到同一套 path helper |
| P5 capability truth model | 新增 `CapabilityState`/`CapabilityStatus`，LoRa/Walkie/SSTV 开始返回 `Simulated` | 概念已经落地为代码，但还没有成为 `platform::ui::*` 合约的一部分，也没有驱动 UI 呈现 |
| P6 demo world/facade 拆分 | 新增 `linux_demo_world.*` | 只是第一步：app facade 里仍保留旧 loopback mesh、dummy crypto、demo seeding，`runtime_mode` 尚未真正控制 composition |
| P7 M5 SDK 主路径 | 暂未看到 shared shell 接入 `M5Stack_Linux_Libs` | 仍未完成 |
| P8 第一个验证 slice | path safety smoke 新增，Settings slice 还未形成真实验收闭环 | 测试方向正确，但当前 smoke test 本身还需要 include 修正 |

当前最高优先级不是继续扩大功能面，而是先让这轮结构性改动重新达到“能构建、能跑 smoke、能解释能力真假”的状态。

### 0.2 本轮必须先修的具体阻断点

这些问题属于“先修它们，否则后续评估会失真”的层级：

| 类型 | 文件 | 2026-05-08 状态 | 仍需动作 |
| --- | --- | --- | --- |
| Build gate | `platform/linux/common/include/app/linux_demo_world.h` / `platform/linux/common/src/app/linux_demo_world.cpp` | 上轮 header `<memory>`、constexpr node id、重复定义风险已修 | 用完整构建确认；后续再清理 facade 内仍重复存在的 loopback mesh 逻辑 |
| Architecture gate | `platform/linux/common/src/app/linux_app_facade.cpp` | demo seeding 已被 `demo_world_enabled(resolve_runtime_mode())` gate | 仍要把 loopback mesh、dummy crypto、loopback pairing 从真实设备默认 composition 中拆出去 |
| Build gate | `platform/linux/common/include/ui/shell_ui_runner.h` / `platform/linux/common/src/ui/shell_ui_runner.cpp` | callback 已恢复为 `lv_area_t` / `lv_indev_data_t` 精确签名，header 只做 LVGL 类型前向声明 | 用完整构建确认 LVGL typedef 与前向声明是否兼容；如果 LVGL 的 `lv_area_t` / `lv_indev_data_t` 不是带 `_lv_*` tag 的 typedef，应改为包含 `lvgl.h` 或把 callback 降到 `.cpp` 私有自由函数 |
| Runtime gate | `platform/linux/common/src/ui/shell_ui_runner.cpp` | `hasPendingKeyEvent()` 已加入，`continue_reading` 不再消费下一事件 | 需要用小测试或真机手测确认 press/release 都能被 LVGL 收到 |
| Build gate | `apps/linux_rpi/CMakeLists.txt` | `evdev_input.cpp` 已加入 device executable | 用 Linux/WSL CI 构建确认 |
| Build gate | `platform/linux/rpi/src/platform/device/evdev_input.cpp` | key map 已改为 `std::to_array<KeyMapping>({...})`，手写数量和 CTAD 风险已修 | 用 Linux 构建确认；后续补真机键位采样和映射测试 |
| Runtime gate | `platform/linux/rpi/src/platform/device/evdev_input.cpp` | by-path/by-id 不存在目录已用 `error_code` 与 `is_directory` 防护 | 还需真机采样 Cardputer Zero 内置键盘事件码，完善 Fn/组合键映射 |
| Build gate | `apps/linux_sim/tests/path_safety_smoke.cpp` | 已 include `platform/linux/capability_status.h` | 用 `ctest` 确认 smoke 真正通过 |
| Contract gate | `modules/core_sys/include/platform/ui/capability_status.h` / `modules/core_sys/include/platform/ui/{lora,sstv,walkie}_runtime.h` | `CapabilityStatus` 已从 Linux helper 上移为 `platform::ui` 合约种子，Linux helper 只做 re-export | 还要在各 runtime public header 中声明 `capability_status()`，更新 contract inventory，并让 UI 使用状态而不是只看 `is_supported()` |
| Path gate | `platform/linux/common/src/platform/ui/*` / `platform/linux/common/src/ui/widgets/map/map_tiles.cpp` | hostlink、team store、SSTV、map tiles 已改用 `runtime_paths`，hostlink 默认 bind 已变成 loopback | `pack_repository_runtime.cpp` 仍有 ad hoc root；map tiles 等处旧 env 常量已无实际用途；`runtime_paths.h` 仍提到 fsync，但 `.cpp` 实现已明确为 no-fsync temp+rename，需要二选一校准 |
| Runtime mode | `apps/linux_sim/src/targets/simulator_main.cpp` / `platform/linux/common/include/platform/linux/runtime_mode.h` | simulator main 只在用户未设置 `TRAIL_MATE_RUNTIME_MODE` 时默认写入 `demo`；Windows `_putenv_s()` 已进入同一 guard | 继续用 Windows/WSL 启动 smoke 确认 env override；继续确认 device shell 默认不进入 demo；真实设备 composition 仍需拆掉 dummy/loopback 默认实现 |
| Specification gate | `apps/linux_rpi/docs/specification/project-baseline.md` 等 | 部分文字已更新，但仍需和 build/test 事实对齐 | `DONE` 只用于 build/test/真机验证过的事实；目标态用 `TARGET`、`PARTIAL` 或 `NOT YET VALIDATED` |

### 0.3 本轮验证记录

本轮回归做了有限验证，结论要分清：

| 命令 | 结果 | 解释 |
| --- | --- | --- |
| `python scripts/check_platform_ui_boundaries.py` | 通过 | 说明 shared/core 的 ESP/Arduino/FreeRTOS include 污染边界仍然干净 |
| `cmake -S apps/linux_sim -B .codex-build/linux-sim-review -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug` | 配置通过 | 说明新的 CMake helper 至少能被配置阶段消费 |
| `cmake --build .codex-build/linux-sim-review --target trailmate_cardputer_zero_path_safety_smoke -j 8` | 未能验证项目代码 | 本机 `clang++ 14` 被 VS 2022 STL 以 “expected Clang 19.0.0 or newer” 拦截，失败发生在标准库兼容层，不代表项目代码已经或没有编译通过 |
| `cmake -S apps/linux_sim -B .codex-build/linux-sim-msvc -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON` | 超时 | 124 秒内未完成配置，不能作为通过或失败结论 |

因此，本文中的 compile gate 清单来自代码阅读和目标依赖关系回归；最终仍要用 Linux/WSL CI 或可用的 MSVC generator 完整构建来确认。

### 0.4 2026-05-08 回归结论

注意：本节记录上一轮 2026-05-08 回归点；当前最新判断以 `0.6` 为准。

本轮相对 2026-05-07 的回归结果如下：

| 指南项 | 2026-05-08 观察 | 当前判断 |
| --- | --- | --- |
| P1 CMake source list 收口 | rpi target 已显式加入 `platform/linux/rpi/src/platform/device/evdev_input.cpp`，sim/rpi 仍通过 shared helper 组织 common/ui sources | P1 继续向好；剩余是完整构建验证、helper 注释/编码清理，以及 target define 语义收口 |
| P2 LVGL ownership 拆分 | `ShellSession::hasPendingKeyEvent()` 已加入，`readInputCb()` 不再为了 `continue_reading` 消费下一个事件；callback 已恢复为 `lv_area_t` / `lv_indev_data_t` 精确签名 | 上一轮的输入吞事件问题已修；callback 风险已收窄到 LVGL typedef/前向声明兼容性，仍需构建确认；SDK native host 仍未接入 |
| P3 真机输入 | `evdev_input.cpp` 已被 rpi CMake 编入；`/dev/input/by-path` 与 `/dev/input/by-id` 已用 `error_code` 与目录判断保护；`kKeyMap` 已改为 `std::to_array<KeyMapping>` | source wiring、目录探测、key map 初始化风险已修；剩余是 Linux 构建确认和 Cardputer Zero 真机键位采样 |
| P4 runtime path/env | `path_safety_smoke.cpp` 已 include `capability_status.h`；route/tracker 仍走 root containment | path safety smoke 的 include 问题已修；但 SSTV、team store、hostlink、pack repository 还没有完全统一到 `runtime_paths` |
| P5 capability truth model | `CapabilityState` 仍是 Linux 内部 helper，LoRa/Walkie/SSTV 仍通过 `capability_status()` 标记 `Simulated` | 概念稳定，但尚未进入 `platform::ui::*` 公共合约和 UI 呈现 |
| P6 demo world/facade 拆分 | `runtime_mode.h` 已补 `<cstring>`，`ensureStarted()` 已用 `demo_world_enabled(resolve_runtime_mode())` gate demo seeding，simulator main 显式默认 `TRAIL_MATE_RUNTIME_MODE=demo` | demo seed 默认语义更清楚；但 facade 仍总是组合 loopback mesh、dummy crypto、loopback pairing，DeviceLocal 还不是干净真实设备 composition |
| P7 M5 SDK 主路径 | 未看到 `apps/linux_rpi/main/src/main.cpp` 接入 `ShellSession`/`NativeLvglHost` | 仍未完成 |
| P8 第一个验证 slice | path safety smoke 源码更完整，但本地尚未完成有效构建/ctest | 测试方向正确，验收仍依赖 Linux/WSL/CI 构建 |

一句话：这轮把 2026-05-07 的一批“明确红灯”基本修成了“等待构建确认”，但还没越过 build/test/真机三道门。下一步最小闭环应是直接在 Linux/WSL 上构建 `linux_sim` tests 和 `linux_rpi` framebuffer target，然后做 Cardputer Zero 真机键位采样与导航验收。

### 0.5 2026-05-08 最新回归结论

本轮在 `0.4` 之后继续推进了三类结构：SDK bring-up、能力合约上移、runtime path 覆盖。结论要比 `0.4` 更细：有些项已经从“缺结构”变成“缺公开 API / 缺验证”，但仍不能直接标记为完成。

| 指南项 | 最新观察 | 当前判断 |
| --- | --- | --- |
| P1 CMake source list 收口 | `cmake/TrailMateLinuxSources.cmake` 的路径注释已经校准为“从 helper 所在目录推 repo root”；rpi target 继续显式编入 `evdev_input.cpp` | 结构上继续健康；当前剩余不是再抽象，而是完整 Linux/WSL build、ctest 和 rpi target build |
| P2 LVGL ownership 拆分 | `ShellSession` / `CanvasLvglHost` / `NativeLvglHost` 结构未再倒退；callback 精确签名和 `hasPendingKeyEvent()` 仍保持 | 方向稳定；仍需构建确认 LVGL typedef/前向声明兼容性，并把 SDK main loop 从注释切到 `NativeLvglHost` |
| P3 真机输入 | CMake framebuffer path 的 evdev adapter 仍是当前 shared shell 输入主线；SDK path 也已用 LVGL evdev 创建 pointer/keypad | 输入结构明显增强；仍缺真机事件码采样、Fn/组合键映射、press/release 回归和 rpi build 结果 |
| P4 runtime path/env | settings、route、tracker、hostlink、team store、SSTV 已基本走 `runtime_paths`；hostlink 默认 bind 已是 `127.0.0.1`，需要对外监听时显式设 `TRAIL_MATE_HOSTLINK_BIND=0.0.0.0` | P4 从“零散 helper”推进到“多数核心写入点收口”；剩余 map tiles、pack repository、旧 env 常量、`safe_write_under_root()` fsync 注释/实现不一致 |
| P5 capability truth model | 新增 `modules/core_sys/include/platform/ui/capability_status.h`，Linux 侧 `platform/linux/capability_status.h` 变为 re-export；LoRa/Walkie/SSTV 实现层返回 `Simulated` | 概念已经进入合约层，但 public runtime headers 尚未声明 `capability_status()`，contract README 未列入 `capability_status.h`，UI 也还没消费；因此只能算“合约种子落地”，不能算能力呈现完成 |
| P6 demo world/facade 拆分 | `runtime_mode` 继续只 gate demo seed；`MinimalLinuxAppFacade` 仍总是组合 loopback mesh、dummy crypto、loopback pairing | demo/local 的数据种子边界更清楚，但真实设备 composition 仍不干净；另有 Windows simulator 会覆盖用户预设 `TRAIL_MATE_RUNTIME_MODE` 的小坑 |
| P7 M5 SDK 主路径 | `apps/linux_rpi/main/src/main.cpp` 已具备 ST7789 framebuffer 探测、LVGL fbdev、LVGL evdev pointer/keypad、启动日志和 bring-up UI；shared shell 接入代码仍在注释中 | SDK path 已不再是空壳，但仍是 bring-up UI，不是 Trail Mate shared shell；L5 仍未完成 |
| P8 第一个验证 slice | 本轮未新增有效 build/ctest 结果；已有本地 build 受 Windows clang/MSVC STL 版本拦截 | 继续保持“代码阅读评估 + 边界检查可验证”，最终结论仍要等 Linux/WSL CI 或真机验收 |

一句话：最新推进把 P4/P5/P7 的结构成熟度又抬了一档，但 Linux 线仍停在“结构越来越像最终形态、验证还没闭环”的阶段。下一步最优先不是继续加功能，而是把这些结构跑通：Linux/WSL build、capability public API、SDK shared shell 接入、真机输入采样。

### 0.6 2026-05-08 再次回归结论

本轮相对 `0.5` 又推进了三个上一轮点名的缺口：simulator runtime mode 覆盖语义、LoRa/SSTV/Walkie capability public API、map tile runtime path。结论如下：

| 指南项 | 最新观察 | 当前判断 |
| --- | --- | --- |
| P0-E runtime mode 覆盖语义 | `apps/linux_sim/src/targets/simulator_main.cpp` 现在只在 `TRAIL_MATE_RUNTIME_MODE` 未设置时写入 `demo`；Windows `_putenv_s()` 也被放到同一个 guard 后面 | 上轮指出的 Windows 覆盖用户预设问题已修；剩余是用 Windows/WSL 启动 smoke 验证 env override |
| P4 runtime path/env | `platform/linux/common/src/ui/widgets/map/map_tiles.cpp` 已改为 `resolve_paths().sd_root`；hostlink/team/SSTV/map tiles 都不再复制完整 storage root fallback | P4 继续变好；仍剩 `pack_repository_runtime.cpp` 默认从 `__FILE__` 推 repo root，map tiles 里旧 `kSdRootEnv`/`kSettingsRootEnv` 常量已无用途，`runtime_paths.h` 头文件仍写 fsync 但实现已改成 no-fsync temp+rename |
| P5 capability truth model | `lora_runtime.h`、`sstv_runtime.h`、`walkie_runtime.h` 已 include `platform/ui/capability_status.h` 并公开声明 `CapabilityStatus capability_status()` | P5 从“合约种子落地”推进到“部分 runtime public API 落地”；剩余是更新 `modules/core_sys/include/platform/ui/README.md` contract inventory，并让 UI 消费该状态 |
| P7 M5 SDK 主路径 | `apps/linux_rpi/main/src/main.cpp` 仍是 bring-up UI，shared shell 接入仍停在注释 | 本轮未推进；L5 仍未完成 |
| P8 验证 slice | 本轮重新跑了 boundary check，通过；仍未完成 Linux/WSL build、ctest 或真机验证 | 边界干净，但 build/run gate 仍是下一步硬门槛 |

一句话：这轮把 `0.5` 里的几项“结构缺口”又补了一层，尤其是 P0-E 和 P5 public API。当前最值得立刻做的不是继续写更多 runtime，而是清理两个残留文档/API 小口子，然后跑 Linux/WSL 构建和至少一个 capability UI 消费闭环。

## 1. 先做区分

### 1.1 当前最容易混在一起的概念

Linux 适配不能只理解成“让代码能在 Linux 编译”。当前仓库里至少有这些必须分开的对象：

| 概念 | 它是什么 | 它不是什么 |
| --- | --- | --- |
| Linux 目标线 | Trail Mate 的一等平台族 | 不是 ESP/Arduino 的另一个 board profile |
| `apps/linux_sim` | 桌面模拟器和开发工具壳 | 不是真机运行时，不应承接 Pi OS 硬件细节 |
| `apps/linux_rpi` | Pi OS / Cardputer Zero 真机 app shell | 不应吸收模拟器几何、桌面输入或 demo app 结构 |
| `platform/linux/common` | simulator 与 rpi 都可共享的 Linux-safe 实现层 | 不应放 SDL 外壳、fbdev 设备假设或业务规则 |
| `platform/linux/rpi` | Pi OS / framebuffer / 设备适配层 | 不应放共享 UI 页面逻辑 |
| `M5Stack_Linux_Libs` | 真实设备 SDK 基线 | 不是 Trail Mate 架构边界的来源 |
| `M5CardputerZero-UserDemo` | 板级线索参考 | 不是项目模板 |
| 平台合约 | `modules/core_sys/include/platform/ui/*` 中的协作面 | 不是某个平台的实现细节 |
| 平台实现 | `platform/esp/*`、`platform/linux/*` 中的具体实现 | 不拥有 domain/usecase/UI 结构 |
| 能力支持 | 真实或可解释的运行时能力 | 不是“菜单上有入口”或“有一个模拟桩” |

### 1.2 当前 specification 基线

后续 Linux 适配应遵守以下基线：

- `modules/core_*` 是核心业务、协议、策略、用例、状态和纯工具层。
- `modules/ui_shared` 是共享 LVGL 表现层，可依赖核心模块和平台合约，不可依赖平台实现。
- `modules/core_sys/include/platform/ui/*` 是共享 UI 与平台实现之间的合约层。
- `platform/linux/common` 实现 Linux 共享合约，只放 simulator 与 device 都成立的东西。
- `platform/linux/rpi` 实现 Pi OS / Cardputer Zero 特有适配。
- `apps/linux_sim` 和 `apps/linux_rpi` 只做 composition root、启动和目标选择。
- `M5Stack_Linux_Libs` 应位于 Trail Mate 平台边界之下，由 Linux device shell 或 `platform/linux/rpi` 消费。

### 1.3 明确非法的切法

这些做法会让 Linux 适配变得不优雅，后续应避免：

- 把 `Cardputer Zero` 当成另一个 MCU board，强行通过 `BoardBase` 抽象接入。
- 把 SDL、fbdev、evdev、`/dev/input/*`、`/dev/fb*` 写进 `ui_shared`。
- 把 `M5CardputerZero-UserDemo` 的目录结构或 app 组织方式复制进仓库。
- 把“环境变量可模拟”误认为“真机能力已支持”。
- 把 hostlink、GPS、SSTV、LoRa 的模拟数据当成产品真实能力。
- 因为 CMake 中两个 Linux app 都需要同一批源码，就长期手写两份 source list。
- 在 shared/core 里撒 `#ifdef __linux__` 或 `#ifdef _WIN32` 来逃避边界设计。
- 在 `apps/linux_rpi` 里直接堆真机驱动、UI 页面和业务逻辑。

## 2. 当前适配程度

### 2.1 成熟度分级

这里的等级不是测试覆盖率，而是架构和产品可用性的成熟度。

| 等级 | 含义 | 当前状态 |
| --- | --- | --- |
| L0 | 有目录和文档，Linux 被识别为目标线 | 已完成 |
| L1 | Linux host 可以配置、编译基础目标 | 历史上成立；本轮结构改动后仍需要重新跑 build gate |
| L2 | 桌面模拟器能跑共享 shell，形成快速验证闭环 | 结构成立；仍需重新证明 simulator build/test |
| L3 | Linux common runtime 覆盖主要平台合约，有 smoke tests | 基本完成且本轮有 path/capability 进展；`CapabilityStatus` 已上移为合约种子并进入 LoRa/SSTV/Walkie public header，path smoke 源码已修正但未验收 |
| L4 | Pi OS framebuffer/device shell 可编译并启动共享 UI | 结构推进明显，evdev source wiring 已补；SDK bring-up shell 更完整；仍未 build/run-closed |
| L5 | `M5Stack_Linux_Libs` 主路径跑 Trail Mate 共享 shell | SDK bring-up 已加强，但 shared shell 尚未接入 |
| L6 | 真机显示、输入、存储、GPS、音频、网络/无线能力按能力模型闭环 | 未完成 |
| L7 | 可安装、可发布、可回归、可支持多 Linux 设备族 | 未完成 |

当前整体结论：L3 的方向和代码面都成立，但本轮改动后仍必须重新 build-closed；L4 已经从 display-only 走向 display+input + SDK bring-up 的结构阶段，但仍属于未验收状态；L5 还没闭环。

### 2.2 分项评估

| 维度 | 当前程度 | 证据 | 缺口 |
| --- | --- | --- | --- |
| 目录结构 | 较好 | `apps/linux_sim`、`apps/linux_rpi`、`apps/linux_unoq`、`platform/linux/common`、`platform/linux/rpi` 已存在；本轮新增 `cmake/TrailMateLinuxSources.cmake` | `platform/linux/unoq` 还只有规划；Linux docs 与 app-local specs 的层级关系仍需维护 |
| 架构规范 | 较好 | `docs/LINUX_ADAPTATION_GUIDE.md` 与 `apps/linux_rpi/docs/specification/*` 已形成 final-shape / checklist / 回归指南组合 | app-local specs 里个别 `DONE` 状态比代码更乐观，需要用 build/test 结果校准 |
| 边界保护 | 较好 | `scripts/check_platform_ui_boundaries.py` 存在并保护 shared/core 不被 ESP 污染 | 规则还偏 include 污染，尚未覆盖 CMake/source ownership、能力语义、路径安全、demo/real composition |
| 模拟器 | 较好 | `apps/linux_sim` 有 SDL3 simulator、CMake presets、tests、WSL/dev-container 脚本，并开始复用 shared CMake helper | 本轮改动后要重新证明 simulator build/test；模拟器能力和真实能力界线还需更强标注 |
| Linux common runtime | 中等偏好 | settings/time/screen/device/GPS/hostlink/tracker/route/SSTV/walkie/LoRa/pack repository 等已实现；runtime path/env/capability 基础继续推进，hostlink/team/SSTV/map tiles 已开始收口到 `runtime_paths` | 很多是 soft runtime 或 synthetic runtime，不应直接宣称真机支持；pack repository 仍有 ad hoc path root；capability 尚未进入 UI |
| Pi OS CMake device shell | 中等偏早 | custom framebuffer target 使用共享 shell runner，已从 `EvdevInput` drain 真机输入，rpi CMake 已编入 evdev 源文件，key map 已改为 `std::to_array<KeyMapping>` | 仍需 Linux 构建确认；Cardputer Zero 真机键位验证仍未闭环 |
| M5Stack SDK path | 早期偏中 | `apps/linux_rpi/SConstruct`、`main/SConstruct`、`config_defaults.mk`、`main/src/main.cpp` 已存在；main 已具备 ST7789 framebuffer 探测、LVGL fbdev、LVGL evdev pointer/keypad 和启动日志 | SDK path 目前仍是 bring-up UI，shared shell 接入只在注释中，未复用 Trail Mate `ShellSession` / `NativeLvglHost` |
| UI 共享 | 中等偏好 | `trailmate_cardputer_zero_ui_shell` 编译大量 `modules/ui_shared` 页面和资产；新增 `ShellSession`/`CanvasLvglHost`/`NativeLvglHost`，输入事件吞掉问题已修，callback 已回到 LVGL 精确类型签名 | SDK native path 尚未接入；仍需完整构建确认 LVGL typedef/前向声明兼容性 |
| CMake 结构 | 明显改善 | linux_sim 与 linux_rpi 已改为 include 共享 helper，source list 不再主要散在两个 app CMake 里，rpi-only evdev source 已接入，helper 路径注释已校准 | 需要跑 sim/rpi build；target compile definitions 和 runtime mode 默认语义还需收口 |
| 真机能力 | 早期 | fbdev path、LVGL evdev bring-up、NMEA serial path、hostlink TCP path 已有雏形；hostlink 默认 loopback 更安全 | Cardputer Zero 键位、battery、brightness、Wi-Fi、USB、firmware update、real radio/audio 等未闭环 |
| CI | 中等偏好 | `.github/workflows/linux-simulator.yml` 覆盖 simulator build/tests；`.github/workflows/uconsole-linux.yml` 覆盖 uConsole GTK build/tests/package | Cardputer Zero Linux 不再借用这个 workflow 名义；仍不构建 M5 SDK path，不跑真机/虚拟 fbdev，不验证 Docker GUI path |
| 开发体验 | 较好 | Windows PowerShell、Linux shell、WSL validate、Docker dev container 脚本齐全 | 本地环境变量和 path bridge 还在演进；不同 host 的路径/权限策略需要统一文档化 |

### 2.3 已经做得比较好的地方

1. Linux 不是外挂目录，而是进入了目标结构。

`apps/README.md`、`docs/ARCHITECTURE.md`、`platform/linux/README.md` 都表明 Linux 目标线已被纳入长期结构。

2. Simulator 与 real device 的边界已经开始分开。

`apps/linux_sim` 的 README 明确 simulator-first，`apps/linux_rpi` 的 README 明确 Pi OS device shell。`platform/linux/common` 被定义为两者共享层。

3. shared UI 已经能被 Linux shell 消费。

Linux CMake target 编译大量 `modules/ui_shared/src/ui/*` 页面、组件、资产，并通过 `SharedUiShellStartup` 进入共享 boot/menu shell。

4. 核心模块平台污染已经显著改善。

当前边界检查通过，`modules/ui_shared/library.json` 已不再带 ESP Arduino include root。过去规范中提到的某些违规项已经被修掉。

5. Linux common runtime 已经不是空壳。

`platform/linux/common/src/platform/ui/*` 已经覆盖了一批真实或可模拟的能力：文件设置、屏幕 timeout、GPS NMEA、hostlink TCP、tracker/route 文件、team UI store、pack repository、SSTV/walkie/LoRa synthetic runtime 等。

6. CMake 重复已经开始收口。

`cmake/TrailMateLinuxSources.cmake` 把 common sources、UI shell sources、include roots 和 warnings helper 提到了共享层。后续添加 shared UI 文件时，不应再让 simulator 和 rpi 各自维护一份大 source list。

7. LVGL ownership 的正确方向已经出现。

`ShellSession` 开始只拥有 app facade、startup、事件队列和 per-frame app tick；`CanvasLvglHost` 拥有 `lv_init()`、display、RGB565 buffer 和 canvas copy；`NativeLvglHost` 给 SDK-owned LVGL path 留了入口。这是接入 `M5Stack_Linux_Libs` 的正确中间形态。

8. 真机输入和路径安全都开始从“口头要求”变成代码。

`EvdevInput` 已经把 `/dev/input/event*`、by-path/by-id 键盘探测、`TRAIL_MATE_INPUT_DEVICE` override、Linux key code 到 `InputEvent` 的映射放到了 `platform/linux/rpi`。`runtime_paths` 和 `resolve_child_under_root()` 也让 route/tracker 删除开始具备 root containment；最新推进还把 hostlink、team store、SSTV 等更多写入点收口到同一套 runtime root。

9. CI 已经能挡住基础倒退。

专门的 `Linux Simulator` workflow 会跑 simulator build 和 smoke tests；`uConsole Linux` workflow 会跑 uConsole GTK build、smoke tests 和 Debian package。Cardputer Zero Linux 需要单独的 device-shell / hardware validation gate，当前不再用 workflow 名称暗示它已经完成。

10. 能力真假开始从 Linux 私有概念上移到公共合约。

`modules/core_sys/include/platform/ui/capability_status.h` 已经成为 `Unsupported` / `Simulated` / `Available` / `Degraded` / `Error` 的共享类型来源，Linux 侧 `platform/linux/capability_status.h` 退化为 re-export。LoRa/SSTV/Walkie 的 public header 已经公开 `capability_status()`，这是好方向；下一步要更新 contract inventory，并让 UI 真正消费这些状态，而不是只在实现层或文档层停留。

### 2.4 主要未完成项

1. 真机主路径尚未闭环。

当前有两个 device path：

- CMake custom framebuffer path：接了 shared shell，并已把 evdev 输入源文件接入 rpi target；但还没有通过 Linux 构建和真机操作验收。
- `M5Stack_Linux_Libs` SCons path：更接近未来真机主路径，目前已具备 fbdev 自动探测、LVGL evdev、启动日志和 bring-up UI，但还没有运行 Trail Mate shared shell。

这两个路径需要收敛：优先让 SDK path 跑 shared shell，同时保留 custom framebuffer path 作为轻量 fallback 或验证工具。

2. LVGL ownership 已经拆出雏形，但还未验收。

当前 `ShellSession`、`CanvasLvglHost`、`NativeLvglHost` 的职责划分比上一版正确很多。新的问题是：

- `shell_ui_runner.h` 已前向声明 `lv_area_t`、`lv_indev_data_t`，callback 已恢复为 LVGL 精确类型签名。后续必须通过完整构建确认这些前向声明是否与 LVGL 自身 typedef 兼容；若 LVGL 使用匿名 typedef，最稳妥的做法是把 callback 改成 `.cpp` 私有自由函数或直接在 header 包含 `lvgl.h`。
- `readInputCb()` 的事件吞掉问题已经通过 `hasPendingKeyEvent()` 修掉，但需要用测试或真机输入回归确认。
- `NativeLvglHost` 只是 thin host，还没有被 `M5Stack_Linux_Libs` main loop 消费。

3. 输入已经开始适配设备，但有阻断点。

SDL 模拟器有完整键盘和鼠标映射。Pi CMake framebuffer path 现在通过 `EvdevInput` 返回 `InputEvent`，这是关键进展。上一轮的 source wiring 和目录探测问题已基本修掉。剩余必须修：

- `EvdevInput` 的 `kKeyMap` 已改为 `std::to_array<KeyMapping>({...})`，手写数量和 CTAD 风险已消除；后续重点是 Linux 构建确认与真机键位采样。
- Cardputer Zero 内置键盘的 Fn/组合键/方向键需要用真机事件码采样后建立映射表，不要只依赖 PC keyboard key code。

4. 能力真假还需要制度化。

例如：

- `lora::is_supported()` 当前返回 true，但实现是 synthetic RSSI。
- `walkie::is_supported()` 当前返回 true，但实现是 synthetic 音量/收发电平。
- `sstv::is_supported()` 当前返回 true，但实现是生成 PPM 模拟图。
- `wifi::is_supported()`、`usb_support::is_supported()`、`firmware_update::is_supported()` 返回 false，这类比较诚实。

本轮新增 `CapabilityState`/`CapabilityStatus` 是正确方向，而且最新代码已经把它放进 `modules/core_sys/include/platform/ui/capability_status.h`，这意味着它开始成为平台合约。LoRa/SSTV/Walkie 的 public header 已经声明 `capability_status()`。剩余问题是：`modules/core_sys/include/platform/ui/README.md` 的 contract inventory 还没有列出 `capability_status.h`；共享 UI 还没有用该状态替代“只看 `is_supported()`”。后续需要制度化区分 `Unsupported`、`Simulated`、`Available`、`Degraded`、`Error`，否则 UI 和用户预期会失真。

5. 构建描述重复已经缓解，但 build gate 尚未通过。

`cmake/TrailMateLinuxSources.cmake` 已经解决了最大块的 source list 重复。剩余问题是：

- rpi-only 源文件已经有显式接入点，后续可考虑抽成 rpi adapter target，但不必为了形式立即重构。
- simulator main 已显式设置默认 `TRAIL_MATE_RUNTIME_MODE=demo`，这比让 common target define 决定 simulator 默认行为更清楚。最新代码已把默认写入放到 `std::getenv("TRAIL_MATE_RUNTIME_MODE")` guard 后面，Windows 和 POSIX 语义已经对齐；后续还要用启动 smoke 验证用户预设不会被覆盖，并确认 device shell 默认 `local`，让真实设备 composition 不再默认使用 dummy/loopback。

6. 文件路径安全已经改善，但还没有覆盖所有写入点。

`route_storage::remove_route()`、`tracker::remove_track()` 已经开始用 `resolve_child_under_root()`。最新推进又把 hostlink、team store、SSTV output、map tiles 的默认 root 收到了 `runtime_paths`。下一步要把同一套 path helper 扩展到 pack repository、settings temp file 等路径写入点，并清理 map/hostlink/team/SSTV 中已经无实际用途的旧 env 常量。`safe_write_under_root()` 的 `.cpp` 注释已经改成 no-fsync temp+rename，但 header 仍写 fsync；要么补真实 fsync / `FlushFileBuffers`，要么把 header 注释改成准确描述。

7. demo/loopback 逻辑只完成了抽取起点。

`linux_demo_world.*` 已经出现，demo peer seeding 已经通过 `runtime_mode` gate；但 `MinimalLinuxAppFacade` 里仍保留旧 `LinuxLoopbackMeshAdapter`、dummy crypto、loopback pairing service 等真实设备不应默认启用的逻辑。`runtime_mode` 还没有真正决定整个 facade composition。

8. 真机安全和网络默认值需要持续保持谨慎。

`hostlink` 当前已经默认 bind 到 `127.0.0.1`，这比上一版更适合真机安全默认值。需要对外监听时应显式设置 `TRAIL_MATE_HOSTLINK_BIND=0.0.0.0`，并在 UI 或文档里说明这会把端口暴露到设备所在网络。

## 3. 目标结构规划

### 3.1 最终目录职责

推荐目标结构保持如下：

```text
trail-mate/
  apps/
    linux_sim/        # 桌面模拟器、WSL、dev-container、host tooling
    linux_rpi/        # Cardputer Zero / Pi OS 真机 app shell
    linux_unoq/       # 未来 UNO Q Linux shell

  modules/
    core_sys/         # 系统合约、clock、portable utilities
    core_chat/        # chat domain/usecase/protocol-neutral infra
    core_gps/         # GPS domain/filter/policy
    core_team/        # team domain/protocol/usecase
    core_hostlink/    # hostlink protocol/session
    ui_shared/        # LVGL shared UI and presentation

  platform/
    linux/
      common/         # Linux-safe implementations shared by sim and rpi
      rpi/            # Pi OS/Cardputer Zero specific adapters
      unoq/           # UNO Q specific adapters when introduced
    esp/
      ...
    shared/
      ...
```

### 3.2 依赖方向

合法方向：

```text
apps/linux_* -> platform/linux/*
apps/linux_* -> modules/ui_shared
apps/linux_* -> modules/core_*

modules/ui_shared -> modules/core_*
modules/ui_shared -> platform contracts in modules/core_sys/include/platform/ui/*

platform/linux/* -> platform contracts
platform/linux/* -> OS / SDK / drivers

modules/core_* -> standard C/C++ and portable module dependencies only
```

非法方向：

```text
modules/core_* -> platform/linux/*
modules/core_* -> platform/esp/*
modules/ui_shared -> platform/linux/*
modules/ui_shared -> platform/esp/*
platform/linux/common -> apps/linux_sim/*
platform/linux/common -> apps/linux_rpi/*
apps/linux_rpi -> apps/linux_sim/*
```

### 3.3 建议新增的结构

后续可以逐步增加这些结构，不要求一次性完成：

```text
cmake/
  TrailMateLinuxSources.cmake
  TrailMateModuleTargets.cmake

modules/core_sys/include/platform/ui/
  capability_status.h

platform/linux/common/include/platform/linux/
  runtime_paths.h
  capability_status.h  # re-export only; shared contract lives in modules/core_sys
  env_config.h

platform/linux/common/src/platform/linux/
  runtime_paths.cpp
  env_config.cpp
  safe_file_ops.cpp

platform/linux/rpi/src/platform/device/
  evdev_input.cpp
  evdev_input.h
  m5stack_lvgl_shell_host.cpp
  m5stack_lvgl_shell_host.h

platform/linux/common/include/ui/
  shell_session.h

platform/linux/common/src/ui/
  shell_session.cpp
  canvas_lvgl_host.cpp
  native_lvgl_host.cpp
```

核心意图：

- CMake source ownership 要集中。
- path/env/capability 不要在十几个 runtime 文件里重复。
- LVGL session 要和 LVGL display owner 分离。
- 真实设备输入要成为 `platform/linux/rpi` 的 adapter，不要写进 shared UI。

## 4. 接下来要做什么

先看本轮回归后的优先级，不要直接沿用上一版 backlog 的顺序：

| 优先级 | 事项 | 为什么现在要先做 | 完成标准 |
| --- | --- | --- | --- |
| P0-A | 完成构建闭环 | 主要源码红灯已修，下一步需要用真实构建确认 common/ui/rpi/test 都成立 | sim common、ui shell、path safety smoke、rpi framebuffer target 都能编译 |
| P0-B | 回归 Shell input callback | 事件吞掉问题已修成 peek 语义，但需要验证 press/release 是否都被 LVGL 收到 | 单个 `InputEvent` enqueue 后 LVGL 能看到 press 和 release |
| P0-C | 验证 evdev path 正常参与 rpi 构建 | `evdev_input.cpp` 已进入 rpi target，但还没有 Linux/CI build 结果 | rpi framebuffer target 编译通过；无输入设备时只降级不崩溃 |
| P0-D | 校准 docs/spec 的 DONE 状态 | app-local specs 里有些 DONE 是目标态，不是已验收事实 | 文档中的 DONE 只代表经过 build/test/运行验证的事实 |
| P0-E | 验证 simulator runtime mode 覆盖语义 | 代码已改为只在 env 未设置时填入 `demo`，Windows `_putenv_s()` 也已进入 guard | Windows 与 POSIX 都通过启动 smoke 证明用户预设不会被覆盖 |
| P1 | 继续 CMake helper 收口 | helper 已存在，下一步是让它稳定、可维护、可 CI 检查 | 添加 shared source 只改一处；sim/rpi CI 都绿 |
| P2 | 完成 LVGL host/session 分层 | 这是接 SDK path 的前提 | simulator/custom fbdev 使用 `CanvasLvglHost`，SDK path 使用 `NativeLvglHost` |
| P3 | 真机输入采样与映射 | evdev adapter 只解决了读事件，还没证明 Cardputer Zero 内置键盘语义 | 有事件码采样记录、映射表、真机导航验收 |
| P4 | 统一 runtime path/env/capability | 基础 helper 已覆盖更多 runtime，map tiles 已收口；pack repository、旧 env 常量和部分注释仍未收口 | 所有文件 runtime 走同一套 root/path helper；各 runtime public header 能报告 capability status；synthetic 能力不再冒充真实 |
| P5 | demo world 与真实 facade 分离 | 新文件已出现，但 composition 未变 | `SimulatorDemo`、`DeviceLocal`、`DeviceRealMesh` 使用明确不同的 wiring |
| P6 | SDK path 接 shared shell | SDK bring-up 已加强，正好可以切换到 `NativeLvglHost` | `apps/linux_rpi/main/src/main.cpp` 不再是 bring-up UI，而是 Trail Mate shared shell |

### P0：把现状文档和 specification 校准

目标：文档不能继续描述已经修掉的债务，也不能把当前 demo 能力写成真机能力。

要做：

- 保留 `apps/linux_rpi/docs/specification/*` 作为历史和 target-specific specification。
- 以本文作为顶层 Linux 适配总指南。
- 更新旧文档中已经过期的“known current violations”，例如 `ui_shared/library.json` 的 ESP include 依赖已经不再成立。
- 在 README 中明确本文与 app-local specs 的层级关系。

验收：

- 新同事读 `docs/LINUX_ADAPTATION_GUIDE.md` 能判断文件应该放哪里。
- 旧文档不再误导工程师去修已不存在的问题。

### P1：消除 Linux CMake source list 重复

回归状态：已开始落地。`cmake/TrailMateLinuxSources.cmake` 已经承担 shared source list、include roots 和 target helper，rpi-only `evdev_input.cpp` 也已显式接入 device target，helper 的 repo root 注释已经校准。下一步不是重新设计，而是确认 target define 语义，并让 simulator/rpi 两条构建都通过。

目标：让 `linux_sim` 与 `linux_rpi` 共享同一套 CMake source/target 定义。

建议做法：

1. 新建 `cmake/TrailMateLinuxSources.cmake`。
2. 抽出这些变量：

```cmake
set(TRAIL_MATE_LINUX_COMMON_SOURCES
    "${TRAIL_MATE_LINUX_COMMON_ROOT}/app/linux_app_facade.cpp"
    "${TRAIL_MATE_LINUX_COMMON_ROOT}/platform/ui/settings_store.cpp"
    ...
)

set(TRAIL_MATE_UI_SHARED_SOURCES
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/app_runtime.cpp"
    ...
)
```

3. 提供 helper：

```cmake
function(trailmate_add_linux_common target_name)
    add_library(${target_name} STATIC ${TRAIL_MATE_LINUX_COMMON_SOURCES})
    target_include_directories(${target_name} PUBLIC ...)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
endfunction()

function(trailmate_add_linux_ui_shell target_name common_target)
    add_library(${target_name} STATIC ${TRAIL_MATE_UI_SHARED_SOURCES})
    target_link_libraries(${target_name} PUBLIC ${common_target} lvgl)
endfunction()
```

4. `apps/linux_sim/CMakeLists.txt` 只保留 simulator 特有 SDL target。
5. `apps/linux_rpi/CMakeLists.txt` 只保留 device 特有 framebuffer target。

注意：

- 先抽 source list，不急着把所有 `modules/core_*` 都变成独立 CMake target。
- 抽完后确保 CI、WSL validate、Windows VS preset 仍能工作。

验收：

- 添加一个 shared UI 源文件时，只需要改一处 CMake source list。
- simulator 与 rpi CMake target 使用同一批 common/ui sources。

### P2：拆分 LVGL shell session 与 display owner

回归状态：已开始落地。`ShellSession`、`CanvasLvglHost`、`NativeLvglHost` 的形状是对的；input dequeue 语义已修成 peek，LVGL callback 也已恢复为精确 typed signature。当前还需要完整构建确认 LVGL typedef/前向声明兼容性，并接入 SDK main loop。

目标：同一套 Trail Mate shared shell 可以跑在 SDL/canvas，也可以跑在 `M5Stack_Linux_Libs` 已创建的 LVGL display 上。

当前问题：

- `ShellUiRunner` 负责 `lv_init()`、display 创建、buffer 创建、input 创建、app facade、startup、tick、canvas copy。
- 这对模拟器方便，但对 SDK path 不优雅，因为 SDK 已经拥有 fbdev/evdev/LVGL backend。

建议拆法：

```text
ShellSession
  - 绑定 MinimalLinuxAppFacade 或未来 LinuxAppFacade
  - setTeamUiEventDispatcher
  - SharedUiShellStartup begin/tick
  - app facade update/tick/dispatch
  - lv_timer_handler 前后的共享逻辑
  - 不创建 display，不拥有 lv_init/lv_deinit

CanvasLvglHost
  - 给 simulator/custom fbdev 用
  - 拥有 lv_init/lv_deinit
  - 创建 LVGL display 和 RGB565 buffer
  - flush 后复制到 Canvas

NativeLvglHost
  - 给 M5Stack SDK path 用
  - 不拥有 display
  - 使用 SDK 已创建的 LVGL display/input
  - 只驱动 ShellSession tick
```

实现细节：

- `lv_init()` 每个进程只能有清晰 owner。不能 simulator、SDK、shell session 各自调用。
- `lv_deinit()` 只能由 owner 调。
- `ShellSession` 析构时只解绑 facade 和 dispatcher，不删除它不拥有的 display/input。
- `ShellSession::tick()` 不应该 sleep。sleep 由 host runner 决定。
- `CanvasLvglHost` 继续用 16 ms frame pacing。
- `NativeLvglHost` 在 SDK main loop 里按 SDK 推荐 tick cadence 调用。

验收：

- simulator 仍可跑。
- CMake custom framebuffer shell 仍可跑。
- SDK `main/src/main.cpp` 能从 bring-up UI 切到 Trail Mate shared shell。

### P3：实现真机输入适配

回归状态：已开始落地。`EvdevInput` 已经出现并被 `LinuxFramebufferPlatform::drainInput()` 调用，rpi CMake source wiring、by-path/by-id 目录探测异常、key map 初始化风险都已修。当前必须完成 Linux 构建确认和真机键位采样。

目标：Cardputer Zero 在 Pi OS 上能通过内置键盘/按钮操作 shared shell。

当前状态：

- SDL simulator 输入完整。
- CMake framebuffer device shell 已开始接 `EvdevInput`，但还未通过编译和真机按键验收。
- SDK bring-up path 有 `LV_LINUX_KEYBOARD_DEVICE` 和 evdev hint，但尚未进入 Trail Mate 输入语义。

建议路线：

1. 短期：在 SDK path 使用 LVGL evdev keypad。

适合快速真机 bring-up。`apps/linux_rpi/main/src/main.cpp` 已经具备类似结构：

```cpp
lv_indev_t* keyboard = lv_evdev_create(LV_INDEV_TYPE_KEYPAD, keyboard_device);
lv_indev_set_display(keyboard, display);
```

需要补：

- 默认设备发现不要只写死某个 `/dev/input/by-path/...`。
- 支持 `LV_LINUX_KEYBOARD_DEVICE` 显式覆盖。
- 记录检测日志，失败时不要静默。

2. 中期：在 `platform/linux/rpi` 做 evdev -> `InputEvent` adapter。

适合 custom framebuffer path 和未来 UNOQ path：

```text
platform/linux/rpi/src/platform/device/evdev_input.{h,cpp}
```

职责：

- nonblocking open evdev file。
- 读取 `struct input_event`。
- 处理 `EV_KEY` press/release/repeat。
- 映射 Linux key code 到 `trailmate::cardputer_zero::app::InputEvent`。
- 允许 env 覆盖：`TRAIL_MATE_INPUT_DEVICE`。
- 支持多个候选路径：`/dev/input/by-path/*-event-kbd`、`/dev/input/event*`。
- 不把 evdev 头文件暴露到 `platform/linux/common`。

映射建议：

| Linux key | Trail Mate key |
| --- | --- |
| `KEY_ESC` | `Power` 或 back，取决于设备语义 |
| `KEY_ENTER` | `Enter` |
| `KEY_BACKSPACE` | `Backspace` |
| `KEY_TAB` | `Tab` |
| `KEY_HOME` | `Home` |
| `KEY_END` | `Next` |
| `KEY_LEFT/RIGHT/UP/DOWN` | directional keys |
| printable ASCII | `Character` |
| `KEY_LEFTSHIFT/RIGHTSHIFT` | `Shift` |
| `KEY_LEFTCTRL/RIGHTCTRL` | `Ctrl` |
| `KEY_LEFTALT/RIGHTALT` | `Alt` |

实现细节：

- 输入 adapter 不应该直接调用 LVGL。
- `SurfacePresenter::drainInput()` 可以短期继续承载输入队列，但长期可拆出 `InputSource`。
- repeat 策略要明确：导航键可以 repeat，文本输入可以按系统 repeat，modifier 不 repeat。
- 字符输入必须考虑 shift。短期可用简单 US keyboard map；长期如需中文/IME，应走 `ui_shared` IME。

验收：

- 真机按键能进入菜单、打开页面、返回、输入文本。
- `TRAIL_MATE_INPUT_DEVICE=/dev/input/eventX` 可覆盖设备。
- 没有输入设备时，device shell 明确日志提示并继续显示 UI。

### P4：集中 Linux runtime path/env/capability

回归状态：部分落地且本轮继续推进。`runtime_paths.*`、`env_config.*` 已出现，route/tracker 的删除路径开始做 root containment；settings、hostlink、team store、SSTV、map tiles 也已经开始通过 `runtime_paths` 取默认 root。下一步是把剩余文件写入点和 env parsing 收到同一套 helper，并把 path safety smoke 纳入稳定测试。

目标：避免每个 runtime 文件重复实现 `TRAIL_MATE_SD_ROOT`、`TRAIL_MATE_SETTINGS_ROOT`、`HOME`、`APPDATA` fallback。

当前仍需继续收口的重复点：

- `map_tiles.cpp` 已改用 `runtime_paths`，但旧的 `kSdRootEnv` / `kSettingsRootEnv` 常量还残留，容易造成误读或编译告警
- `pack_repository_runtime.cpp` 仍默认从 `__FILE__` 推 repo root，没有进入 runtime root 模型
- `settings_store.cpp` 已用 `settings_file()`，但 temp file 写入还没有复用 `safe_write_under_root()`
- `route_storage.cpp` 和 `tracker_runtime.cpp` 已部分改用 path helper，但还需统一写入和 list 语义
- `device_runtime.cpp`、`hostlink_runtime.cpp`、`team_ui_store_runtime.cpp`、`sstv_runtime.cpp` 中仍有一些旧 env 常量或注释需要清理，避免后来者误判真实配置来源
- `runtime_paths.h` 仍写着 `safe_write_under_root()` 会 fsync，但 `.cpp` 已明确说当前只是 close + atomic rename

已有基础形态，后续保持这个方向扩展：

```cpp
namespace platform::linux_runtime
{
struct RuntimePaths
{
    std::filesystem::path settings_root;
    std::filesystem::path sd_root;
    std::filesystem::path cache_root;
    std::filesystem::path state_root;
};

RuntimePaths resolve_paths();
std::filesystem::path settings_file(const char* ns);
std::filesystem::path sd_child(std::string_view relative);
bool resolve_child_under_root(const std::filesystem::path& root,
                              std::string_view relative,
                              std::filesystem::path& out);
}
```

路径策略：

- `TRAIL_MATE_SETTINGS_ROOT` 显式指定 settings/state 根目录。
- `TRAIL_MATE_SD_ROOT` 显式指定模拟 SD 卡根目录。
- Linux host 默认可使用 `$HOME/.trailmate_cardputer_zero`，后续可演进到 XDG。
- Windows simulator 默认可继续使用 `%APPDATA%/TrailMateCardputerZero`。
- 真机 system service 模式后续应支持 `/var/lib/trail-mate` 或用户配置目录，但不要现在硬编码。

安全规则：

- UI 删除 route/track 时只接受文件名或相对 ID，不接受任意 absolute path。
- 平台层必须做 root containment 检查。
- 拒绝 `..`、absolute path、空名字、路径分隔符混入 ID。
- 删除前 resolve canonical/weakly_canonical，确认结果仍在 root 下。

验收：

- 所有 Linux runtime 使用同一套 path helper。
- route/tracker/team/sstv/hostlink/map/pack repository 不再各自复制 storage root 逻辑。
- 路径安全有单元测试。

### P5：建立能力真实性模型

回归状态：概念已进入公共合约种子，并且 LoRa/Walkie/SSTV 已经公开 public API。`CapabilityState`/`CapabilityStatus` 已上移到 `modules/core_sys/include/platform/ui/capability_status.h`，Linux 侧 helper 只做 re-export，`lora_runtime.h`、`walkie_runtime.h`、`sstv_runtime.h` 已声明 `CapabilityStatus capability_status()`，实现层开始返回 `Simulated`。下一步是让 contract inventory 和 UI 呈现跟上。

目标：UI 与用户文档能区分“不支持”“模拟支持”“真实支持”“降级支持”。

建议新增统一状态：

```cpp
enum class CapabilityState
{
    Unsupported,
    Simulated,
    Available,
    Degraded,
    Error,
};

struct CapabilityStatus
{
    CapabilityState state;
    const char* message;
};
```

短期不需要一次性改完所有合约，但不能长期停在“API 存在但无人消费”的状态。建议先做三件小事：

- 在 `modules/core_sys/include/platform/ui/README.md` 的 contract inventory 中列出 `capability_status.h`。
- 保持 `lora_runtime.h`、`walkie_runtime.h`、`sstv_runtime.h` 的声明与 Linux 实现一致，并在后续新增 runtime 时沿用同一模式。
- 在至少一个共享 UI status/footer 或 diagnostics 页面中展示 `Simulated`，验证 UI 不再把 synthetic runtime 当成真实支持。

最终最好让关键 runtime 暴露状态：

- Wi-Fi
- GPS
- LoRa
- Walkie
- SSTV
- Hostlink
- USB mass storage
- Firmware update
- Battery/power
- Display/input

当前建议分类：

| 功能 | 当前代码状态 | 建议能力状态 |
| --- | --- | --- |
| Settings store | file-backed | Available |
| Time offset | settings + system clock | Available |
| Screen timeout | soft idle state | Degraded |
| Device memory | `/proc/meminfo` on Linux | Available/Degraded |
| Battery | env only | Simulated |
| GPS default data | env/default | Simulated |
| GPS NMEA file/serial | parser + Linux serial | Degraded to Available, 取决于设备 |
| Hostlink TCP | local TCP server，默认 loopback | Available, 对外监听必须显式配置 |
| Route/tracker storage | file-backed | Available with safety fix |
| Team store | memory + GPX append | Degraded |
| LoRa RSSI | synthetic | Simulated |
| Walkie | synthetic | Simulated |
| SSTV | generated frame | Simulated |
| Wi-Fi | unsupported | Unsupported |
| USB mass storage | unsupported | Unsupported |
| Firmware update | unsupported | Unsupported |
| Orientation | empty | Unsupported |

验收：

- UI 不会把 synthetic LoRa/walkie/SSTV 展示成真实硬件能力。
- 文档和 status message 能解释为什么不可用或仅模拟。

### P6：拆出真实 Linux app facade 与 demo world

回归状态：完成了第二步。`linux_demo_world.*` 已新增，demo seeding 已由 `runtime_mode` gate；但 facade 仍然直接拥有 loopback mesh、dummy crypto、loopback pairing。下一步要让 runtime mode 决定完整 composition，而不是让真实设备默认继承 simulator demo world 的通信与加密假实现。

目标：把“为模拟器好用的假世界”和“真实设备 facade”分开。

当前 `MinimalLinuxAppFacade` 同时做了：

- app config persistence
- service composition
- demo peers seed
- loopback mesh adapter
- team pairing loopback
- dummy team crypto
- event bus bridge
- fallback team UI updates

建议拆分：

```text
platform/linux/common/src/app/
  linux_app_facade.cpp
  linux_app_facade.h
  linux_demo_world.cpp
  linux_demo_world.h
  linux_loopback_mesh_adapter.cpp
  linux_loopback_mesh_adapter.h
  linux_team_runtime.cpp
  linux_team_runtime.h
  linux_app_composition.cpp
  linux_app_composition.h
```

然后区分 composition mode：

```cpp
enum class LinuxRuntimeMode
{
    SimulatorDemo,
    DeviceLocal,
    DeviceRealMesh,
};
```

关键要求：

- dummy crypto 只能在 `SimulatorDemo` 使用。
- real device path 禁止使用 XOR crypto 伪实现。
- demo peer seed 只能由 simulator 或显式 demo mode 启用。
- app facade 不应该知道“显示器是 SDL 还是 fbdev”。

验收：

- 启动 simulator 仍有 demo 数据。
- 启动 device shell 默认不注入假 peer，除非显式 `TRAIL_MATE_DEMO_WORLD=1`。
- dummy crypto 不会进入真机 release build。

### P7：让 `M5Stack_Linux_Libs` 成为真机主路径

目标：真实 Cardputer Zero 设备路径优先使用 SDK 的 LVGL/fbdev/evdev/device plumbing。

步骤：

1. 保留 `apps/linux_rpi/main/src/main.cpp` 里的 display/input detect 逻辑，但把 bring-up UI 替换成 shared shell session。
2. 在 SDK main 中：

```cpp
lv_init();
initLinuxDisplay();

trailmate::cardputer_zero::linux_ui::ShellSession shell;
shell.begin();

while (true)
{
    shell.tick();
    lv_timer_handler();
    usleep(1000);
}
```

实际代码以拆分后的 `ShellSession` API 为准。

3. `config_defaults.mk` 继续启用：

```text
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_USE_EVDEV=y
```

4. 自动检测 framebuffer：

- 优先 `LV_LINUX_FBDEV_DEVICE`
- 其次 `/proc/fb` 中的 `fb_st7789v`
- 最后 `/dev/fb0`

5. 自动检测 keyboard：

- 优先 `LV_LINUX_KEYBOARD_DEVICE`
- 其次 by-path/by-id 候选
- 最后明确报错但不中断显示

验收：

- `bash apps/linux_rpi/scripts/build-sdk-device.sh` 能构建 shared shell。
- 真机能显示 boot/menu shell。
- 真机键盘能导航。
- 不再用 SDK bring-up placeholder 文案作为默认 UI。

### P8：选择第一个真实 feature verification slice

建议第一个真实页面选择 `Settings`，原因：

- 它依赖 settings/time/screen/device 这些基础合约。
- 它不需要真实 radio、GPS、audio、network。
- 它能验证 shared UI、平台合约、持久化、输入、能力状态。
- 它能暴露 Linux common runtime 的真实质量。

Settings slice 应验证：

- timezone offset 保存/读取。
- screen timeout 保存/读取。
- notification volume 保存/读取。
- battery/GPS/Wi-Fi/USB/FOTA 能力状态正确显示。
- 重启 simulator/device 后设置仍存在。
- 无设备能力时 UI 不出现不可执行动作。

验收：

- simulator 通过 Settings 页面进行真实设置。
- rpi SDK path 通过 Settings 页面进行真实设置。
- smoke test 或 integration test 验证 settings persistence。

## 5. 优雅适配 Linux 的原则

### 5.1 平台合约先于平台实现

如果一个功能需要被 `ui_shared` 使用，先确认它是否属于已有 `platform::ui::*` 合约。

- 已有合约足够：只加 Linux 实现。
- 合约缺字段：先扩展合约，再同时审视 ESP/Linux 实现。
- 没有合约：先判断这是共享能力、平台能力、还是 app shell 私有能力。

不要从 Linux 实现反推共享 API。实现方便不等于抽象正确。

### 5.2 app shell 保持薄

`apps/linux_sim` 和 `apps/linux_rpi` 应该主要做：

- main/entrypoint
- build configuration
- target-specific composition
- process lifecycle
- CLI/env parsing
- selecting adapters

不要在 app shell 里长期放：

- business state
- page layout
- protocol handling
- storage format
- radio/GPS/audio logic

### 5.3 common 只放真正共享的 Linux-safe 代码

进入 `platform/linux/common` 的标准：

- simulator 和 rpi 都需要。
- 不依赖 SDL。
- 不依赖 fbdev/evdev/ioctl。
- 不依赖具体 device path。
- 不含真机特定假设。
- 不拥有 UI 页面结构。

如果只是 desktop simulator 需要，放 `apps/linux_sim`。

如果只有 Pi OS/Cardputer Zero 需要，放 `platform/linux/rpi` 或 `apps/linux_rpi`。

### 5.4 真实、模拟、unsupported 必须诚实

Linux 早期开发需要大量模拟能力，这是合理的。但必须诚实标注。

推荐规则：

- synthetic runtime 可用于 simulator。
- synthetic runtime 可用于真机 debug，但必须显式 opt-in。
- 真机默认 UI 不应把 synthetic runtime 展示为硬件能力。
- `is_supported()` 不要表示“有代码路径”，而要表示“此目标下用户可合理使用”。

### 5.5 不用 `#ifdef` 替代边界

允许的 `#if defined(...)`：

- 在平台实现 `.cpp` 内隔离 OS API，例如 hostlink socket、GPS serial、gmtime。
- 在 build system 中选择 target。
- 在少量 cross-host simulator 代码里处理 Windows/Linux 差异。

不推荐的 `#if defined(...)`：

- 在 `modules/core_*` 里判断 Linux/ESP。
- 在 `modules/ui_shared` 页面里判断 Linux/ESP。
- 在业务逻辑里判断设备型号。

### 5.6 SDK 是设备依赖，不是架构来源

`M5Stack_Linux_Libs` 应该帮我们少写 fbdev、evdev、LVGL glue。

它不应该决定：

- Trail Mate 的模块边界。
- app/page 结构。
- core/usecase 的接口。
- storage/schema。
- Linux common 的职责。

### 5.7 能用测试守住的边界就写测试

文档是必要的，但不够。每个容易退化的点都应尽量有自动检查：

- include 污染：已有 boundary check。
- path 安全：新增 unit test。
- capability truth：新增 status test。
- CMake source ownership：通过 shared cmake helper 降低人工错误。
- simulator/device 构建：CI 继续跑。
- SDK path：至少提供可选 CI 或 manual verification script。

## 6. 新功能开发流程

任何新的 Linux 适配功能都按这个流程走。

### Step 1：先分类

先回答：

- 这是 domain/usecase/protocol 吗？是则属于 `modules/core_*`。
- 这是 LVGL 共享页面/组件吗？是则属于 `modules/ui_shared`。
- 这是平台合约吗？是则属于 `modules/core_sys/include/platform/ui/*`。
- 这是 Linux 实现吗？是则属于 `platform/linux/common` 或 `platform/linux/rpi`。
- 这是进程启动/目标选择/build wiring 吗？是则属于 `apps/linux_*`。

### Step 2：确认合约

检查 `modules/core_sys/include/platform/ui/*`。

如果已有合约：

- 不新增平行 util。
- 不在 UI 里直接访问 Linux 文件、socket、env。

如果没有合约：

- 先设计最小合约。
- 合约只表达共享语义，不表达 Linux 细节。

示例：

```cpp
namespace platform::ui::wifi
{
bool is_supported();
bool load_config(Config& out);
bool save_config(const Config& config);
Status status();
}
```

合约里不应该出现：

```cpp
std::filesystem::path;
int fd;
struct input_event;
sockaddr_in;
lv_indev_t*;
```

### Step 3：实现 Linux common 或 rpi adapter

放置规则：

| 情况 | 位置 |
| --- | --- |
| file-backed settings | `platform/linux/common/src/platform/ui/settings_store.cpp` |
| generic host TCP transport | `platform/linux/common/src/platform/ui/hostlink_runtime.cpp` |
| generic NMEA parser | `platform/linux/common/src/platform/ui/gps_runtime.cpp` 或拆出 helper |
| `/dev/input/event*` | `platform/linux/rpi/src/platform/device` |
| `/dev/fb*` mmap/ioctl | `platform/linux/rpi/src/platform/device` |
| SDL geometry/input | `apps/linux_sim/src/platform/simulator` |
| M5Stack SDK glue | `apps/linux_rpi/main` 或 `platform/linux/rpi` |

### Step 4：UI 通过合约消费

`modules/ui_shared` 只能：

- 调 `platform::ui::<feature>` 合约。
- 根据 status/capability 渲染 enabled/disabled/unsupported state。
- 触发合约动作。

不能：

- 读 Linux env。
- 打开文件。
- 访问 `/dev/*`。
- 引入 `platform/linux/*` 头。

### Step 5：添加测试

最少测试层级：

- contract smoke：是否能调用、是否返回合理默认状态。
- persistence test：设置是否能保存/读取。
- path safety test：非法路径是否被拒绝。
- simulated data test：GPS NMEA、hostlink、tracker、team 等。
- UI smoke：如果功能影响 shell，确保 simulator boot/menu/page 不崩。

Linux tests 当前可放：

```text
apps/linux_sim/tests/
```

随着模块化增强，可逐步迁移到：

```text
tests/linux/
tests/modules/
```

### Step 6：更新构建

短期：

- 改 shared CMake source helper。
- 确保 `apps/linux_sim` 和 `apps/linux_rpi` 都用同一处 source list。

中期：

- 把 `modules/core_*` 变成 CMake targets。
- `ui_shared` 也成为 target。
- app shell 只 link targets，不手写内部文件。

### Step 7：更新文档

每个 Linux 适配 slice 完成后，至少更新：

- 本文的当前状态或 backlog。
- 对应 feature 文档。
- app README 中的运行方式。
- 如果能力状态变化，更新 capability table。

## 7. 关键实现细节指南

### 7.1 CMake

当前问题是两个 Linux app 都手写大 source list。优雅做法是集中 source ownership。

短期推荐：

```text
cmake/TrailMateLinuxSources.cmake
```

并在两个 app 中：

```cmake
include("${PROJECT_SOURCE_DIR}/../../cmake/TrailMateLinuxSources.cmake")

trailmate_add_linux_common(trailmate_cardputer_zero_linux_common)
trailmate_add_linux_ui_shell(trailmate_cardputer_zero_ui_shell trailmate_cardputer_zero_linux_common)
```

注意：

- `FetchContent` 的 SDL3 只应在 simulator target 中出现。
- `FetchContent` 的 LVGL 可以由 shared helper 提供，但 SDK path 不一定走这个 LVGL。
- `apps/linux_rpi` CMake path 可以继续使用 fetched LVGL，SDK SCons path 用 SDK LVGL。
- 不要让 root `CMakeLists.txt` 被 Linux 目标劫持；root 仍有 ESP-IDF 历史职责。

### 7.2 LVGL

LVGL 适配最容易失控，必须明确 owner。

规则：

- 一个进程中 `lv_init()` 只能由一个 host layer 调。
- 一个 display 的 buffer 和 flush callback 只由创建者拥有。
- `ui_shared` 创建 object graph，不拥有 Linux display。
- `ShellSession` 只管 Trail Mate session 生命周期，不管 OS display。
- simulator 可以通过 Canvas host 把 LVGL RGB565 buffer 拷到 SDL shell。
- 真机 SDK path 应用 native LVGL display，不走 Canvas copy。

需要避免：

- 在 `ui_shared` 中 include Linux headers。
- 在每个页面里写 Linux 特例。
- 在 SDK path 再套一层 Canvas，导致性能和职责都变差。

### 7.3 输入

当前 shared shell 输入语义是 `InputEvent` 到 LVGL key 的映射。后续保持这个模型即可。

实现建议：

- simulator：继续 SDL event -> `InputEvent`。
- custom framebuffer path：evdev -> `InputEvent` -> `ShellSession`。
- SDK native path：优先用 LVGL evdev；如需要统一行为，再引入 evdev -> `InputEvent`。

输入 adapter 要解决：

- key down/up/repeat。
- modifier。
- printable ASCII。
- back/home/next/power 语义。
- 无输入设备时的 graceful degradation。

### 7.4 存储

所有 Linux storage 都应走统一 root resolver。

建议约定：

| 用途 | env override | 默认 |
| --- | --- | --- |
| settings/state | `TRAIL_MATE_SETTINGS_ROOT` | `$HOME/.trailmate_cardputer_zero` 或 Windows `%APPDATA%` |
| SD-like files | `TRAIL_MATE_SD_ROOT` | settings root 下的 `sdcard` |
| packs | `TRAIL_MATE_PACK_ROOT` | repo root 下 `packs` |
| GPS NMEA file | `TRAIL_MATE_GPS_NMEA_FILE` | 无 |
| GPS serial | `TRAIL_MATE_GPS_DEVICE` | 无自动打开 |

文件写入：

- 小文件使用 temp file + fsync + rename。
- settings namespace 文件名必须 sanitize。
- blob 建议继续 hex 编码或迁移到明确 binary file，不能混入非转义分隔符。
- 删除动作必须 root-contained。

### 7.5 GPS

当前 `gps_runtime.cpp` 已经有不错的基础：

- env 默认位置。
- NMEA RMC/GGA/GSA/GSV 解析。
- Linux serial nonblocking read。
- file source incremental read。
- stale detection。
- GNSS satellite snapshot。

后续改进：

- 把 NMEA parser 拆成可单测 helper。
- 增加 checksum 严格模式配置。
- 增加 serial reconnect backoff。
- 增加 device discovery，但默认不要随便打开所有 `/dev/tty*`。
- 真机 UI 应显示 source：simulated/env/file/serial/stale。

### 7.6 Hostlink

当前 hostlink TCP runtime 已经可用，但需要调整默认安全姿态。

建议：

- simulator 默认 bind `127.0.0.1`。
- 真机要对外监听时必须显式设置 `TRAIL_MATE_HOSTLINK_BIND=0.0.0.0`。
- endpoint file 写入 SD root 下 `hostlink/endpoint.txt` 可以保留。
- 后续如果承载敏感操作，需要认证或 pairing，不要只靠局域网隔离。

### 7.7 LoRa、Walkie、SSTV

当前这三块主要是 synthetic runtime：

- LoRa：生成 RSSI 曲线。
- Walkie：生成 tx/rx level。
- SSTV：生成 frame 并保存 PPM。

短期保留价值：

- 帮助 UI 页面和 flow 在 Linux 上可验证。
- 支撑 simulator 演示。

必须补的边界：

- status 显示 simulated。
- 真机默认不要宣称可用 radio/audio。
- 真实 radio/audio 接入前，不要让上层 protocol 依赖这些 synthetic 行为。

### 7.8 Team 和 Chat

当前 Linux facade 已经让 chat/contact/team 在 simulator 中很有生命力，但这部分最需要防止概念漂移。

要拆清楚：

- `core_chat` 的 domain/usecase/infra store 是可复用核心。
- Linux loopback mesh 是 simulator adapter。
- demo contacts 是 simulator data seed。
- team dummy crypto 是 demo-only。
- team UI store 中内存 snapshot 与 GPX append 是早期 runtime，不等于真实团队持久化完整实现。

真实设备前必须做：

- 替换 dummy crypto。
- 把 demo seed 变成 opt-in。
- 明确 mesh transport：LoRa、BLE、hostlink、local loopback 各是什么。
- 让 contact/node store 的持久化格式有版本和迁移策略。

### 7.9 Wi-Fi、USB、FOTA、Orientation

这些目前多数是 unsupported 或空实现。正确策略是继续显式 unsupported，直到有真实 Linux path。

不要为了菜单完整而返回 `true`。

UI 应根据状态显示不可用，而不是进入半功能页面。

### 7.10 线程和生命周期

Linux common 中已经有 hostlink worker thread。后续新增线程时遵守：

- `start()` 幂等。
- `stop()` 必须 join。
- 析构不抛异常。
- 后台线程不能调用 LVGL。
- shared state 用 mutex/atomic 明确保护。
- 测试要覆盖 start-stop-start。

### 7.11 错误处理

原则：

- app shell main 可以 catch exception 并返回非 0。
- platform runtime 合约尽量返回 `Status` 或 bool + message。
- 真机设备路径失败要可诊断：输出设备路径、errno、fallback。
- simulator 脚本失败要给安装建议。

### 7.12 环境变量命名

继续使用 `TRAIL_MATE_` 前缀。

建议规则：

- runtime root：`TRAIL_MATE_SETTINGS_ROOT`、`TRAIL_MATE_SD_ROOT`
- simulator：`TRAIL_MATE_SIM_*`
- GPS：`TRAIL_MATE_GPS_*`
- hostlink：`TRAIL_MATE_HOSTLINK_*`
- demo：`TRAIL_MATE_DEMO_*`
- device：`TRAIL_MATE_FBDEV`、`TRAIL_MATE_INPUT_DEVICE`

环境变量是启动配置和测试注入，不应成为业务持久化的唯一来源。

## 8. 具体 backlog

### 8.1 立即做

1. 修复当前 compile gate。

验收：simulator common、UI shell、path safety smoke、rpi framebuffer target 都能编译。2026-05-08 这轮已经修掉 LVGL callback 签名和 `evdev_input.cpp` 的 `kKeyMap` 初始化风险，下一步应直接跑 Linux/WSL 下的完整 CMake build。

2. 稳定 Linux CMake source helper。

验收：sim/rpi CMake 不再重复大 source list；rpi-only source 有清晰入口；添加 shared UI 源文件时只改一处。

3. 回归 `ShellSession` 与 LVGL input 细节。

验收：simulator 行为不变；单个 key enqueue 能产生 press/release；`continue_reading` 不吞事件；SDK path 可以复用 session 而不被 `lv_init()` 所有权冲突阻塞。

4. 完成 `platform/linux/rpi` evdev input adapter。

验收：custom framebuffer shell 能通过键盘导航；`TRAIL_MATE_INPUT_DEVICE=/dev/input/eventX` 可覆盖；无输入设备时只降级并打印诊断；Cardputer Zero 内置键盘 Fn/组合键映射来自真机事件码采样。

5. 扩大 runtime path helper 覆盖。

验收：settings、route、tracker、sstv、team、hostlink、map tiles、pack repository 共用路径解析；absolute path、`..`、跨 root 删除都被拒绝；map/hostlink/team/SSTV 的旧 env 常量和 `runtime_paths.h` 中不准确的 fsync 注释被清掉。

6. 修复并纳入 path traversal tests。

验收：absolute path、`..`、跨 root 删除都被拒绝。

7. 建立 capability truth table 并更新 UI status。

验收：`capability_status.h` 出现在 contract inventory；LoRa/Walkie/SSTV 等 public header 继续暴露 `capability_status()`；至少一个 UI 状态入口消费该 API；synthetic 功能不再被展示为真实支持。

8. 让 runtime mode 真正控制 facade composition。

验收：`TRAIL_MATE_RUNTIME_MODE=demo/local/mesh` 能改变 demo seed、loopback mesh、dummy crypto、真实设备能力呈现；默认真机不启用 demo world，也不默认组合 dummy crypto 和 loopback pairing。

9. 验证 simulator runtime mode 默认值写入语义。

验收：POSIX 和 Windows 都只在 `TRAIL_MATE_RUNTIME_MODE` 未设置时写入 `demo`；用户显式设置 `local` 或 `mesh` 时不会被 simulator main 覆盖。

### 8.2 第一轮真实设备闭环

1. 把 `apps/linux_rpi/main/src/main.cpp` 从 bring-up UI 改为 shared shell。
2. 保留 `LV_LINUX_FBDEV_DEVICE` 和 `/proc/fb` auto-detect。
3. 保留 `LV_LINUX_KEYBOARD_DEVICE`，补更稳健的 input detection。
4. 记录启动日志：display path、input path、settings root、sd root、capability mode。
5. 在真机上验证 boot/menu/settings。

验收：

- SDK path 构建成功。
- 真机显示 shared shell。
- 真机可按键操作。
- Settings 持久化。

### 8.3 第一轮功能迁移

优先顺序：

1. Settings
2. Contacts/Chat local store
3. GPS page with NMEA source status
4. Tracker file workflow
5. Hostlink page
6. Team local runtime cleanup
7. Map tile/file storage

暂缓：

- real LoRa
- real walkie/audio
- Wi-Fi provisioning
- USB mass storage
- firmware update
- BLE phone integration

### 8.4 CI 和验证增强

新增建议：

- Linux CMake source helper smoke。
- path safety test。
- NMEA parser unit tests。
- hostlink start/stop/restart test。
- capability status test。
- optional Docker image build check。
- optional SDK build job，需要能缓存或固定 SDK 获取方式。

当前已有命令：

```bash
python3 scripts/check_platform_ui_boundaries.py

cd builds/linux_cmake
cmake --preset linux-simulator-debug
cmake --build --preset linux-simulator-debug-build
ctest --preset linux-simulator-debug-test

cmake --preset linux-uconsole-debug
cmake --build --preset linux-uconsole-debug-build
ctest --preset linux-uconsole-debug-test

cmake --preset linux-uconsole-release
cmake --build --preset linux-uconsole-deb
```

Windows/WSL simulator:

```powershell
wsl.exe --exec bash -lc 'cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate/builds/linux_cmake && cmake --preset linux-simulator-debug && cmake --build --preset linux-simulator-debug-build && ctest --preset linux-simulator-debug-test'
```

Windows/WSL uConsole:

```powershell
wsl.exe --exec bash -lc 'cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate/builds/linux_cmake && cmake --preset linux-uconsole-debug && cmake --build --preset linux-uconsole-debug-build && ctest --preset linux-uconsole-debug-test'
```

SDK device:

当前仓库没有活跃的 Cardputer Zero Linux CI 入口。该目标需要补齐独立 device shell、SDK build 入口和硬件验证 gate 后，再作为专门命令列入这里。

## 9. 代码审查清单

每个 Linux 适配 PR 都应该检查：

- 是否新增了 `modules/core_* -> platform/*` 依赖。
- 是否新增了 `modules/ui_shared -> platform/linux/*` 依赖。
- 是否把 simulator-only 代码放进了 `platform/linux/common`。
- 是否把 Pi-specific 代码放进了 `platform/linux/common`。
- 是否新增了无解释的 `#ifdef __linux__`。
- 是否新增了可被路径穿越影响的文件操作。
- 是否把 synthetic runtime 标成 supported。
- 是否让 app shell 长出业务逻辑。
- 是否更新了 CMake shared source helper。
- 是否有至少 smoke 或 unit test。
- 是否更新了能力状态和文档。

## 10. Definition of Done

Linux 适配不能只以“能编译”作为完成标准。

一个 Linux slice 完成，至少满足：

- 合约位置正确。
- 实现位置正确。
- simulator 能跑。
- rpi CMake target 不退化。
- 如涉及真机，SDK path 有验证路径。
- 能力状态诚实。
- synthetic/demo 行为不会默认污染真实设备。
- 文件路径安全。
- 生命周期可停止、可重启、无明显泄露。
- CI 或手动验证命令明确。
- 文档同步。

整个 Cardputer Zero Linux 适配真正可称为完成时，应满足：

- `apps/linux_sim` 是稳定开发和验证工具。
- `apps/linux_rpi` SDK path 是真实设备主路径。
- `platform/linux/common` 不含 simulator/device 特例。
- `platform/linux/rpi` 只含真机特有适配。
- shared shell 在 simulator 和真机上是同一套。
- Settings、Chat/Contacts、GPS、Tracker、Hostlink 至少有真实 Linux runtime。
- LoRa/Walkie/SSTV/Wi-Fi/USB/FOTA 等能力要么真实可用，要么诚实 unsupported/simulated。
- `modules/core_*` 与 `modules/ui_shared` 边界由自动检查守住。
- 新增 Linux 设备族不需要复制现有 app shell。

## 11. 推荐路线图

### Milestone A：结构收口

目标：减少重复和过期文档。

- 抽 CMake source helper。
- 更新 app-local specs。
- 建立 path/capability helper。
- 增加 path safety tests。

### Milestone B：真机 shell 收口

目标：让 SDK path 跑 Trail Mate shared shell。

- 拆 ShellSession。
- SDK main 接 shared shell。
- evdev input 可用。
- 启动日志清晰。

### Milestone C：Settings 验证 slice

目标：验证最小真实功能闭环。

- Settings 页面在 simulator/device 都可用。
- settings persistence 真正落盘。
- capability status 在 UI 中可信。

### Milestone D：数据和通信基础

目标：让 Linux 设备不只是 UI demo。

- Chat/contact/node store 整理。
- Hostlink 安全默认值。
- GPS NMEA serial 真机验证。
- Tracker/route 文件工作流。

### Milestone E：硬件能力

目标：逐步接真实设备能力。

- Battery/power。
- Brightness/backlight。
- Audio/walkie。
- Radio/LoRa。
- Wi-Fi。
- USB/FOTA，如设备和产品形态需要。

### Milestone F：发布与维护

目标：从开发 shell 进入可用产品线。

- systemd service 或启动脚本。
- 配置目录和权限策略。
- 日志策略。
- release artifact。
- 设备回归手册。

## 12. 当前最终判断

当前 Linux 适配已经完成了最难的第一件事：把目标结构和边界意识立起来，并让 simulator 与 Linux common runtime 具备复用共享 UI 的基础。根据 2026-05-08 最新回归，代码还进一步做了 CMake helper、LVGL session/host 分层、evdev 输入、runtime path/env、capability status 上移与部分 public API、demo world 抽取、SDK bring-up 强化等正确方向的结构推进。

但这轮推进还没有到“适配完成”。下一阶段的关键不是继续堆更多模拟功能，而是先把这批结构改动收口成可构建、可测试、可运行的事实：

- 先修 compile gate，让 sim/rpi/test 都重新绿。
- 再验证 LVGL input callback，让按键事件语义可靠。
- 再收口真机输入/display 主路径，特别是 rpi CMake、SDK main、evdev 真机验证。
- 再收口 path/capability/runtime 配置，让 pack repository 路径不再游离，让 capability contract inventory 和 UI 呈现跟上。
- 再收口 demo 与真实设备 facade 的边界，让真实设备默认不继承模拟世界。

如果按这个顺序推进，Linux 线会自然长成 Trail Mate 的一等平台，而不是新的平行项目。
