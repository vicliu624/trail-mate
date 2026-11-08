"""Bounded local HTTP fixture for search, online creation and cancellation."""
import collections
import http.server
import json
import subprocess
import sys
import threading
import time
import urllib.parse

calls = collections.Counter()
xml = '''<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6" generator="TrailMate test">
<node id="1" version="1" lat="25.005" lon="102.705"><tag k="name" v="Test Spring 测试泉"/><tag k="amenity" v="drinking_water"/></node>
<node id="2" version="1" lat="25.001" lon="102.701"/>
<node id="3" version="1" lat="25.009" lon="102.709"/>
<way id="10" version="1"><nd ref="2"/><nd ref="3"/><tag k="highway" v="path"/><tag k="name" v="Test Trail"/></way>
</osm>'''.encode()


def area(identity, country, extent, osm_type="R"):
    return {"type": "Feature", "properties": {
        "osm_type": osm_type, "osm_id": identity, "name": "Test district",
        "countrycode": country, "country": "China" if country == "CN" else "United States",
        "state": "Yunnan" if country == "CN" else "California", "type": "district",
        **({"extent": extent} if extent else {})}}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        if parsed.path == "/search":
            text = query["q"][0]
            calls["search:" + text] += 1
            if text == "slow":
                time.sleep(3)
            if text == "malformed":
                body = b"{bad"
            else:
                features = [area(12, "CN", None, "N")]
                if text != "point-only":
                    features += [area(10, "CN", [102.70, 25.01, 102.71, 25]),
                                 area(11, "US", [-122.5, 37.8, -122.4, 37.7]),
                                 area(10, "CN", [102.70, 25.01, 102.71, 25]),
                                 area(13, "CN", ["bad", 25.01, 102.71, 25])]
                body = json.dumps({"features": features}).encode()
        else:
            assert parsed.path.startswith("/map-")
            assert "[out:xml]" in query["data"][0]
            assert "(._;>;);out body qt;" in query["data"][0], "Recurse before a single output so OSM objects are not duplicated"
            assert "25,102.7,25.01,102.71" in query["data"][0]
            scenario = parsed.path.removeprefix("/map-")
            calls["map:" + scenario] += 1
            if scenario == "failure":
                self.send_error(503)
                return
            if scenario == "oversize":
                self.send_response(200)
                self.send_header("Content-Length", str(65 * 1024 * 1024))
                self.end_headers()
                return
            if scenario == "cancel":
                time.sleep(3)
            body = b'<osm version="0.6"/>' if scenario == "empty" else xml
        try:
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}", sys.argv[2]])
    if result.returncode == 0:
        assert calls["search:ambiguous"] == 1, calls
        assert calls["map:success"] == 1, calls
    raise SystemExit(result.returncode)
finally:
    server.shutdown()
    server.server_close()
    thread.join()
