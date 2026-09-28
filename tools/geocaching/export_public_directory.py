"""Export current public signed records for GitHub Pages, without private state."""
import argparse
import base64
from datetime import datetime, timezone
import json
from pathlib import Path
import sqlite3

from directory_store import packed, verify_cache


def export(database, output):
    output = Path(output)
    objects = output / "objects"
    objects.mkdir(parents=True, exist_ok=True)
    summaries = []
    with sqlite3.connect(Path(database).resolve().as_uri() + "?mode=ro", uri=True) as db:
        db.execute("BEGIN")
        rows = db.execute("""SELECT o.cache, o.hash, o.raw, o.signature FROM heads h
            JOIN objects o ON o.hash=h.hash JOIN public_refs p ON p.hash=o.hash
            WHERE h.conflict=0 ORDER BY o.cache""")
        for cache_id, revision_hash, raw, signature in rows:
            verified = verify_cache([raw, signature])
            if verified.cache_id != cache_id or verified.revision_hash != revision_hash:
                raise ValueError("Stored object identity mismatch")
            (objects / f"{revision_hash.hex()}.bin").write_bytes(packed([raw, signature]))
            summaries.append(base64.b64encode(packed(verified.summary)).decode("ascii"))
            if len(summaries) > 20000:
                raise ValueError("Published directory requires partitioning above 20000 caches")
    data = {"version": 1, "updatedAt": datetime.now(timezone.utc).isoformat(), "summaries": summaries}
    temporary = output / "index.json.tmp"
    temporary.write_text(json.dumps(data, ensure_ascii=True, separators=(",", ":")) + "\n", encoding="utf-8")
    temporary.replace(output / "index.json")
    return len(summaries)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    print(f"Exported {export(args.database, args.output)} public caches")
