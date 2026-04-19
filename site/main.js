const releaseVersionEls = document.querySelectorAll("[data-release-version]");
const releaseLinkEls = document.querySelectorAll("[data-release-link]");
const boardCards = document.querySelectorAll("[data-board-id]");

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

loadReleaseData();
