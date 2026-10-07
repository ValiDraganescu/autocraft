# /// script
# requires-python = ">=3.11"
# ///
"""Writes one JSON file per post of a round (and the round's own doc) for
the voting page's database, and prints the batch to send.

    uv run seed.py ROUND [--changes "what changed in the skill"]"""
import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent

ap = argparse.ArgumentParser()
ap.add_argument("round", type=int)
ap.add_argument("--changes", default="")
a = ap.parse_args()
posts = json.loads((HERE / "rounds" / f"{a.round}.json").read_text())
out = HERE / "rounds" / f"{a.round}-db"
out.mkdir(exist_ok=True)
writes = []
for p in posts:
    f = out / f"{p['id']}.json"
    f.write_text(json.dumps({**p, "vote": 0, "note": ""}, ensure_ascii=False))
    writes.append({"op": "set", "collection": "posts", "doc_id": p["id"], "file_path": str(f)})
f = out / "round.json"
f.write_text(json.dumps({"n": a.round, "changes": a.changes, "note": "", "submitted": False, "satisfied": False}, ensure_ascii=False))
writes.append({"op": "set", "collection": "rounds", "doc_id": str(a.round), "file_path": str(f)})
print(json.dumps(writes))
