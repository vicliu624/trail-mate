"""Durable Geocaching v1 directory operations; transport authenticates the caller.

SQLite owns objects, current heads, conflict proofs, public log, snapshots and
request receipts in one transaction. Objects/logs are retained indefinitely;
capacity admission fails closed instead of breaking an existing retention promise.
"""
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import sqlite3
import threading
import time

import msgpack
import RNS

APP_TYPE = "trailmate.geocache"
DEDUP_TTL = 30 * 86400
CURSOR_TTL = 7 * 86400


def packed(value):
    return msgpack.packb(value, use_bin_type=True)


def unpacked(raw, limit=8192):
    if type(raw) is not bytes or not 0 < len(raw) <= limit:
        raise ValueError("payload size")
    value = msgpack.unpackb(raw, raw=False, max_array_len=2048, max_map_len=0,
                            max_bin_len=limit, max_str_len=limit, max_ext_len=0)
    pending, elements = [(value, 0)], 0
    while pending:
        item, depth = pending.pop()
        if depth > 8 or type(item) not in (list, int, bytes, str, type(None)):
            raise ValueError("CMP1 type/depth")
        if type(item) is list:
            elements += len(item)
            if elements > 2048:
                raise ValueError("CMP1 element count")
            pending.extend((child, depth + 1) for child in item)
    if packed(value) != raw:
        raise ValueError("noncanonical CMP1")
    return value


def integer(value, low, high):
    return type(value) is int and low <= value <= high


def binary(value, length):
    return type(value) is bytes and len(value) == length


def text_valid(value, limit, multiline=False, nonempty=False):
    return (type(value) is str and len(value.encode("utf-8")) <= limit
            and (not nonempty or bool(value.strip()))
            and all(not ((ord(c) < 32 and not (multiline and c in "\t\n"))
                         or 127 <= ord(c) <= 159 or ord(c) in (65534, 65535)) for c in value))


class ProtocolError(Exception):
    def __init__(self, status, token, context=None):
        self.status, self.body = status, [token, None, context]
        super().__init__(token)


@dataclass(frozen=True)
class Verified:
    cache_id: bytes
    revision_hash: bytes
    signed: list
    record: list
    summary: list


def verify_cache(signed):
    if type(signed) is not list or len(signed) != 2 or not binary(signed[1], 64) or type(signed[0]) is not bytes:
        raise ProtocolError(422, "invalid_record")
    raw, signature = signed
    if len(raw) > 4096:
        raise ProtocolError(413, "record_too_large", [4096])
    try:
        record = unpacked(raw, 4096)
    except (ValueError, TypeError, msgpack.UnpackException):
        raise ProtocolError(422, "invalid_record") from None
    if type(record) is list and record and type(record[0]) is int and record[0] != 1:
        raise ProtocolError(426, "unsupported_version", [[1], [1]])
    if type(record) is not list or len(record) != 16 or record[0] != 1 or not binary(record[1], 64) or not binary(record[2], 16):
        raise ProtocolError(422, "invalid_record")
    for index, low, high in ((3, 1, 0xFFFFFFFF), (5, 0, 2), (6, -900000000, 900000000),
                             (7, -1800000000, 1799999999), (11, 2, 10), (12, 2, 10), (13, 0, 5),
                             (14, 0, 253402300799), (15, 0, 253402300799)):
        if not integer(record[index], low, high):
            raise ProtocolError(422, "invalid_record")
    if (record[3] == 1 and record[4] is not None) or (record[3] > 1 and not binary(record[4], 32)):
        raise ProtocolError(422, "invalid_record")
    if not text_valid(record[8], 96, nonempty=True) or not text_valid(record[9], 2048, True) or not text_valid(record[10], 512, True):
        raise ProtocolError(422, "invalid_record")
    identity = RNS.Identity(create_keys=False)
    try:
        valid = identity.load_public_key(record[1]) and identity.validate(signature, b"trailmate.geocache/sign/v1\0" + raw)
    except (ValueError, TypeError):
        valid = False
    if not valid:
        raise ProtocolError(422, "invalid_signature")
    cache_id = hashlib.sha256(b"trailmate.geocache/id/v1\0" + record[1] + record[2]).digest()
    revision_hash = hashlib.sha256(b"trailmate.geocache/revision/v1\0" + raw).digest()
    summary = [cache_id, record[3], revision_hash, record[5], record[6], record[7], record[8], record[11], record[12], record[13], len(packed(signed))]
    return Verified(cache_id, revision_hash, signed, record, summary)


class DirectoryStore:
    def __init__(self, path, destination, name="Trail Mate public directory", *, clock=time.time,
                 max_objects=100000, max_results=1000000, max_snapshots=256,
                 max_snapshot_items=100000, requests_per_minute=120):
        if not binary(destination, 16) or not text_valid(name, 40, nonempty=True):
            raise ValueError("invalid directory identity/name")
        self.clock, self.destination, self.name = clock, destination, name
        self.max_objects, self.max_results = max_objects, max_results
        self.max_snapshots, self.max_snapshot_items = max_snapshots, max_snapshot_items
        self.requests_per_minute = requests_per_minute
        self.lock = threading.RLock()
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(path, timeout=5, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA foreign_keys=ON")
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        version = self.db.execute("PRAGMA user_version").fetchone()[0]
        if version not in (0, 1):
            raise ValueError("unsupported directory database schema")
        self.db.executescript("""
          CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value BLOB NOT NULL);
          CREATE TABLE IF NOT EXISTS objects (
            hash BLOB PRIMARY KEY, cache BLOB NOT NULL, revision INTEGER NOT NULL,
            previous BLOB, state INTEGER NOT NULL, created INTEGER NOT NULL,
            raw BLOB NOT NULL, signature BLOB NOT NULL, summary BLOB NOT NULL,
            author BLOB NOT NULL, latitude INTEGER NOT NULL, longitude INTEGER NOT NULL,
            accepted INTEGER NOT NULL);
          CREATE INDEX IF NOT EXISTS object_history ON objects(cache,revision,hash);
          CREATE TABLE IF NOT EXISTS heads (cache BLOB PRIMARY KEY, hash BLOB NOT NULL,
            conflict INTEGER NOT NULL DEFAULT 0, proof_a BLOB, proof_b BLOB);
          CREATE TABLE IF NOT EXISTS public_refs (hash BLOB PRIMARY KEY REFERENCES objects(hash));
          CREATE TABLE IF NOT EXISTS log (sequence INTEGER PRIMARY KEY AUTOINCREMENT,
            hash BLOB UNIQUE NOT NULL REFERENCES objects(hash));
          CREATE TABLE IF NOT EXISTS requests (source BLOB NOT NULL, version INTEGER NOT NULL,
            id BLOB NOT NULL, fingerprint BLOB NOT NULL, response BLOB NOT NULL,
            created INTEGER NOT NULL, PRIMARY KEY(source,version,id));
          CREATE INDEX IF NOT EXISTS request_age ON requests(created);
          CREATE INDEX IF NOT EXISTS request_rate ON requests(source,created);
          CREATE TABLE IF NOT EXISTS snapshots (id BLOB PRIMARY KEY, source BLOB NOT NULL,
            operation INTEGER NOT NULL, binding BLOB NOT NULL, phase INTEGER NOT NULL,
            high INTEGER NOT NULL, expires INTEGER NOT NULL);
          CREATE TABLE IF NOT EXISTS snapshot_items (position INTEGER PRIMARY KEY AUTOINCREMENT,
            snapshot BLOB NOT NULL REFERENCES snapshots(id) ON DELETE CASCADE,
            hash BLOB NOT NULL REFERENCES objects(hash));
          CREATE INDEX IF NOT EXISTS snapshot_position ON snapshot_items(snapshot,position);
          CREATE TABLE IF NOT EXISTS cursors (token BLOB PRIMARY KEY, source BLOB NOT NULL,
            operation INTEGER NOT NULL, binding BLOB NOT NULL, snapshot BLOB,
            position INTEGER NOT NULL, expires INTEGER NOT NULL, body BLOB);
          CREATE INDEX IF NOT EXISTS cursor_age ON cursors(expires);
          CREATE TABLE IF NOT EXISTS peers (destination BLOB PRIMARY KEY, public_key BLOB NOT NULL,
            state BLOB NOT NULL, due INTEGER NOT NULL, scheduled INTEGER NOT NULL);
          PRAGMA user_version=1;
        """)
        with self.db:
            self.db.execute("INSERT OR IGNORE INTO meta VALUES('destination',?)", (destination,))
            self.db.execute("INSERT OR IGNORE INTO meta VALUES('epoch',?)", (os.urandom(16),))
        if self.db.execute("SELECT value FROM meta WHERE key='destination'").fetchone()[0] != destination:
            raise ValueError("directory identity does not match database")
        self.epoch = self.db.execute("SELECT value FROM meta WHERE key='epoch'").fetchone()[0]

    def close(self):
        self.db.close()

    def sequence(self):
        return self.db.execute("SELECT COALESCE(MAX(sequence),0) FROM log").fetchone()[0]

    def capabilities(self):
        return [[1], [1], [0, 1, 2, 3, 4], 8192, 4096, 64, CURSOR_TTL, DEDUP_TTL, 2, self.name, 1, DEDUP_TTL]

    def handle(self, source, raw):
        """Return durable response bytes, or None for an unaccepted request.

        The adapter MUST have verified the LXMF source, destination and signature.
        No successful response escapes a failed transaction.
        """
        if not binary(source, 16):
            return None
        try:
            request = unpacked(raw)
        except (ValueError, TypeError, msgpack.UnpackException):
            return None
        if (type(request) is not list or len(request) != 6 or not integer(request[0], 0, 255)
                or request[1] != 0 or not integer(request[2], 0, 255) or not binary(request[3], 16)):
            return None
        version, _, operation, request_id, budget, body = request
        fingerprint = hashlib.sha256(raw).digest()
        now = int(self.clock())
        try:
            with self.lock, self.db:
                self.db.execute("BEGIN IMMEDIATE")
                prior = self.db.execute("SELECT fingerprint,response,created FROM requests WHERE source=? AND version=? AND id=?", (source, version, request_id)).fetchone()
                if prior and prior["created"] + DEDUP_TTL > now:
                    if prior["fingerprint"] == fingerprint:
                        return prior["response"]
                    return packed([1, 1, operation, request_id, 409, ["request_id_reused", None, None]])
                if self.db.execute("SELECT COUNT(*) FROM requests").fetchone()[0] >= self.max_results:
                    return None
                self.db.execute("SAVEPOINT operation")
                try:
                    if version != 1:
                        raise ProtocolError(426, "unsupported_version", [[1], [1]])
                    if not integer(budget, 512, 8192) or type(body) is not list:
                        raise ProtocolError(400, "malformed_request")
                    count = self.db.execute("SELECT COUNT(*) FROM requests WHERE source=? AND created>?", (source, now - 60)).fetchone()[0]
                    if count >= self.requests_per_minute:
                        raise ProtocolError(429, "rate_limited")
                    status, result = self._dispatch(source, operation, body, budget, request_id, now)
                    size = len(packed([1, 1, operation, request_id, status, result]))
                    if size > budget:
                        raise ProtocolError(413, "response_too_large", [size])
                except ProtocolError as error:
                    self.db.execute("ROLLBACK TO operation")
                    status, result = error.status, error.body
                self.db.execute("RELEASE operation")
                response = packed([1, 1, operation, request_id, status, result])
                self.db.execute("INSERT OR REPLACE INTO requests VALUES(?,?,?,?,?,?)", (source, version, request_id, fingerprint, response, now))
            return response
        except sqlite3.Error:
            # A disk-full/read-only/failed commit cannot durably reserve even
            # a 503 result. Leave the sender unconfirmed; never send a false 200.
            return None

    def _dispatch(self, source, operation, body, budget, request_id, now):
        if operation == 0:
            if body:
                raise ProtocolError(400, "malformed_request")
            return 200, self.capabilities()
        if operation == 1:
            if len(body) != 1:
                raise ProtocolError(400, "malformed_request")
            return self._accept(verify_cache(body[0]), now, False)
        if operation == 2:
            self._query_parameters(body)
            binding = packed(body[:4] + [budget])
            token = body[4]
            if token is None:
                snapshot = self._snapshot(source, 2, binding, 0, now)
                bbox, mask, author, _, _ = body
                south, west, north, east = bbox
                self.db.execute("""INSERT INTO snapshot_items(snapshot,hash)
                    SELECT ?,o.hash FROM heads h JOIN objects o ON o.hash=h.hash
                    WHERE h.conflict=0 AND o.latitude BETWEEN ? AND ?
                    AND (o.longitude BETWEEN ? AND ? OR (?=1800000000 AND o.longitude=-1800000000))
                    AND (? & (1 << o.state))!=0 AND (? IS NULL OR o.author=?)
                    ORDER BY o.cache LIMIT ?""", (snapshot, south, north, west, east, east, mask, author, author, self.max_snapshot_items + 1))
                self._snapshot_capacity(snapshot)
                token = self._cursor(source, 2, binding, snapshot, 0, now + CURSOR_TTL)
            return 200, self._page(source, 2, binding, token, body[3], budget, request_id, now)
        if operation == 3:
            if len(body) != 3 or not binary(body[0], 32) or any(value is not None and not binary(value, 32) for value in body[1:]) or (body[1] is not None and body[2] is not None):
                raise ProtocolError(400, "malformed_request")
            cache_id, wanted, known = body
            head = self._head(cache_id)
            if not head:
                raise ProtocolError(404, "not_found")
            if wanted is None and head["conflict"]:
                raise ProtocolError(409, "version_conflict")
            if wanted is None and known == head["hash"]:
                return 304, [cache_id, head["revision"], head["hash"], head["state"]]
            row = self.db.execute("SELECT * FROM objects WHERE cache=? AND hash=?", (cache_id, wanted or head["hash"])).fetchone()
            if not row:
                raise ProtocolError(404, "revision_unavailable")
            return 200, [[row["raw"], row["signature"]], int(row["hash"] == head["hash"]), head["conflict"]]
        if operation == 4:
            if len(body) != 2 or not integer(body[1], 1, 64) or not self._valid_cursor(body[0]):
                raise ProtocolError(400, "malformed_request")
            token, limit = body
            binding = packed([limit, budget])
            if token is None:
                snapshot = self._snapshot(source, 4, binding, 0, now)
                self.db.execute("""INSERT INTO snapshot_items(snapshot,hash)
                    SELECT ?,o.hash FROM public_refs p JOIN objects o ON o.hash=p.hash
                    ORDER BY o.cache,o.revision,o.hash LIMIT ?""", (snapshot, self.max_snapshot_items + 1))
                self._snapshot_capacity(snapshot)
                token = self._cursor(source, 4, binding, snapshot, 0, now + CURSOR_TTL)
            return 200, self._page(source, 4, binding, token, limit, budget, request_id, now)
        raise ProtocolError(501, "unsupported_operation")

    @staticmethod
    def _valid_cursor(token):
        return token is None or (type(token) is bytes and 1 <= len(token) <= 64)

    def _query_parameters(self, body):
        if len(body) != 5:
            raise ProtocolError(400, "malformed_request")
        bbox, mask, author, limit, cursor = body
        if (type(bbox) is not list or len(bbox) != 4 or not integer(mask, 1, 7)
                or not integer(limit, 1, 64) or not self._valid_cursor(cursor)
                or (author is not None and not binary(author, 32))
                or not all(integer(bbox[i], -900000000 if i % 2 == 0 else -1800000000,
                                   900000000 if i % 2 == 0 else 1800000000) for i in range(4))
                or bbox[0] > bbox[2] or bbox[1] > bbox[3]):
            raise ProtocolError(400, "malformed_request")

    def _head(self, cache_id):
        return self.db.execute("SELECT o.*,h.conflict,h.proof_a,h.proof_b FROM heads h JOIN objects o ON o.hash=h.hash WHERE h.cache=?", (cache_id,)).fetchone()

    def _accept(self, item, now, replication):
        cache_id, revision_hash, record = item.cache_id, item.revision_hash, item.record
        head = self._head(cache_id)
        duplicate = self.db.execute("SELECT 1 FROM objects WHERE hash=?", (revision_hash,)).fetchone() is not None
        contradiction = self.db.execute("""SELECT * FROM objects WHERE cache=? AND hash!=? AND
            (revision=? OR created!=? OR (revision=? AND hash!=?) OR (revision=? AND previous!=?)
             OR (state=2 AND revision<?) OR (?=2 AND revision>?))
            ORDER BY revision,hash LIMIT 1""", (cache_id, revision_hash, record[3], record[14], record[3] - 1,
                record[4], record[3] + 1, revision_hash, record[3], record[5], record[3])).fetchone()
        if head and not contradiction and not head["conflict"] and head["revision"] > record[3] and not replication:
            return 409, ["stale_revision", None, [head["revision"], head["hash"], head["state"]]]
        if not duplicate:
            if self.db.execute("SELECT COUNT(*) FROM objects").fetchone()[0] >= self.max_objects:
                raise ProtocolError(503, "temporarily_unavailable")
            self.db.execute("INSERT INTO objects VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)", (revision_hash, cache_id, record[3], record[4], record[5], record[14],
                item.signed[0], item.signed[1], packed(item.summary), hashlib.sha256(record[1]).digest(), record[6], record[7], now))
        else:
            self.db.execute("UPDATE objects SET accepted=MAX(accepted,?) WHERE hash=?", (now, revision_hash))
        if contradiction and head and not head["conflict"]:
            self.db.execute("UPDATE heads SET conflict=1,proof_a=?,proof_b=? WHERE cache=?", (contradiction["hash"], revision_hash, cache_id))
            for proof in (contradiction["hash"], revision_hash):
                self.db.execute("INSERT OR IGNORE INTO public_refs VALUES(?)", (proof,))
                self._append_log(proof)
        if (head and head["conflict"]) or contradiction:
            return 409, ["version_conflict", None, None]
        if not head or head["revision"] < record[3]:
            self.db.execute("INSERT OR REPLACE INTO heads(cache,hash) VALUES(?,?)", (cache_id, revision_hash))
            self.db.execute("DELETE FROM public_refs WHERE hash IN (SELECT hash FROM objects WHERE cache=?)", (cache_id,))
            self.db.execute("INSERT INTO public_refs VALUES(?)", (revision_hash,))
            self._append_log(revision_hash)
        if head and head["revision"] > record[3]:
            return 200, None  # Verified history during replication; no head rollback/log amplification.
        return 200, [cache_id, record[3], revision_hash, int(duplicate), record[3], revision_hash, record[5]]

    def _append_log(self, revision_hash):
        # Ignored AUTOINCREMENT inserts consume IDs. Only a newly visible
        # version advances the catalog sequence, including conflict proofs.
        self.db.execute("""INSERT INTO log(sequence,hash)
            SELECT COALESCE(MAX(sequence),0)+1,? FROM log
            HAVING NOT EXISTS (SELECT 1 FROM log WHERE hash=?)""",
            (revision_hash, revision_hash))

    def import_record(self, signed, reference):
        item = verify_cache(signed)
        if reference != [item.cache_id, item.record[3], item.revision_hash, item.record[5]]:
            raise ValueError("sync reference differs from signed object")
        with self.lock, self.db:
            self.db.execute("BEGIN IMMEDIATE")
            self._accept(item, int(self.clock()), True)

    def has_reference(self, reference):
        row = self.db.execute("SELECT cache,revision,hash,state FROM objects WHERE hash=?", (reference[2],)).fetchone()
        return row is not None and list(row) == reference

    def _snapshot(self, source, operation, binding, phase, now):
        if self.db.execute("SELECT COUNT(*) FROM snapshots").fetchone()[0] >= self.max_snapshots:
            raise ProtocolError(503, "temporarily_unavailable")
        token = os.urandom(16)
        self.db.execute("INSERT INTO snapshots VALUES(?,?,?,?,?,?,?)", (token, source, operation, binding, phase, self.sequence(), now + CURSOR_TTL))
        return token

    def _snapshot_capacity(self, snapshot):
        if self.db.execute("SELECT COUNT(*) FROM snapshot_items WHERE snapshot=?", (snapshot,)).fetchone()[0] > self.max_snapshot_items:
            raise ProtocolError(503, "temporarily_unavailable")

    def _cursor(self, source, operation, binding, snapshot, position, expires):
        token = os.urandom(32)
        self.db.execute("INSERT INTO cursors VALUES(?,?,?,?,?,?,?,NULL)", (token, source, operation, binding, snapshot, position, expires))
        return token

    def _page(self, source, operation, binding, token, limit, budget, request_id, now):
        cursor = self.db.execute("SELECT * FROM cursors WHERE token=?", (token,)).fetchone()
        expired = "cursor_expired" if operation == 2 else "sync_reset_required"
        if not cursor:
            raise ProtocolError(410, expired)
        if cursor["source"] != source or cursor["operation"] != operation or cursor["binding"] != binding:
            raise ProtocolError(400, "invalid_cursor")
        if cursor["expires"] <= now:
            raise ProtocolError(410, expired)
        if cursor["body"] is not None:
            result = msgpack.unpackb(cursor["body"], raw=False)
            next_cursor = result[2] if operation == 2 else result[3]
            expires = cursor["expires"]
            if next_cursor is not None:
                expires = self.db.execute("SELECT expires FROM cursors WHERE token=?", (next_cursor,)).fetchone()[0]
            result[-1] = max(0, expires - now)
            return result
        snapshot_id = cursor["snapshot"]
        if snapshot_id is None:
            # A checkpoint captures a NEW high watermark for each new request.
            snapshot_id = self._snapshot(source, 4, binding, 1, now)
            high = self.sequence()
            self.db.execute("""INSERT INTO snapshot_items(snapshot,hash)
                SELECT ?,hash FROM log WHERE sequence>? AND sequence<=? ORDER BY sequence LIMIT ?""", (snapshot_id, cursor["position"], high, self.max_snapshot_items + 1))
            self._snapshot_capacity(snapshot_id)
            position = 0
        else:
            position = cursor["position"]
        snapshot = self.db.execute("SELECT * FROM snapshots WHERE id=?", (snapshot_id,)).fetchone()
        if not snapshot or snapshot["expires"] <= now:
            raise ProtocolError(410, expired)
        rows = self.db.execute("""SELECT i.position,o.* FROM snapshot_items i JOIN objects o ON o.hash=i.hash
            WHERE i.snapshot=? AND i.position>? ORDER BY i.position LIMIT ?""", (snapshot_id, position, limit + 1)).fetchall()
        items = [msgpack.unpackb(row["summary"], raw=False) if operation == 2 else [row["cache"], row["revision"], row["hash"], row["state"]] for row in rows]
        take = min(limit, len(items))
        while True:
            more = take < len(items)
            ttl = snapshot["expires"] - now if operation == 2 or more else DEDUP_TTL
            next_token = b"\0" * 32 if operation == 4 or more else None
            result = ([snapshot_id, items[:take], next_token, ttl] if operation == 2 else
                      [snapshot["phase"], self.epoch, items[:take], next_token, int(more), ttl])
            size = len(packed([1, 1, operation, request_id, 200, result]))
            if size <= budget:
                break
            if take <= 1:
                raise ProtocolError(413, "response_too_large", [size])
            take -= 1
        if more:
            next_token = self._cursor(source, operation, binding, snapshot_id, rows[take - 1]["position"], snapshot["expires"])
        elif operation == 4:
            next_token = self._cursor(source, operation, binding, None, snapshot["high"], now + DEDUP_TTL)
        result[2 if operation == 2 else 3] = next_token
        if cursor["snapshot"] is not None:
            self.db.execute("UPDATE cursors SET body=? WHERE token=?", (packed(result), token))
        return result

    def maintenance(self):
        now = int(self.clock())
        with self.lock, self.db:
            self.db.execute("DELETE FROM requests WHERE created<=?", (now - DEDUP_TTL,))
            self.db.execute("DELETE FROM cursors WHERE expires<=?", (now,))
            self.db.execute("DELETE FROM snapshots WHERE expires<=?", (now,))
