#!/usr/bin/env python3
"""Verify a published TrailMate release before submitting it to the Zero Store.

No local DEB input or CI-artifact shortcut is accepted. Without --publish this
command performs the same release, CI, checksum and package checks read-only.
"""
import argparse
import base64
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import quote
from urllib.error import HTTPError


class GateError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise GateError(message)


def api(path):
    result = subprocess.run(["gh", "api", path], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    require(result.returncode == 0,
            "Cannot verify a published release or its CI: " + result.stderr.strip())
    return json.loads(result.stdout)


def validate_release(release, tag):
    require(re.fullmatch(r"v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", tag),
            "A stable vMAJOR.MINOR.PATCH release tag is required")
    require(release.get("tag_name") == tag, "Release tag does not match the requested version")
    require(release.get("draft") is False and release.get("prerelease") is False and release.get("published_at"),
            "Publish the stable GitHub Release in this repository before submitting to the Store")


def release_commit(read, repository, tag):
    obj = read(f"repos/{repository}/git/ref/tags/{quote(tag, safe='')}")["object"]
    for _ in range(4):
        if obj.get("type") == "commit":
            require(re.fullmatch(r"[0-9a-f]{40}", obj.get("sha", "")), "Invalid release commit")
            return obj["sha"]
        require(obj.get("type") == "tag", "Release tag must resolve to a commit")
        obj = read(f"repos/{repository}/git/tags/{obj['sha']}")["object"]
    raise GateError("Release tag indirection is too deep")


def successful_tag_run(read, repository, workflow, commit, tag, required_jobs):
    path = f"repos/{repository}/actions/workflows/{workflow}/runs?head_sha={commit}&event=push&per_page=100"
    runs = read(path).get("workflow_runs", [])
    matching = [run for run in runs if run.get("head_sha") == commit and run.get("head_branch") == tag
                and run.get("event") == "push"]
    require(matching, f"No tagged {workflow} run exists for the release commit")
    latest = max(matching, key=lambda run: run["id"])
    require(latest.get("status") == "completed" and latest.get("conclusion") == "success",
            f"Release {workflow} is not complete and successful (run {latest['id']})")
    jobs = read(f"repos/{repository}/actions/runs/{latest['id']}/jobs?per_page=100").get("jobs", [])
    for name in required_jobs:
        matches = [job for job in jobs if job.get("name") == name]
        require(len(matches) == 1 and matches[0].get("conclusion") == "success",
                f"Release build prerequisite did not succeed: {name}")
    return latest["id"]


def checked_asset(release, repository, tag, name):
    matches = [asset for asset in release.get("assets", []) if asset.get("name") == name]
    require(len(matches) == 1, f"Expected exactly one published release asset: {name}")
    asset = matches[0]
    expected_url = f"https://github.com/{repository}/releases/download/{quote(tag, safe='')}/{quote(name, safe='')}"
    require(asset.get("state") == "uploaded" and asset.get("size", 0) > 0,
            f"Release asset is not fully uploaded: {name}")
    require(asset.get("browser_download_url") == expected_url, "Asset is outside this repository's release")
    require(re.fullmatch(r"sha256:[0-9a-f]{64}", asset.get("digest") or ""), "Missing release asset SHA256")
    return asset


def check_release_gate(read, repository, tag, checkout_commit):
    require(repository == "vicliu624/trail-mate", "Store publishing must use the TrailMate repository")
    release = read(f"repos/{repository}/releases/tags/{quote(tag, safe='')}")
    validate_release(release, tag)
    commit = release_commit(read, repository, tag)
    require(commit == checkout_commit, "Check out the exact release commit before preparing Store metadata")
    ci_run = successful_tag_run(read, repository, "ci.yml", commit, tag, (
        "Linux device release packages / cardputer-zero Debian package",
        "Cardputer Zero release regressions / Cardputer Zero shell", "Publish GitHub Release"))
    zero_run = successful_tag_run(read, repository, "cardputer-zero-linux.yml", commit, tag,
                                  ("Cardputer Zero shell",))
    name = f"trailmate_{tag[1:]}_arm64.deb"
    asset = checked_asset(release, repository, tag, name)
    checksums = [name for name in ("SHA256SUMS", "STORE_SHA256SUMS")
                 if any(item.get("name") == name for item in release.get("assets", []))]
    require(checksums, "Release has no published checksum file")
    for checksum in checksums:
        checked_asset(release, repository, tag, checksum)
    return {"release": release, "commit": commit, "ci_run": ci_run, "zero_run": zero_run,
            "asset": asset, "checksums": checksums, "tag": tag, "repository": repository}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_download(path, asset):
    require(path.is_file() and path.stat().st_size == asset["size"], "Downloaded asset size mismatch")
    digest = sha256(path)
    require(asset["digest"] == "sha256:" + digest, "Downloaded asset SHA256 mismatch")
    return digest


def checksum_for(files, filename):
    entries = []
    for path in files:
        for line in path.read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"([0-9a-fA-F]{64})\s+\*?(.+)", line)
            if match and match[2] == filename:
                entries.append(match[1].lower())
    require(entries and len(set(entries)) == 1, "Release checksums are missing or ambiguous for the Store package")
    return entries[0]


def dpkg_command(arguments):
    if os.name != "nt":
        return ["dpkg-deb", *map(str, arguments)]
    translated = []
    for value in map(str, arguments):
        if len(value) > 2 and value[1] == ":":
            value = "/mnt/" + value[0].lower() + value[2:].replace("\\", "/")
        translated.append(value)
    return ["wsl", "-e", "dpkg-deb", *translated]


def verify_package(package, version):
    for field, expected in (("Package", "trailmate"), ("Version", version), ("Architecture", "arm64")):
        result = subprocess.run(dpkg_command(["-f", package, field]), check=True, capture_output=True,
                                text=True, encoding="utf-8", errors="replace")
        require(result.stdout.strip() == expected, f"Published DEB has an incorrect {field}")


def api_pool_commit(client, owner, repository, branch, package, app_dir, store_meta, meta_out, extra, message):
    """Official czdev transport callback; preserve all metadata and native images."""
    require(package == "trailmate", "Unexpected Store package namespace")
    base = client.get_ref_sha("CardputerZero", "packages", "heads/main")
    client.create_ref(owner, repository, branch, base)
    base_tree = client.get_commit("CardputerZero", "packages", base)[1]
    prefix = "pool/main/trailmate/"
    payloads = {"meta.json": (json.dumps(meta_out, indent=2, ensure_ascii=False) + "\n").encode()}
    payloads.update({name: text.encode() for name, text in extra.items()})
    app_dir = Path(app_dir)
    for screenshot in store_meta.get("screenshots") or []:
        path = app_dir / screenshot
        payloads["screenshots/" + path.name] = path.read_bytes()
    if store_meta.get("icon"):
        path = app_dir / store_meta["icon"]
        payloads[path.name] = path.read_bytes()
    entries = []
    try:
        previous = client._get("/repos/CardputerZero/packages/contents/" + prefix + "screenshots?ref=main")
    except HTTPError as error:
        if error.code != 404:
            raise
        previous = []
    for item in previous:
        relative = "screenshots/" + item["name"]
        require(item.get("path") == prefix + relative, "Unexpected upstream screenshot path")
        if relative not in payloads:
            entries.append({"path": prefix + relative, "mode": "100644", "type": "blob", "sha": None})
    for name, content in payloads.items():
        require(not name.startswith("/") and ".." not in Path(name).parts, "Unsafe Store metadata path")
        blob = client.create_blob(owner, repository, base64.b64encode(content).decode())
        entries.append({"path": prefix + name, "mode": "100644", "type": "blob", "sha": blob})
    tree = client._post(f"/repos/{owner}/{repository}/git/trees", {"base_tree": base_tree, "tree": entries})["sha"]
    commit = client.create_commit(owner, repository, message, tree, base)
    client._request("PATCH", f"/repos/{owner}/{repository}/git/refs/heads/{branch}", {"sha": commit, "force": False})


def submit_verified_package(evidence, package, app_builder, app_dir):
    """Use official czdev policy/ownership checks and the existing Release URL."""
    require((app_builder / "scripts/czdev/publish.py").is_file(), "Provide an official AppBuilder checkout")
    sys.path.insert(0, str(app_builder / "scripts"))
    auth = importlib.import_module("czdev.auth")
    publish = importlib.import_module("czdev.publish")
    client = importlib.import_module("czdev.github_client").GitHubClient
    original_run, original_token = subprocess.run, auth.load_token
    original_upload, original_pool = client.upload_release_asset, publish.push_pool_branch

    def run_with_dpkg(command, *args, **kwargs):
        if command and command[0] == "dpkg-deb":
            command = dpkg_command(command[1:])
            if kwargs.get("text") or kwargs.get("universal_newlines"):
                kwargs.update(encoding="utf-8", errors="replace")
        return original_run(command, *args, **kwargs)

    def published_asset(_self, _owner, _repo, _release, path, name):
        require(name == evidence["asset"]["name"], "czdev requested a different package name")
        verify_download(Path(path), evidence["asset"])
        return evidence["asset"]["browser_download_url"]

    previous_directory = Path.cwd()
    try:
        subprocess.run = run_with_dpkg
        auth.load_token = lambda: subprocess.check_output(["gh", "auth", "token"], text=True).strip()
        client.upload_release_asset = published_asset
        publish.push_pool_branch = api_pool_commit
        os.chdir(app_dir)
        publish.run(deb=str(package))
    finally:
        os.chdir(previous_directory)
        subprocess.run, auth.load_token = original_run, original_token
        client.upload_release_asset, publish.push_pool_branch = original_upload, original_pool


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--publish", action="store_true", help="Submit after all release gates pass")
    parser.add_argument("--app-builder", type=Path, help="Official CardputerZero/AppBuilder checkout")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[3]
    require(shutil.which("gh"), "GitHub CLI is required; sign in with gh auth login")
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    evidence = check_release_gate(api, "vicliu624/trail-mate", args.tag, head)
    app_dir = root / "apps/linux_cardputer_zero"
    manifest = json.loads((app_dir / "app-builder.json").read_text(encoding="utf-8"))
    require(manifest.get("package_name") == "trailmate" and manifest.get("version") == args.tag[1:],
            "AppBuilder metadata must match the published release")
    dirty = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no", "--",
                                    "apps/linux_cardputer_zero/app-builder.json",
                                    "apps/linux_cardputer_zero/packaging/store/screenshots",
                                    "apps/linux_cardputer_zero/packaging/trailmate-cardputer-zero.png"], cwd=root, text=True)
    require(not dirty.strip(), "Release listing and screenshots have uncommitted edits")
    if args.publish:
        require(args.app_builder is not None, "--app-builder is required with --publish")
    with tempfile.TemporaryDirectory(prefix="trailmate-store-release-") as temporary:
        directory = Path(temporary)
        names = [evidence["asset"]["name"], *evidence["checksums"]]
        for name in names:
            subprocess.run(["gh", "release", "download", args.tag, "--repo", evidence["repository"],
                            "--pattern", name, "--dir", str(directory)], check=True)
            verify_download(directory / name, checked_asset(evidence["release"], evidence["repository"], args.tag, name))
        package = directory / evidence["asset"]["name"]
        digest = checksum_for([directory / name for name in evidence["checksums"]], package.name)
        require(sha256(package) == digest, "Published release checksum does not match the Store package")
        verify_package(package, args.tag[1:])
        print(f"Release gate passed: {args.tag}, commit {head}, CI {evidence['ci_run']}, "
              f"Zero {evidence['zero_run']}, SHA256 {digest}", flush=True)
        if args.publish:
            submit_verified_package(evidence, package, args.app_builder.resolve(), app_dir)
    return 0


if __name__ == "__main__":
    if os.name == "nt" and not sys.flags.utf8_mode:
        os.execv(sys.executable, [sys.executable, "-X", "utf8", *sys.argv])
    try:
        sys.exit(main())
    except (GateError, subprocess.CalledProcessError) as error:
        print(f"Store publication blocked: {error}", file=sys.stderr)
        sys.exit(1)
