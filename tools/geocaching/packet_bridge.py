"""Bounded raw-RNS WebSocket <-> Reticulum TCP/HDLC transport bridge.

One browser connection gets one upstream TCP connection. This module contains
no LXMF, identities, application fields, directory storage or query API.
Terminate TLS here or at a reverse proxy bound to the loopback listener.
"""
import argparse
import asyncio
import ipaddress
import json
import ssl
import time

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

MTU = 500


def hdlc_frame(packet):
    return b"\x7e" + packet.replace(b"\x7d", b"\x7d\x5d").replace(b"\x7e", b"\x7d\x5e") + b"\x7e"


class HDLCReader:
    def __init__(self):
        self.frame = bytearray()
        self.escaped = False
        self.started = False

    def feed(self, chunk):
        for byte in chunk:
            if byte == 0x7E:
                if self.escaped:
                    raise ValueError("invalid HDLC escape")
                if self.started and len(self.frame) > 19:
                    yield bytes(self.frame)
                self.frame.clear()
                self.started = True
            elif self.started:
                if self.escaped:
                    self.frame.append(byte ^ 0x20)
                    self.escaped = False
                elif byte == 0x7D:
                    self.escaped = True
                else:
                    self.frame.append(byte)
                if len(self.frame) > MTU:
                    raise ValueError("RNS packet exceeds MTU")


class PacketBridge:
    def __init__(self, host, port, max_clients=32):
        self.host, self.port, self.max_clients = host, port, max_clients
        self.clients = 0

    async def connection(self, websocket):
        if self.clients >= self.max_clients:
            await websocket.close(1013, "Connection capacity reached")
            return
        self.clients += 1
        writer, tasks = None, []
        try:
            reader, writer = await asyncio.wait_for(asyncio.open_connection(self.host, self.port, limit=4096), 5)

            async def browser_to_network():
                tokens, tick = 128.0, time.monotonic()
                async for packet in websocket:
                    now = time.monotonic()
                    tokens, tick = min(128, tokens + (now - tick) * 128) - 1, now
                    if tokens < 0:
                        await websocket.close(1008, "Packet rate exceeded")
                        return
                    if type(packet) is not bytes or not 19 < len(packet) <= MTU:
                        await websocket.close(1003, "Expected one binary RNS packet")
                        return
                    writer.write(hdlc_frame(packet))
                    await asyncio.wait_for(writer.drain(), 10)

            async def network_to_browser():
                decoder = HDLCReader()
                while chunk := await reader.read(4096):
                    for packet in decoder.feed(chunk):
                        await asyncio.wait_for(websocket.send(packet), 10)

            tasks = [asyncio.create_task(browser_to_network()), asyncio.create_task(network_to_browser())]
            done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
        except (OSError, ValueError, TimeoutError, ConnectionClosed):
            pass
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            if writer is not None:
                writer.close()
                try:
                    await writer.wait_closed()
                except OSError:
                    pass
            await websocket.close()
            self.clients -= 1


async def run(args):
    tls = None
    if args.tls_cert:
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.minimum_version = ssl.TLSVersion.TLSv1_2
        tls.load_cert_chain(args.tls_cert, args.tls_key)
    bridge = PacketBridge(args.tcp_host, args.tcp_port, args.max_clients)
    origins = list(args.origin)
    if args.allow_no_origin:
        origins.append(None)
    async with serve(bridge.connection, args.listen, args.port, origins=origins,
                     ssl=tls, compression=None, max_size=MTU, max_queue=16,
                     write_limit=16384, close_timeout=3, server_header=None) as server:
        print(json.dumps({"event": "bridge_ready", "port": server.sockets[0].getsockname()[1],
                          "framing": "raw", "tls": tls is not None}), flush=True)
        if args.run_seconds:
            await asyncio.sleep(args.run_seconds)
        else:
            await server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--tcp-host", default="127.0.0.1")
    parser.add_argument("--tcp-port", required=True, type=int)
    parser.add_argument("--origin", action="append", default=[], help="Exact allowed website origin; repeatable")
    parser.add_argument("--allow-no-origin", action="store_true", help="Permit non-browser local test clients")
    parser.add_argument("--tls-cert")
    parser.add_argument("--tls-key")
    parser.add_argument("--max-clients", type=int, default=32)
    parser.add_argument("--run-seconds", type=float)
    args = parser.parse_args()
    if bool(args.tls_cert) != bool(args.tls_key):
        parser.error("TLS certificate and key must be specified together")
    if not args.origin and not args.allow_no_origin:
        parser.error("Specify at least one allowed --origin")
    if not 1 <= args.max_clients <= 256:
        parser.error("--max-clients must be 1..256")
    if not args.tls_cert and not ipaddress.ip_address(args.listen).is_loopback:
        parser.error("Plaintext listeners must bind loopback; use TLS or a local TLS proxy")
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
