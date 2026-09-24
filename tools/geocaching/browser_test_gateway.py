"""Bounded loopback Reticulum gateway with two independently cuttable uplinks.

Uses real Python RNS interfaces. Fault injection cuts the actual current
next-hop TCP connection, without editing paths, forging announces, or restarting
the browser/directory. Never connects to public endpoints.
"""
import argparse
import json
import os
from pathlib import Path
import select
import socket
import threading
import time

import RNS


class CuttableTCPProxy:
    """Transparent loopback cable; closing it also refuses reconnections."""
    def __init__(self, upstream_port):
        self.upstream_port = upstream_port
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen()
        self.listener.settimeout(.2)
        self.port = self.listener.getsockname()[1]
        self.stopped = threading.Event()
        self.lock = threading.Lock()
        self.connections = set()
        threading.Thread(target=self.accept, daemon=True).start()

    def accept(self):
        while not self.stopped.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                upstream = socket.create_connection(('127.0.0.1', self.upstream_port), timeout=2)
            except OSError:
                client.close()
                continue
            with self.lock:
                self.connections.update((client, upstream))
            threading.Thread(target=self.forward, args=(client, upstream), daemon=True).start()

    def forward(self, client, upstream):
        try:
            while not self.stopped.is_set():
                ready, _, _ = select.select([client, upstream], [], [], .2)
                for source in ready:
                    raw = source.recv(4096)
                    if not raw:
                        return
                    (upstream if source is client else client).sendall(raw)
        except OSError:
            pass
        finally:
            with self.lock:
                self.connections.difference_update((client, upstream))
            for stream in (client, upstream):
                try:
                    stream.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                stream.close()

    def close(self):
        self.stopped.set()
        self.listener.close()
        with self.lock:
            streams = list(self.connections)
        for stream in streams:
            try:
                stream.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--upstream-port', type=int, required=True)
    parser.add_argument('--destination', required=True)
    args = parser.parse_args()
    args.state.mkdir(parents=True, exist_ok=False)
    cables = [CuttableTCPProxy(args.upstream_port) for _ in range(2)]
    config = args.state / 'config'
    config.write_text('[reticulum]\nshare_instance = No\nenable_transport = Yes\n[interfaces]\n'
                      '[[Browser bridge]]\nenabled = Yes\ntype = TCPServerInterface\n'
                      f'listen_ip = 127.0.0.1\nlisten_port = {args.port}\n' + ''.join(
                          f'[[Uplink {number}]]\nenabled = Yes\ntype = TCPClientInterface\n'
                          f'target_host = 127.0.0.1\ntarget_port = {cable.port}\n'
                          for number, cable in enumerate(cables, 1)))
    rns = RNS.Reticulum(configdir=str(args.state), loglevel=(RNS.LOG_DEBUG
        if os.environ.get('GEOCACHING_NETWORK_DEBUG') == '1' else RNS.LOG_WARNING))
    destination = bytes.fromhex(args.destination)
    uplinks = [interface for interface in RNS.Transport.interfaces if interface.name.startswith('Uplink ')]
    assert len(uplinks) == 2 and all(interface.online for interface in uplinks)
    print(json.dumps({'event': 'gateway_ready', 'online_uplinks': 2}), flush=True)
    deadline = time.monotonic() + 160
    dropped = None
    last_path = None
    try:
        while time.monotonic() < deadline and not (args.state / 'stop').exists():
            path = RNS.Transport.next_hop_interface(destination)
            if path and path.name != last_path:
                last_path = path.name
                print(json.dumps({'event': 'gateway_path', 'interface': last_path}), flush=True)
            if dropped is None and (args.state / 'drop-upstream').exists():
                assert path in uplinks and path.online, 'Current directory path must use a live uplink'
                dropped = path.name
                cables[uplinks.index(path)].close()
                offline_deadline = time.monotonic() + 5
                while path.online and time.monotonic() < offline_deadline:
                    time.sleep(.05)
                assert not path.online and sum(interface.online for interface in uplinks) == 1
                print(json.dumps({'event': 'gateway_uplink_dropped', 'interface': dropped,
                                  'online_uplinks': 1}), flush=True)
            time.sleep(.1)
    finally:
        for cable in cables:
            cable.close()
        RNS.Reticulum.exit_handler()
        print(json.dumps({'event': 'gateway_stopped'}), flush=True)


if __name__ == '__main__':
    main()
