"""Replicate a completed network acceptance database to a fresh directory.

The fresh directory learns its peer from native RNS announces, fetches objects
over LXMF, then serves a new reader after both services have been stopped and
only the replica restarted. The origin's private identity is never copied.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import socket
import sqlite3
import subprocess
import sys
import threading
import time
from types import SimpleNamespace


def replica(work, port, origin_port):
    from directory_service import DirectoryService, exclusive_state
    config = work / "rns"
    config.mkdir(exist_ok=True)
    content = ("[reticulum]\nshare_instance = No\nenable_transport = Yes\n[interfaces]\n"
               "[[Origin link]]\nenabled = Yes\ntype = TCPClientInterface\ntarget_host = 127.0.0.1\n"
               f"target_port = {origin_port}\n[[Reader link]]\nenabled = Yes\ntype = TCPServerInterface\n"
               f"listen_ip = 127.0.0.1\nlisten_port = {port}\n")
    path = config / "config"
    if path.exists():
        assert path.read_text() == content
    else:
        path.write_text(content)
    with exclusive_state(work / "service"):
        service = DirectoryService(work / "service", config, "Replication acceptance directory")

        def stop_when_requested():
            while not service.stopped.wait(0.1):
                if (work / "stop").exists():
                    service.stopped.set()

        threading.Thread(target=stop_when_requested, daemon=True).start()
        service.run(startup_delay=5, run_seconds=120)


def reader(work, port):
    from directory_store import unpacked, verify_cache
    from interop_peer import Peer
    peer = Peer(SimpleNamespace(state=str(work / "reader"), mode="probe", host="127.0.0.1", port=port))
    if not peer.found.wait(20):
        raise TimeoutError("Replica announcement missing")
    signed = unpacked((work / "expected.bin").read_bytes())
    expected = verify_cache(signed)
    query = peer.exchange(2, [[-900000000, -1800000000, 900000000, 1800000000], 7, None, 20, None])
    assert query[5][1] == [expected.summary]
    assert peer.exchange(3, [expected.cache_id, expected.revision_hash, None])[5] == [signed, 1, 0]
    print(json.dumps({"event": "replica_restart_reader_verified", "revision": expected.record[3],
                      "origin": "offline", "signature": "valid"}), flush=True)


def acceptance(origin, work):
    from directory_store import unpacked, verify_cache
    expected = verify_cache(unpacked((origin / "publisher" / "sent-signed.bin").read_bytes()))
    origin_port = int(re.search(r"listen_port = (\d+)", (origin / "rns" / "config").read_text())[1])
    work.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(origin / "publisher" / "sent-signed.bin", work / "expected.bin")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    running = []

    def start(root, script, mode, log_name, extra):
        stop = root / "stop"
        if stop.exists():
            stop.unlink()
        log = (root / log_name).open("w", encoding="utf-8")
        process = subprocess.Popen([sys.executable, str(script), mode, "--work", str(root), *extra],
                                   stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
        running.append((root, process, log))
        return process

    def stop_all():
        for root, process, log in running:
            (root / "stop").touch()
        for root, process, log in running:
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=10)
                raise RuntimeError(f"Service failed to shut down: {root}")
            finally:
                log.close()
            if process.returncode:
                raise RuntimeError(f"Service failed with {process.returncode}: {root}")
        running.clear()

    try:
        source = start(origin, Path(__file__).with_name("test_directory_network.py"), "serve",
                       "replication-origin.log", ["--port", str(origin_port)])
        # Ensure the origin listener is ready before attaching the client interface.
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if '"event": "ready"' in (origin / "replication-origin.log").read_text(encoding="utf-8"):
                break
            if source.poll() is not None:
                raise RuntimeError("Origin stopped before readiness")
            time.sleep(0.1)
        target = start(work, Path(__file__), "replica", "replication.log",
                       ["--port", str(port), "--origin-port", str(origin_port)])
        deadline = time.monotonic() + 85
        while time.monotonic() < deadline:
            if source.poll() is not None or target.poll() is not None:
                raise RuntimeError("A directory stopped during replication")
            database = work / "service" / "directory.sqlite"
            ready = '"event": "ready"' in (work / "replication.log").read_text(encoding="utf-8")
            if ready and database.exists():
                connection = sqlite3.connect(database)
                try:
                    row = connection.execute("SELECT raw,signature FROM objects WHERE hash=?", (expected.revision_hash,)).fetchone()
                    if row and list(row) == expected.signed:
                        print(json.dumps({"event": "automatic_replication_verified", "revision": 2}), flush=True)
                        break
                finally:
                    connection.close()
            time.sleep(0.2)
        else:
            raise TimeoutError("Replica did not accept the exact signed object")
        stop_all()
        start(work, Path(__file__), "replica", "replication-restart.log",
              ["--port", str(port), "--origin-port", str(origin_port)])
        client = subprocess.run([sys.executable, __file__, "read", "--work", str(work), "--port", str(port)],
                                capture_output=True, text=True, timeout=50, creationflags=flags)
        (work / "reader.log").write_text(client.stdout + client.stderr, encoding="utf-8")
        if client.returncode:
            raise RuntimeError(client.stdout + client.stderr)
        print(client.stdout, end="", flush=True)
    finally:
        stop_all()
    print(json.dumps({"event": "replication_network_acceptance_passed", "work": str(work)}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("acceptance", "replica", "read"))
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--origin", type=Path)
    parser.add_argument("--port", type=int)
    parser.add_argument("--origin-port", type=int)
    args = parser.parse_args()
    if args.mode == "acceptance":
        acceptance(args.origin.resolve(), args.work.resolve())
    elif args.mode == "replica":
        replica(args.work.resolve(), args.port, args.origin_port)
    else:
        reader(args.work.resolve(), args.port)


if __name__ == "__main__":
    main()
