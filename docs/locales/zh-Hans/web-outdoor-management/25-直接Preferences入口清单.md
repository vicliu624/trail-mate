# 直接 Preferences 持久化入口清单

由 scripts/audit_preferences_access.mjs 扫描 ESP 与共享 UI 源码中声明为 Preferences / ConfigPreferences 的对象及引用参数，提取显式成员调用。排除普通注释和字符串中的伪调用，只记录 namespace/key 实参表达式、方法和位置，不输出默认值、配置值、凭据或字节内容。

这是候选入口清单，不是完整编译器调用图或配置覆盖证明。对象作用域、条件编译、宏、跨文件别名、箭头访问、裸 NVS API、第三方库及其他平台仍需核对；尤其不把同名对象上最近一次 begin 自动当作后续键的 namespace，以免作用域或分支造成错误归类。原始字符串、复杂模板和预处理器结构亦须人工复核。动态实参全部保留。

扫描 1300 个源文件，发现 117 个调用：namespace 开启 21，键访问 96，整域清理 0。键访问中 30 个为直接字面量，66 个仍需追踪常量或动态表达式。

## Namespace 开启

| 接收对象 | Namespace 实参 | 来源 |
|---|---|---|
| prefs | "chat" | [platform/esp/arduino_common/include/platform/esp/arduino_common/device_identity.h:53](../../../../platform/esp/arduino_common/include/platform/esp/arduino_common/device_identity.h#L53) |
| preferences | kResetNamespace | [platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp:48](../../../../platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp#L48) |
| preferences | kResetNamespace | [platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp:60](../../../../platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp#L60) |
| prefs | ns | [platform/esp/arduino_common/src/app_config_store.cpp:102](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L102) |
| prefs | kMtBleNamespace | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:238](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L238) |
| prefs | kMcBleNamespace | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:250](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L250) |
| prefs | kMtModuleNamespace | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:265](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L265) |
| prefs | "mc_ble" | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1710](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1710) |
| prefs | "mc_ble" | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1730](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1730) |
| prefs | "mc_contacts" | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1750](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1750) |
| prefs | "mc_contacts" | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1820](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1820) |
| prefs | kBlePrefsNs | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:84](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L84) |
| prefs | kBlePrefsNs | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:110](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L110) |
| prefs | kModulePrefsNs | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:169](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L169) |
| prefs | kModulePrefsNs | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:190](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L190) |
| preferences | kPrefsNs | [platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp:195](../../../../platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp#L195) |
| prefs | ns | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:193](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L193) |
| prefs | ns | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:224](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L224) |
| prefs | ns | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:264](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L264) |
| prefs | ns | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:309](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L309) |
| prefs | ns | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:347](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L347) |

## 键访问

| 接收对象 | 方法 | 键实参 | 字面量键 | 来源 |
|---|---|---|---|---|
| prefs | getUInt | "node_id" | node_id | [platform/esp/arduino_common/include/platform/esp/arduino_common/device_identity.h:55](../../../../platform/esp/arduino_common/include/platform/esp/arduino_common/device_identity.h#L55) |
| prefs | putUInt | "node_id" | node_id | [platform/esp/arduino_common/include/platform/esp/arduino_common/device_identity.h:57](../../../../platform/esp/arduino_common/include/platform/esp/arduino_common/device_identity.h#L57) |
| preferences | getBool | kResetPendingKey | 待追踪 | [platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp:52](../../../../platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp#L52) |
| preferences | putBool | kResetPendingKey | 待追踪 | [platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp:64](../../../../platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp#L64) |
| preferences | remove | kResetPendingKey | 待追踪 | [platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp:65](../../../../platform/esp/arduino_common/src/app_config_sd_tms_runtime.cpp#L65) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:146](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L146) |
| prefs | getBool | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:147](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L147) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:166](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L166) |
| prefs | getUChar | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:167](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L167) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:186](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L186) |
| prefs | getChar | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:187](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L187) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:206](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L206) |
| prefs | getUShort | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:207](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L207) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:226](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L226) |
| prefs | getUInt | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:227](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L227) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:246](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L246) |
| prefs | getInt | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:247](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L247) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:266](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L266) |
| prefs | getFloat | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:267](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L267) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:286](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L286) |
| prefs | getString | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:287](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L287) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:309](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L309) |
| prefs | getBytes | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:310](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L310) |
| prefs | putBool | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:348](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L348) |
| prefs | putUChar | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:366](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L366) |
| prefs | putChar | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:384](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L384) |
| prefs | putUShort | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:402](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L402) |
| prefs | putUInt | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:420](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L420) |
| prefs | putFloat | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:438](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L438) |
| prefs | putString | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:458](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L458) |
| prefs | putBytes | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:480](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L480) |
| prefs | isKey | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:497](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L497) |
| prefs | remove | key | 待追踪 | [platform/esp/arduino_common/src/app_config_store.cpp:498](../../../../platform/esp/arduino_common/src/app_config_store.cpp#L498) |
| prefs | putBool | "enabled" | enabled | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:242](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L242) |
| prefs | putUChar | "mode" | mode | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:243](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L243) |
| prefs | putUInt | "pin" | pin | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:244](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L244) |
| prefs | putUInt | "pin" | pin | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:254](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L254) |
| prefs | putBool | "manual_add" | manual_add | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:255](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L255) |
| prefs | putUChar | "telem_base" | telem_base | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:256](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L256) |
| prefs | putUChar | "telem_loc" | telem_loc | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:257](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L257) |
| prefs | putUChar | "telem_env" | telem_env | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:258](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L258) |
| prefs | putUChar | "advert_loc" | advert_loc | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:259](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L259) |
| prefs | putBytes | kModuleBlobKey | 待追踪 | [platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp:269](../../../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp#L269) |
| prefs | getUInt | "pin" | pin | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1712](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1712) |
| prefs | getBool | "manual_add" | manual_add | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1713](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1713) |
| prefs | getUChar | "telem_base" | telem_base | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1714](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1714) |
| prefs | getUChar | "telem_loc" | telem_loc | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1715](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1715) |
| prefs | getUChar | "telem_env" | telem_env | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1716](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1716) |
| prefs | getUChar | "advert_loc" | advert_loc | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1717](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1717) |
| prefs | putUInt | "pin" | pin | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1732](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1732) |
| prefs | putBool | "manual_add" | manual_add | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1733](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1733) |
| prefs | putUChar | "telem_base" | telem_base | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1734](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1734) |
| prefs | putUChar | "telem_loc" | telem_loc | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1735](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1735) |
| prefs | putUChar | "telem_env" | telem_env | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1736](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1736) |
| prefs | putUChar | "advert_loc" | advert_loc | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1737](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1737) |
| prefs | getUChar | "ver" | ver | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1754](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1754) |
| prefs | getBytesLength | "blob" | blob | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1760](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1760) |
| prefs | getBytes | "blob" | blob | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1770](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1770) |
| prefs | getBytes | "blob" | blob | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1794](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1794) |
| prefs | putUChar | "ver" | ver | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1824](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1824) |
| prefs | remove | "blob" | blob | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1827](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1827) |
| prefs | putBytes | "blob" | blob | [platform/esp/arduino_common/src/ble/meshcore_ble.cpp:1831](../../../../platform/esp/arduino_common/src/ble/meshcore_ble.cpp#L1831) |
| prefs | getBool | kBleEnabledKey | 待追踪 | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:86](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L86) |
| prefs | isKey | kBleModeKey | 待追踪 | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:87](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L87) |
| prefs | getUChar | kBleModeKey | 待追踪 | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:89](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L89) |
| prefs | getUInt | kBlePinKey | 待追踪 | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:96](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L96) |
| prefs | putBool | kBleEnabledKey | 待追踪 | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:112](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L112) |
| prefs | putUChar | kBleModeKey | 待追踪 | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:113](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L113) |
| prefs | putUInt | kBlePinKey | 待追踪 | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:114](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L114) |
| prefs | getBytes | kModuleBlobKey | 待追踪 | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:171](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L171) |
| prefs | putBytes | kModuleBlobKey | 待追踪 | [platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp:192](../../../../platform/esp/arduino_common/src/ble/meshtastic_ble_owner_hooks.cpp#L192) |
| preferences | getBytes | kEncPubKey | 待追踪 | [platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp:196](../../../../platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp#L196) |
| preferences | getBytes | kEncPrivKey | 待追踪 | [platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp:197](../../../../platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp#L197) |
| preferences | getBytes | kSigPubKey | 待追踪 | [platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp:198](../../../../platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp#L198) |
| preferences | getBytes | kSigPrivKey | 待追踪 | [platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp:199](../../../../platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_identity.cpp#L199) |
| prefs | isKey | version_key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:43](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L43) |
| prefs | getUChar | version_key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:46](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L46) |
| prefs | isKey | crc_key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:48](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L48) |
| prefs | getUInt | crc_key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:51](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L51) |
| prefs | putUChar | version_key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:66](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L66) |
| prefs | remove | version_key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:70](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L70) |
| prefs | putUInt | crc_key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:78](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L78) |
| prefs | remove | crc_key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:82](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L82) |
| prefs | getBytesLength | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:198](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L198) |
| prefs | getBytes | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:206](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L206) |
| prefs | putBytes | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:232](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L232) |
| prefs | remove | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:236](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L236) |
| prefs | getBytesLength | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:270](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L270) |
| prefs | getBytes | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:276](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L276) |
| prefs | putBytes | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:317](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L317) |
| prefs | remove | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:320](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L320) |
| prefs | putBytes | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:321](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L321) |
| prefs | remove | key | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:328](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L328) |
| prefs | remove | key_a | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:354](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L354) |
| prefs | remove | key_b | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:358](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L358) |
| prefs | remove | key_c | 待追踪 | [platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp:362](../../../../platform/esp/arduino_common/src/chat/internal/blob_store_io.cpp#L362) |

## 整域清理

| 接收对象 | 来源 |
|---|---|
