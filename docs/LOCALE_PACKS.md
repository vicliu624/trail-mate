# Locale、Font 与 IME Pack

本文档解释 pack 机制与打包细节。
整个本地化系统的规范性规格现在位于
[`docs/LOCALIZATION_SPEC.md`](./LOCALIZATION_SPEC.md)。
如果两份文档之间存在冲突，以 `LOCALIZATION_SPEC.md` 为准。

## 目标

pack 系统同时为了解决四个问题而存在：

1. 让固件镜像保持尽可能小。English 保持内建，大体积脚本资源移出固件镜像，进入外部 pack 存储。
2. 明确区分 `installed` 与 `loaded`。一个 pack 出现在 Flash 或 SD 上，并不意味着它的字体已经加载进 RAM。
3. 让混合语言内容正确渲染。即使 UI 是西班牙语，只要对应字体 pack 已安装，系统仍应能显示中文联系人名。
4. 给 RAM 使用量设定边界，适配差异很大的设备，包括无 PSRAM 目标。

这已经是一套一等公民的运行时架构，而不是若干基于文本的旁路拼接。

## 核心模型

Trail Mate 把本地化明确建模为三类 pack：

- `Locale Pack`
  拥有翻译后的 UI 字符串，并声明它依赖的 UI font pack、content font pack，以及可选的 IME pack。
- `Font Pack`
  拥有字形覆盖元数据以及外部 `font.bin` 资源。
- `IME Pack`
  声明脚本特定的输入行为，例如拼音。

Settings 页选择的是 `Locale Pack`，而不是直接选择字体或 IME。

## 内建与外部

固件有意只携带最小内建基线：

- 内建 locale pack：`en`
- 内建 font pack：`builtin-latin-ui`
- 内建 IME pack：无

因此，即使没有任何外部语言包存在，English 也始终可用。

运行时 payload 可以从外部 pack 根目录中被发现，例如 SD，或者设备上的 Flash 安装存储。
具体运行时根目录由当前固件实现定义；已安装布局保持为：

```text
/trailmate/packs/fonts/<font-pack-id>/manifest.ini
/trailmate/packs/locales/<locale-id>/manifest.ini
/trailmate/packs/ime/<ime-pack-id>/manifest.ini
```

启动时，registry 分两阶段建立：

1. 编目所有内建 pack 以及所有外部 pack manifest。
2. 解析依赖，并根据当前 memory profile 决定哪些 locale 被允许使用。

重要说明：发现 manifest 的成本很低。编目阶段不会把外部字体加载进 RAM。

## 三种布局

同一份本地化资源现在同时存在三种表示形式，刻意把它们分开是设计要求的一部分：

### 仓库源码 Bundle

Git 中 `packs/locale-bundles/<bundle-id>/` 下的内容。

- 包含运行时 manifest
- 包含面向人的 package 元数据
- 包含仅构建期使用的文件，例如 `charset.txt` 与 `build.ini`
- 可能不跟踪 `font.bin`，因为 Pages 构建可以重新生成它

### 已安装运行时布局

固件真正扫描的已安装运行时布局：

```text
/trailmate/packs/fonts/<font-pack-id>/...
/trailmate/packs/locales/<locale-id>/...
/trailmate/packs/ime/<ime-pack-id>/...
```

这是运行时 registry 唯一理解的布局。

### 分发包

网站以及未来 Extensions 页面应该分发和消费的对象：

- 每个可安装 bundle 一个 zip
- 一个带版本与兼容性元数据的 package manifest
- 一个用于 UI 展示的描述文件
- 一个用于发现与更新检查的远程 catalog 条目

运行时不会直接扫描 zip。package manager 层负责下载 zip，把其中 payload 解开到已安装运行时布局，然后要求运行时 registry 刷新。

## Installed 不等于 Loaded

这是整个设计的中心区分。

- `Installed`
  manifest 存在于 SD 上，registry 知道这个 pack 存在。
- `Loaded`
  外部 `font.bin` 已经真正传给 `lv_binfont_create()`，现在开始消耗运行时 RAM。

运行时的行为如下：

1. 从 `settings/display_locale` 解析当前活动 locale。
2. 当该 locale 被激活时，立即加载活动 UI font pack。
3. 活动 content font pack 采用惰性加载，只在 content-scope 文本真正需要时才加载。
4. 如果当前文本包含活动 content chain 尚未覆盖的 codepoint，则惰性加载额外的 content supplement pack。
5. 切换 locale 时，会卸载所有运行时已加载的外部字体，并从头重建整条链。

这意味着，一个设备可以安装很多 pack，但任意时刻真正驻留在 RAM 中的只会有一到两个。

## UI Scope 与 Content Scope

运行时有意维持两条不同的 fallback chain。

### UI Chain

用于静态应用 chrome：

- 菜单标签
- 设置页标签
- 标题
- 按钮
- 其他翻译后的界面文本

链路如下：

```text
screen-selected Latin base font -> active UI font pack
```

### Content Chain

用于用户生成或外部接收的文本：

- 聊天发送者行
- 聊天预览与正文
- 联系人名
- 队伍成员名
- 节点名与描述
- 选择器中显示的 locale 名称

链路如下：

```text
screen-selected Latin base font
-> active content font pack
-> active UI font pack
-> lazily loaded content supplement packs
```

这就是为什么在非中文 UI 下，只要安装了相应 pack，系统仍然能正确显示中文内容。

## Memory Profile

pack 的可用性由共享运行时代码中的 board-specific memory profile 约束。

当前 profile 如下：

- `constrained`
  locale 字体预算 `128 KiB`，禁用 content supplement，解码后的地图 cache 为 `2` 张 tile，页面退出后不保留 cache。
- `standard`
  locale 字体预算 `768 KiB`，content supplement 预算 `640 KiB`，最多 `1` 个 supplement pack，解码后的地图 cache 为 `4` 张 tile，页面退出后不保留 cache。
- `extended`
  locale 字体预算 `2 MiB`，content supplement 预算 `2 MiB`，最多 `3` 个 supplement pack，解码后的地图 cache 为 `12` 张 tile，页面退出后保留 cache。

当前板级映射：

- `extended`：`Tab5`、`T-Display P4`
- `standard`：`T-Deck`、`T-Deck Pro`
- `constrained`：其余所有设备，包括无 PSRAM 和 pager 级目标

locale 预算会基于当前活动 locale 的真实成本进行检查：

```text
unique(UI font pack, content font pack)
```

supplement 预算与之分离，只作用于后续为混合脚本内容额外拉入的 content pack。

## 持久化

活动 locale 存储为：

- namespace：`settings`
- key：`display_locale`

旧版 ESP 安装使用的整型 `display_language` key，会被一次性迁移到新的字符串 key。迁移完成后，旧 key 会被移除。

## Manifest Schema

manifest 使用普通的 `key=value` 文件格式。

### Font Pack Manifest

```ini
kind=font
id=zh-hans-core
display_name=Simplified Chinese Core
usage=both
estimated_ram_bytes=217456
source=binfont
file=font.bin
ranges=ranges.txt
```

字段说明：

- `id`
  稳定的 pack 标识符。
- `display_name`
  用于诊断的人类可读名称。
- `usage`
  取值为 `ui`、`content` 或 `both`。
- `estimated_ram_bytes`
  使用 LVGL binfont loader 加载后的预期运行时 RAM 成本。该值用于 profile 决策与 supplement 规划。
- `source`
  当前外部文件使用 `binfont`，编译进固件的字体别名使用 `builtin`。
- `file`
  `font.bin` 的相对路径，相对于该 pack 目录。
- `ranges`
  覆盖元数据文件的相对路径，相对于该 pack 目录。它用于 codepoint 规划，而不是直接用于渲染。

如果 `estimated_ram_bytes` 缺失或为 `0`，运行时会把该 pack 视为“成本未知”，因而无法准确做预算。仓库中的 pack 应始终提供此字段。

### IME Pack Manifest

```ini
kind=ime
id=zh-hans-pinyin
display_name=Pinyin
backend=builtin-pinyin
```

目前已出货的 backend 实现是：

- `builtin-pinyin`

backend 的真实实现位于固件代码中。IME pack manifest 是把它注册进运行时并暴露出来的那一层。

### Locale Pack Manifest

```ini
kind=locale
id=zh-Hans
display_name=Simplified Chinese
native_name=简体中文
ui_font_pack=zh-hans-cjk
content_font_pack=zh-hans-cjk
ime_pack=zh-hans-pinyin
strings=strings.tsv
```

一个带分层中文覆盖的示例：

```ini
kind=locale
id=zh-Hans
display_name=Simplified Chinese
native_name=简体中文
ui_font_pack=zh-hans-core
content_font_pack=zh-hans-core
preferred_content_supplement_packs=zh-hans-ext
ime_pack=zh-hans-pinyin
strings=strings.tsv
```

字段说明：

- `id`
  用于持久化和日志的 locale 标识符。
- `display_name`
  面向英文语境的名称。
- `native_name`
  在选择器中显示的本语言自称。
- `ui_font_pack`
  该 locale 用于界面 chrome 的 font pack。
- `content_font_pack`
  该 locale 在内容表面优先使用的 font pack。
- `preferred_content_supplement_packs`
  可选的逗号分隔 content supplement font pack 列表；当这个 locale 遇到缺字时，优先尝试它们。
- `ime_pack`
  可选 IME 依赖。
- `strings`
  locale TSV 文件的相对路径，相对于该 pack 目录。

如果省略 `content_font_pack`，默认回退为 `ui_font_pack`。
如果省略 `ui_font_pack`，默认回退为 `builtin-latin-ui`。

## String Table 格式

locale 字符串以 TSV 存储：

```text
English source string<TAB>Localized string
```

支持的转义包括：

- `\\n`
- `\\t`
- `\\r`
- `\\\\`

示例：

```text
Settings	Paramètres
Send this code and compare:\\n	Envoyez ce code et comparez:\\n
```

代码中的稳定查找 key 始终是 English 源字符串。

## 失败行为

如果某个 locale pack 无法被使用：

- 缺少依赖的 font pack：跳过该 locale
- 缺少依赖的 IME pack：跳过该 locale
- 活动 locale 的字体成本超出当前 memory profile：跳过该 locale
- 持久化的 locale id 不再能解析：运行时回退到 `en`

不会仅仅因为可移除 pack 当前缺席，就把持久化的 `display_locale` 值擦掉。如果该 pack 之后重新出现，这个 locale 会再次变得可选。

如果某个 content supplement 无法加载，UI 仍然要继续存活。只有那段具体文本可能显示为缺字，但活动 locale 不会因此改变。

## 仓库 Bundle

仓库当前提供的源码 bundle 位于：

- `packs/locale-bundles/europe-latin-ext`
- `packs/locale-bundles/zh-Hans`
- `packs/locale-bundles/zh-Hant`
- `packs/locale-bundles/ja`
- `packs/locale-bundles/ko`

每个源码 bundle 包含：

- `package.ini`
- `DESCRIPTION.txt`
- `README.md` 中的生成说明
- 位于 `fonts/`、`locales/` 与可选 `ime/` 下的运行时 payload 树
- 每个外部 font-pack 目录中的 `build.ini` 与 `charset.txt`
- 供运行时覆盖规划使用的 `ranges.txt`

`font.bin` 有意不作为 source-of-truth 布局的一部分。如果本地已经存在，就直接使用；如果缺失，Pages pack 构建会在生成 zip 之前，基于 bundle 内的 `charset.txt` 与 `build.ini` 元数据重新生成它。

## Package Manifest

每个源码 bundle 现在都带有 bundle-level 的 `package.ini`。

示例：

```ini
kind=package
id=zh-Hans
package_type=locale-bundle
version=1.0.0
display_name=Simplified Chinese
summary=Full Simplified Chinese locale bundle with external CJK font coverage and the built-in Pinyin IME backend.
description=DESCRIPTION.txt
readme=README.md
author=Trail Mate
homepage=https://vicliu624.github.io/trail-mate/
min_firmware_version=0.1.18-alpha
supported_memory_profiles=standard,extended
tags=language,cjk,chinese,ime
```

字段说明：

- `id`
  稳定 package 标识符，供远程 catalog 与未来的 installed-index 文件使用。
- `package_type`
  高层级 package 角色。当前仓库 bundle 使用 `locale-bundle`。
- `version`
  package 版本号，独立于固件 tag。
- `display_name`
  Extensions UI 中给用户看的 package 名称。
- `summary`
  用于列表视图的一行简述。
- `description`
  相对于 bundle 根目录的纯文本描述文件。
- `readme`
  相对于 bundle 根目录的面向开发者文档文件。
- `author`
  package 发布者字符串。
- `homepage`
  项目或 package 首页。
- `min_firmware_version`
  正确理解该 bundle 契约所要求的最小固件版本。
- `supported_memory_profiles`
  声明式兼容性提示，可取 `constrained`、`standard` 与/或 `extended`。
- `tags`
  供未来 Extensions 浏览器搜索与分组的元数据。

## Font 构建元数据

每个外部 font-pack 目录还可以声明一个仅供工具链使用的 `build.ini`。

示例：

```ini
font=tools/fonts/NotoSansCJKsc-Regular.otf
size=16
bpp=2
no_compress=true
```

字段说明：

- `font`
  仓库中的源字体文件。
- `size`
  传给 binfont generator 的像素尺寸。
- `bpp`
  生成字体的 bits-per-pixel。
- `no_compress`
  是否让生成的 `font.bin` 禁用 lv_font_conv 的 RLE 压缩。

这个文件只属于构建期元数据。运行时永远不会读取它。

## 分发 Archive 布局

GitHub Pages 构建现在会在以下路径下为每个 bundle 生成一个 zip：

```text
site/assets/packs/<package-id>-<version>.zip
```

每个 archive 包含：

```text
package.ini
DESCRIPTION.txt
README.md
payload/fonts/<font-pack-id>/...
payload/locales/<locale-id>/...
payload/ime/<ime-pack-id>/...
```

`payload/` 内只包含可安装的运行时文件。像 `charset.txt`、`build.ini` 和 `.gitignore` 这类仅构建期文件都会被排除在 archive 之外。

## 远程 Catalog

Pages 构建还会生成：

```text
site/data/packs.json
```

这个 catalog 是未来 Extensions UI 的发现面。每个条目包含：

- package id、version、display name、summary 和长描述
- `min_firmware_version`、supported memory profiles 之类的兼容性提示
- bundle 提供的 locale/font/IME 记录列表
- 用于下载与更新检查的 archive 路径、大小和 SHA-256
- 用于安装前规划的预计运行时字体 RAM 总量

这个 catalog 被有意设计在 runtime registry 之上。它描述的是“可下载 bundle”，而不是“当前已经加载的字体”。

## 已安装 Package 索引

runtime registry 仍然直接从 `/trailmate/packs` 对已解包资源进行编目。
对于 package 管理，对应的 installer 层还应额外维护：

```text
/trailmate/packs/.index/installed.json
```

该文件应记录：

- 已安装的 package id
- 已安装的 package version
- 安装时间
- 来源 archive 的 SHA-256

未来的更新提示应把这个 installed index 与 `site/data/packs.json` 对比，而不是靠目录名猜测。

## 当前 IME 覆盖

目前仓库 bundle 中，只有简体中文声明了 IME pack：

- `zh-Hans` -> `zh-hans-pinyin`

繁体中文、日文和韩文 bundle 目前都是 display-only。它们有意继续沿用现有 `EN` / `123` 输入路径，直到对应脚本的专用 IME pack 出现。

## 新增一种语言

1. 先决定这个 locale 能否复用已有 font pack，还是需要新建一个。
2. 新增源码 bundle：`packs/locale-bundles/<bundle-id>/`，并写好 `package.ini`、`DESCRIPTION.txt` 与 `README.md`。
3. 为该 bundle 中的每个外部 font pack 生成 `charset.txt` 与 `ranges.txt`。
4. 为每个生成型 font pack 写好 `build.ini`。
5. 写运行时 font manifest，包括 `usage`、`estimated_ram_bytes` 和 `ranges`。
6. 写 locale pack manifest，包括 `ui_font_pack` 与 `content_font_pack`。
7. 只有在该脚本确实需要额外输入行为时，才添加 `ime_pack`。
8. 运行 `python scripts/build_pack_repository.py --pack-root packs --site-root site`。
9. 可以手工把 runtime payload 拷贝到 SD 安装，也可以通过 Pages 产出的 zip 提供给未来的 Extensions installer。
10. 重启后，在 Settings 中选择该 locale。

## 设计规则

这套架构有意避免以下做法：

- 使用整数语言枚举
- 开机时急切加载所有已安装字体
- 用一个全局的“只要非 ASCII 就切换字体”快捷规则
- 把 UI chrome 文本与用户内容文本混为一谈
- 让不同平台上的本地化实现逐渐漂离
- 混淆运行时 manifest 与 package 分发元数据
- 让 runtime registry 去理解远程 catalog 或 zip archive

依赖图保持显式：

```text
Locale Pack -> UI Font Pack
Locale Pack -> Content Font Pack
Locale Pack -> IME Pack
Content text -> Optional supplement font packs
```

这样既让代码更容易推理，也让 Trail Mate 在不强迫所有设备承担同样固件体积与 RAM 成本的前提下，拥有扩展到更广泛语言支持的路径。
