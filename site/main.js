const releaseVersionEls = document.querySelectorAll("[data-release-version]");
const releaseLinkEls = document.querySelectorAll("[data-release-link]");
const boardCards = document.querySelectorAll("[data-board-id]");
const packGrid = document.querySelector("[data-pack-grid]");
const packCountEls = document.querySelectorAll("[data-pack-count]");
const localeCountEls = document.querySelectorAll("[data-locale-count]");

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

function setStat(elements, value) {
  elements.forEach((element) => {
    element.textContent = String(value);
  });
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

function createInstallButton(manifestPath) {
  const wrapper = document.createElement("div");
  wrapper.className = "install-button-wrap";

  const button = document.createElement("esp-web-install-button");
  button.setAttribute("manifest", `./${manifestPath}`);
  button.innerHTML = `
    <button slot="activate" class="flash-button">Flash In Browser</button>
    <span slot="unsupported" class="support-note">Use Chrome or Edge on desktop with Web Serial enabled.</span>
    <span slot="not-allowed" class="support-note">Open this page over HTTPS to use the web flasher.</span>
  `;

  wrapper.append(button);
  return wrapper;
}

function renderBoardInstall(card, releaseData) {
  const installSlot = card.querySelector("[data-install-slot]");
  const boardId = card.dataset.boardId;
  const target = releaseData.targets?.[boardId];

  installSlot.replaceChildren();

  if (target?.available) {
    installSlot.append(createInstallButton(target.manifest_path));
    const note = document.createElement("p");
    note.className = "board-hint";
    note.textContent = "Merged firmware image from the latest published release.";
    installSlot.append(note);
    return;
  }

  const fallback = document.createElement("p");
  fallback.className = "board-hint";
  fallback.textContent = releaseData.available
    ? "This board is missing web flasher assets in the latest release. Use the manual download for now."
    : "Web flasher assets will appear here after the first tagged release is published.";
  installSlot.append(fallback);
}

function createPackCard(pack) {
  const locales = pack.provides?.locales ?? [];
  const runtime = pack.runtime ?? {};
  const fontCount = runtime.font_count ?? (pack.provides?.fonts?.length ?? 0);
  const imeCount = runtime.ime_count ?? (pack.provides?.ime?.length ?? 0);

  const card = createElement("article", "language-pack-card");
  card.append(createElement("p", "feature-kicker", "Locale Bundle"));
  card.append(createElement("h3", "", pack.display_name || pack.id || "Unnamed package"));
  card.append(createElement("p", "language-pack-summary", pack.summary || "No summary available."));

  const meta = createElement("div", "language-pack-meta");
  meta.append(createPill(`${locales.length} locale${locales.length === 1 ? "" : "s"}`));
  meta.append(createPill(`${fontCount} font pack${fontCount === 1 ? "" : "s"}`));
  meta.append(createPill(`${imeCount} IME${imeCount === 1 ? "" : "s"}`));
  card.append(meta);

  if (locales.length > 0) {
    const localeRow = createElement("div", "language-pack-locales");
    locales.slice(0, 7).forEach((locale) => {
      localeRow.append(createPill(locale.display_name || locale.id || "Locale"));
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
    const link = createElement("a", "release-link", "Download package");
    link.href = `./${pack.archive.path}`;
    card.append(link);
  }

  return card;
}

async function loadReleaseData() {
  try {
    const response = await fetch("./data/latest-release.json", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Unexpected status ${response.status}`);
    }

    const releaseData = await response.json();
    const versionLabel = releaseData.tag_name
      ? `${releaseData.tag_name} ready for install`
      : "Waiting for first tagged release";

    setReleaseVersion(versionLabel);
    if (releaseData.release_url) {
      setReleaseLinks(releaseData.release_url);
    }

    boardCards.forEach((card) => renderBoardInstall(card, releaseData));
  } catch (error) {
    setReleaseVersion("Release metadata unavailable");
    boardCards.forEach((card) => {
      const installSlot = card.querySelector("[data-install-slot]");
      installSlot.replaceChildren();
      const note = document.createElement("p");
      note.className = "board-hint";
      note.textContent = "Could not load release metadata. Open the GitHub release page for manual downloads.";
      installSlot.append(note);
    });
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

    const packData = await response.json();
    const packs = (packData.packages ?? []).filter((pack) => pack.package_type === "locale-bundle");
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
      emptyCard.append(createElement("p", "feature-kicker", "Catalog"));
      emptyCard.append(createElement("h3", "", "No published language bundles yet."));
      emptyCard.append(createElement("p", "language-pack-summary", "Pages will show locale bundles here after the package catalog is generated."));
      packGrid.append(emptyCard);
      return;
    }

    packs.forEach((pack) => {
      packGrid.append(createPackCard(pack));
    });
  } catch (error) {
    setStat(packCountEls, 0);
    setStat(localeCountEls, 0);
    packGrid.replaceChildren();

    const fallback = createElement("article", "language-pack-card language-pack-empty");
    fallback.append(createElement("p", "feature-kicker", "Catalog"));
    fallback.append(createElement("h3", "", "Language catalog unavailable"));
    fallback.append(createElement("p", "language-pack-summary", "The homepage could not load the published language-pack metadata right now."));
    packGrid.append(fallback);
  }
}

loadReleaseData();
loadPackCatalog();
