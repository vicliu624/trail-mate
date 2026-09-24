"""Persistent public Geocaching endpoint over native Reticulum/LXMF.

Use an explicit Reticulum configuration directory. This process never edits
that configuration and has no HTTP business API. Keep --state private.
"""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import queue
import random
import signal
import threading
import time

import LXMF
import RNS
import msgpack

from directory_store import APP_TYPE, DirectoryStore, packed, unpacked
from directory_sync import ReplicaScheduler


def report(event, **fields):
    print(json.dumps({"event": event, **fields}, ensure_ascii=False), flush=True)


@contextmanager
def exclusive_state(state):
    """An OS-owned lock releases even after a crash; stale files are harmless."""
    state.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (state / "service.lock").open("a+b") as handle:
        if handle.tell() == 0:
            handle.write(b"\0")
            handle.flush()
        handle.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            handle.seek(0)
            if os.name == "nt":
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def load_identity(state):
    path = state / "identity"
    if path.exists():
        identity = RNS.Identity.from_file(str(path))
        if identity is None:
            raise ValueError("Cannot read persistent identity; refusing to replace it")
        return identity
    identity = RNS.Identity()
    # Persist the identity before creating any database/announce using it.
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        output.write(identity.get_private_key())
        output.flush()
        os.fsync(output.fileno())
    return identity


class DirectoryService:
    def __init__(self, state, config, name):
        self.identity = load_identity(state)
        self.rns = RNS.Reticulum(configdir=str(config), loglevel=RNS.LOG_WARNING)
        self.router = LXMF.LXMRouter(storagepath=str(state / "lxmf"),
                                     enforce_stamps=False, delivery_limit=16,
                                     autopeer=False)
        self.delivery = self.router.register_delivery_identity(self.identity, display_name=name, stamp_cost=None)
        self.discovery = RNS.Destination(self.identity, RNS.Destination.IN, RNS.Destination.SINGLE,
                                         "trailmate", "geocache", "directory")
        self.store = DirectoryStore(state / "directory.sqlite", self.delivery.hash, name)
        # Only validated source keys and <=8 KiB application payloads enter this
        # queue. Do not retain full transport messages/resources in app memory.
        self.inbound = queue.Queue(maxsize=32)
        self.awaiting_identity = queue.Queue(maxsize=8)
        self.deferred_identity = {}
        self.next_identity_request = 0
        self.outbound = []
        self.stopped = threading.Event()
        self.dropped = 0
        self.replication = ReplicaScheduler(self.store, report)
        RNS.Transport.register_announce_handler(self.replication)
        self.router.register_delivery_callback(self.received)

    def received(self, message):
        if message.destination_hash != self.delivery.hash or message.fields.get(0xFB) != APP_TYPE:
            return
        raw = message.fields.get(0xFC)
        if type(raw) is not bytes or not 0 < len(raw) <= 8192:
            return
        if not message.signature_validated:
            # The first direct LXMF message can precede the sender's announce
            # and backchannel identification. Keep only a bounded original
            # packet; execute nothing until LXMF verifies its full signature.
            if (message.unverified_reason == LXMF.LXMessage.SOURCE_UNKNOWN
                    and type(message.packed) is bytes and len(message.packed) <= 16000):
                try:
                    self.awaiting_identity.put_nowait((message.hash, message.source_hash, message.packed))
                except queue.Full:
                    self.dropped += 1
            return
        source = message.get_source()
        if source is None or source.identity is None:
            return
        expected = RNS.Destination.hash_from_name_and_identity("lxmf.delivery", source.identity)
        if message.source_hash != expected:
            return
        try:
            self.inbound.put_nowait((source.identity.get_public_key(), raw))
        except queue.Full:
            self.dropped += 1  # No request accepted; sender may retry its ID.

    def send(self, public_key, raw):
        identity = RNS.Identity(create_keys=False)
        if not identity.load_public_key(public_key):
            raise ValueError("Invalid authenticated source key")
        destination = RNS.Destination(identity, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
        kind = unpacked(raw)[1]
        content = "Geocache request" if kind == 0 else "Geocache response"
        message = LXMF.LXMessage(destination, self.delivery, content, "Trail Mate Geocache v1",
                                 fields={0xFB: APP_TYPE, 0xFC: raw}, desired_method=LXMF.LXMessage.DIRECT)
        self.router.handle_outbound(message)
        self.outbound.append(message)

    def recheck_identities(self, now):
        try:
            message_hash, source, raw = self.awaiting_identity.get_nowait()
            if message_hash not in self.deferred_identity and len(self.deferred_identity) < 8:
                self.deferred_identity[message_hash] = [source, raw, now + 60, False]
        except queue.Empty:
            pass
        for message_hash, item in list(self.deferred_identity.items()):
            source, raw, expires, requested = item
            if now >= expires:
                del self.deferred_identity[message_hash]
                self.dropped += 1
            elif RNS.Identity.recall(source) is not None:
                del self.deferred_identity[message_hash]
                try:
                    verified = LXMF.LXMessage.unpack_from_bytes(raw)
                    if verified.signature_validated:
                        self.received(verified)
                except (ValueError, TypeError, msgpack.UnpackException):
                    self.dropped += 1
            elif not requested and now >= self.next_identity_request:
                RNS.Transport.request_path(source)
                item[3] = True
                self.next_identity_request = now + 1

    def run(self, startup_delay=None, run_seconds=None):
        started = time.monotonic()
        next_announce = started + (random.uniform(0, 60) if startup_delay is None else startup_delay)
        last_announce, announced_sequence, maintenance_at = -float("inf"), -1, started
        report("ready", delivery=self.delivery.hash.hex(), discovery=self.discovery.hash.hex(),
               epoch=self.store.epoch.hex(), sequence=self.store.sequence())
        try:
            while not self.stopped.is_set():
                now = time.monotonic()
                if run_seconds is not None and now - started >= run_seconds:
                    break
                self.recheck_identities(now)
                self.outbound[:] = [m for m in self.outbound if m.state not in
                                    (LXMF.LXMessage.DELIVERED, LXMF.LXMessage.FAILED,
                                     LXMF.LXMessage.REJECTED, LXMF.LXMessage.CANCELLED)]
                sequence = self.store.sequence()
                dirty_due = sequence != announced_sequence and now - last_announce >= 1800
                if now >= next_announce or (last_announce != -float("inf") and dirty_due):
                    self.router.announce(self.delivery.hash)
                    # Path requests must carry the same signed service metadata
                    # as periodic announces so newly opened clients can discover us.
                    self.discovery.set_default_app_data(packed([1, self.delivery.hash, self.store.epoch, sequence, self.store.name]))
                    self.discovery.announce()
                    last_announce, announced_sequence = now, sequence
                    next_announce = now + random.uniform(0.8, 1.2) * 6 * 3600
                    report("announced", sequence=sequence)
                if now >= maintenance_at:
                    self.store.maintenance()
                    maintenance_at = now + 60
                if len(self.outbound) >= 32:
                    self.stopped.wait(0.1)
                    continue
                self.replication.step(self.send)
                try:
                    public_key, raw = self.inbound.get(timeout=0.1)
                except queue.Empty:
                    continue
                identity = RNS.Identity(create_keys=False)
                identity.load_public_key(public_key)
                source = RNS.Destination.hash_from_name_and_identity("lxmf.delivery", identity)
                try:
                    envelope = unpacked(raw)
                except (ValueError, TypeError, msgpack.UnpackException):
                    continue
                if type(envelope) is list and len(envelope) == 6 and envelope[1] == 1:
                    self.replication.received(source, raw)
                    continue
                response = self.store.handle(source, raw)
                if response is not None:
                    self.send(public_key, response)
        finally:
            self.stopped.set()
            self.store.close()
            RNS.Reticulum.exit_handler()
            report("stopped", dropped=self.dropped)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", required=True, type=Path)
    parser.add_argument("--rns-config", required=True, type=Path,
                        help="Existing Reticulum configuration directory, never rewritten")
    parser.add_argument("--name", default="Trail Mate public directory")
    parser.add_argument("--startup-delay", type=float,
                        help="Startup announce delay, 0..60 seconds; default randomized")
    parser.add_argument("--run-seconds", type=float, help="Optional bounded service lifetime for local tests")
    args = parser.parse_args()
    if not (args.rns_config / "config").is_file():
        parser.error("--rns-config must contain an existing config file")
    if args.startup_delay is not None and not 0 <= args.startup_delay <= 60:
        parser.error("--startup-delay must be within 0..60")
    if args.run_seconds is not None and args.run_seconds <= 0:
        parser.error("--run-seconds must be positive")
    with exclusive_state(args.state.resolve()):
        service = DirectoryService(args.state.resolve(), args.rns_config.resolve(), args.name)
        signal.signal(signal.SIGINT, lambda *_: service.stopped.set())
        signal.signal(signal.SIGTERM, lambda *_: service.stopped.set())
        service.run(args.startup_delay, args.run_seconds)


if __name__ == "__main__":
    main()
