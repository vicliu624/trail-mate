"""Exercise native propagation RPC against the official RNS 1.5.4 stack."""
import hashlib
import json
import pathlib
import socket
import subprocess
import sys
import tempfile

import RNS
from RNS.vendor import umsgpack


def server():
    with tempfile.TemporaryDirectory(prefix="trailmate-rpc-reference-") as temporary:
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        pathlib.Path(temporary, "config").write_text(
            "[reticulum]\nshare_instance=no\nenable_transport=no\n[interfaces]\n"
            f"[[Reference]]\ntype=TCPServerInterface\nenabled=yes\nlisten_ip=127.0.0.1\nlisten_port={port}\n")
        reticulum = RNS.Reticulum(configdir=temporary, loglevel=RNS.LOG_CRITICAL)
        RNS.loglevel = -1
        identity = RNS.Identity()
        destination = RNS.Destination(identity, RNS.Destination.IN, RNS.Destination.SINGLE, "lxmf", "propagation")
        links = {}
        uploads = []

        def resource_concluded(resource):
            if resource.status != RNS.Resource.COMPLETE:
                return
            resource.data.seek(0)
            data = resource.data.read()
            if data.startswith(b"UPLOAD"):
                expected = b"UPLOAD" + b"".join(hashlib.sha256(str(i).encode()).digest() for i in range(1250))
                assert data == expected
                uploads.append(data)

        def established(link):
            link.set_resource_strategy(RNS.Link.ACCEPT_ALL)
            link.set_resource_concluded_callback(resource_concluded)
            links[link.link_id] = link

        def response(path, data, request_id, link_id, remote_identity, requested_at):
            assert remote_identity is not None
            if data == 1:
                RNS.Packet(links[link_id], umsgpack.packb([request_id, None]), context=RNS.Packet.RESPONSE).send()
                return None
            if data == 2:
                return bytes([42]) * 6000
            if data == 3:
                return b"".join(hashlib.sha256(str(i).encode()).digest() for i in range(1250))
            if data == 4:
                RNS.Packet(links[link_id], umsgpack.packb([bytes([0x99]) * 16, 0]), context=RNS.Packet.RESPONSE).send()
                return 19
            if isinstance(data, bytes):
                return len(data)
            return 17

        destination.set_link_established_callback(established)
        destination.register_request_handler("/get", response_generator=response, allow=RNS.Destination.ALLOW_ALL)
        print(json.dumps({"port": port, "destination": destination.hash.hex(), "key": identity.get_public_key().hex()}), flush=True)
        assert sys.stdin.readline().strip() == "STOP"
        assert len(uploads) == 1, "expected one complete resource upload"


if __name__ == "__main__":
    assert RNS.__version__ == "1.5.4"
    if len(sys.argv) == 2 and sys.argv[1] == "--server":
        server()
    else:
        peer = subprocess.Popen([sys.executable, __file__, "--server"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        try:
            info = json.loads(peer.stdout.readline())
            subprocess.run([sys.argv[1], "--reference", "127.0.0.1", str(info["port"]), info["destination"], info["key"]], check=True, timeout=100)
            peer.stdin.write("STOP\n"); peer.stdin.flush()
            assert peer.wait(timeout=5) == 0, peer.stderr.read()
        finally:
            if peer.poll() is None:
                peer.terminate()
                try:
                    peer.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    peer.kill(); peer.wait()
