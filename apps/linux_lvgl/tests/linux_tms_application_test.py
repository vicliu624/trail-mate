"""Exercise the real application, TCP framing and exec handoff without images."""
import hashlib
import os
import pathlib
import re
import secrets
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.parse


def encode(value):
    if value is None:
        return b"\xc0"
    if isinstance(value, int):
        if value < 128:
            return bytes([value])
        if value <= 255:
            return b"\xcc" + bytes([value])
        if value <= 65535:
            return b"\xcd" + struct.pack(">H", value)
        return b"\xce" + struct.pack(">I", value)
    if isinstance(value, bytes):
        if len(value) <= 255:
            return b"\xc4" + bytes([len(value)]) + value
        return b"\xc5" + struct.pack(">H", len(value)) + value
    if isinstance(value, list):
        return bytes([0x90 + len(value)]) + b"".join(map(encode, value))
    raise TypeError(type(value))


def decode(data):
    cursor = 0

    def take(size):
        nonlocal cursor
        value = data[cursor:cursor + size]
        assert len(value) == size
        cursor += size
        return value

    def item():
        tag = take(1)[0]
        if tag < 128:
            return tag
        if 0x90 <= tag <= 0x9f:
            return [item() for _ in range(tag & 15)]
        if tag == 0xc0:
            return None
        if tag in (0xcc, 0xcd, 0xce, 0xcf):
            return int.from_bytes(take({0xcc: 1, 0xcd: 2, 0xce: 4, 0xcf: 8}[tag]), "big")
        if tag in (0xc4, 0xc5, 0xc6):
            return take(int.from_bytes(take({0xc4: 1, 0xc5: 2, 0xc6: 4}[tag]), "big"))
        if tag in (0xdc, 0xdd):
            return [item() for _ in range(int.from_bytes(take(2 if tag == 0xdc else 4), "big"))]
        raise AssertionError(f"Unsupported response tag {tag:#x}")

    value = item()
    assert cursor == len(data)
    return value


def crc16(data):
    crc = 65535
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 65535
    return crc


class Peer:
    def __init__(self, endpoint, session):
        address = urllib.parse.urlsplit(endpoint)
        self.socket = socket.create_connection((address.hostname, address.port), timeout=2)
        self.socket.settimeout(2)
        self.sequence = 0
        self.session = session
        self.send(1, b"")
        self.receive(2)

    def exact(self, size):
        output = b""
        while len(output) < size:
            part = self.socket.recv(size - len(output))
            if not part:
                raise ConnectionError("Application connection closed")
            output += part
        return output

    def send(self, kind, payload):
        self.sequence += 1
        header = struct.pack("<2sBBHH", b"HL", 1, kind, self.sequence, len(payload))
        self.socket.sendall(header + payload + struct.pack("<H", crc16(header + payload)))

    def receive(self, wanted):
        while True:
            header = self.exact(8)
            magic, version, kind, sequence, size = struct.unpack("<2sBBHH", header)
            assert magic == b"HL" and version == 1 and size <= 512
            payload = self.exact(size)
            assert struct.unpack("<H", self.exact(2))[0] == crc16(header + payload)
            if kind == wanted and sequence == self.sequence:
                return payload

    def request(self, operation, body):
        deadline = time.monotonic() + 15
        while True:
            request = secrets.token_bytes(16)
            self.send(0x20, encode([1, self.session, request, operation, body]))
            version, session, correlation, status, response = decode(self.receive(0x21))
            assert version == 1 and session == self.session and correlation == request
            if status == 5 and time.monotonic() < deadline:
                time.sleep(.03)
                continue
            assert status == 0, (operation, status)
            return response

    def document(self):
        output, expected = b"", None
        while True:
            offset, total, digest, chunk = self.request(17, [len(output), 320, expected])
            assert offset == len(output) and 0 < total <= 32768 and chunk
            if expected is None:
                expected = digest
            assert expected == digest
            output += chunk
            if len(output) == total:
                assert hashlib.sha256(output).digest() == expected
                return output, expected

    def close(self):
        self.socket.close()


def auto_udp_sockets(process, document):
    if os.environ.get("TRAIL_MATE_TEST_REQUIRE_AUTO_UDP") != "1":
        return set()
    records = dict(line.split(b"=", 1) for line in document.splitlines() if b"=" in line)
    expected = set()
    for key, value in records.items():
        if not key.endswith(b".type") or value != b"u8:1":
            continue
        prefix = key[:-4]
        if records.get(prefix + b"enabled") != b"bool:1":
            continue
        discovery = int(records[prefix + b"discovery_port"].split(b":")[1])
        data = int(records[prefix + b"data_port"].split(b":")[1])
        expected.update((discovery, discovery + 1, data))
    assert expected, "No enabled AutoInterface in the formal application's configuration"
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        owned = set()
        for path in pathlib.Path(f"/proc/{process.pid}/fd").iterdir():
            try:
                target = os.readlink(path)
            except OSError:
                continue
            if target.startswith("socket:["):
                owned.add(target[8:-1])
        found = set()
        ports = set()
        for line in pathlib.Path("/proc/net/udp6").read_text().splitlines()[1:]:
            columns = line.split()
            if len(columns) > 9 and columns[9] in owned:
                port = int(columns[1].rsplit(":", 1)[1], 16)
                if port in expected:
                    found.add(columns[9])
                    ports.add(port)
        if expected <= ports:
            return found
        time.sleep(.03)
    raise AssertionError(f"Formal AutoInterface did not own its IPv6 UDP sockets: expected {expected}, found {ports}")


def main():
    with tempfile.TemporaryDirectory(prefix="trailmate-tms-application-") as temporary:
        root = pathlib.Path(temporary)
        profile = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else root / "device.conf"
        if len(sys.argv) < 3:
            profile.write_text("title=Configuration socket test\nux_pack=t_display_p4_touch\nwidth=800\nheight=480\nfullscreen=false\n")
        env = os.environ.copy()
        for key, directory in (("SETTINGS", "settings"), ("CACHE", "cache"), ("SD", "sd"), ("STATE", "state")):
            env[f"TRAIL_MATE_{key}_ROOT"] = str(root / directory)
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", TRAIL_MATE_LORA_DISABLE="1",
                   TRAIL_MATE_RUNTIME_MODE="local", TRAIL_MATE_HOSTLINK_BIND="127.0.0.1")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            env["TRAIL_MATE_HOSTLINK_PORT"] = str(probe.getsockname()[1])
        log_path = root / "application.log"
        endpoint_path = root / "sd/hostlink/endpoint.txt"
        peer = None
        with log_path.open("wb") as log:
            process = subprocess.Popen([sys.argv[1], "--profile", str(profile), "--quit-after-ms", "8000"], env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 15
                session = secrets.token_bytes(16)
                while True:
                    assert process.poll() is None, log_path.read_text(errors="replace")[-4000:]
                    if endpoint_path.exists():
                        try:
                            peer = Peer(endpoint_path.read_text().strip(), session)
                            break
                        except (OSError, ConnectionError):
                            pass
                    assert time.monotonic() < deadline, "Formal application did not start HostLink"
                    time.sleep(.03)
                capabilities = peer.request(0, [])
                assert all(code in capabilities[4] for code in (17, 20, 21, 32, 33, 34, 35, 36))
                original, base = peer.document()
                original_udp = auto_udp_sockets(process, original)
                candidate, replacements = re.subn(rb"(?m)^device.node_name=str:.*$", b"device.node_name=str:Socket-Validated", original)
                assert replacements == 1
                digest, transfer = hashlib.sha256(candidate).digest(), secrets.token_bytes(16)
                peer.request(32, [transfer, 1, len(candidate), digest])
                for offset in range(0, len(candidate), 320):
                    peer.request(33, [transfer, offset, candidate[offset:offset + 320]])
                peer.request(35, [transfer])
                try:
                    receipt = peer.request(21, [transfer, base])
                    assert receipt == [transfer, digest, 1]
                except (ConnectionError, OSError):
                    pass  # A lost ACK is confirmed below after the real exec.
                peer.close()
                peer = None
                deadline = time.monotonic() + 15
                while True:
                    assert process.poll() is None, log_path.read_text(errors="replace")[-4000:]
                    try:
                        if endpoint_path.exists():
                            peer = Peer(endpoint_path.read_text().strip(), session)
                            restored, _ = peer.document()
                            assert b"device.node_name=str:Socket-Validated\n" in restored
                            assert peer.request(21, [transfer, base]) == [transfer, digest, 0]
                            restored_udp = auto_udp_sockets(process, restored)
                            assert not original_udp.intersection(restored_udp), "Old AutoInterface sockets survived exec"
                            break
                    except (OSError, ConnectionError):
                        if peer:
                            peer.close()
                            peer = None
                    assert time.monotonic() < deadline, "Application did not resume after configuration exec"
                    time.sleep(.03)
                peer.request(3, [])
                peer.close()
                peer = None
                assert process.wait(timeout=15) == 0
                print("Formal application: loopback HostLink, paged config, upload, atomic commit, real exec, receipt replay and orderly exit passed")
                if original_udp:
                    print("Formal AutoInterface: configured IPv6 UDP sockets were opened and replaced across exec")
            except Exception:
                print(log_path.read_text(errors="replace")[-4000:], file=sys.stderr)
                raise
            finally:
                if peer:
                    peer.close()
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == "__main__":
    main()
