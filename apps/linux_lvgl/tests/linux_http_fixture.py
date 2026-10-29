"""Loopback HTTP/TLS fixture; also runnable with the armhf probe on the device."""
import http.server
import pathlib
import struct
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import zlib

# A small PNG is valid source data even though KML cache paths end in .jpg.
def png_chunk(kind, data):
    return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))


IMAGE = (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack("!IIBBBBB", 2, 1, 8, 2, 0, 0, 0))
         + png_chunk(b"IDAT", zlib.compress(b"\x00\xff\x00\x00\x00\xff\x00")) + png_chunk(b"IEND", b""))
JPEG = pathlib.Path(__file__).with_name("linux_http_image.jpg").read_bytes()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        try:
            if self.path == "/slow":
                time.sleep(2)
            if self.path == "/not-modified":
                self.send_response(304)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if self.path in ("/redirect", "/file-redirect"):
                self.send_response(302)
                self.send_header("Location", "/ok" if self.path == "/redirect" else "file:///etc/hosts")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if self.path == "/chunked":
                self.send_response(200)
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                self.wfile.write(b"1\r\nA\r\n2\r\n\x00B\r\n0\r\n\r\n")
                return
            data = IMAGE if self.path == "/image" else JPEG if self.path == "/jpeg" else b"x" * 2000 if self.path == "/oversize" else b"Ab!9"
            self.send_response(404 if self.path == "/missing" else 200)
            self.send_header("Content-Length", str(100 if self.path == "/partial" else len(data)))
            self.end_headers()
            self.wfile.write(data)
            self.close_connection = True
        except (ConnectionError, ssl.SSLError):
            pass


def main():
    with tempfile.TemporaryDirectory(prefix="trailmate-http-fixture-") as directory:
        root = pathlib.Path(directory)
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=localhost", "-keyout", str(root / "key.pem"), "-out", str(root / "cert.pem")], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        secure = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(root / "cert.pem", root / "key.pem")
        secure.socket = context.wrap_socket(secure.socket, server_side=True)
        threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (plain, secure)]
        for thread in threads:
            thread.start()
        try:
            subprocess.run([sys.argv[1], f"http://127.0.0.1:{plain.server_port}", f"https://127.0.0.1:{secure.server_port}"], check=True)
        finally:
            for server in (plain, secure):
                server.shutdown()
                server.server_close()
            for thread in threads:
                thread.join()


if __name__ == "__main__":
    main()
