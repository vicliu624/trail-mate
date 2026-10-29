"""Formal app: public TMS configuration, genuine LXMF mailbox and durable chat."""
import base64
import hashlib
import json
import os
import pathlib
import re
import secrets
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time

from linux_tms_application_test import Peer


def stop_owned_process(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main():
    repository = pathlib.Path(__file__).resolve().parents[3]
    server = subprocess.Popen([sys.executable, str(repository / "tests/reticulum_gateway/propagation_mailbox_reference.py"), "--storage-server"],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    process = None
    peer = None
    try:
        info = json.loads(server.stdout.readline())
        with tempfile.TemporaryDirectory(prefix="trailmate-propagation-application-") as temporary:
            root = pathlib.Path(temporary)
            profile = root / "device.conf"
            profile.write_text("title=Propagation storage test\nux_pack=t_display_p4_touch\nwidth=800\nheight=480\nfullscreen=false\n")
            env = os.environ.copy()
            for key, directory in (("SETTINGS", "settings"), ("CACHE", "cache"), ("SD", "sd"), ("STATE", "state")):
                env[f"TRAIL_MATE_{key}_ROOT"] = str(root / directory)
            env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", TRAIL_MATE_LORA_DISABLE="1",
                       TRAIL_MATE_RUNTIME_MODE="local", TRAIL_MATE_HOSTLINK_BIND="127.0.0.1")
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                env["TRAIL_MATE_HOSTLINK_PORT"] = str(probe.getsockname()[1])
            endpoint = root / "sd/hostlink/endpoint.txt"
            database = root / "settings/trailmate.sqlite3"
            session = secrets.token_bytes(16)

            def connect():
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    assert process.poll() is None, (root / "application.log").read_text(errors="replace")[-4000:]
                    if endpoint.exists():
                        try:
                            candidate = Peer(endpoint.read_text().strip(), session)
                            candidate.document()
                            return candidate
                        except (OSError, ConnectionError):
                            pass
                    time.sleep(.03)
                raise AssertionError("Formal application HostLink did not become ready")

            def rows():
                with sqlite3.connect(f"file:{database}?mode=ro", uri=True) as db:
                    return db.execute("SELECT protocol,text,reticulum_identity_valid,reticulum_destination_hash,reticulum_identity_hash FROM chat_messages WHERE text LIKE 'Official LXMF mail%'").fetchall()

            with (root / "application.log").open("wb") as log:
                arguments = [sys.argv[1], "--profile", str(profile), "--quit-after-ms", "45000"]
                process = subprocess.Popen(arguments, env=env, stdout=log, stderr=log)
                try:
                    peer = connect()
                    original, base = peer.document()
                    with sqlite3.connect(f"file:{database}?mode=ro", uri=True) as db:
                        public_key = db.execute("SELECT public_key FROM linux_reticulum_identity WHERE id=1").fetchone()[0]
                    assert len(public_key) == 64
                    server.stdin.write("RECIPIENT " + public_key.hex() + "\n"); server.stdin.flush()
                    replacements = {
                        "protocol.active": "u8:4", "rt.net.interface_count": "u8:1",
                        "rt.net.interface.0.id": "str:mailbox-reference", "rt.net.interface.0.type": "u8:2",
                        "rt.net.interface.0.enabled": "bool:1", "rt.net.interface.0.target_host": "str:127.0.0.1",
                        "rt.net.interface.0.target_port": f"u16:{info['port']}",
                        "rt.net.propagation.automatic_node": "bool:0",
                        "rt.net.propagation.node_hash": "b64:" + base64.b64encode(bytes.fromhex(info["destination"])).decode(),
                        "rt.net.propagation.sync_interval_s": "u32:60",
                    }
                    candidate = original
                    for key, value in replacements.items():
                        candidate, count = re.subn(rb"(?m)^" + re.escape(key.encode()) + rb"=.*$", key.encode() + b"=" + value.encode(), candidate)
                        assert count == 1, key
                    transfer, digest = secrets.token_bytes(16), hashlib.sha256(candidate).digest()
                    peer.request(32, [transfer, 1, len(candidate), digest])
                    for offset in range(0, len(candidate), 320):
                        peer.request(33, [transfer, offset, candidate[offset:offset + 320]])
                    peer.request(35, [transfer])
                    try:
                        peer.request(21, [transfer, base])
                    except (OSError, ConnectionError):
                        pass
                    peer.close(); peer = None
                    peer = connect()
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline and not rows():
                        assert process.poll() is None
                        time.sleep(.05)
                    stored = rows()
                    assert len(stored) == 1, ("missing/duplicate durable propagation message", len(stored))
                    author = bytes.fromhex(info["author"])
                    author_identity = hashlib.sha256(author).digest()[:16]
                    author_destination = hashlib.sha256(hashlib.sha256(b"lxmf.delivery").digest()[:10] + author_identity).digest()[:16]
                    assert stored[0][0] == 4 and len(stored[0][1]) == int(os.environ.get("TRAIL_MATE_TEST_MESSAGE_BYTES", "40000")) and stored[0][2] == 1
                    assert stored[0][3] == author_destination and stored[0][4] == author_identity
                    server.stdin.write("CHECK\n"); server.stdin.flush()
                    output, errors = server.communicate(timeout=10)
                    assert server.returncode == 0, output + errors
                    print(output.strip())
                    peer.close(); peer = None
                    stop_owned_process(process)
                    process = subprocess.Popen(arguments[:-1] + ["3000"], env=env, stdout=log, stderr=log)
                    peer = connect()
                    assert rows() == stored, "Committed propagation chat changed across application restart"
                    peer.close(); peer = None
                    assert process.wait(timeout=15) == 0, "Restarted application did not complete its timed exit"
                    print("Formal application: imported TCP/propagation config, genuine encrypted mailbox delivery, SQLite author identity, durable remote acknowledgement and persistent chat across restart passed")
                except Exception:
                    print((root / "application.log").read_text(errors="replace")[-5000:], file=sys.stderr)
                    raise
                finally:
                    if peer: peer.close(); peer = None
                    stop_owned_process(process)
    finally:
        if server.poll() is None:
            server.terminate()
            try: server.wait(timeout=5)
            except subprocess.TimeoutExpired: server.kill(); server.wait()


if __name__ == "__main__":
    main()
