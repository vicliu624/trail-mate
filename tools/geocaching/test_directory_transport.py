"""A late identity must not lose or prematurely execute signed LXMF requests."""
import queue
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import LXMF
import RNS

from directory_service import DirectoryService
from directory_store import APP_TYPE, packed


class IdentityArrivalTests(unittest.TestCase):
    def setUp(self):
        self.sender, self.recipient = RNS.Identity(), RNS.Identity()
        source = RNS.Destination(self.sender, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
        target = RNS.Destination(self.recipient, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
        self.source, self.target = source.hash, target.hash
        self.application = packed([1, 0, 0, bytes(16), 8192, []])
        message = LXMF.LXMessage(target, source, "Geocache request", "Trail Mate Geocache v1",
                                 fields={0xFB: APP_TYPE, 0xFC: self.application}, desired_method=LXMF.LXMessage.DIRECT)
        message.pack()
        self.original = message.packed
        self.service = DirectoryService.__new__(DirectoryService)
        self.service.delivery = SimpleNamespace(hash=self.target)
        self.service.inbound = queue.Queue(maxsize=32)
        self.service.awaiting_identity = queue.Queue(maxsize=8)
        self.service.deferred_identity = {}
        self.service.next_identity_request = 0
        self.service.dropped = 0

    def recall(self, destination, *args, **kwargs):
        return {self.source: self.sender, self.target: self.recipient}.get(destination)

    def test_unknown_source_waits_then_original_signature_is_verified(self):
        with patch.object(RNS.Identity, "recall", return_value=None):
            unknown = LXMF.LXMessage.unpack_from_bytes(self.original)
            self.assertEqual(unknown.unverified_reason, LXMF.LXMessage.SOURCE_UNKNOWN)
            self.service.received(unknown)
            self.assertTrue(self.service.inbound.empty())
            with patch.object(RNS.Transport, "request_path") as request_path:
                self.service.recheck_identities(100)
                request_path.assert_called_once_with(self.source)
        with patch.object(RNS.Identity, "recall", side_effect=self.recall):
            self.service.recheck_identities(101)
        self.assertEqual(self.service.inbound.get_nowait(), (self.sender.get_public_key(), self.application))
        self.assertFalse(self.service.deferred_identity)

    def test_unknown_source_with_bad_signature_never_executes(self):
        corrupted = self.original[:32] + bytes([self.original[32] ^ 1]) + self.original[33:]
        with patch.object(RNS.Identity, "recall", return_value=None):
            self.service.received(LXMF.LXMessage.unpack_from_bytes(corrupted))
        with patch.object(RNS.Identity, "recall", side_effect=self.recall):
            self.service.recheck_identities(100)
        self.assertTrue(self.service.inbound.empty())
        self.assertFalse(self.service.deferred_identity)


if __name__ == "__main__":
    unittest.main()
