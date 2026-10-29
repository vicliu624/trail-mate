"""Self-contained repository with valid and hostile ZIP/catalog fixtures."""
import hashlib
import http.server
import io
import json
import pathlib
import stat
import subprocess
import sys
import threading
import time
import zipfile


def archive(kind):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as bundle:
        def write(name, data):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | 0o600) << 16
            bundle.writestr(info, data)

        write("package.ini", "kind=package\nid=remote.test\nversion=1.0.0\n")
        write("locales/remote-test/manifest.ini", "kind=locale\nid=remote-test\nstrings=strings.tsv\nui_font_pack=builtin-latin-ui\n")
        write("locales/remote-test/strings.tsv", "Settings\tRemote settings\n")
        if kind == "traversal":
            write("../outside", "bad")
        elif kind == "symlink":
            info = zipfile.ZipInfo("locales/link")
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            bundle.writestr(info, "/tmp")
        elif kind == "duplicate":
            write("locales/remote-test/strings.tsv", "replacement")
        elif kind == "absolute":
            write("/outside", "bad")
        elif kind == "reserved":
            write(".installed-at", "bad")
    return output.getvalue()


ARCHIVES = {name: archive(name) for name in ("valid", "traversal", "symlink", "duplicate", "absolute", "reserved")}
CATALOG_ONLINE = True
PACKAGES = [dict(id="remote.test", version="1.0.0", package_type="locale-bundle", display_name="Remote test", summary="Fixture",
                 supported_memory_profiles=[], min_firmware_version="0.1.0", runtime={"estimated_unique_font_ram_bytes": 0},
                 provides={"locales": [{"id": "remote-test"}], "fonts": [], "ime": []},
                 archive={"layout": "trailmate-pack-v1", "path": name + ".zip", "size_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            for name, data in ARCHIVES.items()]


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        global CATALOG_ONLINE
        name = self.path.lstrip("/")
        if name == "catalog-offline":
            CATALOG_ONLINE = False
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if name in ("slow.zip", "slow-catalog.json"):
            time.sleep(5)
            name = "valid.zip" if name == "slow.zip" else "catalog.json"
        if name == "catalog.json":
            if not CATALOG_ONLINE:
                self.send_error(503)
                return
            data = json.dumps(dict(schema_version=1, archive_layout="trailmate-pack-v1", packages=PACKAGES[:1])).encode()
        elif name == "cases.json":
            data = json.dumps(dict(schema_version=1, archive_layout="trailmate-pack-v1", packages=[dict(item, id="case." + str(index)) for index, item in enumerate(PACKAGES)])).encode()
        elif name.endswith(".zip") and name[:-4] in ARCHIVES:
            data = ARCHIVES[name[:-4]]
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except ConnectionError:
            pass


def main():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}"], check=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    main()
