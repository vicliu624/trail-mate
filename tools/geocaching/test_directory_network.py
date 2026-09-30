"""Bounded, loopback-only multi-process LXMF acceptance test.

Publisher and fresh reader run separately from the persistent directory. The
directory is stopped and restarted between them, keeping its identity/database.
No firmware build or existing Reticulum configuration is involved.
"""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time
from types import SimpleNamespace


def serve(work, port):
    from directory_service import DirectoryService, exclusive_state
    config = work / "rns"
    config.mkdir(exist_ok=True)
    expected = ("[reticulum]\nshare_instance = No\nenable_transport = Yes\n"
                "[interfaces]\n[[Local acceptance]]\nenabled = Yes\n"
                f"type = TCPServerInterface\nlisten_ip = 127.0.0.1\nlisten_port = {port}\n")
    path = config / "config"
    if path.exists():
        assert path.read_text() == expected
    else:
        path.write_text(expected)
    with exclusive_state(work / "service"):
        service = DirectoryService(work / "service", config, "Local acceptance directory")

        def stop_when_requested():
            while not service.stopped.wait(0.1):
                if (work / "stop").exists():
                    service.stopped.set()

        threading.Thread(target=stop_when_requested, daemon=True).start()
        service.run(startup_delay=5, run_seconds=90)


def read_after_restart(work, port):
    from interop_peer import Peer, verified_cache
    from directory_store import unpacked
    peer = Peer(SimpleNamespace(state=str(work / "reader"), mode="probe", host="127.0.0.1", port=port))
    if not peer.found.wait(20):
        raise TimeoutError("Restarted directory announcement missing")
    expected = unpacked((work / "publisher" / "sent-signed.bin").read_bytes())
    older = unpacked((work / "publisher" / "sent-signed-v1.bin").read_bytes())
    cache_id, revision, _, _ = verified_cache(expected)
    capabilities = peer.exchange(0, [])
    assert capabilities[5][2] == [0, 1, 2, 3, 4]
    query = peer.exchange(2, [[-900000000, -1800000000, 900000000, 1800000000], 7, None, 20, None])
    assert any(row[:3] == [cache_id, 2, revision] for row in query[5][1])
    assert peer.exchange(3, [cache_id, None, None])[5] == [expected, 1, 0]
    assert peer.exchange(3, [cache_id, verified_cache(older)[1], None])[5] == [older, 0, 0]
    baseline = peer.exchange(4, [None, 20])[5]
    assert baseline[2] == [[cache_id, 2, revision, 0]] and baseline[4] == 0
    assert peer.exchange(4, [baseline[3], 20])[5][2] == []
    print(json.dumps({"event": "restart_reader_verified", "cache_id": cache_id.hex(),
                      "latest": 2, "history": 1, "sync": "baseline and empty delta"}), flush=True)


def acceptance(work):
    work.mkdir(parents=True, exist_ok=False)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    identity = None
    for phase in ("publish", "restart"):
        stop = work / "stop"
        if stop.exists():
            stop.unlink()
        log_path = work / f"service-{phase}.log"
        with log_path.open("w", encoding="utf-8") as log:
            server = subprocess.Popen([sys.executable, __file__, "serve", "--work", str(work), "--port", str(port)],
                                      stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
            try:
                deadline = time.monotonic() + 20
                ready = None
                while time.monotonic() < deadline and server.poll() is None:
                    for line in log_path.read_text(encoding="utf-8").splitlines():
                        if line.startswith('{"event": "ready"'):
                            ready = json.loads(line)
                    if ready:
                        break
                    time.sleep(0.1)
                if not ready:
                    raise RuntimeError("Service not ready: " + log_path.read_text(encoding="utf-8"))
                current_identity = (ready["delivery"], ready["epoch"])
                if identity is not None:
                    assert identity == current_identity, "Restart changed directory identity/epoch"
                identity = current_identity
                if phase == "publish":
                    command = [sys.executable, str(Path(__file__).with_name("interop_peer.py")), "publish-probe",
                               "--state", str(work / "publisher"), "--port", str(port)]
                else:
                    command = [sys.executable, __file__, "read", "--work", str(work), "--port", str(port)]
                client = subprocess.run(command, capture_output=True, text=True, timeout=65, creationflags=flags)
                (work / f"client-{phase}.log").write_text(client.stdout + client.stderr, encoding="utf-8")
                if client.returncode:
                    raise RuntimeError(f"Client failed ({client.returncode}): {client.stdout}{client.stderr}")
                print(client.stdout, end="", flush=True)
            finally:
                stop.touch()
                try:
                    server.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    server.terminate()
                    server.wait(timeout=10)
                    raise RuntimeError("Service did not shut down gracefully")
            if server.returncode != 0:
                raise RuntimeError(log_path.read_text(encoding="utf-8"))
    print(json.dumps({"event": "network_acceptance_passed", "work": str(work)}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("acceptance", "serve", "read"))
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--port", type=int)
    args = parser.parse_args()
    {"acceptance": lambda: acceptance(args.work.resolve()),
     "serve": lambda: serve(args.work.resolve(), args.port),
     "read": lambda: read_after_restart(args.work.resolve(), args.port)}[args.mode]()


if __name__ == "__main__":
    main()
