你是资深嵌入式 GUI 工程师，请用 LVGL 9.x（C/C++）实现一个 SSTV 接收页面（仅接收端 UI）。
屏幕分辨率：横屏 480(w) x 222(h)。
TopBar 高度：30px（必须严格）。
目标：按以下像素布局复刻 UI；整体尽量简洁；不画外框、不加多余容器效果；图片展示区域最大化；右侧显示状态文字 + 模式 + 音频电平；底部显示一条简洁的接收进度条（横向）。

============================================================
1) 全局颜色与样式 token（固定，统一风格）
============================================================
主色（Amber）：        #EBA341  (0xEBA341)
主色深（AmberDark）：  #C98118
背景（WarmBG）：       #F6E6C6
面板浅底（PanelBG）：  #FAF0D8
边线（Line）：         #E7C98F
文字主（Text）：       #6B4A1E
文字弱（TextDim）：    #8A6A3A
成功（Ok）：           #3E7D3E
警告（Warn）：         #B94A2C
灰（Gray）：           #6E6E6E

字体建议：
- TopBar 标题：跟随系统主题默认字号
- 状态文本：14px
- 子状态/提示：14px
- 小标签：12~14px

圆角：统一 8px（图片框也用 8px），不需要复杂阴影/浮雕。

============================================================
2) 页面总体布局（像素级）
============================================================
root：480x222，背景 WarmBG。

分区：
A) TopBar：x=0, y=0, w=480, h=30
B) Main：x=0, y=30, w=480, h=192

Main 内部布局：
- 左侧：ImageArea（3:2 容器）用于显示接收的图片
- 右侧：InfoArea（文字信息 + 音频电平）
- 底部：ProgressBar 横向进度条（位于右侧 InfoArea 底部，不覆盖图片）

(1) 图片容器（严格 3:2）
- img_box：x=8, y=0, w=288, h=192
  说明：img_box 高度必须为 192（Main 全高），为满足 3:2，宽=192*1.5=288。
  这样左侧图片最大化且比例正确。
- img_box 样式：背景 PanelBG；边框 2px Line；圆角 8px；无额外内框。
- 图片对象 img：
  - 放在 img_box 内部，内容按“等比居中适配”：
    - 若收到的 SSTV 图为 3:2，直接填满；
    - 若不是，保持等比，留空用 PanelBG。

(2) 右侧信息区
- info_area：x=304, y=0, w=168, h=192
  （因为 8px 左边距 + 288 图片 + 8px 间距 => 304 起）
  宽度 480 - 304 - 8(右边距) = 168

(3) 进度条位置
- prog_bg：x=304, y=208, w=168, h=8（绝对坐标）
  （等价于 Main 内 y=178）

============================================================
3) TopBar（30px，简洁）
============================================================
TopBar 容器：与系统现有 UI 一致（复用 top_bar 组件）
元素：
- btn_back：x=8, y=4, w=22, h=22，显示返回图标，颜色 TextDim
- title：居中对齐，文本 "SSTV RECEIVER"，颜色 Text，字号 20
- battery：右侧 x=420, y=6 显示电池图标（占位）+ 文本 "100%"（x=446, y=5），颜色 TextDim

============================================================
4) 右侧信息区（info_area）
============================================================
布局区域：info_area，位于图片右侧

(1) 状态/提示文本（label_state_sub）
- label_state_sub
  x=0, y=6, w=140（相对 info_area）
  文本示例：
    - "Listening for SSTV signal..."
    - "Decoding line: 120/240"
    - "Saved: /sstv/2026-02-09_001.bmp"
  字体：14，颜色 TextDim

(2) 解析指标（新增 3 行）
位置：label_state_sub 与 label_mode 之间，约 80px 高度、140px 宽度
- label_metric_sync：x=0, y=34, w=140，文本 "SYNC: LOCK" / "SYNC: --"
- label_metric_slant：x=0, y=54, w=140，文本 "SLANT: --"
- label_metric_level：x=0, y=74, w=140，文本 "LEVEL: 42%"
字体：14，颜色 TextDim

(3) 模式显示
- label_mode
  x=0, y=106, w=140
  文本示例："MODE: Auto" / "MODE: Scottie 1" / "MODE: PD240"
  字体：14，颜色 TextDim

(4) 就绪/状态提示
- label_ready
  x=0, y=142, w=140
  文本示例：
    - WAITING： "SSTV RX READY"（Text）
    - RECEIVING： "RECEIVING"（Ok + Text）
    - COMPLETE： "COMPLETE"（Ok）
    - ERROR： "ERROR"（Warn）
  字体：14

============================================================
5) 音频电平（右侧竖直电平表，必须）
============================================================
位置：info_area 的最右侧贯穿中上区
- meter_box：x=136, y=34, w=32, h=120（相对 info_area）

样式：
- 竖向 12 段，每段高 8px，段间距 2px
- 底部段颜色 Ok（绿）
- 中部段颜色 #C18B2C（黄棕）
- 顶部段颜色 Warn（橙）
- 未点亮段：Line 或 Gray（偏暗）
- 外框：1~2px Line；背景透明或 PanelBG

实现：
- 12 个小矩形 lv_obj 组成
- 接口 ui_sstv_set_audio_level(float level_0_1)
  - level_0_1 映射点亮段数 n = round(level*12)
  - 从底部点亮 n 段

============================================================
6) 底部进度条（简洁）
============================================================
放在右侧 InfoArea 底部，不覆盖图片内容：
- prog_bg：x=304, y=208, w=168, h=8（绝对坐标）
- 进度条背景：Line
- 进度条填充：Amber
- 圆角：2~4px
- 接口 ui_sstv_set_progress(float p_0_1)

============================================================
7) 状态机（至少支持 WAITING / RECEIVING / COMPLETE / ERROR）
============================================================
WAITING：
- img_box 显示占位
- label_state_sub = "Listening for SSTV signal..."
- label_ready = "SSTV RX READY"（Text）
- progress = 0

RECEIVING：
- img 逐行刷新
- label_state_sub = "Decoding line: x/xxx"
- label_ready = "RECEIVING"（Ok）
- progress 跟随解码进度

COMPLETE：
- img 显示最终图
- label_state_sub = "Image received" 或 "Saved: ..."
- label_ready = "COMPLETE"（Ok）
- progress = 1.0

ERROR：
- label_state_sub = 错误描述
- label_ready = "ERROR"（Warn）

必要接口：
- void ui_sstv_set_state(enum State s)
- void ui_sstv_set_mode(const char* mode_str)
- void ui_sstv_set_audio_level(float level_0_1)
- void ui_sstv_set_progress(float p_0_1)
- void ui_sstv_set_image(const void* img_src_or_lv_img_dsc)

============================================================
8) 交付要求
============================================================
- 提供 ui_sstv_create(lv_obj_t* parent) 返回 root 页面对象
- 不使用任何外部 PNG/图标资源（返回箭头、电池可用字符或简单矢量绘制）
- 严格 480x222，TopBar=30，img_box=288x192，3:2
- UI 必须足够简洁：禁止额外卡片、禁止厚重边框/阴影、禁止多余按钮（Mode/Clear/Save 不要）
- 代码可编译，不要伪代码

============================================================
9) Scottie Modes (Protocol Notes)
============================================================
VIS codes:
- Scottie 1: 60 (decimal)
- Scottie 2: 56 (decimal)
- Scottie DX: 76 (decimal)

Color scan order: Green, Blue, Red (RGB)
Luminance range: 1500-2300 Hz
Lines: 256 (Scottie 1/2)
Normal display: 320x256 (includes 16-line header)

Scottie 1 timing sequence:
- Starting sync (first line only): 9.0 ms @ 1200 Hz
- Separator / porch: 1.5 ms @ 1500 Hz
- Green scan: 138.240 ms
- Separator / porch: 1.5 ms @ 1500 Hz
- Blue scan: 138.240 ms
- Sync pulse: 9.0 ms @ 1200 Hz (between Blue and Red)
- Sync porch: 1.5 ms @ 1500 Hz
- Red scan: 138.240 ms

After the first line, repeat from the separator before Green (no starting sync).
Note: Scottie sync is mid-line (between Blue and Red), not at line start.
Scottie 2 timing difference:
- Green/Blue/Red scan: 88.064 ms (0.2752 ms/pixel @ 320 px)
Scottie DX timing difference:
- Green/Blue/Red scan: 345.6 ms (1.0800 ms/pixel @ 320 px)
Other timing (sync/porch) is the same as Scottie 1.

============================================================
10) VIS Header (JL Barber proposal)
============================================================
Source: "Proposal for SSTV Mode Specifications" (Dayton SSTV forum, 20 May 2000).
This description is useful for VIS decode alignment and parity checks.

VIS / calibration header sequence:
- 300 ms @ 1900 Hz (leader)
- 10 ms @ 1200 Hz (break)
- 300 ms @ 1900 Hz (leader)
- 30 ms @ 1200 Hz (VIS start bit)
- 7 data bits, LSB first, 30 ms each
  - 1100 Hz = "1"
  - 1300 Hz = "0"
- 30 ms parity bit
  - even parity = 1300 Hz
  - odd parity = 1100 Hz
- 30 ms @ 1200 Hz (VIS stop bit)

Note: mode-specific timing begins immediately after the VIS stop bit.

============================================================
11) Robot 72 Color (Mode Notes)
============================================================
VIS code: 12 (decimal)
Color mode: Y, R-Y, B-Y
Scan sequence: Y, R-Y, B-Y
Lines: 240

Timing sequence (one line):
- Sync pulse: 9.0 ms @ 1200 Hz
- Sync porch: 3.0 ms @ 1500 Hz
- Y scan: 138 ms
- Separator pulse: 4.5 ms @ 1500 Hz
- Porch: 1.5 ms @ 1900 Hz
- R-Y scan: 69 ms
- Separator pulse: 4.5 ms @ 2300 Hz
- Porch: 1.5 ms @ 1500 Hz
- B-Y scan: 69 ms

Repeat the above sequence for 240 lines.

============================================================
12) Robot 36 Color (Mode Notes)
============================================================
VIS code: 8 (decimal)
Color mode: Y, R-Y, B-Y
Scan sequence: Y, R-Y (even lines), Y, B-Y (odd lines)
Lines: 240

Timing sequence (two lines shown):
Even line:
- Sync pulse: 9.0 ms @ 1200 Hz
- Sync porch: 3.0 ms @ 1500 Hz
- Y scan: 88.0 ms
- "Even" separator pulse: 4.5 ms @ 1500 Hz
- Porch: 1.5 ms @ 1900 Hz
- R-Y scan: 44.0 ms

Odd line:
- Sync pulse: 9.0 ms @ 1200 Hz
- Sync porch: 3.0 ms @ 1500 Hz
- Y scan: 88.0 ms
- "Odd" separator pulse: 4.5 ms @ 2300 Hz
- Porch: 1.5 ms @ 1900 Hz
- B-Y scan: 44.0 ms

Repeat the above sequence for 240 lines.
Note: R-Y is transmitted on even lines, B-Y on odd lines.

============================================================
13) Martin Modes (Mode Notes)
============================================================
VIS codes:
- Martin 1: 44 (decimal)
- Martin 2: 40 (decimal)

Color mode: RGB (1500-2300 Hz)
Scan sequence: Green, Blue, Red
Lines: 256

Color scan time:
- Martin 1: 146.432 ms (0.4576 ms/pixel @ 320 px)
- Martin 2: 73.216 ms (0.2288 ms/pixel @ 320 px)

Timing sequence (per line):
- Sync pulse: 4.862 ms @ 1200 Hz
- Sync porch: 0.572 ms @ 1500 Hz
- Green scan
- Separator pulse: 0.572 ms @ 1500 Hz
- Blue scan
- Separator pulse: 0.572 ms @ 1500 Hz
- Red scan
- Separator pulse: 0.572 ms @ 1500 Hz

Repeat the above sequence for 256 lines.

============================================================
14) PD Modes (Mode Notes)
============================================================
VIS codes:
- PD50: 93 (decimal)
- PD90: 99 (decimal)
- PD120: 95 (decimal)
- PD160: 98 (decimal)
- PD180: 96 (decimal)
- PD240: 97 (decimal)
- PD290: 94 (decimal)

Color mode: Y, R-Y, B-Y
Scan sequence: Y (odd line), R-Y (avg 2 lines), B-Y (avg 2 lines), Y (even line)
Sync pulse: 20.0 ms @ 1200 Hz
Porch: 2.080 ms @ 1500 Hz

Color scan times (Y, R-Y, B-Y):
- PD50: 91.520 ms
- PD90: 170.240 ms
- PD120: 121.600 ms
- PD160: 195.584 ms
- PD180: 183.040 ms
- PD240: 244.480 ms
- PD290: 228.800 ms

Nominal resolutions (all include 16-line header):
- PD50: 320x256
- PD90: 320x256
- PD120: 640x496
- PD160: 512x400
- PD180: 640x496
- PD240: 640x496
- PD290: 800x616

Note: This receiver decodes to 320px width and scales vertical lines to the display height.

============================================================
15) Pasokon "P" Modes (Mode Notes)
============================================================
VIS codes:
- P3: 113 (decimal)
- P5: 114 (decimal)
- P7: 115 (decimal)

Color mode: RGB (1500-2300 Hz)
Scan sequence: Red, Green, Blue
Lines: 496 (includes 16-line header)

Color scan times:
- P3: 133.333 ms
- P5: 200.000 ms
- P7: 266.666 ms

Sync/porch periods:
- P3: sync 5.208 ms, porch 1.042 ms
- P5: sync 7.813 ms, porch 1.563 ms
- P7: sync 10.417 ms, porch 2.083 ms

Timing sequence (per line):
- Sync pulse
- Porch
- Red scan
- Porch
- Green scan
- Porch
- Blue scan
- Porch

Repeat the above sequence for 496 lines.
Note: This receiver decodes to 320px width and scales vertical lines to the display height.
