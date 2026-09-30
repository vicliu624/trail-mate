"""Bounded, durable directory discovery and pull replication.

Only the service loop touches SQLite. RNS callbacks enqueue verified announce
data. A page cursor advances only after every exact signed object is durable.
"""
import os
import queue
import sqlite3

import msgpack
import RNS

from directory_store import DEDUP_TTL, ProtocolError, binary, integer, packed, text_valid, unpacked


class ReplicaScheduler:
    aspect_filter = "trailmate.geocache.directory"
    BACKOFF = (60, 300, 1800, 21600)
    POLL = 21600
    ROUND_BYTES = 256 * 1024

    def __init__(self, store, report=lambda *args, **kwargs: None):
        self.store, self.db, self.report = store, store.db, report
        self.announces = queue.Queue(maxsize=32)
        self.active = None
        self.truncated = 0

    def received_announce(self, destination_hash, announced_identity, app_data):
        try:
            data = unpacked(app_data, 96)
            if (type(data) is not list or len(data) != 5 or data[0] != 1
                    or not binary(data[1], 16) or not binary(data[2], 16)
                    or not integer(data[3], 0, 0xFFFFFFFFFFFFFFFF)
                    or not text_valid(data[4], 40, nonempty=True)
                    or destination_hash != RNS.Destination.hash_from_name_and_identity(self.aspect_filter, announced_identity)
                    or data[1] != RNS.Destination.hash_from_name_and_identity("lxmf.delivery", announced_identity)
                    or data[1] == self.store.destination):
                return
            self.announces.put_nowait((destination_hash, announced_identity.get_public_key(), data))
        except (ValueError, TypeError, AttributeError, msgpack.UnpackException):
            return
        except queue.Full:
            self.truncated += 1

    @staticmethod
    def decode(row):
        return msgpack.unpackb(row["state"], raw=False)

    def save(self, row, state, due=None, scheduled=None):
        with self.db:
            self.db.execute("UPDATE peers SET state=?,due=?,scheduled=? WHERE destination=?",
                            (packed(state), row["due"] if due is None else due,
                             row["scheduled"] if scheduled is None else scheduled, row["destination"]))

    def ingest(self, now):
        # Drain at most one callback per loop; incoming announcements cannot
        # starve accepted requests or create an unbounded discovery backlog.
        try:
            destination, public_key, data = self.announces.get_nowait()
        except queue.Empty:
            return
        row = self.db.execute("SELECT * FROM peers WHERE destination=?", (destination,)).fetchone()
        if row:
            state = self.decode(row)
            dirty = data[2] != state["announced_epoch"] or data[3] > state["announced_sequence"]
            state["seen"] = now
            if data[2] != state["announced_epoch"]:
                state["announced_epoch"], state["announced_sequence"] = data[2:4]
            else:
                state["announced_sequence"] = max(state["announced_sequence"], data[3])
            due = row["due"]
            if dirty and state["health"] == "Ready":
                due = min(due, max(now, row["scheduled"] + 1800))
            self.save(row, state, due)
            return
        with self.db:
            if self.db.execute("SELECT COUNT(*) FROM peers").fetchone()[0] >= 256:
                # Only evict a stale candidate without an unresolved request.
                candidates = self.db.execute("SELECT * FROM peers ORDER BY scheduled").fetchall()
                victim = next((r for r in candidates if self.decode(r)["seen"] < now - 7 * 86400
                               and self.decode(r)["pending"] is None and r["destination"] != self.active), None)
                if victim is None:
                    self.truncated += 1
                    return
                self.db.execute("DELETE FROM peers WHERE destination=?", (victim["destination"],))
            state = dict(delivery=data[1], announced_epoch=data[2], announced_sequence=data[3],
                         seen=now, health="Candidate", capabilities_until=0, cursor=None, phase=0,
                         epoch=None, page=None, index=0, page_expires=0, pending=None,
                         request_started=0, deadline=0, failures=0, diagnostics=[], reset_at=0,
                         round_bytes=0)
            self.db.execute("INSERT INTO peers VALUES(?,?,?,?,?)", (destination, public_key, packed(state), now, 0))
        self.report("peer_discovered", delivery=data[1].hex())

    def fail(self, row, state, reason, now, *, quarantine=False, terminal=False):
        state["diagnostics"] = (state["diagnostics"] + [[now, reason]])[-16:]
        state["failures"] += 1
        state["health"] = "Quarantined" if quarantine else "Backoff"
        delay = 21600 if quarantine else self.BACKOFF[min(state["failures"] - 1, 3)]
        if terminal or quarantine:
            state["pending"] = None
        if quarantine:
            state["capabilities_until"] = 0
        state["deadline"] = 0
        self.save(row, state, now + delay)
        if self.active == row["destination"]:
            self.active = None
        self.report("replication_wait", delivery=state["delivery"].hex(), reason=reason, seconds=delay)

    def step(self, send):
        now = int(self.store.clock())
        self.ingest(now)
        if self.active is not None:
            row = self.db.execute("SELECT * FROM peers WHERE destination=?", (self.active,)).fetchone()
            state = self.decode(row)
            if state["deadline"] > now:
                return
            self.fail(row, state, "request_timeout", now)
        row = self.db.execute("SELECT * FROM peers WHERE due<=? ORDER BY scheduled,destination LIMIT 1", (now,)).fetchone()
        if row is None:
            return
        state = self.decode(row)
        if state["round_bytes"] >= self.ROUND_BYTES:
            state["round_bytes"] = 0
            self.save(row, state, now + self.POLL, now)
            return
        if state["pending"] is None:
            if state["capabilities_until"] <= now:
                operation, body = 0, []
            elif state["page"] is not None:
                refs = state["page"][2]
                while state["index"] < len(refs) and self.store.has_reference(refs[state["index"]]):
                    state["index"] += 1
                if state["index"] == len(refs):
                    # All object transactions committed before this checkpoint.
                    more = state["page"][4]
                    state["cursor"] = state["page"][3]
                    state["phase"] = state["page"][0] if more else 1
                    state["page"], state["index"] = None, 0
                    state["health"], state["failures"] = "Ready", 0
                    if not more:
                        state["round_bytes"] = 0
                    self.save(row, state, now if more else now + self.POLL, now)
                    self.report("replication_page_committed", delivery=state["delivery"].hex(), more=bool(more))
                    return
                ref = refs[state["index"]]
                operation, body = 3, [ref[0], ref[2], None]
            else:
                operation, body = 4, [state["cursor"], 64]
            state["pending"] = packed([1, 0, operation, os.urandom(16), 8192, body])
            state["request_started"] = now
        state["deadline"] = now + 120
        state["round_bytes"] += len(state["pending"])
        self.save(row, state, now, now)  # Persist exact request BEFORE transmission.
        self.active = row["destination"]
        try:
            send(row["public_key"], state["pending"])
        except (OSError, ValueError) as error:
            self.fail(row, state, type(error).__name__, now)

    def received(self, source, raw):
        rows = self.db.execute("SELECT * FROM peers").fetchall()
        row = next((r for r in rows if self.decode(r)["delivery"] == source), None)
        if row is None:
            return
        state, now = self.decode(row), int(self.store.clock())
        if state["pending"] is None:
            return
        request = unpacked(state["pending"])
        try:
            response = unpacked(raw)
            if (type(response) is not list or len(response) != 6 or response[:4] != [1, 1, request[2], request[3]]):
                return  # A late response to a previous request cannot change this page.
            status, body = response[4:]
            if status in (429, 503):
                self.fail(row, state, f"remote_{status}", now, terminal=True)
                return
            if status == 410 and request[2] == 4:
                if now - state["reset_at"] < 86400:
                    self.fail(row, state, "repeated_sync_reset", now, quarantine=True)
                    return
                state.update(cursor=None, phase=0, epoch=None, page=None, index=0, pending=None, reset_at=now)
                self.save(row, state, now)
                self.active = None
                return
            if status != 200:
                raise ValueError(f"unexpected_status_{status}")
            if request[2] == 0:
                if (type(body) is not list or len(body) != 12 or body[0] != [1] or body[1] != [1]
                        or body[2] != [0, 1, 2, 3, 4] or body[3:6] != [8192, 4096, 64]
                        or not integer(body[6], 1, 604800) or body[7] != DEDUP_TTL
                        or body[8] != 2 or not text_valid(body[9], 40, nonempty=True)
                        or body[10] != 1 or body[11] < DEDUP_TTL):
                    raise ValueError("incompatible_capabilities")
                state["capabilities_until"] = now + 86400
            elif request[2] == 4:
                if (type(body) is not list or len(body) != 6 or body[0] != state["phase"]
                        or not binary(body[1], 16) or type(body[2]) is not list or len(body[2]) > 64
                        or type(body[3]) is not bytes or not 1 <= len(body[3]) <= 64
                        or not integer(body[4], 0, 1) or (body[4] == 1 and not body[2])
                        or not integer(body[5], 1, DEDUP_TTL)
                        or (state["epoch"] is not None and state["epoch"] != body[1])):
                    raise ValueError("invalid_sync_page")
                seen = set()
                for ref in body[2]:
                    if (type(ref) is not list or len(ref) != 4 or not binary(ref[0], 32)
                            or not integer(ref[1], 1, 0xFFFFFFFF) or not binary(ref[2], 32)
                            or not integer(ref[3], 0, 2) or ref[2] in seen):
                        raise ValueError("invalid_object_reference")
                    seen.add(ref[2])
                if body[0] == 0 and body[2] != sorted(body[2], key=lambda r: (r[0], r[1], r[2])):
                    raise ValueError("unordered_baseline")
                state["epoch"], state["page"], state["index"] = body[1], body, 0
                # Conservative lease evidence: replaying a response cannot renew it.
                state["page_expires"] = state["request_started"] + body[5]
            elif request[2] == 3:
                if (type(body) is not list or len(body) != 3 or not integer(body[1], 0, 1)
                        or not integer(body[2], 0, 1) or state["page"] is None):
                    raise ValueError("invalid_get_response")
                reference = state["page"][2][state["index"]]
                self.store.import_record(body[0], reference)
                state["index"] += 1
            else:
                raise ValueError("invalid_replication_operation")
        except (ValueError, TypeError, IndexError, msgpack.UnpackException, ProtocolError) as error:
            if isinstance(error, ProtocolError) and error.status == 503:
                self.fail(row, state, "local_capacity", now, terminal=True)
            else:
                self.fail(row, state, str(error), now, quarantine=True)
            return
        except sqlite3.Error:
            self.fail(row, state, "local_storage", now)
            return
        state["round_bytes"] += len(raw)
        state["pending"], state["deadline"] = None, 0
        self.save(row, state, now)
        if self.active == row["destination"]:
            self.active = None
