"""Exercise persisted replication against real stores and signed records."""
import os
from pathlib import Path
import tempfile
import unittest

import RNS

from directory_store import DirectoryStore, packed, unpacked, verify_cache
from directory_sync import ReplicaScheduler


class ReplicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.now = 1800000000
        self.remote_identity = RNS.Identity()
        self.remote_delivery = RNS.Destination.hash_from_name_and_identity("lxmf.delivery", self.remote_identity)
        self.discovery = RNS.Destination.hash_from_name_and_identity("trailmate.geocache.directory", self.remote_identity)
        self.local = DirectoryStore(Path(self.temp.name) / "local.sqlite", os.urandom(16), clock=lambda: self.now)
        self.remote = DirectoryStore(Path(self.temp.name) / "remote.sqlite", self.remote_delivery, clock=lambda: self.now)
        self.scheduler = ReplicaScheduler(self.local)
        self.sent = []

    def tearDown(self):
        self.local.close()
        self.remote.close()
        self.temp.cleanup()

    def publish(self, title):
        author = RNS.Identity()
        raw = packed([1, author.get_public_key(), os.urandom(16), 1, None, 0, 0, 0,
                      title, "", "", 2, 2, 0, self.now, self.now])
        signed = [raw, author.sign(b"trailmate.geocache/sign/v1\0" + raw)]
        response = self.remote.handle(os.urandom(16), packed([1, 0, 1, os.urandom(16), 8192, [signed]]))
        self.assertEqual(unpacked(response)[4], 200)
        return signed

    def announce(self):
        self.scheduler.received_announce(self.discovery, self.remote_identity,
                                        packed([1, self.remote_delivery, self.remote.epoch,
                                                self.remote.sequence(), "Replication test"]))

    def step(self):
        self.scheduler.step(lambda key, raw: self.sent.append(raw))

    def deliver(self, request=None):
        request = self.sent.pop(0) if request is None else request
        response = self.remote.handle(self.local.destination, request)
        self.assertIsNotNone(response)
        self.scheduler.received(self.remote_delivery, response)

    def state(self):
        row = self.local.db.execute("SELECT * FROM peers").fetchone()
        return self.scheduler.decode(row)

    def test_discovery_and_restart_resume_exact_request_before_checkpoint(self):
        self.publish("A")
        self.publish("B")
        self.announce()
        self.step()
        self.assertEqual(unpacked(self.sent[0])[2], 0)
        self.deliver()
        self.step()
        self.assertEqual(unpacked(self.sent[0])[2], 4)
        self.deliver()
        self.step()
        self.deliver()  # First exact object committed.
        self.assertIsNone(self.state()["cursor"])
        self.assertEqual(self.local.db.execute("SELECT COUNT(*) FROM objects").fetchone()[0], 1)
        self.step()
        pending = self.sent.pop()
        path, destination = Path(self.temp.name) / "local.sqlite", self.local.destination
        self.local.close()
        self.local = DirectoryStore(path, destination, clock=lambda: self.now)
        self.scheduler = ReplicaScheduler(self.local)
        self.step()
        self.assertEqual(self.sent[0], pending)
        self.deliver()
        self.step()  # Page completion, after both object commits.
        self.assertIsNotNone(self.state()["cursor"])
        self.assertIsNone(self.state()["page"])
        self.assertEqual(self.local.db.execute("SELECT COUNT(*) FROM objects").fetchone()[0], 2)

    def test_valid_signature_for_wrong_reference_quarantines_without_advancing(self):
        expected = self.publish("Expected")
        wrong = self.publish("Other")
        self.announce()
        for _ in range(2):
            self.step()
            self.deliver()
        self.step()
        request = unpacked(self.sent.pop())
        reference = self.state()["page"][2][0]
        replacement = wrong if reference[2] == verify_cache(expected).revision_hash else expected
        response = packed([1, 1, 3, request[3], 200, [replacement, 1, 0]])
        self.scheduler.received(self.remote_delivery, response)
        state = self.state()
        self.assertEqual(state["health"], "Quarantined")
        self.assertEqual(state["index"], 0)
        self.assertIsNone(state["cursor"])
        self.assertEqual(self.local.sequence(), 0)

    def test_timeout_reuses_exact_request_and_coalesces_announcements(self):
        self.announce()
        self.step()
        first = self.sent.pop()
        self.now += 121
        self.step()
        self.assertFalse(self.sent)
        for _ in range(10):
            self.announce()
            self.step()
        self.assertEqual(self.local.db.execute("SELECT COUNT(*) FROM peers").fetchone()[0], 1)
        self.now += 60
        self.step()
        self.assertEqual(self.sent.pop(), first)

    def test_forged_discovery_binding_is_ignored(self):
        self.scheduler.received_announce(os.urandom(16), self.remote_identity,
                                        packed([1, self.remote_delivery, self.remote.epoch, 0, "Bad binding"]))
        self.step()
        self.assertFalse(self.sent)
        self.assertEqual(self.local.db.execute("SELECT COUNT(*) FROM peers").fetchone()[0], 0)


if __name__ == "__main__":
    unittest.main()
