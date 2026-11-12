#!/usr/bin/env python3
"""Small standard RNS/LXMF interop CLI for Trail Mate field testing."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import time

import LXMF
import RNS


DEFAULT_CONFIG = "/home/vicliu/.reticulum-trailmate"
DEFAULT_IDENTITY = "/home/vicliu/.reticulum-trailmate/identities/trailmate_lxmf_cli.identity"
DEFAULT_STORAGE = "/home/vicliu/trailmate-reticulum/lxmf-cli"
DEFAULT_LOG = "/home/vicliu/trailmate-reticulum/logs/lxmf-cli.log"
LOG_MAX_BYTES = 256 * 1024
DEFAULT_ANNOUNCE_INTERVAL = 4 * 60 * 60


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z", time.localtime())


def hex_to_bytes(value: str) -> bytes:
    cleaned = value.replace(":", "").replace(" ", "").strip()
    if len(cleaned) != 32:
        raise ValueError("destination hash must be 16 bytes / 32 hex chars")
    return bytes.fromhex(cleaned)


def hexrep(value: bytes | None) -> str:
    return value.hex().upper() if value else "-"


def rotate_log(path: Path) -> None:
    try:
        if path.exists() and path.stat().st_size > LOG_MAX_BYTES:
            backup = path.with_suffix(path.suffix + ".1")
            if backup.exists():
                backup.unlink()
            path.rename(backup)
    except OSError:
        pass


def emit(args: argparse.Namespace, event: str, **fields: object) -> None:
    record = {"ts": now_iso(), "event": event, **fields}
    line = json.dumps(record, sort_keys=True, ensure_ascii=True)
    print(line, flush=True)
    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    rotate_log(log_path)
    with log_path.open("a", encoding="utf-8") as handle:
        handle.write(line + "\n")


def load_identity(path: str) -> RNS.Identity:
    identity_path = Path(path)
    identity_path.parent.mkdir(parents=True, exist_ok=True)
    identity = RNS.Identity.from_file(str(identity_path)) if identity_path.exists() else None
    if identity is None:
        identity = RNS.Identity()
        identity.to_file(str(identity_path))
    return identity


def init_stack(args: argparse.Namespace) -> tuple[RNS.Identity, LXMF.LXMRouter, RNS.Destination]:
    Path(args.storage).mkdir(parents=True, exist_ok=True)
    RNS.Reticulum(
        configdir=args.config,
        loglevel=RNS.LOG_NOTICE,
        require_shared_instance=not args.standalone,
    )
    identity = load_identity(args.identity)
    router = LXMF.LXMRouter(
        identity=identity,
        storagepath=args.storage,
        autopeer=False,
        enforce_stamps=False,
        name=args.name,
    )
    delivery = router.register_delivery_identity(identity, display_name=args.name, stamp_cost=None)
    if delivery is None:
        raise RuntimeError("failed to register LXMF delivery identity")
    return identity, router, delivery


def delivery_hash_for_public_key(public_key: bytes) -> bytes:
    identity_hash = RNS.Identity.full_hash(public_key)[: RNS.Reticulum.TRUNCATED_HASHLENGTH // 8]
    return RNS.Destination.hash(identity_hash, "lxmf", "delivery")


def propagation_hash_for_public_key(public_key: bytes) -> bytes:
    identity_hash = RNS.Identity.full_hash(public_key)[: RNS.Reticulum.TRUNCATED_HASHLENGTH // 8]
    return RNS.Destination.hash(identity_hash, "lxmf", "propagation")


def command_identity(args: argparse.Namespace) -> int:
    identity, _router, delivery = init_stack(args)
    emit(
        args,
        "identity",
        identity_hash=hexrep(identity.hash),
        delivery_hash=hexrep(delivery.hash),
        name=args.name,
    )
    return 0


def command_announce(args: argparse.Namespace) -> int:
    _identity, router, delivery = init_stack(args)
    for index in range(args.count):
        router.announce(delivery.hash)
        emit(args, "announce", delivery_hash=hexrep(delivery.hash), index=index + 1)
        if index + 1 < args.count:
            time.sleep(args.interval)
    return 0


def command_known(args: argparse.Namespace) -> int:
    init_stack(args)
    rows = []
    for destination_hash, entry in RNS.Identity.known_destinations.items():
        public_key = entry[2]
        if not isinstance(public_key, bytes):
            continue
        kind = None
        if delivery_hash_for_public_key(public_key) == destination_hash:
            kind = "delivery"
        elif propagation_hash_for_public_key(public_key) == destination_hash:
            kind = "propagation"
        if kind is None:
            continue
        rows.append(
            {
                "kind": kind,
                "destination_hash": hexrep(destination_hash),
                "last_seen": entry[0],
                "app_data_len": len(entry[3]) if entry[3] else 0,
            }
        )
    rows.sort(key=lambda row: (row["kind"], row["destination_hash"]))
    emit(args, "known", count=len(rows), rows=rows[: args.limit])
    return 0


def recall_destination(destination_hash: bytes, timeout: float) -> RNS.Destination:
    deadline = time.time() + timeout
    if not RNS.Transport.has_path(destination_hash):
        RNS.Transport.request_path(destination_hash)
    identity = RNS.Identity.recall(destination_hash)
    while identity is None and time.time() < deadline:
        time.sleep(0.5)
        identity = RNS.Identity.recall(destination_hash)
    if identity is None:
        raise RuntimeError(f"no identity known for {hexrep(destination_hash)}")
    return RNS.Destination(identity, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")


def command_send(args: argparse.Namespace) -> int:
    _identity, router, delivery = init_stack(args)
    destination_hash = hex_to_bytes(args.to)
    destination = recall_destination(destination_hash, args.path_timeout)
    if destination.hash != destination_hash:
        raise RuntimeError(
            f"recalled identity produced {hexrep(destination.hash)}, expected {hexrep(destination_hash)}"
        )

    message = LXMF.LXMessage(
        destination,
        delivery,
        content=args.text,
        title=args.title,
        desired_method=LXMF.LXMessage.OPPORTUNISTIC,
    )
    message.register_delivery_callback(
        lambda lxm: emit(args, "send_delivered", hash=hexrep(lxm.hash), to=hexrep(destination_hash))
    )
    message.register_failed_callback(
        lambda lxm: emit(args, "send_failed", hash=hexrep(lxm.hash), to=hexrep(destination_hash), state=lxm.state)
    )
    router.handle_outbound(message)
    emit(args, "send_queued", hash=hexrep(message.hash), to=hexrep(destination_hash), text_len=len(args.text))

    deadline = time.time() + args.wait
    while time.time() < deadline:
        if message.state in (LXMF.LXMessage.DELIVERED, LXMF.LXMessage.FAILED):
            break
        time.sleep(0.5)
    emit(args, "send_state", hash=hexrep(message.hash), to=hexrep(destination_hash), state=message.state)
    return 0 if message.state != LXMF.LXMessage.FAILED else 2


def command_listen(args: argparse.Namespace) -> int:
    _identity, router, delivery = init_stack(args)

    def on_delivery(message: LXMF.LXMessage) -> None:
        emit(
            args,
            "rx",
            hash=hexrep(message.hash),
            source_hash=hexrep(message.source_hash),
            destination_hash=hexrep(message.destination_hash),
            title=message.title_as_string(),
            content=message.content_as_string(),
            method=message.method,
            rssi=message.rssi,
            snr=message.snr,
        )

    router.register_delivery_callback(on_delivery)
    router.announce(delivery.hash)
    emit(args, "listen", delivery_hash=hexrep(delivery.hash), duration=args.duration)
    deadline = time.time() + args.duration if args.duration > 0 else None
    next_announce = time.time() + args.announce_interval if args.announce_interval > 0 else None
    while deadline is None or time.time() < deadline:
        if next_announce is not None and time.time() >= next_announce:
            router.announce(delivery.hash)
            emit(args, "announce", delivery_hash=hexrep(delivery.hash), periodic=True)
            next_announce = time.time() + args.announce_interval
        time.sleep(0.5)
    emit(args, "listen_done", delivery_hash=hexrep(delivery.hash))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("--identity", default=DEFAULT_IDENTITY)
    parser.add_argument("--storage", default=DEFAULT_STORAGE)
    parser.add_argument("--log", default=DEFAULT_LOG)
    parser.add_argument("--name", default="TrailMate 31.232")
    parser.add_argument("--standalone", action="store_true")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("identity").set_defaults(func=command_identity)

    announce = sub.add_parser("announce")
    announce.add_argument("--count", type=int, default=1)
    announce.add_argument("--interval", type=float, default=5.0)
    announce.set_defaults(func=command_announce)

    known = sub.add_parser("known")
    known.add_argument("--limit", type=int, default=50)
    known.set_defaults(func=command_known)

    send = sub.add_parser("send")
    send.add_argument("--to", required=True)
    send.add_argument("--text", required=True)
    send.add_argument("--title", default="")
    send.add_argument("--path-timeout", type=float, default=20.0)
    send.add_argument("--wait", type=float, default=30.0)
    send.set_defaults(func=command_send)

    listen = sub.add_parser("listen")
    listen.add_argument("--duration", type=float, default=0.0)
    listen.add_argument(
        "--announce-interval", type=float, default=DEFAULT_ANNOUNCE_INTERVAL,
        help="Periodic announcement interval in seconds (default: 4 hours; 0 disables). Use shorter values only for isolated tests.",
    )
    listen.set_defaults(func=command_listen)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except Exception as exc:
        emit(args, "error", error=str(exc), command=args.command)
        return 1


if __name__ == "__main__":
    sys.exit(main())
