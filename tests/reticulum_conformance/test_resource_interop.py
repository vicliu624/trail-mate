"""Cross-check the native ResourceAdvertisement goldens with pinned Python RNS.

Run with tools/geocaching/interop-requirements.txt installed, alongside the
reticulum_supported_subset_vectors native executable that checks emitted bytes.
"""

from pathlib import Path
import re
from types import SimpleNamespace
import unittest

import RNS
from RNS.vendor import umsgpack


SOURCE = Path(__file__).with_name("test_reticulum_supported_subset_vectors.cpp")


def native_golden(name: str) -> bytes:
    declaration = re.search(
        rf"constexpr const char\* {name}\s*=\s*((?:\s*\"[0-9a-f]+\")+)\s*;",
        SOURCE.read_text(encoding="utf-8"),
    )
    if declaration is None:
        raise AssertionError(f"Missing native golden {name}")
    return bytes.fromhex("".join(re.findall(r'"([0-9a-f]+)"', declaration[1])))


class ResourceInteropTests(unittest.TestCase):
    def test_reference_accepts_native_resource_goldens(self):
        for name in ("kResourceAdvertisement", "kLxmfResourceAdvertisement"):
            with self.subTest(name=name):
                wire = native_golden(name)
                advertisement = RNS.ResourceAdvertisement.unpack(wire)
                self.assertEqual(advertisement.t, 258)
                self.assertEqual(advertisement.d, 240)
                self.assertEqual(advertisement.n, 3)
                self.assertEqual(advertisement.h, bytes(range(0x40, 0x60)))
                self.assertEqual(advertisement.pack(), wire)

    def test_reference_encoder_matches_lxmf_resource_golden(self):
        resource = SimpleNamespace(
            size=258,
            total_size=240,
            parts=[None] * 3,
            hash=bytes(range(0x40, 0x60)),
            random_hash=bytes([1, 2, 3, 4]),
            original_hash=bytes(range(0x60, 0x80)),
            hashmap=bytes(range(12)),
            compressed=False,
            encrypted=True,
            split=False,
            has_metadata=False,
            segment_index=1,
            total_segments=1,
            request_id=None,
        )
        advertisement = RNS.ResourceAdvertisement(resource)
        self.assertEqual(advertisement.pack(), native_golden("kLxmfResourceAdvertisement"))

    def test_binary_field_keys_are_rejected_by_reference(self):
        fields = umsgpack.unpackb(native_golden("kLxmfResourceAdvertisement"))
        broken_wire = umsgpack.packb({key.encode(): value for key, value in fields.items()})
        # This is the former device encoding. RNS Link.receive tears down its
        # link when the ResourceAdvertisement parser raises this exception.
        with self.assertRaises(KeyError):
            RNS.ResourceAdvertisement.unpack(broken_wire)


if __name__ == "__main__":
    unittest.main()
