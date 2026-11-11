#!/usr/bin/env python3
"""Bounded native brcmfmac ESP-NOW v1 TX/RX probe (not proof by command ACK).

Uses nl80211 vendor DCMD actframe and registered management RX. No monitor
interface, firmware replacement, or ESP32 change. Pair with a distinct peer.
"""
import argparse
import errno
import json
from pathlib import Path
import re
import select
import socket
import struct
import time


def log(**values):
    print(json.dumps({"time": time.time(), **values}), flush=True)


def enums(name):
    text = Path("/usr/include/linux/nl80211.h").read_text()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    body = re.search(r"enum\s+" + name + r"\s*\{(.*?)\};", text, re.S).group(1)
    result = {}
    current = -1
    for item in body.split(","):
        if not item.strip():
            continue
        match = re.fullmatch(r"\s*(\w+)\s*(?:=\s*(.*))?", item, re.S)
        key, expression = match.groups()
        if expression:
            expression = re.sub(r"\b[A-Za-z_]\w*\b", lambda m: str(result[m.group()]), expression)
            if not re.fullmatch(r"[\d+\-\s]+", expression):
                raise ValueError("Unsupported enum expression")
            current = sum(int(re.sub(r"\s", "", p))
                          for p in re.findall(r"[+\-]?\s*\d+", expression))
        else:
            current += 1
        result[key] = current
    return result


def attr(kind, payload):
    size = 4 + len(payload)
    return struct.pack("=HH", size, kind) + payload + bytes((-size) & 3)


def attrs(data):
    output = {}
    while len(data) >= 4:
        size, kind = struct.unpack_from("=HH", data)
        if size < 4 or size > len(data):
            raise ValueError("Bad netlink attribute")
        output[kind & 0x3fff] = data[4:size]
        data = data[(size + 3) & ~3:]
    return output


class Netlink:
    def __init__(self):
        self.sock = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, 16)
        self.sock.bind((0, 0))
        self.sock.settimeout(3)
        self.sequence = 0

    def receive(self):
        data = self.sock.recv(65536)
        while len(data) >= 16:
            size, kind, flags, sequence, pid = struct.unpack_from("=IHHII", data)
            if size < 16 or size > len(data):
                raise ValueError("Bad netlink message")
            yield kind, sequence, data[16:size]
            data = data[(size + 3) & ~3:]

    def request(self, family, command, data):
        self.sequence += 1
        payload = struct.pack("=BBH", command, 1, 0) + data
        self.sock.sendto(struct.pack("=IHHII", 16 + len(payload), family, 5,
                                     self.sequence, 0) + payload, (0, 0))
        output = []
        for _ in range(32):
            for kind, sequence, payload in self.receive():
                if sequence != self.sequence:
                    continue
                if kind == 2:
                    error = struct.unpack_from("=i", payload)[0]
                    if error:
                        raise OSError(-error, errno.errorcode.get(-error, "netlink error"))
                    return output
                if kind == 3:
                    return output
                output.append(payload)
        raise RuntimeError("Too many responses")


class Firmware:
    """Small fixed-operation adapter used for reversible radio power policy."""
    def __init__(self):
        self.link = Netlink()
        self.commands = enums("nl80211_commands")
        self.attributes = enums("nl80211_attrs")
        self.family = struct.unpack("=H", attrs(self.link.request(16, 3, attr(2, b"nl80211\0"))[0][4:])[1])[0]
        self.base = attr(self.attributes["NL80211_ATTR_IFINDEX"], struct.pack("=I", socket.if_nametoindex("wlan0")))
        self.base += attr(self.attributes["NL80211_ATTR_VENDOR_ID"], struct.pack("=I", 0x001018))
        self.base += attr(self.attributes["NL80211_ATTR_VENDOR_SUBCMD"], struct.pack("=I", 1))

    def call(self, command, data, size, setting=False):
        header = struct.pack("=IiIII", command, size, 20, int(setting), 0)
        replies = self.link.request(self.family, self.commands["NL80211_CMD_VENDOR"], self.base
                                    + attr(self.attributes["NL80211_ATTR_VENDOR_DATA"], header + data))
        return b"".join(attrs(attrs(reply[4:])[self.attributes["NL80211_ATTR_VENDOR_DATA"]])[2] for reply in replies)

    def power_policy(self):
        return {"mpc": struct.unpack_from("<I", self.call(262, b"mpc\0", 4))[0],
                "pm": struct.unpack_from("<I", self.call(85, bytes(4), 4))[0]}

    def set_power_policy(self, policy):
        if policy["mpc"] not in (0, 1) or policy["pm"] not in (0, 1, 2):
            raise ValueError("Unsupported firmware power policy")
        value = b"mpc\0" + struct.pack("<I", policy["mpc"])
        self.call(263, value, len(value), True)
        self.call(86, struct.pack("<I", policy["pm"]), 4, True)
        if self.power_policy() != policy:
            raise RuntimeError("Radio power policy readback mismatch")

    def close(self):
        self.link.sock.close()


def encode_body(payload, random):
    if len(payload) > 250 or len(random) != 4:
        raise ValueError("ESP-NOW v1 payload too large")
    return b"\x7f\x18\xfe\x34" + random + bytes((221, len(payload) + 5)) + b"\x18\xfe\x34\x04\x01" + payload


def decode_body(body):
    if len(body) < 15 or body[:4] != b"\x7f\x18\xfe\x34":
        raise ValueError("Not an ESP-NOW action body")
    if body[8] != 221 or body[10:14] != b"\x18\xfe\x34\x04":
        raise ValueError("Bad ESP-NOW vendor element")
    size = body[9]
    if size < 5 or 10 + size > len(body):
        raise ValueError("Truncated ESP-NOW element")
    # Team fits one element; accept the single-element v2 layout as well.
    if body[14] & 15 not in (1, 2) or body[14] & 16:
        raise ValueError("Unsupported ESP-NOW version/fragmentation")
    return body[15:10 + size]


def configure_channel(channel):
    if not 1 <= channel <= 11:
        raise ValueError("Channel outside bounded pairing range")
    commands, attributes = enums("nl80211_commands"), enums("nl80211_attrs")
    link = Netlink()
    try:
        family = struct.unpack("=H", attrs(link.request(16, 3, attr(2, b"nl80211\0"))[0][4:])[1])[0]
        base = attr(attributes["NL80211_ATTR_IFINDEX"], struct.pack("=I", socket.if_nametoindex("wlan0")))
        base += attr(attributes["NL80211_ATTR_VENDOR_ID"], struct.pack("=I", 0x001018))
        base += attr(attributes["NL80211_ATTR_VENDOR_SUBCMD"], struct.pack("=I", 1))
        header = struct.pack("=IiIII", 30, 4, 20, 1, 0)
        link.request(family, commands["NL80211_CMD_VENDOR"], base
                     + attr(attributes["NL80211_ATTR_VENDOR_DATA"], header + struct.pack("<I", channel)))
        header = struct.pack("=IiIII", 29, 12, 20, 0, 0)
        replies = link.request(family, commands["NL80211_CMD_VENDOR"], base
                               + attr(attributes["NL80211_ATTR_VENDOR_DATA"], header + bytes(12)))
        data = b"".join(attrs(attrs(reply[4:])[attributes["NL80211_ATTR_VENDOR_DATA"]])[2] for reply in replies)
        hardware, target, scan = struct.unpack_from("<III", data)
        log(event="channel_readback", hardware=hardware, target=target, scan=scan, requested=channel)
        if hardware != channel:
            raise RuntimeError("Firmware channel readback mismatch")
    finally:
        link.sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--peer", required=True)
    parser.add_argument("--channel", type=int, default=8)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--tag", required=True, choices=("cm0", "zero"))
    parser.add_argument("--set-channel", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.channel <= 11 or not 1 <= args.seconds <= 90:
        parser.error("Unbounded radio experiment refused")
    # Any transmitting experiment requires the independently armed recovery.
    guard = json.loads(Path("/var/lib/trailmate-espnow-probe/armed.json").read_text())
    remaining = guard["deadline_boottime"] - time.clock_gettime(time.CLOCK_BOOTTIME)
    if guard["boot_id"] != Path("/proc/sys/kernel/random/boot_id").read_text().strip() or remaining < args.seconds + 5:
        parser.error("Recovery deadline must outlast this bounded experiment")
    if args.set_channel:
        configure_channel(args.channel)
    peer = bytes.fromhex(args.peer.replace(":", ""))
    if len(peer) != 6:
        parser.error("Bad peer MAC")
    commands = enums("nl80211_commands")
    attributes = enums("nl80211_attrs")
    tx, rx = Netlink(), Netlink()
    family = struct.unpack("=H", attrs(tx.request(16, 3, attr(2, b"nl80211\0"))[0][4:])[1])[0]
    interface = attr(attributes["NL80211_ATTR_IFINDEX"], struct.pack("=I", socket.if_nametoindex("wlan0")))
    rx.request(family, commands["NL80211_CMD_REGISTER_FRAME"], interface
               + attr(attributes["NL80211_ATTR_FRAME_TYPE"], struct.pack("=H", 0xd0))
               + attr(attributes["NL80211_ATTR_FRAME_MATCH"], bytes.fromhex("7f18fe34")))
    log(event="ready", tag=args.tag, peer=args.peer, channel=args.channel)
    end = time.monotonic() + args.seconds
    next_tx = time.monotonic() + 2
    count = 0
    received = 0
    errors = 0
    while time.monotonic() < end:
        if time.monotonic() >= next_tx:
            destination = b"\xff" * 6 if count % 2 == 0 else peer
            payload = b"TMPROBE1:" + args.tag.encode() + b":" + struct.pack("<I", count)
            body = encode_body(payload, struct.pack("<I", int(time.time() * 1000) & 0xffffffff))
            action = struct.pack("<II6sH6sHI", args.channel, 40, b"\xff" * 6, 0,
                                 destination, len(body), count + 1) + body.ljust(1800, b"\0")
            assert len(action) == 1828
            value = b"actframe\0" + action
            header = struct.pack("=IiIII", 263, len(value), 20, 1, 0)
            try:
                tx.request(family, commands["NL80211_CMD_VENDOR"], interface
                           + attr(attributes["NL80211_ATTR_VENDOR_ID"], struct.pack("=I", 0x001018))
                           + attr(attributes["NL80211_ATTR_VENDOR_SUBCMD"], struct.pack("=I", 1))
                           + attr(attributes["NL80211_ATTR_VENDOR_DATA"], header + value))
                log(event="tx_command_accepted", count=count, broadcast=count % 2 == 0,
                    payload=payload.hex(), delivery_confirmed=False)
            except OSError as error:
                errors += 1
                log(event="tx_command_error", count=count, error=str(error), errno=error.errno)
            count += 1
            next_tx = time.monotonic() + 1
        if select.select([rx.sock], [], [], 0.1)[0]:
            for kind, sequence, message in rx.receive():
                if kind != family or len(message) < 4:
                    continue
                values = attrs(message[4:])
                frame = values.get(attributes["NL80211_ATTR_FRAME"], b"")
                if len(frame) < 24:
                    continue
                try:
                    payload = decode_body(frame[24:])
                except ValueError as error:
                    log(event="rx_parse_error", error=str(error), frame=frame.hex())
                    continue
                received += 1
                log(event="rx", source=frame[10:16].hex(":"), payload=payload.hex(), frame=frame.hex())
    tx.sock.close()
    rx.sock.close()
    log(event="summary", sent_commands=count, command_errors=errors, received=received,
        bidirectional_validated=False)


if __name__ == "__main__":
    main()
