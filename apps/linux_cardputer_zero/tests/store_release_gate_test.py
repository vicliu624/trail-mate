"""Publication-contract tests: unready releases never reach submission."""
import copy
import base64
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("store_release", Path(__file__).resolve().parents[1] / "tools/publish_store_release.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class StoreReleaseGate(unittest.TestCase):
    def setUp(self):
        self.repository = "vicliu624/trail-mate"
        self.tag = "v1.2.3"
        self.commit = "a" * 40
        self.package = "trailmate_1.2.3_arm64.deb"
        self.release = {"tag_name": self.tag, "draft": False, "prerelease": False,
                        "published_at": "2026-11-10T00:00:00Z", "assets": []}
        for name in (self.package, "SHA256SUMS"):
            self.release["assets"].append({"name": name, "state": "uploaded", "size": 10,
                "digest": "sha256:" + "b" * 64,
                "browser_download_url": f"https://github.com/{self.repository}/releases/download/{self.tag}/{name}"})
        self.runs = {}
        self.jobs = {}
        for workflow, identifier, names in (
            ("ci.yml", 100, ("Linux device release packages / cardputer-zero Debian package",
                            "Cardputer Zero release regressions / Cardputer Zero shell", "Publish GitHub Release")),
            ("cardputer-zero-linux.yml", 101, ("Cardputer Zero shell",))):
            self.runs[workflow] = [{"id": identifier, "head_sha": self.commit, "head_branch": self.tag,
                                    "event": "push", "status": "completed", "conclusion": "success"}]
            self.jobs[identifier] = [{"name": name, "conclusion": "success"} for name in names]

    def read(self, path):
        if "/releases/tags/" in path:
            return copy.deepcopy(self.release)
        if "/git/ref/tags/" in path:
            return {"object": {"type": "tag", "sha": "c" * 40}}
        if "/git/tags/" in path:
            return {"object": {"type": "commit", "sha": self.commit}}
        if "/actions/workflows/" in path:
            workflow = path.split("/actions/workflows/")[1].split("/")[0]
            return {"workflow_runs": copy.deepcopy(self.runs[workflow])}
        if "/actions/runs/" in path:
            identifier = int(path.split("/actions/runs/")[1].split("/")[0])
            return {"jobs": copy.deepcopy(self.jobs[identifier])}
        raise AssertionError(path)

    def check(self, commit=None):
        return gate.check_release_gate(self.read, self.repository, self.tag, commit or self.commit)

    def test_published_release_and_exact_tagged_builds_pass(self):
        evidence = self.check()
        self.assertEqual((evidence["ci_run"], evidence["zero_run"]), (100, 101))
        self.assertEqual(evidence["asset"]["name"], self.package)

    def test_draft_prerelease_or_unpublished_release_blocks(self):
        for key, value in (("draft", True), ("prerelease", True), ("published_at", None)):
            with self.subTest(key=key):
                original = self.release[key]
                self.release[key] = value
                with self.assertRaises(gate.GateError):
                    self.check()
                self.release[key] = original

    def test_checkout_must_match_release_commit(self):
        with self.assertRaisesRegex(gate.GateError, "exact release commit"):
            self.check("d" * 40)

    def test_pending_failed_cancelled_or_skipped_ci_blocks(self):
        for workflow in self.runs:
            for status, conclusion in (("queued", None), ("in_progress", None), ("completed", "failure"),
                                       ("completed", "cancelled"), ("completed", "skipped")):
                with self.subTest(workflow=workflow, conclusion=conclusion):
                    run = self.runs[workflow][0]
                    original = run.copy()
                    run.update(status=status, conclusion=conclusion)
                    with self.assertRaises(gate.GateError):
                        self.check()
                    run.update(original)

    def test_new_failed_attempt_cannot_fall_back_to_old_success(self):
        latest = self.runs["ci.yml"][0].copy()
        latest.update(id=102, conclusion="failure")
        self.runs["ci.yml"].append(latest)
        with self.assertRaises(gate.GateError):
            self.check()

    def test_green_branch_other_commit_and_manual_runs_do_not_count(self):
        for key, value in (("head_branch", "main"), ("head_sha", "e" * 40), ("event", "workflow_dispatch")):
            run = self.runs["ci.yml"][0]
            original = run[key]
            run[key] = value
            with self.assertRaises(gate.GateError):
                self.check()
            run[key] = original

    def test_skipped_required_packaging_is_not_a_green_release(self):
        self.jobs[100][0]["conclusion"] = "skipped"
        with self.assertRaisesRegex(gate.GateError, "prerequisite"):
            self.check()

    def test_missing_partial_wrong_origin_or_unhashed_package_blocks(self):
        asset = self.release["assets"][0]
        for key, value in (("state", "starter"), ("size", 0), ("digest", None),
                           ("browser_download_url", "https://github.com/other/repo/releases/download/v1.2.3/" + self.package)):
            original = asset[key]
            asset[key] = value
            with self.assertRaises(gate.GateError):
                self.check()
            asset[key] = original
        self.release["assets"].pop(0)
        with self.assertRaises(gate.GateError):
            self.check()

    def test_missing_checksum_asset_blocks(self):
        self.release["assets"].pop()
        with self.assertRaises(gate.GateError):
            self.check()

    def test_draft_and_running_ci_never_reach_publish_callback(self):
        for draft in (True, False):
            self.release["draft"] = draft
            self.runs["ci.yml"][0].update(status="in_progress", conclusion=None)
            with patch.object(gate, "api", self.read), patch.object(gate.shutil, "which", return_value="gh"), \
                    patch.object(gate.subprocess, "check_output", return_value=self.commit), \
                    patch.object(gate, "submit_verified_package") as submit:
                with self.assertRaises(gate.GateError):
                    gate.main(["--tag", self.tag, "--publish", "--app-builder", "."])
                submit.assert_not_called()

    def test_corrupt_download_and_conflicting_checksums_block(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / self.package
            package.write_bytes(b"verified package")
            digest = hashlib.sha256(package.read_bytes()).hexdigest()
            asset = {"size": package.stat().st_size, "digest": "sha256:" + digest}
            self.assertEqual(gate.verify_download(package, asset), digest)
            package.write_bytes(b"corrupt! package")
            with self.assertRaises(gate.GateError):
                gate.verify_download(package, asset)
            first, second = root / "first", root / "second"
            first.write_text(digest + "  " + self.package + "\n")
            second.write_text("f" * 64 + "  " + self.package + "\n")
            self.assertEqual(gate.checksum_for([first], self.package), digest)
            with self.assertRaises(gate.GateError):
                gate.checksum_for([first, second], self.package)

    def test_wrong_package_version_blocks(self):
        def field(command, **kwargs):
            value = {"Package": "trailmate", "Version": "0.0.0", "Architecture": "arm64"}[command[-1]]
            return subprocess.CompletedProcess(command, 0, stdout=value + "\n")
        with patch.object(gate.subprocess, "run", side_effect=field):
            with self.assertRaisesRegex(gate.GateError, "Version"):
                gate.verify_package(Path("test.deb"), "1.2.3")

    def test_api_transport_preserves_listing_images_and_removes_old_gallery(self):
        class Client:
            def __init__(self):
                self.blobs = {}
                self.tree = None
                self.updated = None
            def get_ref_sha(self, *args): return "a" * 40
            def create_ref(self, *args): pass
            def get_commit(self, *args): return ("a" * 40, "base-tree")
            def create_blob(self, owner, repository, content):
                data = base64.b64decode(content)
                identifier = hashlib.sha256(data).hexdigest()
                self.blobs[identifier] = data
                return identifier
            def _get(self, path):
                return [{"name": "obsolete.png", "path": "pool/main/trailmate/screenshots/obsolete.png"}]
            def _post(self, path, value):
                self.tree = value
                return {"sha": "new-tree"}
            def create_commit(self, *args): return "new-commit"
            def _request(self, method, path, value): self.updated = value
        client = Client()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "native.png").write_bytes(b"native-frame")
            (root / "icon.png").write_bytes(b"icon")
            meta = {"description": "Reticulum: 身份属于你", "screenshots": ["screenshots/native.png"],
                    "author": {"website": "https://vicliu624.github.io/trail-mate/"}}
            gate.api_pool_commit(client, "vicliu624", "packages", "publish/test", "trailmate", root,
                                 {"screenshots": ["native.png"], "icon": "icon.png"}, meta,
                                 {"package.release.json": "verified-manifest"}, "publish")
        entries = {item["path"]: item for item in client.tree["tree"]}
        prefix = "pool/main/trailmate/"
        self.assertIsNone(entries[prefix + "screenshots/obsolete.png"]["sha"])
        self.assertEqual(client.blobs[entries[prefix + "screenshots/native.png"]["sha"]], b"native-frame")
        import json
        self.assertEqual(json.loads(client.blobs[entries[prefix + "meta.json"]["sha"]]), meta)
        self.assertEqual(client.updated, {"sha": "new-commit", "force": False})


if __name__ == "__main__":
    unittest.main()
