"""Run as root inside `unshare -n`; all links live only in isolated namespaces."""
import argparse
import os
import pathlib
import selectors
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native")
    parser.add_argument("reference_python")
    parser.add_argument("--bits", type=int, default=0)
    args = parser.parse_args()
    assert os.geteuid() == 0, "Use an isolated root network namespace"
    reference = pathlib.Path(__file__).resolve().parents[3] / "tests/reticulum_gateway/auto_interface_reference.py"
    group = f"trailmate-test-{os.getpid()}"
    discovery = 23000 + os.getpid() % 5000
    data = discovery + 10000
    child = subprocess.Popen(["unshare", "-n", args.reference_python, str(reference), "--peer", "--device", "test-peer",
        "--group", group, "--discovery", str(discovery), "--data", str(data), "--bits", str(args.bits)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
    native = None
    try:
        assert child.stdout.readline().strip() == "NAMESPACE_READY"
        subprocess.run(["ip", "link", "add", "test-native", "type", "veth", "peer", "name", "test-peer"], check=True)
        subprocess.run(["ip", "link", "set", "test-peer", "netns", str(child.pid)], check=True)
        subprocess.run(["ip", "link", "set", "test-native", "addrgenmode", "none"], check=True)
        subprocess.run(["ip", "link", "set", "test-native", "up"], check=True)
        subprocess.run(["ip", "-6", "address", "add", "fe80::1/64", "dev", "test-native", "nodad"], check=True)
        child.stdin.write("GO\n"); child.stdin.flush()
        native = subprocess.Popen([args.native, "--peer", group, str(discovery), str(data), str(args.bits)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
        selector = selectors.DefaultSelector()
        selector.register(native.stdout, selectors.EVENT_READ, "native")
        selector.register(child.stdout, selectors.EVENT_READ, "reference")
        observed = set()
        buffers = {"native": b"", "reference": b""}
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            for key, _ in selector.select(.2):
                chunk = os.read(key.fd, 4096)
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                buffers[key.data] += chunk
                while b"\n" in buffers[key.data]:
                    line, buffers[key.data] = buffers[key.data].split(b"\n", 1)
                    line = line.decode().strip()
                    observed.add(line)
                    if line == "READY_READDRESS":
                        subprocess.run(["ip", "link", "set", "test-native", "down"], check=True)
                        subprocess.run(["ip", "link", "set", "test-native", "name", "renamed-native"], check=True)
                        subprocess.run(["ip", "-6", "address", "flush", "dev", "renamed-native", "scope", "link"], check=True)
                        subprocess.run(["ip", "-6", "address", "add", "fe80::3/64", "dev", "renamed-native", "nodad"], check=True)
                        subprocess.run(["ip", "link", "set", "renamed-native", "up"], check=True)
            if native.poll() is not None:
                if native.returncode:
                    child.terminate()
                    try:
                        child.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        child.kill(); child.wait()
                    raise AssertionError(f"Observed {observed}\nNative: {native.stderr.read()}\nReference: {child.stderr.read()}")
                if {"AUTO_INTEROP_PASSED", "VALID_ANNOUNCE", "PATH_REQUEST", "LINK_ESTABLISHED"} <= observed:
                    break
            assert child.poll() is None, child.stderr.read()
        assert {"READY_READDRESS", "AUTO_INTEROP_PASSED", "VALID_ANNOUNCE", "PATH_REQUEST", "LINK_ESTABLISHED"} <= observed, observed
        child.stdin.write("STOP\n"); child.stdin.flush()
        assert child.wait(timeout=5) == 0, child.stderr.read()
        print(f"AutoInterface/RNS 1.5.4: IFAC {args.bits} bits, IPv6 discovery, 500-byte duplex data, duplicate rejection, renamed/readdressed link, expiry, signed network announce and path routing passed")
    finally:
        for process in (native, child):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()


if __name__ == "__main__":
    main()
