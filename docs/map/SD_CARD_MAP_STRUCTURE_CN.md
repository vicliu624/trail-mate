# SD 卡地图数据目录规范（Trail-Mate）

本文档说明 Trail-Mate 固件在 GPS 地图页面读取 SD 卡地图数据时，要求的目录结构、文件命名与后缀规则。

## 1. 结论速览

- 当前实现使用目录瓦片（`z/x/y`）方式，不支持 `mbtiles` 单文件数据库。
- 地图瓦片根目录为：`/maps`
- 底图目录为：`/maps/base/{source}/{z}/{x}/{y}.{ext}`
- 等高线目录为：`/maps/contour/{profile}/{z}/{x}/{y}.png`
- 支持缩放级别：`z=0..18`

## 2. 必需目录（底图）

至少需要准备一种底图源（`osm`、`terrain`、`satellite` 三选一或多选）。

```text
/
└── maps
    └── base
        ├── osm
        │   └── {z}/{x}/{y}.png
        ├── terrain
        │   └── {z}/{x}/{y}.png
        └── satellite
            └── {z}/{x}/{y}.jpg
```

### 底图源与文件后缀

- `osm` -> `.png`
- `terrain` -> `.png`
- `satellite` -> `.jpg`

注意：后缀必须匹配，`satellite` 目录下放 `.png` 会导致读取失败。

## 3. 可选目录（等高线叠加）

当设置中开启 `Contour Overlay` 时，固件会按缩放级别读取不同 profile 的等高线瓦片。

```text
/
└── maps
    └── contour
        ├── major-500
        │   └── {z}/{x}/{y}.png
        ├── major-200
        │   └── {z}/{x}/{y}.png
        ├── major-100
        │   └── {z}/{x}/{y}.png
        ├── major-50
        │   └── {z}/{x}/{y}.png
        └── major-25
            └── {z}/{x}/{y}.png
```

### 等高线 profile 与缩放映射

- `z <= 7`：不绘制等高线
- `z = 8`：`major-500`
- `z = 9`：`major-200`
- `z = 10`：`major-500`
- `z = 11`：`major-200`
- `z = 12..14`：`major-100`
- `z = 15..16`：`major-50`
- `z >= 17`：`major-25`

## 4. 与地图页相关的其他目录（可选）

这些不是“底图必需项”，但属于 GPS/地图功能常见输入目录。

```text
/
├── routes
│   └── *.kml
└── trackers
    └── *.gpx / *.csv / *.bin
```

- `/routes/*.kml`：用于 Route 模式加载路线。
- `/trackers/*`：用于轨迹文件读取与展示（Track Overlay）。

## 5. 命名与路径注意事项

- 路径区分目录层级，不自动纠错。必须是标准 `z/x/y` 层级。
- `z/x/y` 都使用十进制目录与文件名，不要加前缀或后缀。
- 底图不存在时，界面会提示对应图层缺失（如 `Map - OSM Missing`）。
- 仅放一个空目录不会显示地图，必须有对应瓦片文件。

## 6. 最小可用示例

如果你只想先验证链路，可先放少量瓦片（例如 `z=12` 的局部区域）：

```text
/
└── maps
    └── base
        └── osm
            └── 12
                └── 3340
                    ├── 1788.png
                    ├── 1789.png
                    └── 1790.png
```

进入 GPS 地图页后，切换底图源为 `OSM`，移动到对应区域即可验证加载。

## 7. 代码参考

- 地图源与后缀映射：`src/ui/widgets/map/map_tiles.cpp`
- 底图路径构造：`src/ui/widgets/map/map_tiles.cpp`
- 等高线路径构造与 profile 选择：`src/ui/widgets/map/map_tiles.cpp`
- 缩放范围：`src/ui/screens/gps/gps_constants.h`
- Route 列表目录：`src/ui/screens/tracker/tracker_page_components.cpp`
- Track 列表目录：`src/ui/screens/gps/gps_tracker_overlay.cpp`

