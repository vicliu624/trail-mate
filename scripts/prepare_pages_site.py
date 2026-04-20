from __future__ import annotations

import argparse
import http.client
import json
import os
import shutil
import time
import urllib.error
import urllib.request
from pathlib import Path

from build_pack_repository import build_pack_repository
from webflash_targets import WEBFLASH_TARGETS

SHOWCASE_IMAGES = (
    {"source": "docs/images/logo_big.png", "dest": "brand-logo.png"},
    {"source": "docs/images/main.png", "dest": "home-main.png"},
    {"source": "docs/images/map_osm.png", "dest": "nav-map-osm.png"},
    {"source": "docs/images/map_terrain.png", "dest": "nav-map-terrain.png"},
    {"source": "docs/images/map_satellite.png", "dest": "nav-map-satellite.png"},
    {"source": "docs/images/SkyPlot.png", "dest": "nav-skyplot.png"},
    {"source": "docs/images/screenshot_20260118_200651.png", "dest": "chat-compose.png"},
    {"source": "docs/images/messages.png", "dest": "chat-messages.png"},
    {"source": "docs/images/contacts.png", "dest": "chat-contacts.png"},
    {"source": "docs/images/team_join.png", "dest": "team-join.png"},
    {"source": "docs/images/team_map.png", "dest": "team-map.png"},
    {"source": "docs/images/subGScan.png", "dest": "utility-spectrum.png"},
    {"source": "docs/images/sstv_page.jpg", "dest": "utility-sstv-page.jpg"},
    {"source": "docs/images/sstv_image_result.jpg", "dest": "utility-sstv-result.jpg"},
    {"source": "docs/images/data_exchange.png", "dest": "utility-data-exchange.png"},
    {"source": "docs/images/tracker1.png", "dest": "utility-tracker.png"},
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download the latest web flasher assets and generate site metadata."
    )
    parser.add_argument(
        "--site-root",
        default="site",
        help="Root directory of the static GitHub Pages site",
    )
    parser.add_argument(
        "--repo",
        required=True,
        help="GitHub repository slug, for example owner/name",
    )
    return parser.parse_args()


def github_request(url: str) -> urllib.request.Request:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "trail-mate-pages-prep",
    }
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return urllib.request.Request(url, headers=headers)


def open_with_retries(
    request: urllib.request.Request,
    *,
    attempts: int = 3,
    timeout: int = 30,
):
    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            return urllib.request.urlopen(request, timeout=timeout)
        except urllib.error.HTTPError:
            raise
        except (urllib.error.URLError, TimeoutError, http.client.RemoteDisconnected) as exc:
            last_error = exc
            if attempt >= attempts:
                raise
            time.sleep(attempt)
    if last_error is not None:
        raise last_error
    raise RuntimeError("open_with_retries reached an unexpected empty state")


def fetch_latest_release(repo: str) -> dict | None:
    url = f"https://api.github.com/repos/{repo}/releases?per_page=10"
    try:
        with open_with_retries(github_request(url)) as response:
            releases = json.load(response)
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return None
        raise

    for release in releases:
        if not release.get("draft"):
            return release
    return None


def download(url: str, destination: Path) -> None:
    with open_with_retries(github_request(url)) as response:
        destination.write_bytes(response.read())


def build_unavailable_metadata() -> dict:
    return {
        "available": False,
        "tag_name": None,
        "release_url": None,
        "published_at": None,
        "targets": {
            target["id"]: {
                "available": False,
                "manifest_path": f"manifests/{target['id']}.json",
            }
            for target in WEBFLASH_TARGETS
        },
    }


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def clear_directory_contents(path: Path, preserve_names: tuple[str, ...] = ()) -> None:
    path.mkdir(parents=True, exist_ok=True)
    for child in path.iterdir():
        if child.name in preserve_names:
            continue
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()


def copy_showcase_images(site_root: Path) -> None:
    showcase_dir = site_root / "assets" / "showcase"
    shutil.rmtree(showcase_dir, ignore_errors=True)
    showcase_dir.mkdir(parents=True, exist_ok=True)

    for image in SHOWCASE_IMAGES:
        source = Path(image["source"])
        destination = showcase_dir / image["dest"]
        if not source.exists():
            raise FileNotFoundError(f"Missing showcase image: {source}")
        shutil.copy2(source, destination)


def prepare_site(site_root: Path, release: dict | None) -> None:
    manifests_dir = site_root / "manifests"
    assets_dir = site_root / "assets" / "webflash"
    data_dir = site_root / "data"

    shutil.rmtree(assets_dir, ignore_errors=True)
    clear_directory_contents(manifests_dir, preserve_names=(".gitignore",))
    assets_dir.mkdir(parents=True, exist_ok=True)
    data_dir.mkdir(parents=True, exist_ok=True)
    copy_showcase_images(site_root)
    build_pack_repository(pack_root=Path("packs"), site_root=site_root)

    if release is None:
        write_json(data_dir / "latest-release.json", build_unavailable_metadata())
        return

    assets_by_name = {
        asset["name"]: asset["browser_download_url"] for asset in release.get("assets", [])
    }
    metadata = {
        "available": True,
        "tag_name": release.get("tag_name"),
        "release_url": release.get("html_url"),
        "published_at": release.get("published_at"),
        "targets": {},
    }

    version = (release.get("tag_name") or "").removeprefix("v")
    for target in WEBFLASH_TARGETS:
        asset_name = target["merged_asset_name"]
        manifest_path = f"manifests/{target['id']}.json"
        download_url = assets_by_name.get(asset_name)

        if not download_url:
            metadata["targets"][target["id"]] = {
                "available": False,
                "manifest_path": manifest_path,
            }
            continue

        asset_path = assets_dir / asset_name
        download(download_url, asset_path)

        manifest = {
            "name": f"Trail Mate - {target['name']}",
            "version": version or "latest",
            "new_install_prompt_erase": True,
            "builds": [
                {
                    "chipFamily": target["chip_family"],
                    "parts": [
                        {
                            "path": f"../assets/webflash/{asset_name}",
                            "offset": 0,
                        }
                    ],
                }
            ],
        }
        write_json(manifests_dir / f"{target['id']}.json", manifest)
        metadata["targets"][target["id"]] = {
            "available": True,
            "manifest_path": manifest_path,
        }

    write_json(data_dir / "latest-release.json", metadata)


def main() -> int:
    args = parse_args()
    prepare_site(Path(args.site_root), fetch_latest_release(args.repo))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
