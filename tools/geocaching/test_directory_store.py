"""Real signatures and SQLite transactions; no firmware/toolchain required."""
import os
from pathlib import Path
import tempfile
import unittest

import RNS

from directory_store import DirectoryStore, DEDUP_TTL, CURSOR_TTL, packed, unpacked, verify_cache


class DirectoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "directory.sqlite"
        self.now = 1800000000
        self.destination, self.source = os.urandom(16), os.urandom(16)
        self.author = RNS.Identity()
        self.store = self.open_store()

    def tearDown(self):
        self.store.close()
        self.temp.cleanup()

    def open_store(self):
        return DirectoryStore(self.path, self.destination, clock=lambda: self.now,
                              requests_per_minute=10000)

    def record(self, previous=None, **changes):
        record = ([1, self.author.get_public_key(), os.urandom(16), 1, None, 0,
                   305000000, 1205000000, "Directory test", "Description", "Hint",
                   2, 2, 0, self.now, self.now] if previous is None else
                  unpacked(previous[0]))
        if previous is not None:
            record[3] += 1
            record[4] = verify_cache(previous).revision_hash
        for key, value in changes.items():
            record[int(key)] = value
        raw = packed(record)
        return [raw, self.author.sign(b"trailmate.geocache/sign/v1\0" + raw)]

    def request(self, operation, body, *, source=None, request_id=None, budget=8192):
        raw = packed([1, 0, operation, request_id or os.urandom(16), budget, body])
        response = self.store.handle(source or self.source, raw)
        self.assertIsNotNone(response)
        self.assertLessEqual(len(response), budget)
        return unpacked(response)

    def publish(self, signed, **kwargs):
        return self.request(1, [signed], **kwargs)

    def query(self, cursor=None, limit=64, **kwargs):
        return self.request(2, [[-900000000, -1800000000, 900000000, 1800000000],
                               7, None, limit, cursor], **kwargs)

    def test_publish_get_restart_and_original_receipt(self):
        first = self.record()
        item = verify_cache(first)
        request_id = os.urandom(16)
        original = self.publish(first, request_id=request_id)
        self.assertEqual(original[4], 200)
        second = self.record(first, **{"15": self.now - 100})  # Clock rollback is legal.
        self.assertEqual(self.publish(second)[4], 200)
        epoch = self.store.epoch
        self.store.close()
        self.store = self.open_store()
        self.assertEqual(self.store.epoch, epoch)
        self.assertEqual(self.publish(first, request_id=request_id), original)
        self.assertEqual(self.publish(second, request_id=request_id)[4:6],
                         [409, ["request_id_reused", None, None]])
        self.assertEqual(self.query()[5][1][0][1], 2)
        self.assertEqual(self.request(3, [item.cache_id, item.revision_hash, None])[5],
                         [first, 0, 0])
        self.assertEqual(self.request(3, [item.cache_id, None, verify_cache(second).revision_hash])[4], 304)
        self.assertEqual(self.publish(first)[5][0], "stale_revision")

    def test_receipt_failure_rolls_back_publication(self):
        signed = self.record()
        self.store.db.execute("""CREATE TRIGGER fail_receipt BEFORE INSERT ON requests
                              BEGIN SELECT RAISE(ABORT,'injected receipt failure'); END""")
        response = self.store.handle(self.source, packed([1, 0, 1, os.urandom(16), 8192, [signed]]))
        self.assertIsNone(response)
        self.assertEqual(self.store.sequence(), 0)
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM objects").fetchone()[0], 0)

    def test_query_snapshot_survives_update_restart_and_budget(self):
        records = [self.record(**{"8": "X" * 96}) for _ in range(8)]
        for signed in records:
            self.assertEqual(self.publish(signed)[4], 200)
        expected = sorted((verify_cache(s).summary for s in records), key=lambda row: row[0])
        first = self.query(limit=64, budget=512)
        rows, cursor = first[5][1:3]
        self.assertTrue(rows)
        self.assertIsNotNone(cursor)
        self.publish(self.record(records[0]))
        self.publish(self.record())
        self.store.close()
        self.store = self.open_store()
        self.assertEqual(self.query(cursor, budget=512, source=os.urandom(16))[4], 400)
        while cursor:
            page = self.query(cursor, budget=512)
            self.assertEqual(page[4], 200)
            rows.extend(page[5][1])
            cursor = page[5][2]
        self.assertEqual(rows, expected)

    def test_sync_checkpoint_sees_later_changes_and_freezes_midpage(self):
        records = [self.record() for _ in range(3)]
        for signed in records:
            self.publish(signed)
        baseline = self.request(4, [None, 1])[5]
        self.assertEqual(baseline[4], 1)
        later = self.record(records[0])
        self.publish(later)
        refs = baseline[2]
        while baseline[4]:
            baseline = self.request(4, [baseline[3], 1])[5]
            refs.extend(baseline[2])
        self.assertEqual(len(refs), 3)
        checkpoint = baseline[3]
        delta = self.request(4, [checkpoint, 1])[5]
        self.assertEqual([r[2] for r in delta[2]], [verify_cache(later).revision_hash])
        newest = self.record(later)
        self.publish(newest)
        repeated_checkpoint = self.request(4, [checkpoint, 1])[5]
        self.assertEqual(repeated_checkpoint[4], 1)
        tail = self.request(4, [repeated_checkpoint[3], 1])[5]
        self.assertEqual(tail[2][0][2], verify_cache(newest).revision_hash)
        self.assertEqual(self.request(4, [tail[3], 1])[5][2], [])

    def test_conflict_is_hidden_and_proofs_replicate_without_sequence_gaps(self):
        first = self.record()
        second = self.record(first)
        fork = self.record(first, **{"8": "Conflicting v2"})
        self.publish(first)
        self.publish(second)
        self.assertEqual(self.publish(fork)[4], 409)
        self.assertEqual(self.store.sequence(), 3)
        self.assertEqual(self.publish(fork)[4], 409)
        self.publish(self.record())
        self.assertEqual(self.store.sequence(), 4)
        self.assertEqual(len(self.query()[5][1]), 1)
        item = verify_cache(first)
        self.assertEqual(self.request(3, [item.cache_id, None, None])[4], 409)
        self.assertEqual(self.request(3, [item.cache_id, item.revision_hash, None])[5][2], 1)
        other = DirectoryStore(Path(self.temp.name) / "replica.sqlite", os.urandom(16))
        try:
            refs = self.request(4, [None, 64])[5][2]
            for ref in refs:
                signed = self.request(3, [ref[0], ref[2], None])[5][0]
                other.import_record(signed, ref)
                self.assertTrue(other.has_reference(ref))
            result = unpacked(other.handle(self.source, packed([1, 0, 3, os.urandom(16), 8192,
                                                               [item.cache_id, None, None]])))
            self.assertEqual(result[4], 409)
        finally:
            other.close()

    def test_invalid_signature_and_expired_cursor(self):
        signed = self.record()
        signed[1] = bytes(64)
        self.assertEqual(self.publish(signed)[5][0], "invalid_signature")
        for _ in range(2):
            self.publish(self.record())
        cursor = self.query(limit=1)[5][2]
        self.now += CURSOR_TTL + 1
        self.assertEqual(self.query(cursor, limit=1)[4], 410)
        self.now += DEDUP_TTL
        self.store.maintenance()
        self.assertEqual(len(self.query()[5][1]), 2)  # Data survives receipt cleanup.


if __name__ == "__main__":
    unittest.main()
