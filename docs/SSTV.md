你是资深嵌入式 GUI 工程师，请用 LVGL 9.x（C/C++）实现一个 SSTV 接收页面（仅接收端 UI）。
屏幕分辨率：横屏 480(w) × 222(h)。
TopBar 高度：30px（必须严格）。
目标：按以下像素布局复刻 UI；整体尽量简洁；不画外壳、不加复杂容器效果；图片展示区域最大化；右侧显示状态文字+模式+音柱；底部显示一条简洁的接收进度条（横向）。

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
- TopBar 标题：跟随系统/主题默认字号
- 主状态大字：14px
- 次状态：14px
- 小标签：12~14px

圆角：统一 8px（图片框也用 8px），不要复杂阴影/浮雕。

============================================================
2) 页面总体布局（像素级）
============================================================
root：480×222，背景 WarmBG。

分区：
A) TopBar：x=0,y=0,w=480,h=30
B) Main：x=0,y=30,w=480,h=192

Main 内左右布局：
- 左侧：ImageArea（3:2容器）用于显示接收的图片
- 右侧：InfoArea（文字信息 + 音柱）
- 底部：ProgressBar 横向进度条（放在右侧 InfoArea 底部，宽度=InfoArea，不覆盖图片）

具体像素：
Main 内边距：8px

(1) 图片容器（严格 3:2）
- img_box：x=8, y=38, w=288, h=192
  说明：img_box 高度必须是 192（Main 全高），为满足 3:2，宽=192*1.5=288。
  这样左侧图片最大化且比例正确。
- img_box 样式：背景 PanelBG；边框 2px Line；圆角 8px；无额外内框。
- 图片对象 img：
  - 放在 img_box 内部，内容按“等比居中适配”：
    - 若收到的 SSTV 图为 3:2，直接填满；
    - 若不是，保持等比，留空用 PanelBG。

(2) 右侧信息区
InfoArea 范围：
- info_area：x=304, y=38, w=168, h=192
  （因为 8px 左边距 + 288 图 + 8px 间距 => 304 起）
  宽度 480 - 304 - 8(右边距) = 168

============================================================
3) TopBar（30px，简洁）
============================================================
TopBar 容器：与系统现有 UI 一致（使用共享 top_bar 组件）。
元素：
- btn_back：x=8,y=4,w=22,h=22，显示“←”或返回图标，颜色 TextDim
- title：居中对齐，文本 "SSTV RECEIVER"，颜色 Text，字号 20
- battery：右侧 x=420,y=6 显示电池图标（占位） + 文本 "100%"（x=446,y=5），颜色 TextDim

（TopBar 的位置与风格与您现有系统一致即可，但高度严格 30）

============================================================
4) ???????????????????
============================================================
? info_area ??????????????????????

(1) ?????????
- label_state_big?
  x=0, y=6, w=130??? info_area?
  ??????????
    - WAITING FOR SIGNAL...
    - RECEIVING...
    - COMPLETE
  ?? 14?? Text????????

(2) ????????????
- label_state_sub?
  x=0, y=34, w=130
  ???
    - "Listening for SSTV signal..."
    - "Decoding line: 120/240"???????????
    - "Saved: /sstv/2026-02-09_001.bmp"???????
  ?? 14?? TextDim????????

(3) ????????
- label_mode?
  x=0, y=106, w=100
  ???????"MODE: Scottie 1"?????
  ?? 14?? TextDim????????

(4) ?SSTV RX READY? ???????????
- label_ready?
  x=0, y=142, w=100
  ??????????
    - WAITING? "SSTV RX READY"?? Text?
    - RECEIVING? "RECEIVING"?? Ok ? Text?
    - COMPLETE? "COMPLETE"?? Ok?
  ?? 14?? Text?? Ok?????????????

============================================================
5) 音柱（右侧竖直电平表，必须）
============================================================
目的：让用户确认麦克风确实收到音频。

放置位置：info_area 的最右侧，贯穿中上区域：
- meter_box：x=136, y=34, w=32, h=120（相对 info_area）
  （右侧贴边但留 0~2px 内距都可）

音柱样式（分段，不用渐变也可以）：
- 竖向 12 段，每段高度 8px，段间距 2px
- 底部段颜色 Ok（绿）
- 中部段颜色 #C18B2C（黄褐）
- 顶部段颜色 Warn（橙红）
- 未点亮段：Line 或 Gray（很淡）
- 外框：1~2px Line；背景透明或 PanelBG

实现方式：
- 用 12 个小矩形 lv_obj 组成（最简单可靠）
- 提供接口 ui_sstv_set_audio_level(float level_0_1)：
  - level_0_1 映射点亮段数 n = round(level*12)
  - 从底部点亮 n 段

============================================================
6) 底部进度条（简洁、不要花）
============================================================
放在右侧 InfoArea 底部，不覆盖图片框内容：
为了不挤压图片，进度条放在 Main 的最底部“覆盖在背景上”，与图像框/信息区底对齐即可。

- prog_bg：x=304, y=30+192-14=208, w=168, h=8
  （注意 y 是绝对坐标：Main 底部上移 14px，再留 6px 下边距即可；与 info_area 底部对齐）
- 进度条背景：Line
- 进度条填充：Amber
- 圆角：4
- 接口 ui_sstv_set_progress(float p_0_1)

============================================================
7) 状态机（必须支持三态切换）
============================================================
状态枚举：
- WAITING
- RECEIVING
- COMPLETE

状态切换时 UI 行为：
WAITING：
- img_box 内显示“空白占位”（不画复杂提示，最多居中一个小字 "No image" 或完全空白）
- label_state_big = "WAITING FOR SIGNAL..."
- label_state_sub = "Listening for SSTV signal..."
(4) ?SSTV RX READY? ???????????
- label_ready?
  x=0, y=142, w=100
- progress = 0

RECEIVING：
- img 对象开始逐行刷新（你可先占位，不必实现解码，只留接口）
- label_state_big = "RECEIVING..."
- label_state_sub?
  x=0, y=34, w=130
- label_ready = "RECEIVING"
- progress 跟随解码进度

COMPLETE：
- img 显示最终图
- label_state_big = "COMPLETE"
- label_state_sub?
  x=0, y=34, w=130
- label_ready = "COMPLETE"
- progress = 1.0

必须提供接口：
- void ui_sstv_set_state(enum State s)
- void ui_sstv_set_mode(const char* mode_str)
- void ui_sstv_set_audio_level(float level_0_1)
- void ui_sstv_set_progress(float p_0_1)
- void ui_sstv_set_image(const void* img_src_or_lv_img_dsc)（占位即可）

============================================================
8) 交付要求
============================================================
- 提供 ui_sstv_create(lv_obj_t* parent) 返回 root 页面对象
- 不使用任何外部 PNG/图标资源（返回箭头、电池可用字符或简单矢量绘制）
- 严格 480×222，TopBar=30，img_box=288×192（3:2）
- UI 必须足够简洁：禁止额外卡片、禁止厚重边框/内框、禁止多余按钮（Mode/Clear/Save 不要）
- 代码可编译，不要伪代码

请按以上规格输出完整 LVGL 页面实现代码。
