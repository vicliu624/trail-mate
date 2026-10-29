"""Exercise the native mailbox controller against RNS 1.5.7 / LXMF 1.2.1."""
import json
import pathlib
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

import LXMF
import RNS
from RNS.vendor import umsgpack


def server(require_upload=True):
    with tempfile.TemporaryDirectory(prefix="trailmate-mailbox-reference-") as temporary:
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        pathlib.Path(temporary, "config").write_text(
            "[reticulum]\nshare_instance=no\nenable_transport=no\n[interfaces]\n"
            f"[[Reference]]\ntype=TCPServerInterface\nenabled=yes\nlisten_ip=127.0.0.1\nlisten_port={port}\n")
        RNS.Reticulum(configdir=temporary, loglevel=RNS.LOG_CRITICAL)
        RNS.loglevel = -1
        router = LXMF.LXMRouter(storagepath=temporary, autopeer=False, propagation_cost=13)
        router.enable_propagation()
        author = RNS.Identity()
        source = router.register_delivery_identity(author, display_name="Original author")
        outbound_recipient = RNS.Identity()
        outbound_destination = RNS.Destination(outbound_recipient, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
        print(json.dumps({"port": port, "destination": router.propagation_destination.hash.hex(),
                          "author": author.get_public_key().hex(), "outbound_recipient": outbound_recipient.get_public_key().hex()}), flush=True)
        line = sys.stdin.readline().strip().split()
        assert len(line) == 2 and line[0] == "RECIPIENT"
        recipient = RNS.Identity(create_keys=False)
        recipient.load_public_key(bytes.fromhex(line[1]))
        destination = RNS.Destination(recipient, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
        message_size = int(os.environ.get("TRAIL_MATE_TEST_MESSAGE_BYTES", "40000"))
        assert 18 <= message_size <= 40000
        content = "Official LXMF mail" + "x" * (message_size - 18)
        message = LXMF.LXMessage(destination, source, content, desired_method=LXMF.LXMessage.PROPAGATED)
        message.pack()
        packed = umsgpack.unpackb(message.propagation_packed)[1][0]
        assert router.lxmf_propagation(packed, stamp_value=16, stamp_data=bytes(32))
        # Keep a correctly encrypted envelope with an invalid original signature.
        rejected = LXMF.LXMessage(destination, source, "invalid original signature", desired_method=LXMF.LXMessage.PROPAGATED)
        rejected.pack()
        invalid = bytearray(rejected.packed)
        invalid[32] ^= 1
        invalid = destination.hash + destination.encrypt(bytes(invalid[16:]))
        assert router.lxmf_propagation(invalid, stamp_value=16, stamp_data=bytes(32))
        valid_id, invalid_id = RNS.Identity.full_hash(packed), RNS.Identity.full_hash(invalid)
        stop = threading.Event()

        def announce():
            while not stop.is_set():
                source.announce()
                router.propagation_destination.announce(app_data=router.get_propagation_node_app_data())
                stop.wait(1)

        worker = threading.Thread(target=announce, daemon=True)
        worker.start()
        assert sys.stdin.readline().strip() == "CHECK"
        if not require_upload:
            deadline = time.monotonic() + 5
            while valid_id in router.propagation_entries and time.monotonic() < deadline:
                time.sleep(.05)
        assert valid_id not in router.propagation_entries, "durably accepted message was not acknowledged"
        assert invalid_id in router.propagation_entries, "invalid-author message was deleted"
        if require_upload:
            deadline = time.monotonic() + 5
            uploaded = []
            while time.monotonic() < deadline:
                uploaded = [entry for entry in router.propagation_entries.values() if entry[0] == outbound_destination.hash]
                if uploaded:
                    break
                time.sleep(0.05)
            assert len(uploaded) == 1 and uploaded[0][6] >= 13, "native upload was not validated and stored by the node"
            stored = pathlib.Path(uploaded[0][1]).read_bytes()[:-32]
            plaintext = outbound_recipient.decrypt(stored[16:])
            assert plaintext is not None
            message = LXMF.LXMessage.unpack_from_bytes(stored[:16] + plaintext)
            assert message.signature_validated and message.source_hash == destination.hash
            assert len(message.content) == 40000 and message.content.startswith(b"Native propagation upload")
        stop.set()
        worker.join(timeout=2)
        print("Official LXMF node retained the rejected message and removed only the accepted message", flush=True)
        if require_upload:
            print("Official LXMF node validated the native propagation stamp, stored the upload and verified its original author after recipient decryption", flush=True)
        router.exit_handler()


if __name__ == "__main__":
    assert RNS.__version__ == "1.5.7" and LXMF.__version__ == "1.2.1"
    if len(sys.argv) == 2 and sys.argv[1] in ("--server", "--storage-server"):
        server(require_upload=sys.argv[1] == "--server")
    else:
        peer = subprocess.Popen([sys.executable, __file__, "--server"], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        client = None
        try:
            info = json.loads(peer.stdout.readline())
            client = subprocess.Popen([sys.argv[1], "127.0.0.1", str(info["port"]), info["destination"], info["author"], info["outbound_recipient"]],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            recipient = client.stdout.readline()
            assert recipient.startswith("RECIPIENT "), recipient + client.stderr.read()
            peer.stdin.write(recipient); peer.stdin.flush()
            output, errors = client.communicate(timeout=45)
            assert client.returncode == 0, output + errors
            print(output.strip())
            peer.stdin.write("CHECK\n"); peer.stdin.flush()
            output, errors = peer.communicate(timeout=10)
            assert peer.returncode == 0, output + errors
            print(output.strip())
        finally:
            for process in (client, peer):
                if process is not None and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait()
