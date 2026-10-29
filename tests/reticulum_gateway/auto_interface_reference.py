"""Pinned RNS 1.5.4 AutoInterface vectors and isolated IPv6 peer."""
import argparse
import ipaddress
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

import RNS
from RNS.Interfaces import netinfo
from RNS.Interfaces.AutoInterface import AutoInterface


def instance(root, interfaces=""):
    pathlib.Path(root, "config").write_text("[reticulum]\nshare_instance = no\nenable_transport = no\n[interfaces]\n" + interfaces)
    result = RNS.Reticulum(configdir=root, loglevel=RNS.LOG_CRITICAL)
    RNS.loglevel = -1
    return result


def vectors(check):
    assert RNS.__version__ == "1.5.4"
    with tempfile.TemporaryDirectory(prefix="trailmate-auto-reference-") as root:
        reticulum = instance(root)
        original = netinfo.interfaces
        netinfo.interfaces = lambda: []  # Vector construction performs no network I/O.
        try:
            lines = ["# RNS 1.5.4: group_utf8_hex ipv6 multicast_hex discovery_token_hex"]
            for group in ("reticulum", "trailmate-lan", "réseau"):
                interface = AutoInterface(None, {"name": "vector", "group_id": group})
                multicast = ipaddress.IPv6Address(interface.mcast_discovery_address).packed.hex()
                for address in ("fe80::1", "fe80::200:abff:fe01:2", "fe80::abcd:0:0:ef01"):
                    canonical = str(ipaddress.IPv6Address(address))
                    token = RNS.Identity.full_hash(group.encode() + canonical.encode()).hex()
                    lines.append("\t".join((group.encode().hex(), canonical, multicast, token)))
        finally:
            netinfo.interfaces = original
        output = "\n".join(lines) + "\n"
        target = pathlib.Path(__file__).with_name("auto_interface_vectors.tsv")
        if check:
            assert target.read_text() == output
        else:
            target.write_text(output)
        print("9 AutoInterface vectors match RNS 1.5.4")


def peer(args):
    assert RNS.__version__ == "1.5.4"
    print("NAMESPACE_READY", flush=True)
    assert sys.stdin.readline().strip() == "GO"
    subprocess.run(["ip", "link", "set", args.device, "addrgenmode", "none"], check=True)
    subprocess.run(["ip", "link", "set", args.device, "up"], check=True)
    subprocess.run(["ip", "-6", "address", "add", "fe80::2/64", "dev", args.device, "nodad"], check=True)
    with tempfile.TemporaryDirectory(prefix="trailmate-auto-peer-") as root:
        class Owner:
            link = None

            @staticmethod
            def established(link):
                assert link.mtu == 500
                print("LINK_ESTABLISHED", flush=True)

            def inbound(self, raw, interface):
                if args.bits:
                    valid, plain = RNS.Transport.handle_ifac(raw, interface)
                    assert valid
                else:
                    plain = raw
                if len(plain) == 500 and plain[0] == 0x14:
                    response = bytearray(plain)
                    response[20] ^= 0x80
                    wire = RNS.Transport.handle_outgoing_ifac(interface, bytes(response)) if args.bits else bytes(response)
                    if args.bits:
                        damaged = bytearray(wire)
                        damaged[-1] ^= 1
                        interface.process_outgoing(bytes(damaged))
                    interface.process_outgoing(wire)
                    interface.process_outgoing(wire)
                else:
                    packet = RNS.Packet(None, plain)
                    assert packet.unpack()
                    if packet.packet_type == RNS.Packet.ANNOUNCE:
                        assert RNS.Identity.validate_announce(packet)
                        print("VALID_ANNOUNCE", flush=True)
                        RNS.Transport.inbound(raw, interface)
                        if self.link is None:
                            identity = RNS.Identity.recall(packet.destination_hash)
                            assert identity is not None
                            destination = RNS.Destination(identity, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
                            assert destination.hash == packet.destination_hash
                            self.link = RNS.Link(destination, established_callback=self.established)
                    elif packet.destination_hash == RNS.Transport.path_request_destination.hash:
                        print("PATH_REQUEST", flush=True)
                        RNS.Transport.inbound(raw, interface)
                    else:
                        RNS.Transport.inbound(raw, interface)

        configuration = (f"[[Reference]]\ntype = AutoInterface\nenabled = yes\ndevices = {args.device}\n"
            f"group_id = {args.group}\ndiscovery_port = {args.discovery}\ndata_port = {args.data}\n")
        if args.bits:
            configuration += f"network_name = trailmate-auto\npassphrase = interop-secret\nifac_size = {args.bits}\n"
        # Let the official composition initialize every interface policy and
        # peer owner. Constructing AutoInterface directly omits those policies.
        reticulum = instance(root, configuration)
        interface = next(item for item in RNS.Transport.interfaces if isinstance(item, AutoInterface))
        interface.owner = Owner()
        # The native side must reject a malformed discovery token from a real
        # scoped source, then recover through the unmodified reference peering.
        scope = socket.if_nametoindex(args.device)
        with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as invalid:
            invalid.bind(("fe80::2", 0, 0, scope))
            invalid.sendto(bytes(32), ("fe80::1", args.discovery + 1, 0, scope))
        print("REFERENCE_READY", flush=True)
        try:
            for line in sys.stdin:
                if line.strip() == "STOP":
                    break
        finally:
            interface.detach()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--peer", action="store_true")
    parser.add_argument("--device")
    parser.add_argument("--group")
    parser.add_argument("--discovery", type=int)
    parser.add_argument("--data", type=int)
    parser.add_argument("--bits", type=int, default=0)
    arguments = parser.parse_args()
    peer(arguments) if arguments.peer else vectors(arguments.check)
