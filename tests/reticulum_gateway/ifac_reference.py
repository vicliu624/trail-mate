"""Generate/verify deterministic wire vectors with Python Reticulum 1.5.4.

No Reticulum instance, network connection, identity file or real credentials.
Run in an environment with rns==1.5.4; --check verifies the committed fixture.
"""
import argparse
import pathlib
import types

import RNS


def generate():
    assert RNS.__version__ == "1.5.4", "Use the pinned RNS reference version"
    profiles = [("trail", "", 8), ("", "example-passphrase", 16)]
    profiles += [("trail", "example-passphrase", n) for n in (1, 8, 16, 32, 64)]
    profiles += [("réseau", "示例密码", 16)]
    lines = ["# RNS 1.5.4: name_utf8_hex\tpass_utf8_hex\ttag_bytes\tplain_hex\twire_hex"]
    for name, password, size in profiles:
        origin = b"".join(RNS.Identity.full_hash(s.encode("utf-8"))
                          for s in (name, password) if s)
        key = RNS.Cryptography.hkdf(length=64,
            derive_from=RNS.Identity.full_hash(origin),
            salt=RNS.Reticulum.IFAC_SALT, context=None)
        interface = types.SimpleNamespace(ifac_size=size, ifac_key=key,
            ifac_identity=RNS.Identity.from_bytes(key))
        for length in (3, 19, 500):
            plain = bytes([0x14, 0x02]) + bytes(i % 256 for i in range(length - 2))
            wire = RNS.Transport.handle_outgoing_ifac(interface, plain)
            valid, decoded = RNS.Transport.handle_ifac(wire, interface)
            assert valid and decoded == plain
            lines.append("\t".join((name.encode().hex(), password.encode().hex(),
                                    str(size), plain.hex(), wire.hex())))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    target = pathlib.Path(__file__).with_name("ifac_vectors.tsv")
    data = generate()
    if args.check:
        assert target.read_text(encoding="utf-8") == data
        print("24 IFAC vectors match Python Reticulum 1.5.4")
    else:
        target.write_text(data, encoding="utf-8")
