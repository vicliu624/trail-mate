# Locale Pack Release Specification

本文档定义 Trail Mate 语言包的打包、发布、更新与回滚规则。

它回答的问题不是“如何翻译一句话”，而是：

1. 什么变化必须 bump package version。
2. 什么变化必须提高 `min_firmware_version`。
3. release / review / draft 包如何进入 archive 和 catalog。
4. 用户如何看到语言包更新。
5. 发布前必须跑哪些结构校验。

如果本文档与 `LOCALIZATION_SPEC.md` 冲突，以 `LOCALIZATION_SPEC.md` 为准。
如果本文档与 `LOCALE_PACKS.md` 冲突，以本文档的发布流程为准。

---

## 1. 发布对象

语言包发布涉及五个对象：

1. source bundle
   - 仓库中的 `packs/<bundle-id>/`。
2. runtime payload
   - archive 内的 `payload/fonts`、`payload/locales`、`payload/ime`。
3. package manifest
   - bundle 根目录的 `package.ini`。
4. distribution archive
   - `site/assets/packs/<package-id>-<version>.zip`。
5. remote catalog
   - `site/data/packs.json`。

这五个对象必须一起演进。只改 source bundle 而不重建 archive/catalog，不构成完整发布。

---

## 2. 包状态

locale manifest 的 `translation_status` 控制运行时是否能采用该 locale。

1. `release`
   - 可以出现在语言选择器。
   - 必须通过结构校验和质量审查。
2. `review`
   - 可以打包、分发、安装、更新。
   - 运行时必须跳过，不进入语言选择器。
   - 用于给测试者或母语审校者拿到完整 payload。
3. `draft`
   - 允许缺 key 或翻译不完整。
   - 运行时必须跳过。
   - 原则上不应进入公开 catalog，除非 catalog 明确标记为实验包。

新语言包不得省略 `translation_status`。省略只用于兼容历史包。

---

## 3. Version Bump 规则

`package.ini` 的 `version` 是用户看到更新的主信号之一。以下变化必须 bump version：

1. `strings.tsv` 变化。
2. locale/font/IME `manifest.ini` 变化。
3. `font.bin`、`ranges.txt`、`charset.txt` 变化。
4. `README.md`、`DESCRIPTION.txt` 中影响用户理解的描述变化。
5. `translation_status` 变化。
6. `ime_pack` 增删或 IME backend 变化。
7. `package.ini` 中影响安装、兼容性、搜索、展示的字段变化。

建议 bump 幅度：

1. patch
   - 少量漏翻、错别字、标点、术语修正。
   - 例如 `1.0.0 -> 1.0.1`。
2. minor
   - 大量补齐翻译。
   - 从 `draft` 进入 `review` 或从 `review` 进入 `release`。
   - 移除假 IME 或改变输入法策略。
   - 新增大量 key 覆盖。
   - 例如 `1.0.0 -> 1.1.0`。
3. major
   - payload 布局、依赖语义或兼容性语义发生破坏性变化。
   - 目前 pre-1.0 阶段可以少用 major，但不得用 patch 掩盖破坏性变化。

禁止改变 archive 内容但保持同一个 package version。

---

## 4. `min_firmware_version` 规则

`min_firmware_version` 表示正确理解该 bundle 契约所需的最低固件版本。

以下变化必须提高 `min_firmware_version`：

1. 新增运行时必须理解的 manifest 字段。
2. 改变已有 manifest 字段语义。
3. 改变 locale/font/IME 依赖规则。
4. 改变运行时 payload 布局。
5. 旧固件如果安装该包，会暴露错误语言、假 IME 或错误字体链。

当前 `0.1.24-alpha` 引入 `translation_status=review/draft` 且要求运行时跳过非 release locale，因此所有依赖该质量闸门的 review/draft 包都必须把 `min_firmware_version` 提高到 `0.1.24-alpha` 或更高。

---

## 5. Archive 规则

archive 路径固定为：

```text
site/assets/packs/<package-id>-<version>.zip
```

archive 必须包含：

```text
package.ini
DESCRIPTION.txt
README.md
payload/fonts/<font-pack-id>/manifest.ini
payload/fonts/<font-pack-id>/ranges.txt
payload/fonts/<font-pack-id>/font.bin
payload/locales/<locale-id>/manifest.ini
payload/locales/<locale-id>/strings.tsv
payload/ime/<ime-pack-id>/manifest.ini
```

其中 `payload/ime/...` 只允许包含真实可运行 IME。未实现输入法不得进入 archive。

archive 不得包含：

1. `build.ini`
2. `charset.txt`
3. `.gitignore`
4. 临时文件
5. 空 IME 目录
6. 未使用的实验 payload

---

## 6. Catalog 规则

`site/data/packs.json` 是用户看到可安装/可更新语言包的 catalog。

每次 package version 或 archive 内容变化，都必须重建 catalog，使以下字段同步：

1. package id
2. package version
3. display name
4. summary
5. `min_firmware_version`
6. supported memory profiles
7. provided locale/font/IME records
8. archive URL/path
9. archive size
10. archive SHA-256
11. estimated runtime font RAM

如果 catalog 的 version 或 SHA-256 没变，用户侧更新检查可能看不到变化。

---

## 7. 发布前结构校验

每个 locale `strings.tsv` 必须校验：

1. key 集合完整。
2. 无重复 key。
3. 无空翻译。
4. printf 占位符一致。
5. 转义符一致。
6. release 包无英文补偿项。
7. release 包无第三语言混入。

每个 locale manifest 必须校验：

1. `id` 存在。
2. `translation_status` 存在。
3. `ui_font_pack` 存在且可解析。
4. `content_font_pack` 存在且可解析。
5. `ime_pack` 只指向真实 runtime IME。
6. RTL locale 必须声明 `direction=rtl`。

每个 package manifest 必须校验：

1. `version` 已 bump。
2. `min_firmware_version` 与 manifest 语义匹配。
3. `supported_memory_profiles` 与字体 RAM 成本匹配。
4. `summary` 不谎称已有未实现 IME。

---

## 8. 发布流程

标准流程：

1. 修改 source bundle。
2. 跑 locale 结构校验。
3. 决定每个 locale 的 `translation_status`。
4. 移除所有未实现 IME manifest 和 locale `ime_pack` 假依赖。
5. bump `package.ini` 的 `version`。
6. 必要时提高 `min_firmware_version`。
7. 重建 archive 和 catalog：

```powershell
python scripts/build_pack_repository.py --pack-root packs --site-root site
```

8. 校验 `site/data/packs.json` 中 version、archive path、size、SHA-256 都发生了预期变化。
9. 在目标固件上验证：
   - release locale 可选。
   - review/draft locale 不出现在语言选择器。
   - Settings 的 IME 列表只显示真实可用 IME。
   - 已安装旧包时能看到 update available。

---

## 9. 回滚规则

如果已发布语言包发现严重问题：

1. 不要复用旧 archive 文件名覆盖内容。
2. 发布一个更高 version 的修复包。
3. 如果必须下架，catalog 中移除该 package record，并保留 release note。
4. 如果是 release 质量误判，应发布更高 version，把 locale 降回 `review` 或 `draft`。

这样 installed index 与 update check 才能解释“为什么用户需要更新”。

---

## 10. 当前发布策略

当前策略是：

1. `zh-Hans`
   - 可以作为 `release`。
   - 当前唯一可发布 IME 是 `zh-hans-pinyin`。
2. `zh-Hant`
   - 按台湾繁中 `review` 包处理。
   - 不挂拼音 IME。
   - 进入 `release` 前必须经过台湾用语与输入法习惯审查。
3. `ar`、`ru`、`de`、`es`、`fr`、`it`、`nl`、`pl`、`pt-PT`、`ja`、`ko`
   - 可打包为 `review`。
   - 不进入普通用户语言选择器。
   - 不发布假 IME。
