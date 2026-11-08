# 裸 NVS 入口与存储别名清单

由 scripts/audit_nvs_access.mjs 扫描 Git 跟踪的 ESP、共享 UI 和仓库 third_party C/C++ 文件；不递归已初始化子模块，也不混入本地生成文件。只记录 NVS 函数、namespace/key/handle 实参和位置，不读取设备、不记录值/默认值/缓冲/分区名。不把最近的 nvs_open 自动推断为后续调用的 namespace；跨作用域句柄、宏和条件编译仍需人工核对。

扫描 1573 个源文件，找到 106 个候选调用，分布于 8 个文件。显式打开 15，键访问 45，其余为提交/关闭/清域。调用数不是独立配置数。

这是源码入口清单，不是全部配置或硬件生效证明。系统 SDK、构建环境依赖、其他平台、函数指针/包装器实参以及复杂原始字符串不由此表自动覆盖。依赖库可单独调用 collectNvsAccesses 指定根目录审计；输出应单独记录其版本，不能写进与机器无关的仓库清单。

## NVS 候选调用

| 方法 | 类别 | 句柄表达式 | namespace/key 表达式 | 字面量 | 来源 |
|---|---|---|---|---|---|
| nvs_open | namespace | &record_handle | kPrefsNs | 动态/不适用 | [platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp:165](../../../../platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp#L165) |
| nvs_get_blob | key | record_handle | "native_v1" | native_v1 | [platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp:168](../../../../platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp#L168) |
| nvs_close | close | record_handle |  | 动态/不适用 | [platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp:169](../../../../platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp#L169) |
| nvs_open | namespace | &handle_ | ns | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:29](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L29) |
| nvs_close | close | handle_ |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:33](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L33) |
| nvs_get_u8 | key | handle_ | key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:39](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L39) |
| nvs_get_u32 | key | handle_ | key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:45](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L45) |
| nvs_get_blob | key | handle_ | key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:50](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L50) |
| nvs_set_u8 | key | handle_ | key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:52](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L52) |
| nvs_set_u32 | key | handle_ | key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:54](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L54) |
| nvs_set_blob | key | handle_ | key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:57](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L57) |
| nvs_erase_key | key | handle_ | key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:62](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L62) |
| nvs_commit | commit | handle_ |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/config_preferences.h:68](../../../../platform/esp/common/include/platform/esp/common/config_preferences.h#L68) |
| nvs_open | namespace | &handle | "lxmf_ident" | lxmf_ident | [platform/esp/common/include/platform/esp/common/identity_atomic_record.h:58](../../../../platform/esp/common/include/platform/esp/common/identity_atomic_record.h#L58) |
| nvs_get_blob | key | handle | "native_v1" | native_v1 | [platform/esp/common/include/platform/esp/common/identity_atomic_record.h:70](../../../../platform/esp/common/include/platform/esp/common/identity_atomic_record.h#L70) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/identity_atomic_record.h:71](../../../../platform/esp/common/include/platform/esp/common/identity_atomic_record.h#L71) |
| nvs_open | namespace | &handle | "lxmf_ident" | lxmf_ident | [platform/esp/common/include/platform/esp/common/identity_atomic_record.h:116](../../../../platform/esp/common/include/platform/esp/common/identity_atomic_record.h#L116) |
| nvs_set_blob | key | handle | "native_v1" | native_v1 | [platform/esp/common/include/platform/esp/common/identity_atomic_record.h:117](../../../../platform/esp/common/include/platform/esp/common/identity_atomic_record.h#L117) |
| nvs_commit | commit | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/identity_atomic_record.h:118](../../../../platform/esp/common/include/platform/esp/common/identity_atomic_record.h#L118) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/identity_atomic_record.h:119](../../../../platform/esp/common/include/platform/esp/common/identity_atomic_record.h#L119) |
| nvs_open | namespace | &handle | "tm_author" | tm_author | [platform/esp/common/include/platform/esp/common/identity_generation_recovery.h:14](../../../../platform/esp/common/include/platform/esp/common/identity_generation_recovery.h#L14) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/identity_generation_recovery.h:18](../../../../platform/esp/common/include/platform/esp/common/identity_generation_recovery.h#L18) |
| nvs_get_blob | key | handle | "epoch" | epoch | [platform/esp/common/include/platform/esp/common/identity_generation_recovery.h:22](../../../../platform/esp/common/include/platform/esp/common/identity_generation_recovery.h#L22) |
| nvs_set_blob | key | handle | "epoch" | epoch | [platform/esp/common/include/platform/esp/common/identity_generation_recovery.h:38](../../../../platform/esp/common/include/platform/esp/common/identity_generation_recovery.h#L38) |
| nvs_commit | commit | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/identity_generation_recovery.h:38](../../../../platform/esp/common/include/platform/esp/common/identity_generation_recovery.h#L38) |
| nvs_open | namespace | &handle | "tm_author" | tm_author | [platform/esp/common/include/platform/esp/common/identity_generation_store.h:16](../../../../platform/esp/common/include/platform/esp/common/identity_generation_store.h#L16) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/identity_generation_store.h:20](../../../../platform/esp/common/include/platform/esp/common/identity_generation_store.h#L20) |
| nvs_get_blob | key | handle | "epoch" | epoch | [platform/esp/common/include/platform/esp/common/identity_generation_store.h:24](../../../../platform/esp/common/include/platform/esp/common/identity_generation_store.h#L24) |
| nvs_set_blob | key | handle | "epoch" | epoch | [platform/esp/common/include/platform/esp/common/identity_generation_store.h:43](../../../../platform/esp/common/include/platform/esp/common/identity_generation_store.h#L43) |
| nvs_commit | commit | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/identity_generation_store.h:43](../../../../platform/esp/common/include/platform/esp/common/identity_generation_store.h#L43) |
| nvs_open | namespace | &handle | "lxmf_ident" | lxmf_ident | [platform/esp/common/include/platform/esp/common/identity_key_state.h:11](../../../../platform/esp/common/include/platform/esp/common/identity_key_state.h#L11) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/identity_key_state.h:17](../../../../platform/esp/common/include/platform/esp/common/identity_key_state.h#L17) |
| nvs_get_blob | key | handle | key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/identity_key_state.h:23](../../../../platform/esp/common/include/platform/esp/common/identity_key_state.h#L23) |
| nvs_open | namespace | &handle | ns | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:67](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L67) |
| nvs_get_blob | key | handle | kRadioIdentityRecordKey | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:71](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L71) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:72](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L72) |
| nvs_open | namespace | &handle | ns | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:85](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L85) |
| nvs_get_blob | key | handle | public_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:89](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L89) |
| nvs_get_blob | key | handle | private_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:91](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L91) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:92](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L92) |
| nvs_open | namespace | &handle | ns | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:107](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L107) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:113](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L113) |
| nvs_get_blob | key | handle | public_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:116](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L116) |
| nvs_get_blob | key | handle | private_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:117](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L117) |
| nvs_get_u8 | key | handle | version_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:124](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L124) |
| nvs_get_blob | key | handle | private_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:127](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L127) |
| nvs_get_blob | key | handle | public_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:128](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L128) |
| nvs_open | namespace | &handle | ns | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:180](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L180) |
| nvs_set_blob | key | handle | kRadioIdentityRecordKey | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:181](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L181) |
| nvs_commit | commit | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:182](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L182) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/radio_identity_record.h:183](../../../../platform/esp/common/include/platform/esp/common/radio_identity_record.h#L183) |
| nvs_open | namespace | handle | ns | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:138](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L138) |
| nvs_commit | commit | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:155](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L155) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:156](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L156) |
| nvs_erase_key | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:162](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L162) |
| nvs_set_i32 | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:224](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L224) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:231](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L231) |
| nvs_set_u8 | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:268](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L268) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:275](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L275) |
| nvs_set_u32 | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:312](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L312) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:319](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L319) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:366](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L366) |
| nvs_set_str | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:381](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L381) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:388](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L388) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:437](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L437) |
| nvs_set_blob | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:452](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L452) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:459](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L459) |
| nvs_get_i32 | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:498](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L498) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:499](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L499) |
| nvs_get_u8 | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:536](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L536) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:537](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L537) |
| nvs_get_u32 | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:572](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L572) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:573](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L573) |
| nvs_get_str | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:609](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L609) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:617](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L617) |
| nvs_get_str | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:624](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L624) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:626](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L626) |
| nvs_get_str | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:683](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L683) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:686](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L686) |
| nvs_get_str | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:690](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L690) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:692](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L692) |
| nvs_open | namespace | &handle | ns | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:723](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L723) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:727](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L727) |
| nvs_open | namespace | &handle | ns | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:772](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L772) |
| nvs_get_str | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:776](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L776) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:779](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L779) |
| nvs_get_str | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:784](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L784) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:785](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L785) |
| nvs_open | namespace | &handle | ns | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:804](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L804) |
| nvs_get_blob | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:808](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L808) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:811](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L811) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:817](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L817) |
| nvs_get_blob | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:821](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L821) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:822](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L822) |
| nvs_get_blob | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:858](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L858) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:866](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L866) |
| nvs_get_blob | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:872](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L872) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:873](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L873) |
| nvs_get_blob | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:922](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L922) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:930](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L930) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:940](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L940) |
| nvs_get_blob | key | handle | storage_key | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:945](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L945) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:946](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L946) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:1010](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L1010) |
| nvs_erase_all | clear | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:1024](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L1024) |
| nvs_close | close | handle |  | 动态/不适用 | [platform/esp/common/include/platform/esp/common/settings_store_impl.h:1031](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h#L1031) |

## 通用存储键映射

实际 resolve_storage_key 在通用 settings_store 的读写入口使用这些别名；最大 NVS 键名为 15 字节。映射不是额外可编辑配置，也不能假设直接 Preferences 或其它库自动调用它。原逻辑键的 TMS/网页覆盖应与实际调用方继续对账。

来源：[platform/esp/common/include/platform/esp/common/settings_store_impl.h](../../../../platform/esp/common/include/platform/esp/common/settings_store_impl.h)

| 逻辑键 | 实际 NVS 键 |
|---|---|
| display_language | disp_lang |
| display_locale | disp_locale |
| screen_brightness | screen_bright |
| vibration_enabled | vibe_enabled |
| chat_message_alerts | chat_msg_alert |
| chat_contact_alerts | chat_ct_alert |
| chat_auto_reply_enabled | chat_auto_reply |
| chat_auto_reply_text | chat_auto_txt |
| timezone_profile | timezone_prof |
| gauge_design_mah | gauge_dsgn |
| wifi_profile_count | wifi_prof_count |
| espnow_team_discovery | en_team_disc |
| espnow_beacon_interval_ms | en_beacon_ms |
| wifi_sta_enabled | wifi_sta_en |
| wifi_sta_channel | wifi_sta_ch |
| wifi_persist_credentials | wifi_keep_cred |
| wifi_sta_password | wifi_sta_pwd |
| wifi_ap_password | wifi_ap_pwd |
