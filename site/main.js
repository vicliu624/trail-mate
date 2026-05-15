const releaseVersionEls = document.querySelectorAll("[data-release-version]");
const releaseLinkEls = document.querySelectorAll("[data-release-link]");
const boardCards = document.querySelectorAll("[data-board-id]");
const packGrid = document.querySelector("[data-pack-grid]");
const packCountEls = document.querySelectorAll("[data-pack-count]");
const localeCountEls = document.querySelectorAll("[data-locale-count]");
const languageButtons = document.querySelectorAll("[data-lang-choice]");
const i18nEls = document.querySelectorAll("[data-i18n]");
const deviceNavButtons = document.querySelectorAll("[data-device-nav]");
const deviceTabButtons = document.querySelectorAll("[data-device-target]");
const deviceThumbGrid = document.querySelector("[data-device-thumbs]");
const deviceMainImage = document.querySelector("[data-device-main-image]");
const deviceMainCaption = document.querySelector("[data-device-main-caption]");
const deviceTitle = document.querySelector("[data-device-title]");
const deviceKicker = document.querySelector("[data-device-kicker]");
const deviceSummary = document.querySelector("[data-device-summary]");
const deviceStatus = document.querySelector("[data-device-status]");
const deviceInteractions = document.querySelector("[data-device-interactions]");

const I18N = {
  en: {
    "top.github": "GitHub",
    "top.wiki": "Wiki",
    "top.flash": "Flash",
    "side.label": "Product Map",
    "side.overview": "Overview",
    "side.capabilities": "Capabilities",
    "side.devices": "Devices",
    "side.features": "Feature Walkthrough",
    "side.languages": "Language Packs",
    "side.flasher": "Web Flasher",
    "side.docs": "Docs",
    "hero.eyebrow": "Offline Navigation + LoRa Communication",
    "hero.tagline": "Field firmware for offline maps, LoRa messaging, and team coordination.",
    "hero.lede":
      "A low-power firmware stack for outdoor navigation, GNSS diagnostics, LoRa chat, installable multilingual UI support, team coordination, and protocol interoperability on compact devices you can actually carry into the field.",
    "hero.primaryAction": "Open Web Flasher",
    "hero.secondaryAction": "View Repository",
    "hero.releaseKicker": "Latest Release",
    "hero.proofKicker": "Screenshot Policy",
    "hero.proofText":
      "Device pages only claim screenshots that belong to that device. Pager is complete today; other devices get their own lanes as captures arrive.",
    "overview.copy":
      "Trail Mate is a compact offline field stack: navigation, direct LoRa messaging, team coordination, RF diagnostics, utility tooling, and installable language packs shaped for embedded hardware with tight power and UI constraints.",
    "tag.maps": "Offline Maps",
    "tag.gnss": "GNSS Diagnostics",
    "tag.chat": "Mesh Chat",
    "tag.team": "Team Mode",
    "tag.languages": "Language Packs",
    "tag.scan": "Sub-GHz Scan",
    "tag.usb": "PC Link",
    "capabilities.eyebrow": "Capability Map",
    "capabilities.title": "The homepage now has room for the whole product, not just a few screenshots.",
    "capabilities.note":
      "These lanes make future growth explicit: each area can gain pages, captures, docs, release notes, and board-specific detail without overloading the top navigation.",
    "capabilities.deviceKicker": "Devices",
    "capabilities.deviceTitle": "Board-owned product lanes",
    "capabilities.deviceText":
      "Every board can carry its own screenshots, interaction notes, install state, and hardware caveats.",
    "capabilities.navigationKicker": "Navigation",
    "capabilities.navigationTitle": "Offline map workflows",
    "capabilities.navigationText":
      "Map layers, GNSS diagnostics, route review, and tracker views stay grouped as field navigation.",
    "capabilities.protocolKicker": "Protocols",
    "capabilities.protocolTitle": "Mesh and radio compatibility",
    "capabilities.protocolText":
      "Meshtastic, MeshCore, LoRa runtime work, RF diagnostics, and future protocol notes have a dedicated lane.",
    "capabilities.installKicker": "Install",
    "capabilities.installTitle": "Release-first flashing",
    "capabilities.installText":
      "Web flashing, manual downloads, OTA assets, and per-board availability can evolve together.",
    "capabilities.localeKicker": "Localization",
    "capabilities.localeTitle": "Installable language packs",
    "capabilities.localeText":
      "The site distinguishes homepage language switching from firmware language-pack releases.",
    "capabilities.fieldKicker": "Field Tools",
    "capabilities.fieldTitle": "Utilities beyond chat and maps",
    "capabilities.fieldText":
      "SSTV, Energy Sweep, USB data exchange, and diagnostics can become their own focused pages later.",
    "devices.eyebrow": "Device Library",
    "devices.title": "Each supported board gets its own product lane.",
    "devices.note":
      "The left navigation is ready for a growing hardware catalog. Select a device to see its release status, interaction notes, and only the screenshot set that belongs to that device.",
    "devices.interactionKicker": "Interaction Notes",
    "features.navEyebrow": "Navigation",
    "features.navTitle": "Offline mapping is a first-class workflow.",
    "features.navLead":
      "North-up rendering, SD card map tiles, terrain and satellite layers, sky plot, and fix diagnostics keep field navigation readable on constrained hardware.",
    "features.commEyebrow": "Communication",
    "features.commTitle": "Messaging and team coordination stay practical under low bandwidth.",
    "features.commLead":
      "Compose LoRa messages on-device, review contacts and activity, and keep small teams aligned without assuming a phone or cloud connection.",
    "features.toolsEyebrow": "Field Utilities",
    "features.toolsTitle": "The firmware expands into a toolkit.",
    "features.toolsLead":
      "Energy Sweep, SSTV receive, USB data exchange, and tracker views make the device useful beyond a single chat or map screen.",
    "languages.eyebrow": "Localization",
    "languages.title": "English stays built in; installable packs extend the interface.",
    "languages.note":
      "The homepage itself now switches between English and Chinese. On-device language packs remain release artifacts, with review status visible instead of hidden.",
    "languages.defaultKicker": "Built-In Default",
    "languages.defaultTitle": "English is always available.",
    "languages.defaultText":
      "The device can boot, recover, and stay usable even when no external language packs are installed.",
    "languages.catalogKicker": "Pack Catalog",
    "languages.bundleSuffix": "bundles covering",
    "languages.localeSuffix": "cataloged locales.",
    "languages.catalogText":
      "Locale, font, and IME resources are shipped as packages so firmware builds stay lean.",
    "languages.gateKicker": "Release Gate",
    "languages.gateTitle": "Review packs are visible but not treated as finished UI choices.",
    "languages.gateText":
      "Package metadata carries translation status, archive hashes, and package versions for every release.",
    "languages.loadingKicker": "Catalog",
    "languages.loadingTitle": "Loading package catalog...",
    "languages.loadingText": "Published locale bundles from the current Pages build will appear here.",
    "install.eyebrow": "Install",
    "install.title": "Web Flasher",
    "install.note":
      "Use Google Chrome or Microsoft Edge over HTTPS with a USB data cable. The browser installer currently targets the ESP32-S3 builds.",
    "install.pagerCopy": "SX1262 build for the keyboard pager hardware.",
    "install.tdeckCopy": "Keyboard + touch build tuned for the T-Deck layout.",
    "install.twatchCopy": "Touch-first watch build for compact field experiments.",
    "install.gatCopy":
      "Browser flashing is not wired up for the nRF52 target yet. Use the release package for manual flashing.",
    "install.gatHint": "Download the packaged firmware from the latest GitHub release.",
    "install.checking": "Checking release assets...",
    "install.openRelease": "Open latest release",
    "install.step1": "1. Connect the device with a USB data cable.",
    "install.step2": "2. Put the board into download mode if your hardware requires it.",
    "install.step3": "3. Choose the correct target and let the browser flash the merged image.",
    "docs.eyebrow": "Read More",
    "docs.title": "Long-form project material belongs in the wiki.",
    "docs.copy":
      "The homepage is now a product surface and device library. The wiki carries architecture, supported hardware, flashing, configuration, protocol boundaries, troubleshooting, and development notes.",
    "docs.wiki": "Wiki Home",
    "docs.hardware": "Supported Hardware",
    "footer.eyebrow": "Trail Mate Field Notes",
    "footer.title": "Built for the quiet work between signal and terrain.",
    "footer.copy":
      "Follow the repository for releases, device captures, hardware notes, and language-pack updates as the project grows beyond a single board.",
    "footer.repo": "Repository",
    "footer.releases": "Releases",
    "footer.wiki": "Wiki",
    "release.waiting": "Waiting for first tagged release",
    "release.ready": "{tag} ready for install",
    "release.unavailable": "Release metadata unavailable",
    "flasher.activate": "Flash In Browser",
    "flasher.unsupported": "Use Chrome or Edge on desktop with Web Serial enabled.",
    "flasher.notAllowed": "Open this page over HTTPS to use the web flasher.",
    "flasher.readyNote": "Merged firmware image from the latest published release.",
    "flasher.missingAsset":
      "This board is missing web flasher assets in the latest release. Use the manual download for now.",
    "flasher.pending": "Web flasher assets will appear here after the first tagged release is published.",
    "flasher.error":
      "Could not load release metadata. Open the GitHub release page for manual downloads.",
    "pack.release": "Release Bundle",
    "pack.review": "Review Bundle",
    "pack.locale": "locale",
    "pack.locales": "locales",
    "pack.font": "font pack",
    "pack.fonts": "font packs",
    "pack.ime": "IME",
    "pack.imes": "IMEs",
    "pack.download": "Download package",
    "pack.emptyTitle": "No published language bundles yet.",
    "pack.emptyText": "Pages will show locale bundles here after the package catalog is generated.",
    "pack.errorTitle": "Language catalog unavailable",
    "pack.errorText": "The homepage could not load the published language-pack metadata right now.",
    "device.emptyTitle": "Dedicated screenshot lane reserved.",
    "device.emptyText":
      "This device has a product entry and release path, but its own screen capture set has not been added yet.",
  },
  zh: {
    "top.github": "GitHub",
    "top.wiki": "维基",
    "top.flash": "刷机",
    "side.label": "产品地图",
    "side.overview": "概览",
    "side.capabilities": "能力地图",
    "side.devices": "设备",
    "side.features": "功能走查",
    "side.languages": "语言包",
    "side.flasher": "网页刷机",
    "side.docs": "文档",
    "hero.eyebrow": "离线导航 + LoRa 通信",
    "hero.tagline": "面向离线地图、LoRa 消息和团队协作的户外固件。",
    "hero.lede":
      "Trail Mate 是一个低功耗手持固件栈，覆盖户外导航、GNSS 诊断、LoRa 聊天、可安装多语言 UI、团队协作和多协议互通，目标是能真正带到现场使用的小型设备。",
    "hero.primaryAction": "打开网页刷机",
    "hero.secondaryAction": "查看仓库",
    "hero.releaseKicker": "最新发布",
    "hero.proofKicker": "截图规则",
    "hero.proofText":
      "设备页只展示属于该设备的截图。当前 Pager 截图集较完整，其他设备先保留独立入口，后续补充各自截图。",
    "overview.copy":
      "Trail Mate 是一个紧凑的离线现场工具栈：导航、LoRa 直连消息、团队协作、射频诊断、实用工具，以及面向小内存硬件的可安装语言包。",
    "tag.maps": "离线地图",
    "tag.gnss": "GNSS 诊断",
    "tag.chat": "网格聊天",
    "tag.team": "团队模式",
    "tag.languages": "语言包",
    "tag.scan": "Sub-GHz 扫描",
    "tag.usb": "PC 连接",
    "capabilities.eyebrow": "能力地图",
    "capabilities.title": "官网现在可以承载整个产品，而不只是几张截图。",
    "capabilities.note":
      "这些板块把未来增长显式分开：每个区域都能继续加入页面、截图、文档、发布说明和板卡细节，而不挤爆顶部导航。",
    "capabilities.deviceKicker": "设备",
    "capabilities.deviceTitle": "板卡自己的产品入口",
    "capabilities.deviceText": "每个板卡都可以拥有自己的截图、交互说明、安装状态和硬件注意事项。",
    "capabilities.navigationKicker": "导航",
    "capabilities.navigationTitle": "离线地图工作流",
    "capabilities.navigationText": "地图图层、GNSS 诊断、路线回看和轨迹视图都归入现场导航。",
    "capabilities.protocolKicker": "协议",
    "capabilities.protocolTitle": "网格与电台兼容",
    "capabilities.protocolText": "Meshtastic、MeshCore、LoRa 运行时、射频诊断和未来协议说明都有独立入口。",
    "capabilities.installKicker": "安装",
    "capabilities.installTitle": "围绕发布产物的刷机",
    "capabilities.installText": "网页刷机、手动下载、OTA 资产和每个板卡的可用状态可以一起演进。",
    "capabilities.localeKicker": "本地化",
    "capabilities.localeTitle": "可安装语言包",
    "capabilities.localeText": "官网语言切换和设备端固件语言包发布被明确区分。",
    "capabilities.fieldKicker": "现场工具",
    "capabilities.fieldTitle": "聊天和地图之外的工具",
    "capabilities.fieldText": "SSTV、Energy Sweep、USB 数据交换和诊断后续都可以成为独立页面。",
    "devices.eyebrow": "设备库",
    "devices.title": "每个受支持的板卡都有自己的产品入口。",
    "devices.note":
      "左侧导航已经按长期硬件目录设计。选择设备后，可以看到发布状态、交互说明，以及只属于该设备的截图集。",
    "devices.interactionKicker": "交互说明",
    "features.navEyebrow": "导航",
    "features.navTitle": "离线地图是一条主工作流。",
    "features.navLead":
      "北向地图、SD 卡瓦片、地形与卫星图层、天空图和定位质量诊断，让受限硬件上的现场导航仍然清晰可读。",
    "features.commEyebrow": "通信",
    "features.commTitle": "消息和团队协作在低带宽下仍保持实用。",
    "features.commLead":
      "设备端直接编写 LoRa 消息、查看联系人与活动记录，并在不依赖手机或云端的情况下保持小队同步。",
    "features.toolsEyebrow": "现场工具",
    "features.toolsTitle": "固件正在扩展成一套工具箱。",
    "features.toolsLead":
      "Energy Sweep、SSTV 接收、USB 数据交换和轨迹查看，让设备不只是聊天或地图单页。",
    "languages.eyebrow": "本地化",
    "languages.title": "英文内置可用，安装式语言包扩展界面。",
    "languages.note":
      "官网现在支持英文和中文切换。设备端语言包仍作为发布产物管理，复核状态会明确展示。",
    "languages.defaultKicker": "内置默认",
    "languages.defaultTitle": "英文始终可用。",
    "languages.defaultText": "即使没有安装外部语言包，设备也可以启动、恢复并保持可用。",
    "languages.catalogKicker": "语言包目录",
    "languages.bundleSuffix": "个包，覆盖",
    "languages.localeSuffix": "个目录语言。",
    "languages.catalogText": "语言、字体和输入法资源以包形式发布，让固件镜像保持精简。",
    "languages.gateKicker": "发布门禁",
    "languages.gateTitle": "复核语言包可见，但不会被假装成已完成的 UI 选择。",
    "languages.gateText": "包元数据会记录翻译状态、归档哈希和版本信息。",
    "languages.loadingKicker": "目录",
    "languages.loadingTitle": "正在加载语言包目录...",
    "languages.loadingText": "当前 Pages 构建发布的语言包会显示在这里。",
    "install.eyebrow": "安装",
    "install.title": "网页刷机",
    "install.note": "请使用 Google Chrome 或 Microsoft Edge，通过 HTTPS 和 USB 数据线刷写。目前网页刷机目标是 ESP32-S3 构建。",
    "install.pagerCopy": "面向键盘 Pager 硬件的 SX1262 构建。",
    "install.tdeckCopy": "面向 T-Deck 键盘 + 触屏布局调校的构建。",
    "install.twatchCopy": "面向紧凑触屏手表实验的构建。",
    "install.gatCopy": "nRF52 目标暂未接入浏览器刷机，请使用发布包手动刷写。",
    "install.gatHint": "从最新 GitHub Release 下载打包固件。",
    "install.checking": "正在检查发布产物...",
    "install.openRelease": "打开最新发布",
    "install.step1": "1. 使用 USB 数据线连接设备。",
    "install.step2": "2. 如果硬件需要，请先进入下载模式。",
    "install.step3": "3. 选择正确目标，让浏览器刷写合并镜像。",
    "docs.eyebrow": "继续阅读",
    "docs.title": "长篇项目资料放在维基中。",
    "docs.copy":
      "官网现在作为产品展示面和设备库。架构、支持硬件、刷机、配置、协议边界、排障和开发笔记由维基承载。",
    "docs.wiki": "维基首页",
    "docs.hardware": "支持硬件",
    "footer.eyebrow": "Trail Mate 现场笔记",
    "footer.title": "为信号与地形之间那些安静但重要的工作而做。",
    "footer.copy":
      "关注仓库即可获取发布、设备截图、硬件说明和语言包更新。这个项目会继续超出单一板卡。",
    "footer.repo": "仓库",
    "footer.releases": "发布",
    "footer.wiki": "维基",
    "release.waiting": "等待首个带标签发布",
    "release.ready": "{tag} 可安装",
    "release.unavailable": "无法加载发布信息",
    "flasher.activate": "在浏览器中刷机",
    "flasher.unsupported": "请使用支持 Web Serial 的桌面版 Chrome 或 Edge。",
    "flasher.notAllowed": "请通过 HTTPS 打开页面以使用网页刷机。",
    "flasher.readyNote": "来自最新发布的合并固件镜像。",
    "flasher.missingAsset": "最新发布中缺少此板卡的网页刷机产物，请暂时手动下载。",
    "flasher.pending": "首个带标签发布完成后，网页刷机产物会显示在这里。",
    "flasher.error": "无法加载发布信息。请打开 GitHub Release 页面手动下载。",
    "pack.release": "正式包",
    "pack.review": "复核包",
    "pack.locale": "个语言",
    "pack.locales": "个语言",
    "pack.font": "个字体包",
    "pack.fonts": "个字体包",
    "pack.ime": "个输入法",
    "pack.imes": "个输入法",
    "pack.download": "下载语言包",
    "pack.emptyTitle": "还没有发布语言包。",
    "pack.emptyText": "包目录生成后，Pages 会在这里展示语言包。",
    "pack.errorTitle": "语言包目录不可用",
    "pack.errorText": "官网当前无法加载已发布的语言包元数据。",
    "device.emptyTitle": "已预留专属截图位。",
    "device.emptyText": "该设备已有产品入口和发布路径，但还没有加入属于它自己的屏幕截图集。",
  },
};

const PACK_NAME_ZH = {
  ar: "阿拉伯语",
  "europe-cyrillic-ext": "欧洲西里尔扩展",
  "europe-latin-ext": "欧洲拉丁扩展",
  ja: "日语",
  ko: "韩语",
  "zh-Hans": "简体中文",
  "zh-Hant": "繁体中文（台湾）",
};

const DEVICES = [
  {
    id: "tlora-pager-sx1262",
    chip: "ESP32-S3 + SX1262",
    title: {
      en: "LilyGo T-LoRa Pager",
      zh: "LilyGo T-LoRa Pager",
    },
    status: {
      en: "Complete screenshot set",
      zh: "完整截图集",
    },
    summary: {
      en: "Keyboard pager target with the most complete captured UI surface today.",
      zh: "当前截图素材最完整的键盘 Pager 目标。",
    },
    interactions: {
      en: [
        "Use the thumbnail strip to switch between the captured Pager workflows.",
        "The gallery keeps every screenshot in the same 480:222 screen frame so image sizes stay calm.",
        "Navigation, chat, team mode, and utility screens are separated as device-owned captures.",
      ],
      zh: [
        "点击缩略图可以切换 Pager 已捕获的工作流。",
        "图库统一使用 480:222 屏幕框，避免截图大小忽大忽小。",
        "导航、聊天、团队模式和工具页都作为该设备自己的截图展示。",
      ],
    },
    screenshots: [
      {
        src: "./assets/showcase/home-main.png",
        title: { en: "Home launcher", zh: "主菜单" },
        alt: { en: "T-LoRa Pager home launcher", zh: "T-LoRa Pager 主菜单" },
      },
      {
        src: "./assets/showcase/nav-map-osm.png",
        title: { en: "Offline OSM map", zh: "离线 OSM 地图" },
        alt: { en: "T-LoRa Pager OSM map screen", zh: "T-LoRa Pager OSM 地图界面" },
      },
      {
        src: "./assets/showcase/nav-map-terrain.png",
        title: { en: "Terrain map", zh: "地形地图" },
        alt: { en: "T-LoRa Pager terrain map screen", zh: "T-LoRa Pager 地形地图界面" },
      },
      {
        src: "./assets/showcase/nav-skyplot.png",
        title: { en: "GNSS sky plot", zh: "GNSS 天空图" },
        alt: { en: "T-LoRa Pager GNSS sky plot screen", zh: "T-LoRa Pager GNSS 天空图界面" },
      },
      {
        src: "./assets/showcase/chat-compose.png",
        title: { en: "Message compose", zh: "消息编写" },
        alt: { en: "T-LoRa Pager message compose screen", zh: "T-LoRa Pager 消息编写界面" },
      },
      {
        src: "./assets/showcase/chat-messages.png",
        title: { en: "Message history", zh: "消息记录" },
        alt: { en: "T-LoRa Pager message history screen", zh: "T-LoRa Pager 消息记录界面" },
      },
      {
        src: "./assets/showcase/team-map.png",
        title: { en: "Team map", zh: "团队地图" },
        alt: { en: "T-LoRa Pager team map screen", zh: "T-LoRa Pager 团队地图界面" },
      },
      {
        src: "./assets/showcase/utility-spectrum.png",
        title: { en: "Energy Sweep", zh: "频谱扫描" },
        alt: { en: "T-LoRa Pager Energy Sweep screen", zh: "T-LoRa Pager 频谱扫描界面" },
      },
      {
        src: "./assets/showcase/utility-tracker.png",
        title: { en: "Tracker", zh: "轨迹" },
        alt: { en: "T-LoRa Pager tracker screen", zh: "T-LoRa Pager 轨迹界面" },
      },
    ],
  },
  {
    id: "tdeck",
    chip: "ESP32-S3",
    title: {
      en: "LilyGo T-Deck",
      zh: "LilyGo T-Deck",
    },
    status: {
      en: "Capture lane ready",
      zh: "截图入口已预留",
    },
    summary: {
      en: "Keyboard + touch target. Release and flasher path are present; dedicated UI captures should be added separately.",
      zh: "键盘 + 触屏目标。发布和刷机路径已存在，后续应补充独立 UI 截图。",
    },
    interactions: {
      en: [
        "Keep T-Deck screenshots separate from Pager captures so the hardware story stays honest.",
        "Future captures can cover keyboard navigation, touch gestures, GPS setup, and settings flows.",
        "The device submenu is already stable, so new screenshots can drop into this lane without changing the site shell.",
      ],
      zh: [
        "T-Deck 截图会与 Pager 截图区分，避免硬件展示混淆。",
        "后续可以补充键盘导航、触控手势、GPS 设置和系统设置流程。",
        "设备子菜单已经稳定，新增截图时无需重做官网外壳。",
      ],
    },
    screenshots: [],
  },
  {
    id: "lilygo-twatch-s3",
    chip: "ESP32-S3",
    title: {
      en: "LilyGo T-Watch-S3",
      zh: "LilyGo T-Watch-S3",
    },
    status: {
      en: "Capture lane ready",
      zh: "截图入口已预留",
    },
    summary: {
      en: "Touch-first watch target. It needs its own compact-screen capture set rather than reused pager imagery.",
      zh: "触控优先的手表目标，需要专门的小屏截图集，而不是复用 Pager 截图。",
    },
    interactions: {
      en: [
        "This lane is reserved for watch-scale UI density, touch targets, and compact navigation patterns.",
        "Future screenshots should use the watch build so sizing and interaction claims match the device.",
        "The install card below still exposes the ESP32-S3 web flasher status when release assets are available.",
      ],
      zh: [
        "此入口预留给手表尺寸 UI 密度、触控目标和紧凑导航模式。",
        "未来截图应来自手表构建，确保尺寸和交互描述匹配设备。",
        "下方安装卡片仍会根据发布产物展示 ESP32-S3 网页刷机状态。",
      ],
    },
    screenshots: [],
  },
  {
    id: "gat562-mesh-evb-pro",
    chip: "nRF52",
    title: {
      en: "GAT562 Mesh EVB Pro",
      zh: "GAT562 Mesh EVB Pro",
    },
    status: {
      en: "Manual flashing",
      zh: "手动刷写",
    },
    summary: {
      en: "nRF52 node target. Browser flashing is not wired yet, and UI screenshots should be captured from that target when available.",
      zh: "nRF52 节点目标。浏览器刷机暂未接入，后续应从该目标独立捕获 UI 截图。",
    },
    interactions: {
      en: [
        "Treat this device as a separate product path because its runtime and flashing model differ from ESP32-S3 boards.",
        "Use this lane later for node status, mesh identity, diagnostics, and provisioning screens.",
        "The current homepage intentionally avoids showing Pager images here.",
      ],
      zh: [
        "该设备的运行时和刷写模型不同于 ESP32-S3 板卡，应作为独立产品路径处理。",
        "后续可在这里展示节点状态、Mesh 身份、诊断和配网页面。",
        "当前官网刻意不在这里展示 Pager 截图。",
      ],
    },
    screenshots: [],
  },
];

let currentLanguage = "en";
let releaseDataCache = null;
let packDataCache = null;
let activeDeviceId = "tlora-pager-sx1262";
let activeShotIndex = 0;

function t(key, params = {}) {
  const template = I18N[currentLanguage]?.[key] ?? I18N.en[key] ?? key;
  return Object.entries(params).reduce(
    (text, [name, value]) => text.replaceAll(`{${name}}`, String(value)),
    template,
  );
}

function localize(value) {
  if (typeof value === "string") {
    return value;
  }
  return value?.[currentLanguage] ?? value?.en ?? "";
}

function createElement(tagName, className, text) {
  const element = document.createElement(tagName);
  if (className) {
    element.className = className;
  }
  if (typeof text === "string") {
    element.textContent = text;
  }
  return element;
}

function createPill(text) {
  return createElement("span", "language-pill", text);
}

function setStat(elements, value) {
  elements.forEach((element) => {
    element.textContent = String(value);
  });
}

function setReleaseVersion(text) {
  releaseVersionEls.forEach((element) => {
    element.textContent = text;
  });
}

function setReleaseLinks(url) {
  releaseLinkEls.forEach((element) => {
    element.href = url;
  });
}

function applyStaticTranslations() {
  i18nEls.forEach((element) => {
    element.textContent = t(element.dataset.i18n);
  });

  document.documentElement.lang = currentLanguage === "zh" ? "zh-CN" : "en";
  document.title = currentLanguage === "zh" ? "Trail Mate - 离线现场通信固件" : "Trail Mate";
}

function updateLanguageButtons() {
  languageButtons.forEach((button) => {
    const isActive = button.dataset.langChoice === currentLanguage;
    button.classList.toggle("is-active", isActive);
    button.setAttribute("aria-pressed", String(isActive));
  });
}

function createInstallButton(manifestPath) {
  const wrapper = document.createElement("div");
  wrapper.className = "install-button-wrap";

  const button = document.createElement("esp-web-install-button");
  button.setAttribute("manifest", `./${manifestPath}`);
  button.innerHTML = `
    <button slot="activate" class="flash-button">${t("flasher.activate")}</button>
    <span slot="unsupported" class="support-note">${t("flasher.unsupported")}</span>
    <span slot="not-allowed" class="support-note">${t("flasher.notAllowed")}</span>
  `;

  wrapper.append(button);
  return wrapper;
}

function renderBoardInstall(card, releaseData) {
  const installSlot = card.querySelector("[data-install-slot]");
  if (!installSlot) {
    return;
  }

  const boardId = card.dataset.boardId;
  const target = releaseData.targets?.[boardId];

  installSlot.replaceChildren();

  if (target?.available) {
    installSlot.append(createInstallButton(target.manifest_path));
    installSlot.append(createElement("p", "board-hint", t("flasher.readyNote")));
    return;
  }

  installSlot.append(
    createElement(
      "p",
      "board-hint",
      releaseData.available ? t("flasher.missingAsset") : t("flasher.pending"),
    ),
  );
}

function renderReleaseData() {
  if (!releaseDataCache) {
    setReleaseVersion(t("release.unavailable"));
    boardCards.forEach((card) => {
      const installSlot = card.querySelector("[data-install-slot]");
      if (installSlot) {
        installSlot.replaceChildren(createElement("p", "board-hint", t("flasher.error")));
      }
    });
    return;
  }

  const versionLabel = releaseDataCache.tag_name
    ? t("release.ready", { tag: releaseDataCache.tag_name })
    : t("release.waiting");

  setReleaseVersion(versionLabel);
  if (releaseDataCache.release_url) {
    setReleaseLinks(releaseDataCache.release_url);
  }

  boardCards.forEach((card) => renderBoardInstall(card, releaseDataCache));
}

function localizedPackName(pack) {
  if (currentLanguage === "zh") {
    return PACK_NAME_ZH[pack.id] ?? pack.display_name ?? pack.id ?? "语言包";
  }
  return pack.display_name || pack.id || "Unnamed package";
}

function localizedPackSummary(pack, locales, fontCount, imeCount) {
  if (currentLanguage === "zh") {
    return `${localizedPackName(pack)}语言包，包含 ${locales.length} ${t(locales.length === 1 ? "pack.locale" : "pack.locales")}、${fontCount} ${t(fontCount === 1 ? "pack.font" : "pack.fonts")}、${imeCount} ${t(imeCount === 1 ? "pack.ime" : "pack.imes")}。`;
  }
  return pack.summary || "No summary available.";
}

function createPackCard(pack) {
  const locales = pack.provides?.locales ?? [];
  const runtime = pack.runtime ?? {};
  const fontCount = runtime.font_count ?? (pack.provides?.fonts?.length ?? 0);
  const imeCount = runtime.ime_count ?? (pack.provides?.ime?.length ?? 0);
  const statuses = Array.from(
    new Set(locales.map((locale) => locale.translation_status || "release").filter(Boolean)),
  );
  const hasRelease = statuses.includes("release");

  const card = createElement("article", "language-pack-card");
  card.append(createElement("p", "feature-kicker", t(hasRelease ? "pack.release" : "pack.review")));
  card.append(createElement("h3", "", localizedPackName(pack)));
  card.append(
    createElement(
      "p",
      "language-pack-summary",
      localizedPackSummary(pack, locales, fontCount, imeCount),
    ),
  );

  const meta = createElement("div", "language-pack-meta");
  meta.append(createPill(`${locales.length} ${t(locales.length === 1 ? "pack.locale" : "pack.locales")}`));
  meta.append(createPill(`${fontCount} ${t(fontCount === 1 ? "pack.font" : "pack.fonts")}`));
  meta.append(createPill(`${imeCount} ${t(imeCount === 1 ? "pack.ime" : "pack.imes")}`));
  statuses.forEach((status) => {
    meta.append(createPill(status));
  });
  card.append(meta);

  if (locales.length > 0) {
    const localeRow = createElement("div", "language-pack-locales");
    locales.slice(0, 7).forEach((locale) => {
      const status = locale.translation_status || "release";
      const label = currentLanguage === "zh" ? PACK_NAME_ZH[pack.id] ?? locale.display_name : locale.display_name;
      localeRow.append(createPill(status === "release" ? label : `${label} (${status})`));
    });
    if (locales.length > 7) {
      localeRow.append(createPill(`+${locales.length - 7} more`));
    }
    card.append(localeRow);
  }

  if (Array.isArray(pack.supported_memory_profiles) && pack.supported_memory_profiles.length > 0) {
    const memoryRow = createElement("div", "language-pack-profiles");
    pack.supported_memory_profiles.forEach((profile) => {
      memoryRow.append(createPill(profile));
    });
    card.append(memoryRow);
  }

  if (pack.archive?.path) {
    const link = createElement("a", "release-link", t("pack.download"));
    link.href = `./${pack.archive.path}`;
    card.append(link);
  }

  return card;
}

function renderPackCatalog() {
  if (!packGrid) {
    return;
  }

  if (!packDataCache) {
    setStat(packCountEls, 0);
    setStat(localeCountEls, 0);
    packGrid.replaceChildren();

    const fallback = createElement("article", "language-pack-card language-pack-empty");
    fallback.append(createElement("p", "feature-kicker", t("languages.loadingKicker")));
    fallback.append(createElement("h3", "", t("pack.errorTitle")));
    fallback.append(createElement("p", "language-pack-summary", t("pack.errorText")));
    packGrid.append(fallback);
    return;
  }

  const packs = (packDataCache.packages ?? []).filter((pack) => pack.package_type === "locale-bundle");
  const totalLocales = packs.reduce((sum, pack) => {
    const runtimeLocales = pack.runtime?.locale_count;
    if (typeof runtimeLocales === "number") {
      return sum + runtimeLocales;
    }
    return sum + (pack.provides?.locales?.length ?? 0);
  }, 0);

  setStat(packCountEls, packs.length);
  setStat(localeCountEls, totalLocales);
  packGrid.replaceChildren();

  if (packs.length === 0) {
    const emptyCard = createElement("article", "language-pack-card language-pack-empty");
    emptyCard.append(createElement("p", "feature-kicker", t("languages.loadingKicker")));
    emptyCard.append(createElement("h3", "", t("pack.emptyTitle")));
    emptyCard.append(createElement("p", "language-pack-summary", t("pack.emptyText")));
    packGrid.append(emptyCard);
    return;
  }

  packs.forEach((pack) => {
    packGrid.append(createPackCard(pack));
  });
}

function findDevice(deviceId) {
  return DEVICES.find((device) => device.id === deviceId) ?? DEVICES[0];
}

function updateDeviceActiveStates() {
  deviceNavButtons.forEach((button) => {
    button.classList.toggle("is-active", button.dataset.deviceNav === activeDeviceId);
  });
  deviceTabButtons.forEach((button) => {
    button.classList.toggle("is-active", button.dataset.deviceTarget === activeDeviceId);
  });
}

function renderDeviceShot(device, index) {
  const shot = device.screenshots[index];
  if (!shot) {
    deviceMainImage.src = "./assets/showcase/brand-logo.png";
    deviceMainImage.alt = localize(device.title);
    deviceMainCaption.textContent = t("device.emptyTitle");
    return;
  }

  deviceMainImage.src = shot.src;
  deviceMainImage.alt = localize(shot.alt);
  deviceMainCaption.textContent = localize(shot.title);
}

function renderDevice() {
  const device = findDevice(activeDeviceId);
  activeShotIndex = Math.min(activeShotIndex, Math.max(device.screenshots.length - 1, 0));

  deviceKicker.textContent = device.chip;
  deviceTitle.textContent = localize(device.title);
  deviceSummary.textContent = localize(device.summary);
  deviceStatus.textContent = localize(device.status);

  deviceInteractions.replaceChildren();
  localize(device.interactions).forEach((item) => {
    deviceInteractions.append(createElement("li", "", item));
  });

  renderDeviceShot(device, activeShotIndex);
  deviceThumbGrid.replaceChildren();

  if (device.screenshots.length === 0) {
    const empty = createElement("div", "device-empty");
    empty.append(createElement("h4", "", t("device.emptyTitle")));
    empty.append(createElement("p", "", t("device.emptyText")));
    deviceThumbGrid.append(empty);
    return;
  }

  device.screenshots.forEach((shot, index) => {
    const button = document.createElement("button");
    button.className = `device-thumb${index === activeShotIndex ? " is-active" : ""}`;
    button.type = "button";
    button.dataset.shotIndex = String(index);

    const image = document.createElement("img");
    image.src = shot.src;
    image.alt = localize(shot.alt);
    image.loading = "lazy";

    button.append(image, createElement("span", "", localize(shot.title)));
    deviceThumbGrid.append(button);
  });
}

function selectDevice(deviceId, shouldScroll = false) {
  activeDeviceId = deviceId;
  activeShotIndex = 0;
  updateDeviceActiveStates();
  renderDevice();

  if (shouldScroll) {
    document.querySelector("#devices")?.scrollIntoView({ behavior: "smooth", block: "start" });
  }
}

function setLanguage(language) {
  currentLanguage = language === "zh" ? "zh" : "en";
  localStorage.setItem("trail-mate-language", currentLanguage);
  updateLanguageButtons();
  applyStaticTranslations();
  renderReleaseData();
  renderPackCatalog();
  renderDevice();
}

async function loadReleaseData() {
  try {
    const response = await fetch("./data/latest-release.json", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Unexpected status ${response.status}`);
    }

    releaseDataCache = await response.json();
    renderReleaseData();
  } catch (error) {
    releaseDataCache = null;
    renderReleaseData();
  }
}

async function loadPackCatalog() {
  if (!packGrid) {
    return;
  }

  try {
    const response = await fetch("./data/packs.json", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Unexpected status ${response.status}`);
    }

    packDataCache = await response.json();
    renderPackCatalog();
  } catch (error) {
    packDataCache = null;
    renderPackCatalog();
  }
}

languageButtons.forEach((button) => {
  button.addEventListener("click", () => {
    setLanguage(button.dataset.langChoice);
  });
});

deviceNavButtons.forEach((button) => {
  button.addEventListener("click", () => {
    selectDevice(button.dataset.deviceNav, true);
  });
});

deviceTabButtons.forEach((button) => {
  button.addEventListener("click", () => {
    selectDevice(button.dataset.deviceTarget, false);
  });
});

deviceThumbGrid?.addEventListener("click", (event) => {
  const thumb = event.target.closest("[data-shot-index]");
  if (!thumb) {
    return;
  }

  const device = findDevice(activeDeviceId);
  activeShotIndex = Number(thumb.dataset.shotIndex);
  renderDeviceShot(device, activeShotIndex);
  deviceThumbGrid.querySelectorAll(".device-thumb").forEach((button) => {
    button.classList.toggle("is-active", button === thumb);
  });
});

const urlParams = new URLSearchParams(window.location.search);
const urlLanguage = urlParams.get("lang");
const urlDevice = urlParams.get("device");
const savedLanguage = localStorage.getItem("trail-mate-language");
if (DEVICES.some((device) => device.id === urlDevice)) {
  activeDeviceId = urlDevice;
}
setLanguage(urlLanguage === "zh" || savedLanguage === "zh" ? "zh" : "en");
selectDevice(activeDeviceId, false);
loadReleaseData();
loadPackCatalog();
