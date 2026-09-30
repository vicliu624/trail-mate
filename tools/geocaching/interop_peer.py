"""Local Reticulum/LXMF fixture peer for the device browse milestone.

This is an interoperability test peer, not a production public directory.
It serves fixtures and a bounded, in-memory publication test. By default its
Reticulum interface is loopback-only; no existing RNS configuration is used.
API usage follows https://github.com/markqvist/LXMF/blob/master/docs/example_receiver.py
"""
import argparse
import copy
import json
import hashlib
import os
from pathlib import Path
import queue
import threading
import time

import LXMF
import RNS
import msgpack

APP_TYPE = "trailmate.geocache"
ASPECT = "trailmate.geocache.directory"
FIXTURES = Path(__file__).resolve().parents[2] / "modules/core_geocaching/tests/fixtures"


def packed(value):
    return msgpack.packb(value, use_bin_type=True)


def verified_cache(signed):
    if not isinstance(signed, list) or len(signed) != 2:
        raise ValueError("Invalid SignedCache")
    raw, signature = signed
    if not isinstance(raw, bytes) or len(raw) > 4096 or not isinstance(signature, bytes) or len(signature) != 64:
        raise ValueError("Invalid record/signature sizes")
    record = msgpack.unpackb(raw, raw=False)
    if not isinstance(record, list) or len(record) != 16 or packed(record) != raw or type(record[0]) is not int or record[0] != 1:
        raise ValueError("Invalid canonical record")
    if not isinstance(record[1], bytes) or len(record[1]) != 64 or not isinstance(record[2], bytes) or len(record[2]) != 16:
        raise ValueError("Invalid author or nonce")
    if type(record[3]) is not int or not 1 <= record[3] <= 0xFFFFFFFF:
        raise ValueError("Invalid revision")
    if (record[3] == 1 and record[4] is not None) or (record[3] > 1 and (not isinstance(record[4], bytes) or len(record[4]) != 32)):
        raise ValueError("Invalid predecessor")
    for index, low, high in ((5, 0, 2), (6, -900000000, 900000000), (7, -1800000000, 1799999999),
                             (11, 2, 10), (12, 2, 10), (13, 0, 5), (14, 0, 253402300799), (15, 0, 253402300799)):
        if type(record[index]) is not int or not low <= record[index] <= high:
            raise ValueError("Record field out of range")
    if record[15] < record[14]:
        raise ValueError("Record timestamps reversed")
    for index, limit in ((8, 96), (9, 2048), (10, 512)):
        value = record[index]
        if not isinstance(value, str) or len(value.encode("utf-8")) > limit or (index == 8 and not value.strip()):
            raise ValueError("Invalid record text")
        if any((ord(c) < 32 and not (index != 8 and c in "\t\n")) or 127 <= ord(c) <= 159 or ord(c) in (65534, 65535) for c in value):
            raise ValueError("Forbidden record control character")
    identity = RNS.Identity(create_keys=False)
    identity.load_public_key(record[1])
    if not identity.validate(signature, b"trailmate.geocache/sign/v1\0" + raw):
        raise ValueError("Invalid author signature")
    cache_id = hashlib.sha256(b"trailmate.geocache/id/v1\0" + record[1] + record[2]).digest()
    revision = hashlib.sha256(b"trailmate.geocache/revision/v1\0" + raw).digest()
    summary = [cache_id, record[3], revision, record[5], record[6], record[7], record[8], record[11], record[12], record[13], len(packed(signed))]
    return cache_id, revision, record, summary


def payload(message):
    if not message.signature_validated or message.fields.get(0xFB) != APP_TYPE:
        raise ValueError("Unauthenticated or unrelated LXMF message")
    raw = message.fields.get(0xFC)
    if not isinstance(raw, bytes) or len(raw) > 8192:
        raise ValueError("Invalid application payload")
    result = msgpack.unpackb(raw, raw=False)
    if packed(result) != raw or not isinstance(result, list) or len(result) != 6:
        raise ValueError("Noncanonical envelope")
    if result[0] != 1 or not isinstance(result[3], bytes) or len(result[3]) != 16:
        raise ValueError("Invalid request identity")
    return result


class Peer:
    def __init__(self, args):
        self.args = args
        self.state = Path(args.state).resolve()
        self.state.mkdir(parents=True, exist_ok=True)
        config_dir = self.state / "rns"
        config_dir.mkdir(exist_ok=True)
        interface = (
            f"type = TCPServerInterface\nlisten_ip = {args.host}\nlisten_port = {args.port}\n"
            if args.mode == "directory" else
            f"type = TCPClientInterface\ntarget_host = {args.host}\ntarget_port = {args.port}\n"
        )
        config = "[reticulum]\nshare_instance = No\nenable_transport = Yes\n[interfaces]\n[[Interop TCP]]\nenabled = Yes\n" + interface
        config_path = config_dir / "config"
        if config_path.exists() and config_path.read_text(encoding="utf-8") != config:
            raise ValueError("Use a fresh --state directory for a different test configuration")
        config_path.write_text(config, encoding="utf-8")
        self.rns = RNS.Reticulum(configdir=str(config_dir), loglevel=RNS.LOG_WARNING)
        identity_path = self.state / "identity"
        self.identity = RNS.Identity.from_file(str(identity_path)) if identity_path.exists() else RNS.Identity()
        if not identity_path.exists():
            self.identity.to_file(str(identity_path))
        self.router = LXMF.LXMRouter(storagepath=str(self.state / "lxmf"), enforce_stamps=False)
        self.delivery = self.router.register_delivery_identity(self.identity, display_name="Trail Mate interop peer", stamp_cost=None)
        self.router.register_delivery_callback(self.received)
        self.responses = queue.Queue(maxsize=4)
        self.found = threading.Event()
        self.remote = None
        self.epoch = os.urandom(16)
        self.directory = None
        self.published = {}
        self.history = {}
        self.publication_replies = {}
        if args.mode == "directory":
            self.directory = RNS.Destination(self.identity, RNS.Destination.IN, RNS.Destination.SINGLE,
                                             "trailmate", "geocache", "directory")
        else:
            RNS.Transport.register_announce_handler(self)
        self.router.announce(self.delivery.hash)

    aspect_filter = ASPECT

    def received_announce(self, destination_hash, announced_identity, app_data):
        try:
            announcement = msgpack.unpackb(app_data, raw=False)
            delivery = RNS.Destination(announced_identity, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
            if len(announcement) != 5 or announcement[0] != 1 or announcement[1] != delivery.hash:
                return
            self.remote = delivery
            self.found.set()
        except (ValueError, TypeError, IndexError):
            return

    def send(self, destination, application):
        message = LXMF.LXMessage(destination, self.delivery, "", "Geocaching interop",
                                 fields={0xFB: APP_TYPE, 0xFC: packed(application)},
                                 desired_method=LXMF.LXMessage.DIRECT)
        self.router.handle_outbound(message)

    def received(self, message):
        try:
            envelope = payload(message)
            if self.args.mode != "directory":
                if self.remote and message.source_hash == self.remote.hash:
                    (self.state / f"received-{envelope[2]}.lxmf").write_bytes(message.packed)
                    (self.state / f"received-{envelope[2]}.pub").write_bytes(message.get_source().identity.get_public_key())
                    self.responses.put_nowait(envelope)
                return
            if envelope[1] != 0 or envelope[2] not in (0, 1, 2, 3):
                return
            if envelope[2] == 1:
                if len(envelope[5]) != 1:
                    raise ValueError("Invalid publication body")
                key = (message.source_hash, envelope[3])
                fingerprint = hashlib.sha256(packed(envelope)).digest()
                prior = self.publication_replies.get(key)
                if prior:
                    if fingerprint != prior[0]:
                        raise ValueError("Request ID reused for different publication")
                    reply = prior[1]
                else:
                    signed = envelope[5][0]
                    cache_id, revision, record, summary = verified_cache(signed)
                    existing = self.published.get(cache_id)
                    duplicate = existing is not None and existing[1] == revision
                    if not existing and record[3] != 1:
                        raise ValueError("First version missing")
                    if existing and not duplicate:
                        predecessor = msgpack.unpackb(existing[0][0], raw=False)
                        if record[3] != predecessor[3] + 1 or record[4] != existing[1] or record[14] != predecessor[14] or record[15] < predecessor[15]:
                            raise ValueError("Invalid successor chain")
                    reply = [1, 1, 1, envelope[3], 200, [cache_id, record[3], revision, int(duplicate), record[3], revision, record[5]]]
                    if len(packed(reply)) > envelope[4] or (not existing and len(self.published) >= 8) or len(self.publication_replies) >= 32 or (revision not in self.history and len(self.history) >= 32):
                        raise ValueError("Test directory capacity/budget reached")
                    self.published[cache_id] = (signed, revision, summary)
                    self.history[revision] = self.published[cache_id]
                    self.publication_replies[key] = (fingerprint, reply)
                self.send(message.get_source(), reply)
                print(json.dumps({"event": "publication", "replayed": prior is not None}), flush=True)
                return
            filename = {0: "capabilities-response-v1.bin", 2: "query-response-v1.bin", 3: "get-response-v1.bin"}[envelope[2]]
            reply = msgpack.unpackb((FIXTURES / filename).read_bytes(), raw=False)
            reply = copy.deepcopy(reply)
            reply[3] = envelope[3]
            if envelope[2] == 2:
                region, mask, author, limit, cursor = envelope[5]
                south, west, north, east = region
                rows = sorted(reply[5][1] + [entry[2] for entry in self.published.values()], key=lambda row: row[0])
                reply[5][1] = [row for row in rows if south <= row[4] <= north and west <= row[5] <= east
                               and mask & (1 << row[3])][:min(limit, 20)] if author is None and cursor is None else []
                reply[5][2] = None
            if len(packed(reply)) > envelope[4]:
                raise ValueError("Requested response budget is too small")
            if envelope[2] == 3:
                row = msgpack.unpackb((FIXTURES / "query-response-v1.bin").read_bytes(), raw=False)[5][1][0]
                cache_id, wanted, known = envelope[5]
                head = self.published.get(cache_id)
                published = head if wanted is None else self.history.get(wanted)
                if head and published and published[2][0] == cache_id and known is None:
                    reply[5] = [published[0], int(published[1] == head[1]), 0]
                elif cache_id != row[0] or wanted not in (None, row[2]) or known is not None:
                    raise ValueError("This test peer only serves the fixture revision")
            if len(packed(reply)) > envelope[4]:
                raise ValueError("Requested response budget is too small")
            self.send(message.get_source(), reply)
            print(json.dumps({"event": "reply", "operation": envelope[2], "request": envelope[3].hex()}), flush=True)
        except (ValueError, TypeError, IndexError, queue.Full) as error:
            print(json.dumps({"event": "rejected", "reason": str(error)}), flush=True)

    def run_directory(self):
        print(json.dumps({"event": "ready", "directory": self.directory.hash.hex(),
                          "delivery": self.delivery.hash.hex(), "interface": f"{self.args.host}:{self.args.port}",
                          "data": "checked-in test fixture, not a real treasure"}), flush=True)
        deadline = time.monotonic() + self.args.seconds
        while time.monotonic() < deadline:
            self.router.announce(self.delivery.hash)
            self.directory.announce(app_data=packed([1, self.delivery.hash, self.epoch, 1, "Trail Mate fixture directory"]))
            time.sleep(min(3, max(0, deadline - time.monotonic())))

    def exchange(self, operation, body, request=None):
        request = request or os.urandom(16)
        self.send(self.remote, [1, 0, operation, request, 2048, body])
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            reply = self.responses.get(timeout=max(0.01, deadline - time.monotonic()))
            if reply[3] != request:
                continue  # A retried transport response from a preceding step.
            if reply[:3] != [1, 1, operation] or reply[4] != 200:
                raise ValueError("Unexpected response")
            return reply
        raise TimeoutError("Application response not received")

    def run_publication_probe(self):
        if not self.found.wait(20):
            raise TimeoutError("Directory announcement was not received")
        utc = int(time.time())
        record = [1, self.identity.get_public_key(), os.urandom(16), 1, None, 0,
                  305000000, 1205000000, "Interop published cache", "Local test only", "", 2, 2, 0, utc, utc]
        unsigned = packed(record)
        signed = [unsigned, self.identity.sign(b"trailmate.geocache/sign/v1\0" + unsigned)]
        cache_id, revision, _, _ = verified_cache(signed)
        (self.state / "sent-signed.bin").write_bytes(packed(signed))
        self.exchange(0, [])
        request = os.urandom(16)
        accepted = self.exchange(1, [signed], request)
        if accepted[5] != [cache_id, 1, revision, 0, 1, revision, 0]:
            raise ValueError("Directory did not confirm the exact submitted version")
        repeated = self.exchange(1, [signed], request)
        if repeated != accepted:
            raise ValueError("Request retry did not replay the original result")
        query = self.exchange(2, [[-900000000, -1800000000, 900000000, 1800000000], 7, None, 20, None])
        if not any(row[0] == cache_id and row[2] == revision and row[6] == record[8] for row in query[5][1]):
            raise ValueError("Published cache absent from subsequent query")
        downloaded = self.exchange(3, [cache_id, revision, None])
        if downloaded[5] != [signed, 1, 0] or verified_cache(downloaded[5][0])[:2] != (cache_id, revision):
            raise ValueError("Downloaded version differs from the publication")
        (self.state / "sent-signed-v1.bin").write_bytes(packed(signed))
        successor = copy.deepcopy(record)
        successor[3], successor[4] = 2, revision
        successor[8] = "Interop published cache v2"
        successor[15] = max(int(time.time()), utc)
        raw = packed(successor)
        signed_v2 = [raw, self.identity.sign(b"trailmate.geocache/sign/v1\0" + raw)]
        same_id, hash_v2, _, _ = verified_cache(signed_v2)
        if same_id != cache_id:
            raise ValueError("Successor changed cache identity")
        request_v2 = os.urandom(16)
        accepted_v2 = self.exchange(1, [signed_v2], request_v2)
        if accepted_v2[5] != [cache_id, 2, hash_v2, 0, 2, hash_v2, 0]:
            raise ValueError("Successor was not accepted")
        if self.exchange(1, [signed], request) != accepted:
            raise ValueError("Head update changed an earlier request result")
        if self.exchange(1, [signed_v2], request_v2) != accepted_v2:
            raise ValueError("Successor request retry changed its result")
        query = self.exchange(2, [[-900000000, -1800000000, 900000000, 1800000000], 7, None, 20, None])
        if not any(row[0] == cache_id and row[1] == 2 and row[2] == hash_v2 for row in query[5][1]):
            raise ValueError("Query did not advance to successor")
        historic = self.exchange(3, [cache_id, revision, None])
        if historic[5] != [signed, 0, 0]:
            raise ValueError("Predecessor no longer retrievable")
        for suffix in ("lxmf", "pub"):
            (self.state / f"received-history.{suffix}").write_bytes((self.state / f"received-3.{suffix}").read_bytes())
        latest = self.exchange(3, [cache_id, hash_v2, None])
        if latest[5] != [signed_v2, 1, 0]:
            raise ValueError("Successor download differs")
        (self.state / "sent-signed.bin").write_bytes(packed(signed_v2))
        print(json.dumps({"event": "publication_verified", "retry": "same result", "query": "contains new cache",
                          "download": "same signed record", "successor": 2, "history": "v1 retained", "cache_id": cache_id.hex()}), flush=True)

    def run_probe(self):
        if not self.found.wait(20):
            raise TimeoutError("Directory announcement was not received")
        operations = [(0, []), (2, [[-900000000, -1800000000, 900000000, 1800000000], 3, None, 20, None])]
        for operation, body in operations:
            request = os.urandom(16)
            self.send(self.remote, [1, 0, operation, request, 2048, body])
            reply = self.responses.get(timeout=20)
            if reply[:3] != [1, 1, operation] or reply[3] != request or reply[4] != 200:
                raise ValueError("Unexpected response")
            if operation == 2 and (len(reply[5][1]) != 1 or reply[5][1][0][6] != "Test"):
                raise ValueError("Fixture cache was not returned")
            if operation == 2:
                row = reply[5][1][0]
                operations.append((3, [row[0], row[2], None]))
            if operation == 3 and (len(reply[5][0]) != 2 or len(reply[5][0][1]) != 64):
                raise ValueError("Signed fixture record was not returned")
            print(json.dumps({"event": "verified", "operation": operation, "signature": "valid",
                              "rows": len(reply[5][1]) if operation == 2 else None}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("directory", "probe", "publish-probe"))
    parser.add_argument("--state", required=True, help="Private test state directory; do not commit it")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=44242)
    parser.add_argument("--seconds", type=int, default=60, help="Directory lifetime for a bounded test")
    args = parser.parse_args()
    peer = Peer(args)
    if args.mode == "directory":
        peer.run_directory()
    elif args.mode == "publish-probe":
        peer.run_publication_probe()
    else:
        peer.run_probe()


if __name__ == "__main__":
    main()
