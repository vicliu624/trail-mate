"""Validate native original-message signing/encryption with official RNS/LXMF."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

import LXMF
import RNS

assert RNS.__version__ == "1.5.7" and LXMF.__version__ == "1.2.1"
with tempfile.TemporaryDirectory(prefix="trailmate-outgoing-reference-") as temporary:
    pathlib.Path(temporary, "config").write_text("[reticulum]\nshare_instance=no\nenable_transport=no\n[interfaces]\n")
    RNS.Reticulum(configdir=temporary, loglevel=RNS.LOG_CRITICAL)
    recipient = RNS.Identity()
    destination = RNS.Destination(recipient, RNS.Destination.IN, RNS.Destination.SINGLE, "lxmf", "delivery")
    result = subprocess.run([sys.argv[1], recipient.get_public_key().hex()], check=True, text=True, capture_output=True, timeout=15)
    data = json.loads(result.stdout)
    author_key = bytes.fromhex(data["author"])
    author = RNS.Identity(create_keys=False)
    author.load_public_key(author_key)
    source = RNS.Destination(author, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
    RNS.Identity.remember(packet_hash=None, destination_hash=source.hash, public_key=author_key, app_data=None)
    encrypted = bytes.fromhex(data["message"])
    assert encrypted[:16] == destination.hash
    assert hashlib.sha256(encrypted).hexdigest() == data["transient"]
    plaintext = destination.decrypt(encrypted[16:])
    assert plaintext is not None
    message = LXMF.LXMessage.unpack_from_bytes(destination.hash + plaintext)
    assert message.signature_validated and message.source_hash == source.hash
    assert message.content.decode().startswith("Native encrypted mail") and len(message.content) == 40000
    assert message.hash.hex() == data["message_hash"]
    print("Official RNS/LXMF decrypted native 40 KB propagation envelope and verified its original author, message hash and transient ID")
