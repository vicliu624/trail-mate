#!/usr/bin/env python3
"""Socket-activated privileged radio helper; clients remain unprivileged.

IPC: little-endian uint16 length followed by opcode and body. D carries a
six-byte peer MAC and 1..250 application bytes. R announces radio readiness.
An independent systemd timer owns recovery if this process or its client dies.
"""
import json
import os
from pathlib import Path
import pwd
import select
import socket
import struct
import subprocess
import sys
import time

from linux_espnow_probe import Netlink, attr, attrs, configure_channel, decode_body, encode_body, enums

GUARD = "/usr/local/libexec/trailmate-espnow-guard"


def run(*argv):
    subprocess.run(argv, check=True, stdout=sys.stderr, stderr=sys.stderr, timeout=12)


def packet(client, body):
    client.sendall(struct.pack("<H", len(body)) + body)


def session(client):
    tx, rx = Netlink(), Netlink()
    armed = False
    try:
        commands, attributes = enums("nl80211_commands"), enums("nl80211_attrs")
        family = struct.unpack("=H", attrs(tx.request(16, 3, attr(2, b"nl80211\0"))[0][4:])[1])[0]
        interface = attr(attributes["NL80211_ATTR_IFINDEX"], struct.pack("=I", socket.if_nametoindex("wlan0")))
        run(GUARD, "arm", "--seconds", "150")
        armed = True
        run(GUARD, "prepare-radio")
        run("nmcli", "device", "set", "wlan0", "managed", "no")
        run("ip", "link", "set", "wlan0", "up")
        configure_channel(1)
        rx.request(family, commands["NL80211_CMD_REGISTER_FRAME"], interface
                   + attr(attributes["NL80211_ATTR_FRAME_TYPE"], struct.pack("=H", 0xd0))
                   + attr(attributes["NL80211_ATTR_FRAME_MATCH"], bytes.fromhex("7f18fe34")))
        packet(client, b"R")
        buffer = bytearray()
        deadline = time.monotonic() + 125
        count = 0
        while time.monotonic() < deadline:
            readable, _, _ = select.select([client, rx.sock], [], [], 0.2)
            if client in readable:
                part = client.recv(1024)
                if not part:
                    return
                buffer.extend(part)
                if len(buffer) > 4096:
                    raise ValueError("IPC input limit")
                while len(buffer) >= 2:
                    size = struct.unpack_from("<H", buffer)[0]
                    if not 8 <= size <= 257:
                        raise ValueError("Invalid IPC packet length")
                    if len(buffer) < size + 2:
                        break
                    message = bytes(buffer[2:size + 2])
                    del buffer[:size + 2]
                    if message[0] != ord("D"):
                        raise ValueError("Invalid IPC opcode")
                    # Fixed operation and format: the client cannot submit
                    # arbitrary firmware commands or change the radio channel.
                    body = encode_body(message[7:], os.urandom(4))
                    count += 1
                    action = struct.pack("<II6sH6sHI", 1, 40, b"\xff" * 6, 0,
                                         message[1:7], len(body), count) + body.ljust(1800, b"\0")
                    value = b"actframe\0" + action
                    header = struct.pack("=IiIII", 263, len(value), 20, 1, 0)
                    tx.request(family, commands["NL80211_CMD_VENDOR"], interface
                               + attr(attributes["NL80211_ATTR_VENDOR_ID"], struct.pack("=I", 0x001018))
                               + attr(attributes["NL80211_ATTR_VENDOR_SUBCMD"], struct.pack("=I", 1))
                               + attr(attributes["NL80211_ATTR_VENDOR_DATA"], header + value))
                    print(json.dumps({"event": "tx_application", "bytes": len(message) - 7,
                                      "team_type": message[10] if len(message) > 10 else None}), flush=True)
            if rx.sock in readable:
                for kind, sequence, message in rx.receive():
                    if kind != family or len(message) < 4:
                        continue
                    frame = attrs(message[4:]).get(attributes["NL80211_ATTR_FRAME"], b"")
                    if len(frame) < 39:
                        continue
                    try:
                        payload = decode_body(frame[24:])
                    except ValueError:
                        continue
                    if payload:
                        print(json.dumps({"event": "rx_application", "bytes": len(payload),
                                          "team_type": payload[3] if len(payload) > 3 else None}), flush=True)
                        packet(client, b"D" + frame[10:16] + payload)
    finally:
        tx.sock.close()
        rx.sock.close()
        if armed:
            # Recovery runs in another unit, outside this process group. Never
            # invoke restore here: recovery first stops this helper's unit.
            subprocess.run([GUARD, "expire"], timeout=12, check=False)
            subprocess.run(["systemctl", "--no-block", "start", "trailmate-espnow-recover.service"],
                           timeout=12, check=False)


def main():
    if os.geteuid() != 0 or os.environ.get("LISTEN_PID") != str(os.getpid()) or os.environ.get("LISTEN_FDS") != "1":
        raise RuntimeError("Requires root and systemd socket activation")
    server = socket.socket(fileno=3)
    server.settimeout(10)
    client, _ = server.accept()
    client.settimeout(2)
    try:
        pid, uid, gid = struct.unpack("3i", client.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if uid not in (0, pwd.getpwnam("pi").pw_uid):
            raise PermissionError("Unauthorized radio client")
        session(client)
    except Exception as error:
        print(type(error).__name__ + ": " + str(error), file=sys.stderr, flush=True)
        try:
            packet(client, b"E")
        except OSError:
            pass
        raise
    finally:
        client.close()
        server.close()


if __name__ == "__main__":
    main()
