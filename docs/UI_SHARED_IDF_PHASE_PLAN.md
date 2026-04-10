# UI Shared / IDF 迁移计划

这份文档用于说明：如何把 UI 逻辑从各个 `apps/*` 中抽离出来，沉淀到可复用的 shared 层，并同时兼容 PlatformIO 与 ESP-IDF 两套入口。

---

## 1. 目标

### 1.1 总目标

1. 把通用 UI 页面、shell、controller、runtime 接口集中到 `modules/ui_shared`
2. 让 `apps/esp_pio` 与 `apps/esp_idf` 只保留各自平台入口、启动流程和适配器
3. 避免 shared UI 直接依赖 Arduino 或 ESP-IDF 具体实现细节

### 1.2 迁移原则

- 页面结构、交互和状态逻辑优先沉淀到 shared 层
- 平台差异收敛到 `platform/*` 和 app runtime adapter
- 同一个页面不要在 PIO / IDF 各维护一份实现
- 对暂时无法共享的能力，用 capability-gated fallback 占位

---

## 2. 分层边界

### 2.1 `modules/ui_shared`

承载：

- page shell
- 页面组件
- controller / presenter
- runtime 抽象接口
- 通用 fallback 页面

不应直接依赖：

- `<Arduino.h>`
- `<Preferences.h>`
- `nvs.h`
- 具体板级 API

### 2.2 `apps/esp_pio` / `apps/esp_idf`

承载：

- startup / boot
- menu / app catalog 入口
- loop 驱动
- 生命周期管理
- facade / runtime adapter 装配

### 2.3 `platform/*`

承载：

- 设备能力实现
- 屏幕 / 睡眠 / 音频 / GPS / 存储等平台 API
- Arduino 与 IDF 各自的 contract 实现

---

## 3. 当前状态（截至 2026-03-11）

### 3.1 已经共享的页面骨架

已经迁到 shared `shell + runtime/components/controller` 的页面：

- `Settings`
- `Chat`
- `GPS / Map`
- `GNSS Sky Plot`
- `Contacts`
- `Team`
- `Tracker`
- `PC Link`
- `USB`
- `SSTV`
- `Energy Sweep`
- `Walkie Talkie`

### 3.2 apps 侧现状

`apps/esp_pio/src` 和 `apps/esp_idf/src` 已经逐步收敛到以下职责：

- `startup_runtime.cpp`
- `loop_runtime.cpp`
- `app_runtime_access.cpp`
- `app_registry.cpp`

其中 `esp_idf` 还包含：

- `runtime_config.cpp`
- `app_facade_runtime.cpp`
- `idf_entry.cpp`
- `idf_component_anchor.cpp`

### 3.3 已完成的清理

- 一批旧的 IDF retired stub 已移除
- `modules/ui_shared` 里的 `ui_common_stub.cpp` / `ui_status_stub.cpp` 已显著收缩
- `apps/esp_pio/src` 下保留的 `ui_*.cpp` 多数只剩 wrapper 职责

---

## 4. 分阶段计划

## 阶段 0：盘点与止血

### 目标

- 先明确哪些页面已经 shared，哪些还残留 app 私有实现
- 停止新增重复页面实现

### 交付

- 页面归属清单
- app 私有 wrapper 清单
- shared shell 缺口清单

---

## 阶段 1：apps 入口收敛

### 目标

把 `apps/esp_pio` 和 `apps/esp_idf` 收敛成“启动 + loop + runtime 接线”。

### 任务

1. 把 `app_catalog` / `menu` / `startup` / `loop` 对齐到 shared 模式
2. 清理 `apps/esp_pio` 中历史页面 wrapper / registry 特例
3. 收敛 `app_runtime_access` 的生命周期与运行时访问
4. 统一 `esp_pio` / `esp_idf` 的 startup / loop / event 驱动模式

### 完成标准

- 两端 app 目录不再承载具体 UI 逻辑
- app 入口能稳定驱动 shared app catalog / shared shell

---

## 阶段 2：页面共享完成

### 目标

把页面层真正统一到 shared：

- shell
- host
- fallback
- components / controller / runtime

### 任务

1. 补齐 shared `shell + components/runtime`
2. 移除 app 侧残留页面逻辑
3. 统一 app catalog 与 shared page shell 的接线
4. 对缺失能力使用 capability-gated fallback

### 完成标准

- 页面结构只在 `modules/ui_shared` 维护
- fallback 行为一致
- app 侧不再复制页面实现

---

## 阶段 3：平台能力抽象完成

### 目标

把 shared UI 依赖的设备能力全部收敛到 adapter contract。

### 需要抽象的能力

- restart
- kv / config persistence
- screen sleep
- tone / audio preview
- GPS runtime control
- tracker recording hook
- hostlink / USB capability hook
- walkie / sstv / lora support

### 落点

- Arduino 实现在 `platform/esp/arduino_common`
- IDF 实现在 `platform/esp/idf_common` 与 `apps/esp_idf/*runtime`

### 完成标准

- `modules/ui_shared` 不再直接包含 ESP 专有头文件
- shared 只依赖平台 contract

---

## 阶段 4：配置与 profile 收敛

### 目标

把设备 profile、视觉尺寸、能力差异统一到 runtime config / page profile。

### 重点

- `tab5`
- `tdeck`
- `pager`

### 要求

- topbar、高度、间距、图标卡片等由 shared profile 驱动
- 板级差异不散落在页面代码里

---

## 5. 风险点

- shared 页面已经完成结构迁移，但 runtime hook 仍可能带有平台耦合
- PIO / IDF 两套入口的生命周期节奏不完全一致，容易出现事件顺序差异
- fallback 如果设计过弱，短期内会掩盖真实缺口
- profile 还未彻底统一前，不同设备上可能继续出现布局分叉

---

## 6. 验收标准

- `modules/ui_shared` 成为页面 UI 的唯一事实源
- `apps/esp_pio` 与 `apps/esp_idf` 主要负责启动和装配
- 平台差异只出现在 `platform/*` 与 runtime adapter
- 新页面默认先落到 shared，而不是 app 私有目录
