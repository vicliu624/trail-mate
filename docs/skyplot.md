你是一个资深嵌入式 GUI 工程师，请用 LVGL 8.x（C/C++）实现一个“GNSS Sky Plot（卫星天空图）”页面。
屏幕分辨率：横屏 480(w) × 222(h)。
要求：严格按下面给出的像素布局绘制；不要自作主张改布局；不要改元素文案；颜色体系必须围绕主色 0xEBA341，并与暖色工程风格一致；所有控件必须可复用、可刷新数据。

============================================================
1) 页面总体布局（像素级）
============================================================
- 页面根容器 root：尺寸 480×222，背景色为浅暖米色（见样式 token）。
- 分三块：
  A. 顶部 TopBar：高度 30px，占满宽 480
  B. 内容区 Content：高度 222-30=192px，y=30
     - 左侧 SkyPlotPanel：x=8, y=38, w=277, h=176
     - 右侧 StatusPanel：x=293, y=38, w=179, h=176
  注：Content 区顶部留 8px 间距，因此两个 panel 的 y=30+8=38；左右 panel 之间留 8px 间距。

- 全局圆角：8px
- 全局描边：2px（主色强调边）

============================================================
2) 样式与颜色 token（必须使用，统一风格）
============================================================
主色（Amber）：      #EBA341  (0xEBA341)
主色深（AmberDark）：#C98118
背景（WarmBG）：     #F6E6C6
面板底（PanelBG）：  #FAF0D8
分隔线（Line）：     #E7C98F
文字主（Text）：     #6B4A1E
文字弱（TextDim）：  #8A6A3A
警告（Warn）：       #B94A2C
成功（Ok）：         #3E7D3E
信息蓝（Info）：     #2D6FB6

星座颜色（用于 SYS/图例）：
GPS：  #E3B11F
GLN：  #2D6FB6
GAL：  #3E7D3E
BD：   #B94A2C

SNR 状态颜色（用于点的外环/填充）：
SNR_GOOD：#3E7D3E
SNR_FAIR：#8FBF4D
SNR_WEAK：#C18B2C
NOT_USED：#B94A2C
IN_VIEW： #6E6E6E (灰)

字体：
- 标题/栏标题：20px（或 LVGL 内置近似字号）
- 表头：14px
- 列表内容：16px
- 底部 summary：14px

============================================================
3) TopBar（高度30）
============================================================
TopBar 容器：x=0,y=0,w=480,h=30，背景 WarmBG，底部分隔线 2px Line。
左侧标题：
- label_title：改为 Summary 文本： "USE: 7/18|HDOP: 1.6|FIX: 3D"
- USE/HDOP/FIX 三段使用不同颜色，整体与主题协调

右侧电量显示（仅占位，不实现电量算法）：
- battery_icon：x=410,y=6,w=26,h=14，描边 2px TextDim，填充透明，右侧小凸点 3×6。
- label_batt：x=442,y=5，文本 "100%"，色 TextDim，字号 18。

============================================================
4) 左侧 SkyPlotPanel（x=8,y=38,w=292,h=176）
============================================================
面板容器：
- panel_sky：圆角 10，背景 PanelBG，描边 2px AmberDark。

内部绘制一个“天空圆图”：
- 圆图外接正方形区域 sky_area：x=10,y=2,w=170,h=170（相对于 panel_sky）
  => 圆心 C = (10+85, 2+85) = (95,87) 相对 panel_sky 内部坐标
  => 半径 R = 82（留出描边与文字空间）

绘制内容（使用 lv_canvas 或自绘对象都可以，但必须呈现）：
a) 外圆（地平线 0°）：圆环描边 Line 2px
b) 3 条同心圆（30°/60°/90°）：
   - r=R*2/3（约55）
   - r=R*1/3（约27）
   - r=0（中心点）
   圆线使用 Line 1px 虚线效果（如做不到虚线，用细实线也可以）
c) 十字方位线：N-S 与 E-W 两条线穿过圆心，线色 Line 1px
d) 方位文字：
   - "N" 放在圆上方：中心对齐于圆心x，y=2-2（贴近圆外）
   - "E" 放右侧：x=10+170+8,y=95-10
   - "W" 放左侧：x=2,y=95-10（在圆左边空白处）
   文字色 Text，字号 18
e) 仰角刻度文字：
   - 将刻度改为“指向 10 点半方向”：在圆心到 10:30 方向的斜线处依次标 "90°"（靠近外圈）、"60°"（r=55处）、"30°"（r=27处）
   - 文本沿该斜线排列，整体视觉连线指向 10 点半方向
   文字色 TextDim，字号 16
f) 圆心附近标注：
   - label_horizon：放在圆心略下：文本 "0° Horizon"，色 TextDim，字号 12

卫星点绘制（动态）：
- 每颗卫星用一个小圆点对象 sat_dot（建议 lv_obj + 圆角=半径）
- 点半径：10px（直径20）
- 点内显示卫星 ID（两位或三位数字），文字白色或深棕（根据底色自动取对比度，优先白）
- 点的位置由 (azimuth, elevation) 映射到圆内：
   r = R * (1 - elevation/90)
   x = Cx + r * sin(azimuth)
   y = Cy - r * cos(azimuth)
  (azimuth: 0°=N, 90°=E)
- 点填充颜色：按星座（GPS/GLN/GAL/BD）
- 点外环/小标：按 SNR 状态（GOOD/FAIR/WEAK/NOT_USED/IN_VIEW）：
  - GOOD/FAIR/WEAK：用对应颜色做 2px 外环
  - NOT_USED：外环 Warn
  - IN_VIEW：外环 灰
- 若该卫星 used_in_fix=true，在点旁边增加小标签 "USE"：
  - use_tag：圆角 6，背景 Ok，文字白色，字号 12
  - 位置：贴点左下角偏移（dx=-12, dy=12），避免遮挡数字

图例（在 SkyPlotPanel 内右下角区域）：
- legend_sys（星座图例）放在圆图右侧、靠下：
  - 小色块 10×10 + 文本（GPS/GLONASS/Galileo/BeiDou）
  - 位置：x=190,y=105 起，行高 15
- legend_snr（SNR 图例）放在 legend_sys 上方、与其左对齐（SkyPlotPanel 右上角区域）：
  - 文本顺序： "SNR Good" "SNR Weak" "Not Used" "In View"
  - 用小圆点示意（直径10）+ 文本
  - 位置：x=190 起，纵向排布（行高 15），整体位于 legend_sys 上方；与 legend_sys 间距 6px；整体上移 30px；小圆点与文字排布规则同 legend_sys（色块 y+4，文字 x+14）
  - 文本色 TextDim，字号 12

============================================================
5) 右侧 StatusPanel（x=293,y=38,w=179,h=176）
============================================================
面板容器：
- panel_status：圆角 10，背景 PanelBG，描边 2px AmberDark

标题条（高度 26）：
- header：x=0,y=0,w=179,h=26，背景 Amber，圆角上半部保持
- label：居中 "SATELLITE STATUS"，文字色 #2A1A05，字号 14 加粗

表头行（高度 22，y=26）：
- 背景：#F2D9A5
- 列：ID / SYS / ELEV / SNR / USE
- 列宽（像素）：ID=24, SYS=38, ELEV=39, SNR=38, USED=39（ELEV=USED，SNR=SYS）
- 文本居中，色 TextDim，字号 12

数据列表区（y=48 到 y=176，高度 128）：
- 以“固定行高”方式渲染，行高 17px，可显示 7 行（剩余滚动/分页不实现也可，但必须结构预留）
- 每行 5 列对齐表头
- 行底部分隔线 1px Line
- SYS 文本颜色按星座色（GPS/GLN/GAL/BD）
- USED 列：若 used=true 显示 "YES" 颜色 Ok；否则 "NO" 颜色 Warn
- 其它列文字色 Text
- 当卫星数量超过可显示行数时，列表按以下优先级排序显示：USED=YES 优先，SNR 高优先，其次仰角高，再按 PRN/SVID 升序

底部 Summary 区移除，空间全部让给数据列表区

============================================================
6) 数据结构与刷新接口（必须提供）
============================================================
定义数据结构（示例）：
- struct SatInfo {
    int id;           // PRN/SVID
    enum Sys {GPS, GLN, GAL, BD} sys;
    float azimuth;    // 0..359 deg
    float elevation;  // 0..90 deg
    int snr;          // dB-Hz
    bool used;
    enum SNRState {GOOD, FAIR, WEAK, NOT_USED, IN_VIEW} snr_state;
  };

- struct GnssStatus {
    int sats_in_use;
    int sats_in_view;
    float hdop;
    enum Fix {NOFIX, FIX2D, FIX3D} fix;
  };

必须实现两个刷新函数（供上层调用）：
- void ui_gnss_skyplot_set_sats(const SatInfo* sats, int count);
  -> 更新圆图卫星点（创建/复用对象），更新右侧表格前 N 行
- void ui_gnss_skyplot_set_status(GnssStatus st);
  -> 更新底部 summary 文本与 FIX 颜色

============================================================
7) 交互（最小）
============================================================
- 左上角允许预留 Back 按钮位置（但本需求不必须实现）。
- 页面不需要触摸交互；仅需要刷新显示。
- Backspace 键行为：触发 TopBar 的 Back 按钮逻辑，与其它页面一致。

============================================================
8) 交付物要求
============================================================
- 提供一个函数 ui_gnss_skyplot_create(lv_obj_t* parent) 返回页面根对象。
- 所有对象指针保存在静态/结构体中，支持重复进入页面不泄漏。
- 不要使用外部图片资源；全部用 LVGL 绘制。
- 输出代码应可直接编译（伪代码不接受）。

按以上要求生成完整 LVGL 页面代码。
