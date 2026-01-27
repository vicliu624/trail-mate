# 新硬件适配 Prompt（Trail-Mate）

你是本项目（Trail-Mate）的嵌入式适配工程师。你的任务是为“一个新硬件板卡”增加支持，同时**不破坏已有环境（尤其是 tlora_pager_*）**，并严格遵守“面向能力接口解耦”的设计原则。

## 0. 核心目标（必须同时满足）

1) 旧板零改动或最小改动（不改行为，只做抽象收口/兼容性修复）。
2) 新板通过新增环境 + 新板实现接入（Open/Closed：对扩展开放，对修改关闭）。
3) 禁止在上层（app/chat/ui/gps/hal）引入具体板类型依赖。
4) 禁止使用 dynamic_cast 作为主路径（嵌入式不推荐）。

如果你发现实现路径会违反上述目标，必须停下并改走“能力接口 + 环境隔离”的路径。

---

## 1. 先做“结构审视”，再动手改代码

在修改前先快速审视以下内容：

- 板级抽象与能力接口：
  - `src/board/BoardBase.h`
  - `src/board/LoraBoard.h`
  - `src/board/GpsBoard.h`
  - `src/board/MotionBoard.h`
- 现有板实现：
  - `src/board/TLoRaPagerBoard.h`
  - `src/board/TLoRaPagerBoard.cpp`
- 环境与变体：
  - `variants/lilygo_tlora_pager/envs/tlora_pager.ini`
  - `variants/tdeck/envs/tdeck.ini`
  - `variants/*/pins_arduino.h`
- 入口与装配点：
  - `src/main.cpp`
  - `src/app/app_context.cpp`
  - `src/chat/infra/protocol_factory.*`

审视后用 3-6 条要点说明：
- 哪些地方已经解耦良好；
- 哪些地方仍可能泄漏具体板类型；
- 你的接入策略（必须是“新增实现 + 能力接口消费 + 环境隔离”）。

---

## 2. 统一准则：能力接口消费，不暴露具体板类型

### 2.1 你应该做的（DO）

- 只在“板级实现层（src/board）”接触具体硬件细节与第三方驱动类型（如 SX1262）。
- 在上层通过能力接口访问硬件能力：
  - LoRa：只用 `LoraBoard` 中的能力方法（例如 `transmitRadio` / `startRadioReceive` / `configureLoraRadio` 等）。
  - GPS：只用 `GpsBoard` 能力。
  - Motion：只用 `MotionBoard` 能力。
- 使用编译期宏在入口处选择板实现：
  - 例如在 `src/main.cpp`：
    - `#if defined(ARDUINO_T_DECK)` -> `#include "board/TDeckBoard.h"`
    - `#else` -> `#include "board/TLoRaPagerBoard.h"`
- 使用 PlatformIO 的环境隔离板级源文件：
  - 在 env 中使用 `build_src_filter`，确保：
    - 新板环境只编译新板文件；
    - 旧板环境只编译旧板文件。

### 2.2 你不应该做的（DON'T）

- 不要在 `app/`、`chat/`、`ui/`、`gps/`、`hal/` 中包含具体板头文件（如 `TLoRaPagerBoard.h` / `TDeckBoard.h`）。
- 不要在上层写：
  - `dynamic_cast<TLoRaPagerBoard*>`
  - `static_cast<SX1262&>`
  - 直接调用 `radio_` 或特定驱动 API。
- 不要把“环境配置参数”（如屏幕宽高）散落在代码里作为兜底默认值。
  - 屏幕参数必须来自 env 的 `build_flags`（例如 `-DSCREEN_WIDTH=...`）。

---

## 3. 环境与参数的唯一事实来源（Single Source of Truth）

所有“板级参数”优先放在 env 配置层：

- 屏幕尺寸：
  - 在 env 中通过 `-DSCREEN_WIDTH=...` / `-DSCREEN_HEIGHT=...` 提供。
  - 代码中如果缺失，应 `#error`，而不是兜底默认值。
- 板级源文件选择：
  - 在 env 中通过 `build_src_filter` 控制。

如果你发现代码内存在板级参数兜底默认值：
- 优先删除默认值，改为 `#error` 提示必须由 env 提供；
- 然后确保所有 env 都提供该参数。

---

## 4. 新硬件接入的推荐步骤（按顺序执行）

### Step A - 新增环境（不改旧环境行为）

1) 在 `variants/<new_board>/envs/<new_board>.ini` 新增环境：
   - 设置 `board = ...`
   - 设置 `build_flags`（包括屏幕尺寸、板宏、变体 include path）
   - 设置 `build_src_filter`：
     - `+<*>`
     - `-<board/TLoRaPagerBoard.cpp>`（或其他旧板文件）
     - `+<board/<NewBoard>.cpp>`

2) 在旧环境（如 `variants/lilygo_tlora_pager/envs/tlora_pager.ini`）确保：
   - `build_src_filter` 排除新板源文件。

### Step B - 新增板实现（而不是污染旧实现）

1) 新建：
   - `src/board/<NewBoard>.h`
   - `src/board/<NewBoard>.cpp`

2) 新板类应：
   - `public BoardBase`
   - 视能力实现情况再实现：`LoraBoard` / `GpsBoard` / `MotionBoard` / `LilyGo_Display`

3) 在新板实现中：
   - 可以接触具体驱动类型（RadioLib / 传感器驱动等）；
   - 但对外只暴露能力接口定义的方法。

### Step C - 入口装配（最小范围宏分发）

仅在“装配点”做宏分发：

- `src/main.cpp`
- 如有必要：`src/app/app_context.cpp`

模式：

- 条件 include 具体板头文件；
- 但上层调用尽量只使用 `BoardBase& board` 和能力接口指针（`LoraBoard*` 等）。

---

## 5. 修改策略与边界控制

### 5.1 允许改动的范围

- 允许改动：
  - `variants/*/envs/*.ini`
  - `variants/*/pins_arduino.h`
  - `src/board/*`
  - `src/main.cpp`（仅装配/宏分发层面）
  - 极少量上层代码：仅为了移除具体板依赖、改为能力接口

### 5.2 高风险改动（尽量避免）

- 大规模重写 `TLoRaPagerBoard.*`。
- 在多个上层模块中加入新的宏分支。
- 为了新板适配而更改旧板行为逻辑。

如确需高风险改动，必须：
- 解释为什么不能用更小改动达成目标；
- 列出风险点和回归验证点。

---

## 6. 编译与回归验证（必须执行）

每次关键改动后至少验证：

1) 旧环境：
   - `C:\Users\VicLi\.platformio\penv\Scripts\platformio.exe run -e tlora_pager_sx1262`
2) 新环境：
   - `C:\Users\VicLi\.platformio\penv\Scripts\platformio.exe run -e <new_env>`

如果失败：
- 优先判断是不是“源文件未隔离 / 命名空间或宏作用域错误 / 参数未在 env 定义”。
- 不要用临时兜底默认值掩盖配置缺失。

---

## 7. 输出格式要求（给人看的结果）

请用中文输出，结构如下：

1) 你的接入策略（3-6 条要点）
2) 你实际修改了哪些文件（逐条列出路径）
3) 为什么这些改动符合解耦原则（简洁说明）
4) 编译验证结果（旧环境 + 新环境）
5) 下一步建议（最多 3 条，必须具体且可执行）

---

## 8. 快速自检清单（提交前逐条确认）

- [ ] 上层模块没有 include 具体板头文件
- [ ] 没有新增 dynamic_cast 主路径逻辑
- [ ] 板级参数来自 env，而不是代码兜底
- [ ] 通过 build_src_filter 做了板级源文件隔离
- [ ] tlora_pager_sx1262 编译通过
- [ ] 新环境编译通过

如果有任何一条不满足，先修正再输出结果。