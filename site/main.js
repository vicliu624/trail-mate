import {setLocale, getLocale, translate} from './i18n/runtime.js';
import {applyTranslations} from './i18n/dom.js';
import {locales} from './i18n/registry.js';
import {applySiteNavigation,installNavigationDismissal} from './navigation.js';
import {languageName,packName,packDescription,packCount,packStatus,memoryProfile} from './i18n/pack-presentation.js';

const releaseVersionEls = document.querySelectorAll("[data-release-version]");
const releaseLinkEls = document.querySelectorAll("[data-release-link]");
const boardCards = document.querySelectorAll("[data-board-id]");
const packGrid = document.querySelector("[data-pack-grid]");
const packCountEls = document.querySelectorAll("[data-pack-count]");
const localeCountEls = document.querySelectorAll("[data-locale-count]");
let releaseDataCache = null;
let packDataCache = null;

function t(key, params = {}) {
  return translate(key, params);
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
  applyTranslations();
  applySiteNavigation(document,getLocale(),new URL('./',location.href));
  document.title = t('document.title');
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

  const unavailableButton = createElement('button', 'flash-button', t('flasher.activate'));
  unavailableButton.disabled = true;
  installSlot.append(unavailableButton);
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

  const selector = document.querySelector('#install-target');
  if (selector) {
    const card = boardCards[0];
    card.dataset.boardId = selector.value;
    document.querySelector('#install-target-title').textContent = selector.selectedOptions[0].textContent;
    const manual = selector.value === 't-echo-lite';
    document.querySelector('#install-target-copy').textContent = t(manual?'install.manualInstructions':'install.variantInstructions');
    document.querySelector('#webflasher .workflow-note').hidden = manual;
    if (manual) card.querySelector('[data-install-slot]').replaceChildren();
    else renderBoardInstall(card, releaseDataCache);
  }
}

function localizedPackName(pack) {
  return packName(pack);
}

function localizedPackSummary(pack) {
  return packDescription(pack);
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
      localizedPackSummary(pack),
    ),
  );

  const meta = createElement("div", "language-pack-meta");
  meta.append(createPill(packCount('locales',locales.length)));
  meta.append(createPill(packCount('fonts',fontCount)));
  meta.append(createPill(packCount('ime',imeCount)));
  statuses.forEach((status) => {
    meta.append(createPill(packStatus(status)));
  });
  card.append(meta);

  if (locales.length > 0) {
    const localeRow = createElement("div", "language-pack-locales");
    locales.slice(0, 7).forEach((locale) => {
      const status = locale.translation_status || "release";
      const label = languageName(locale.id);
      localeRow.append(createPill(status === "release" ? label : `${label} (${packStatus(status)})`));
    });
    if (locales.length > 7) {
      localeRow.append(createPill(packCount('more',locales.length-7)));
    }
    card.append(localeRow);
  }

  if (Array.isArray(pack.supported_memory_profiles) && pack.supported_memory_profiles.length > 0) {
    const memoryRow = createElement("div", "language-pack-profiles");
    pack.supported_memory_profiles.forEach((profile) => {
      memoryRow.append(createPill(memoryProfile(profile)));
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
  const packs = (packDataCache?.packages ?? []).filter(pack=>pack.package_type==='locale-bundle');
  const localeTotal=packs.reduce((sum,pack)=>sum+(pack.runtime?.locale_count??pack.provides?.locales?.length??0),0);
  const summary=document.querySelector('[data-pack-summary]');
  if(summary)summary.textContent=`${packCount('packages',packs.length)} · ${packCount('locales',localeTotal)}`;
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

async function setLanguage(language) {
  await setLocale(language);
  const url=new URL(location.href);
  url.searchParams.set("lang",getLocale());
  history.replaceState(null,"",url);
  document.querySelector('#site-language').value=getLocale();
  applyStaticTranslations();
  renderReleaseData();
  renderPackCatalog();
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

const urlParams = new URLSearchParams(window.location.search);
installNavigationDismissal(document);
document.querySelector('#site-language').replaceChildren(...locales.map(locale=>{
  const option=document.createElement('option');
  option.value=locale.id;option.textContent=locale.name;
  return option;
}));
const urlLanguage = urlParams.get("lang");
// A plain site URL always starts in English. Explicit language links preserve
// the visitor's selection without carrying a stale preference into new visits.
await setLanguage(urlLanguage ?? 'en');
document.querySelector('#site-language').addEventListener('change',event=>setLanguage(event.target.value));
loadReleaseData();
loadPackCatalog();
document.querySelector('#install-target')?.addEventListener('change', renderReleaseData);
