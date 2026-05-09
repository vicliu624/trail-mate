# Localization Specification

## 1. Why This Document Exists

本文件不是翻译指南，也不是打包教程。

本文件的目标是把 Trail Mate 的“本地化系统”定义成一个有边界的对象，防止后续开发再次把以下东西混在一起：

- 固件里的 i18n 运行时
- SD / Flash 上的 runtime pack 载荷
- 仓库中的 `packs/<bundle-id>/` 源包
- GitHub Pages 发布出来的 zip 分发包
- 远程 catalog 元数据
- 已安装索引 `installed.json`
- “换语言”这个用户动作

如果这些对象不被区分，系统就会退化成“改了几行英文字符串，再补几条 tsv”的低解释力状态。

---

## 2. Current Distinctions

### 2.1 当前必须切开的对象

Trail Mate 本地化系统至少由八类对象组成：

1. 固件本地化运行时
2. Runtime pack 载荷
3. 仓库源包
4. 分发包
5. 远程 catalog
6. 已安装索引
7. locale 质量状态
8. 输入法运行时后端

它们彼此相关，但不是同一个东西。

### 2.2 非法混淆

以下切法在本规格下视为非法：

1. 把 `strings.tsv` 直接当成“本地化系统本体”。
2. 把 zip 分发包当成运行时直接消费的对象。
3. 把“安装了某语言包”误认为“该字体已经加载进 RAM”。
4. 把固件版本和语言包版本绑成同一个版本号。
5. 把英文源串当成“只是文案”，而不是翻译查找 key。
6. 把页面私有字符串补丁当成合法的本地化实现。
7. 把 `docs/ui_localization_plan.md` 这种历史方案继续当成现行契约。
8. 把繁简转换当成繁中本地化。
9. 把 `zh-Hant` 当成一个不需要地区语境的抽象繁体中文。
10. 把拼音当成所有中文使用者的默认输入法。
11. 把“机器翻译已经填满 TSV”当成可发布质量。
12. 把英文 key 原样复制到翻译列，当成合法的缺省翻译。

---

## 3. Object Model

### 3.1 固件本地化运行时

固件运行时负责：

1. 持有内建英文基线。
2. 扫描、编目并解析可用的 locale / font / IME pack manifest。
3. 根据 `display_locale` 选择活动 locale。
4. 维护 UI font chain 与 content font chain。
5. 在需要时惰性加载外部 `font.bin`。
6. 提供 `tr()`、`format()`、`set_label_text()` 等统一入口。
7. 在外部 pack 缺失、不可用或超出设备能力时安全回退。

它不负责：

1. 读取 zip 分发包。
2. 直接理解网站 catalog。
3. 把“语言包安装”与“当前 locale 激活”混成同一动作。

### 3.2 Runtime Pack 载荷

Runtime pack 载荷是运行时真正扫描和消费的已解包目录树。

当前形态包括：

1. Flash 安装根
   - ` /fs/trailmate/packs/... `，用于 Arduino/ESP32 上的安装器写入
2. SD 安装根
   - ` /trailmate/packs/... `，用于手工拷贝或可移动介质安装

运行时理解的是“解包后的 manifest + strings/ranges/font.bin”，而不是 zip。

### 3.3 仓库源包

仓库中的 `packs/<bundle-id>/` 是源码形态，不是运行时形态。

它可以包含：

1. `package.ini`
2. `README.md`
3. `DESCRIPTION.txt`
4. runtime manifests
5. `strings.tsv`
6. `build.ini`
7. `charset.txt`
8. `ranges.txt`
9. 可选的本地 `font.bin`

其中 `build.ini`、`charset.txt` 这类文件属于构建期元数据，运行时不读取。

### 3.4 分发包

分发包是面向安装器与网站分发的 zip 产物，不是运行时直接扫描的对象。

它当前由 `scripts/build_pack_repository.py` 生成，并包含：

1. 顶层包元数据
2. `payload/fonts/...`
3. `payload/locales/...`
4. `payload/ime/...`

安装器的职责是下载 zip、校验 SHA-256、解出 `payload/`，再写入 runtime pack 根目录。

### 3.5 远程 Catalog

远程 catalog 是“可下载 bundle 的索引”，不是运行时 registry。

它负责提供：

1. package id
2. package version
3. `min_firmware_version`
4. `supported_memory_profiles`
5. 提供了哪些 locale/font/IME
6. archive 路径、大小、SHA-256
7. Extensions 页面所需的展示元数据

它不负责：

1. 字体加载
2. locale 激活
3. zip 内文件解析

### 3.6 已安装索引

已安装索引记录“安装过哪些分发包”，而不是“当前有哪些 locale 被激活”。

当前安装索引的职责是记录：

1. package id
2. package version
3. archive SHA-256
4. storage
5. install time

在当前 Arduino/ESP32 实现中，主安装索引默认写入 Flash 安装根下的 `installed.json`。
历史 SD 路径可以作为迁移兼容来源存在，但不应再被当成新的规范落点。

它不能替代 runtime registry，也不能替代 locale persistence。

### 3.7 Locale 质量状态

locale 质量状态描述的是“这个语言包现在能不能作为用户可选语言出现”，不是描述字体是否存在，也不是描述包是否可以被 catalog 发现。

当前状态值固定为：

1. `release`
   - 可以出现在语言选择器中。
   - 必须完成 native 或项目负责人认可的质量审查。
   - 不得包含英文补偿项，除非该项是明确允许保留的技术名词、协议名、单位或格式占位串。
2. `review`
   - 字符串表结构完整，通常已经被批量翻译或人工初审过。
   - 仍然需要母语审校、地区用语审校、输入法语义审校。
   - 运行时必须跳过，不得出现在普通用户语言选择器中。
3. `draft`
   - 可以缺 key、混用语言、缺少地区用语审查。
   - 运行时必须跳过。

为了兼容旧包，省略 `translation_status` 的历史 locale 视为 `release`。新包不应再省略该字段。

### 3.8 输入法运行时后端

IME pack manifest 只是输入法后端的声明，不是后端本身。

一个输入法要成为可启用输入法，必须同时满足：

1. 固件中存在真实后端。
2. 候选词、组合逻辑、提交行为与目标语言/地区习惯匹配。
3. UI widget 知道如何展示该输入模式。
4. locale manifest 显式依赖它，且该 locale 已经是可发布质量。

因此，不允许用 `backend=builtin-kana`、`backend=builtin-hangul`、`backend=builtin-arabic` 这类尚未实现的名字提前占位。未实现输入法可以写进规划文档，但不能作为 runtime IME pack 进入 payload。

---

## 4. Core Contracts

### 4.1 English Source String Is API

英文源串不是随手写的文案，它是翻译查找 key。

这意味着：

1. `ui::i18n::tr(const char* english)` 的输入是稳定 key，而不是“仅供显示的默认英文”。
2. 修改英文源串，会直接改变 key。
3. 一旦 key 改变，所有 locale pack 中对应条目都会失配并回退到英文。
4. `strings.tsv` 中同一 English key 不允许承载两套不同语义。
5. `strings.tsv` 的行顺序不构成语义；运行时按 English key 查找，而不是按文件顺序查找。

因此，下列操作都属于本地化契约变化：

1. 修改现有英文源串文本
2. 拆分一条英文源串为多条
3. 把一条英文源串合并为另一条
4. 改变 format string 结构，例如 `%s` / `%u` / 换行符位置

它们都不能被当成“只改 UI 文案”的低风险改动。

### 4.2 Locale / Font / IME Dependency Graph

依赖图固定为：

1. `Locale Pack -> UI Font Pack`
2. `Locale Pack -> Content Font Pack`
3. `Locale Pack -> Optional IME Pack`
4. `Content Text -> Optional Supplement Font Packs`

Settings 选择的是 locale，不是 font，不是 IME。

页面或设置页不允许绕过 locale 直接定义另一套“选字体/选输入法”主流程。

### 4.2.1 Locale ID 必须表达地区语境

locale id 必须按 BCP-47 思路表达语言、脚本与地区语境，而不是只表达字形。

推荐形态：

1. `zh-Hans-CN`
   - 简体中文，中国大陆用语。
2. `zh-Hant-TW`
   - 繁体中文，台湾地区用语。
3. `zh-Hant-HK`
   - 繁体中文，香港地区用语。
4. `pt-PT`
   - 葡萄牙语，葡萄牙地区用语。
5. `pt-BR`
   - 葡萄牙语，巴西地区用语。

当前仓库中的 `zh-Hant` 是历史兼容 id，语义必须按 `zh-Hant-TW` 处理；后续如果引入香港、澳门或其他繁中地区包，必须新增独立 locale，而不是继续复用 `zh-Hant`。

禁止把 OpenCC 或其他繁简转换结果直接作为 `release` 级繁中包。转换最多用于生成初稿；发布前必须检查地区用词、标点、语气、技术名词和输入法习惯。

### 4.2.2 翻译质量闸门

运行时 locale catalog 必须执行质量闸门：

1. `translation_status=release`
   - 允许进入运行时 locale 列表。
2. 缺省 `translation_status`
   - 为兼容旧包，暂按 `release` 处理。
3. `translation_status=review`
   - 可以被安装、索引、更新，但运行时必须跳过。
4. `translation_status=draft`
   - 可以作为开发资料存在，但运行时必须跳过。
5. 其他未知状态
   - 运行时必须按不可发布处理并跳过。

一个 release 语言包不得出现以下情况：

1. 空翻译。
2. 缺失当前 English key 集合中的 key。
3. `%s`、`%u`、`%ld`、`\\n` 等占位符或转义符不一致。
4. 大量英文 key 原样复制到翻译列。
5. 混入另一种语言的翻译文本，例如日文包中出现简体中文句子。
6. 地区用语明显不匹配，例如台湾繁中使用大陆软件术语作为主用语。

允许保持英文的项目只限于明确的专名、协议名、单位、测量标签、格式骨架，例如 `GPS`、`RSSI`、`dBm`、`RNode`、`LXMF`、`ID: !%08lX`。

### 4.2.3 输入法必须跟 locale 习惯绑定

输入法不是“有一个能打字的键盘”这么简单。它属于 locale 的语言、脚本和地区习惯。

当前可启用输入法只有：

1. `zh-Hans` / `zh-Hans-CN`
   - `zh-hans-pinyin`
   - 后端：`builtin-pinyin`
   - 适用范围：简体中文拼音输入。

当前不得启用的输入法包括：

1. `zh-Hant` / `zh-Hant-TW`
   - 不得默认拼音。
   - 未来优先级应是注音（Bopomofo / Zhuyin）。
   - 可选扩展可考虑仓颉、速成、台湾常见拼音输入，但必须明确标注，不得冒充默认习惯。
2. `ja`
   - 需要假名/罗马字到假名与汉字候选的日文输入模型。
   - 不得复用中文拼音候选器。
3. `ko`
   - 需要韩文两벌式等韩文输入模型。
   - 不得复用中文或日文输入模型。
4. `ar`
   - 需要阿拉伯键盘、RTL 编辑语义与字体 shaping 一致性。
5. 欧洲 Latin 语言
   - 通常不需要 IME pack；现有 `EN` / `123` 输入路径足够。

如果某 locale 还没有真实输入法，它可以作为 display-only review 包存在，但不得把假 IME 写进 manifest，也不得让设置页显示一个无法工作的输入法选项。

### 4.3 Built-In Baseline

固件必须始终保留最小内建基线：

1. 内建 locale：`en`
2. 内建字体：`builtin-latin-ui`
3. 内建 IME：可为空

English 必须在没有任何外部 pack 的情况下仍可工作。

这条基线不能被 removable pack 破坏。

### 4.4 Installed Is Not Loaded

`Installed` 与 `Loaded` 是两个不同状态：

1. Installed
   - pack manifest 存在于 Flash/SD
   - runtime registry 能发现它
2. Loaded
   - 外部 `font.bin` 已经实际加载进 RAM
   - 会产生即时内存占用

禁止把“安装成功”实现成“开机即把所有外部字体全部加载”。

### 4.5 UI Scope vs Content Scope

本地化运行时必须继续维持两条字体链：

1. UI chain
   - 页面 chrome
   - 菜单、按钮、标题、设置项等
2. Content chain
   - 联系人名
   - 节点名
   - 聊天内容
   - 其他外部文本

这两条链不能被简化成“只要非 ASCII 就统一切换某个 CJK 字体”的旁路实现。

### 4.6 Persistence Contract

当前 locale 的持久化 key 是：

1. namespace：`settings`
2. key：`display_locale`
3. value：locale id string

`display_language` 只是历史迁移键，只允许用于一次性迁移，不得再作为现行设计继续扩展。

### 4.7 Discovery Contract

运行时 registry 只扫描“已解包 runtime pack”。

它不扫描：

1. zip
2. 远程 JSON catalog
3. 仓库里的 `build.ini`
4. 构建期 charset 生成元数据

如果一个 pack 只存在于网站 zip 中、还没解到运行时目录，那它对运行时来说就是不存在。

### 4.8 Installation Contract

安装器层与运行时层必须分离。

安装器负责：

1. 拉取 catalog
2. 下载 zip
3. 校验 SHA-256
4. 解压 `payload/`
5. 更新 installed index
6. 触发 `reload_language()`

运行时负责：

1. 重新扫描可用 pack
2. 解析 manifest / strings / ranges
3. 重新选择 active locale
4. 按需加载字体

当前 Arduino/ESP32 安装器默认把下载得到的 payload 解到 Flash pack 根。
与此同时，手工把 runtime payload 拷贝到 SD 仍然是合法安装方式，因为运行时会同时编目当前支持的 Flash/SD 根。

禁止让运行时 registry 直接承担网络下载或 zip 解释职责。

### 4.9 Failure And Fallback Contract

本地化失败时必须退回到可解释状态，而不是把系统带入半失效状态。

当前允许的失败行为包括：

1. locale 缺少依赖 font pack
   - 跳过该 locale
2. locale 缺少依赖 IME pack
   - 跳过该 locale
3. 当前设备 memory profile 无法承受 locale 成本
   - 跳过该 locale
4. 持久化的 locale id 不再可解析
   - 回退到 `en`
5. 某个 content supplement 无法加载
   - 页面继续存活
   - 个别文字可出现缺字

禁止因为一个外部包损坏而让整个 UI 失去最小英语可用能力。

---

## 5. Versioning And Compatibility Contract

### 5.1 Firmware Version 与 Package Version 必须分离

固件版本与语言包版本不是同一个版本号体系。

它们的职责不同：

1. 固件版本
   - 描述运行时代码能力
2. package version
   - 描述 bundle 载荷与元数据版本

不能因为固件升级一次，就机械把所有语言包 version 同步改掉。
也不能因为语言包换了翻译文本，就伪造一次固件版本升级。

### 5.2 `min_firmware_version` 的语义

`min_firmware_version` 是下界，不是“必须完全相等”的绑定版本。

它表示：

1. 低于这个固件版本，当前 bundle 语义可能无法被正确理解
2. 高于或等于这个版本，可以认为运行时至少理解这份 bundle 契约

它不表示：

1. 这个 bundle 只能给某一个固件版本使用
2. 每次小翻译更新都必须改 `min_firmware_version`

### 5.3 `supported_memory_profiles` 的语义

`supported_memory_profiles` 描述的是设备能力兼容边界，不是语言偏好。

它影响：

1. Extensions 页展示是否兼容
2. 某设备是否应被允许安装/更新该 bundle

它不能被拿来表达：

1. 用户喜欢哪种语言
2. 当前 locale 是否被激活

### 5.4 更新判定

当前“是否有更新”以 package record 为单位判定，至少比较：

1. version
2. archive SHA-256

因此，哪怕 version 不变，只要 archive 内容变了，也可能被视为不同包。

这意味着偷偷改 zip 内容而不更新包版本，是危险的发布行为。

### 5.5 兼容性 gating 的归属

兼容性 gating 当前属于“安装/发现层”的职责，主要体现在 catalog 解析和 Extensions UI。

但运行时仍必须对手工拷贝到 Flash/SD 的 payload 保持鲁棒：

1. 不兼容 pack 可以被发现
2. 依赖不满足时可以跳过
3. 系统仍需回退到 English 基线

禁止把系统安全性完全建立在“用户一定只会通过 Extensions 正规安装”这个假设上。

### 5.6 语言包版本必须表达用户可见变化

语言包版本不是装饰字段。只要用户安装后看到的文字、可用 locale、可用 IME、字体覆盖或质量状态发生变化，就必须 bump package version。

最低要求：

1. 只修正少量错别字或补少量漏翻
   - bump patch，例如 `1.0.0 -> 1.0.1`。
2. 大量补齐字符串、改写地区用语、改变 `translation_status`、移除假 IME、改变默认输入法依赖
   - bump minor，例如 `1.0.0 -> 1.1.0`。
3. 改变 payload 布局或 manifest 语义，导致旧运行时可能错误理解
   - bump `min_firmware_version`，并视情况 bump minor 或 major。

发布语言包更新时必须同时重建：

1. zip archive。
2. `site/data/packs.json`。
3. archive SHA-256。
4. installed/update comparison 所需的 package metadata。

否则用户看不到更新，或者看到更新却无法解释更新内容。

---

## 6. Change Classification

### 6.1 固件改动但通常不要求语言包跟随变化

以下改动通常不要求语言包更新：

1. 纯逻辑修复，不引入新的用户可见文本
2. 不改变 manifest schema 的运行时重构
3. 不改变翻译 key 的内部实现优化
4. 不改变字体需求的布局调整

### 6.2 固件改动后通常要求更新语言包字符串

以下改动通常要求至少更新一个或多个 locale pack：

1. 新增用户可见英文源串
2. 删除旧源串并引入新源串
3. 修改旧英文源串文本
4. 修改 format string 参数结构
5. 修改带转义字符的字符串结构，例如 `\n`

### 6.3 固件改动后可能要求更新字体包

以下改动不仅可能改 `strings.tsv`，还可能要求重建 font pack：

1. 新翻译文本引入当前字体未覆盖的新字形
2. `native_name` 改动引入新字形
3. 新 IME 字典或候选字集引入新字形
4. 新增 locale/self-name/设置项文本导致 charset 扩展

一旦字形覆盖变化，至少要同步处理：

1. `charset.txt`
2. `ranges.txt`
3. `font.bin`
4. `estimated_ram_bytes`

### 6.4 固件改动后必须提高 `min_firmware_version`

以下改动属于 bundle 契约变化，必须考虑提高 `min_firmware_version`：

1. manifest schema 新增运行时必需字段
2. 现有 manifest 字段语义改变
3. runtime 对 bundle 布局的期望发生变化
4. catalog / archive / installed index 语义发生不兼容变化
5. locale/font/IME 依赖规则发生不兼容变化

### 6.5 只改语言包也必须 bump package version

以下情况即使不改固件，也应 bump package version：

1. `strings.tsv` 变化
2. `manifest.ini` 变化
3. `ranges.txt` 变化
4. `font.bin` 变化
5. `package.ini` 中影响安装或兼容性的字段变化

否则 installed index 与 update check 将失去解释力。

---

## 7. Release Obligations

### 7.1 固件改动合入前必须回答的问题

1. 这次是否新增了用户可见英文源串？
2. 这次是否改动了现有英文 key？
3. 这次是否改变了 format string 结构？
4. 这次是否引入了新字形需求？
5. 这次是否改变了 manifest / package / catalog 契约？

只要上述任一答案为“是”，就不能只提交固件代码而不审视语言包。

### 7.2 语言包发布前必须回答的问题

1. `package version` 是否已反映 payload 变化？
2. 如有契约变化，`min_firmware_version` 是否已提高？
3. `font.bin` 是否与 `charset.txt` / `ranges.txt` / manifest 保持一致？
4. `estimated_ram_bytes` 是否仍真实反映生成结果？
5. `site/data/packs.json` 与 zip 产物是否已重建？
6. `translation_status` 是否符合真实质量？
7. release 包是否已经通过空值、缺 key、重复 key、占位符一致性检查？
8. release 包是否已经清除英文补偿项？
9. 该 locale 的地区用语是否经过明确审查？
10. 该 locale 的 IME 依赖是否真实可用，而不是占位后端？

### 7.3 新 locale 合入前必须满足的最小条件

1. English 基线不被破坏。
2. locale manifest 依赖完整。
3. 对应 font pack 的 RAM 成本可被当前目标 memory profile 解释。
4. 运行时目录布局、分发包布局、catalog 元数据三者一致。
5. 手工安装与 Extensions 安装都不会把系统带离 English 回退能力。
6. 默认 `translation_status` 不得伪装成 release；未审校语言必须先进入 `review` 或 `draft`。
7. 输入法要么真实可用，要么不写进 locale manifest。
8. 如果 locale 是地区敏感语言，例如繁体中文或葡萄牙语，必须明确地区语境。

---

## 8. Prohibited Implementations

以下实现方式在本规格下明确禁止：

1. 继续引入新的整数语言枚举作为主持久化键。
2. 页面直接绕过 `ui::i18n` 写死另一套翻译逻辑。
3. 通过“检测到非 ASCII 就随便切某个字体”来替代 locale/font chain。
4. 把 zip 当运行时 pack 根目录。
5. 改了英文源串却不把它当成 key 变化处理。
6. 改了翻译文本所需字形，却不重建相关字体包。
7. 改了 bundle payload，却不 bump package version。
8. 改了 bundle 契约，却不调整 `min_firmware_version`。
9. 让不兼容或损坏的 pack 破坏 English 最小基线。
10. 用机器翻译或繁简转换结果直接发布 release 包。
11. 在没有真实后端时发布 IME manifest 或 locale `ime_pack` 依赖。
12. 给台湾繁中默认启用拼音输入法。
13. release 语言包中保留大面积英文补偿项。

---

## 9. Relationship To Other Documents

本文件是规范性文档。

如果与其他文档冲突，优先级如下：

1. `docs/LOCALIZATION_SPEC.md`
2. `docs/specs/LOCALE_PACK_RELEASE_SPEC.md`
3. `docs/LOCALE_PACKS.md`
4. 各 `packs/<bundle-id>/README.md`
5. `docs/ui_localization_plan.md`

其中：

1. `docs/LOCALE_PACKS.md`
   - 负责解释 pack 机制、布局和字段
2. `docs/specs/LOCALE_PACK_RELEASE_SPEC.md`
   - 负责解释打包、发布、版本、catalog、archive 与更新可见性
3. `docs/ui_localization_plan.md`
   - 仅保留历史演进价值，不再作为当前设计依据

---

## 10. One-Sentence Baseline

Trail Mate 的本地化不是“固件里几条翻译表”，而是“以 English key 为稳定接口、以 locale/font/IME pack 为显式依赖、以安装层与运行时层分离为前提、并允许固件与语言包独立演进但受兼容契约约束”的完整系统。
