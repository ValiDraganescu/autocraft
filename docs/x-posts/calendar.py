# /// script
# requires-python = ">=3.11"
# ///
"""The X post calendar: the dated posts in order, then the drafts.

    uv run docs/x-posts/calendar.py [--check]
    uv run docs/x-posts/calendar.py --export DIR   # one JSON per post, for the vote page's calendar tab
    uv run docs/x-posts/calendar.py --apply DIR    # take status, date and notes back from the page's JSON"""
import json
import re
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def parse(path: Path) -> dict:
    text = path.read_text()
    m = re.match(r"---\n(.*?)\n---\n(.*)", text, re.S)
    if not m:
        return {"slug": path.stem, "error": "no front matter"}
    meta = {}
    for line in m.group(1).splitlines():
        k, _, v = line.partition(":")
        meta[k.strip()] = v.split("#")[0].strip() if k.strip() != "posted" else v.strip()
    sections = dict(re.findall(r"^## (\w+)\n(.*?)(?=^## |\Z)", m.group(2), re.S | re.M))
    return {"slug": path.stem, **meta, "post": sections.get("Post", "").strip(), "reply": sections.get("Reply", "").strip(),
            "notes": sections.get("Notes", "").strip()}


def export(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    for p in posts():
        keys = ("status", "date", "angle", "video", "posted", "post", "reply", "notes")
        (out / f"{p['slug']}.json").write_text(json.dumps({k: p.get(k, "") for k in keys} | {"changed": False}, ensure_ascii=False))
        print(out / f"{p['slug']}.json")


def apply(src: Path) -> None:
    """The page's edits (status, date, notes) into the post files; the text stays as it is."""
    for f in sorted(src.rglob("*.json")):
        d = json.loads(f.read_text())
        d = d.get("data", d)
        path = HERE / f"{f.stem}.md"
        if not d.get("changed") or not path.exists():
            continue
        text = path.read_text()
        for k in ("status", "date"):
            text = re.sub(rf"^{k}:.*$", f"{k}: {d.get(k, '')}".rstrip(), text, count=1, flags=re.M)
        if "notes" in d:
            text = re.sub(r"^## Notes\n.*\Z", "## Notes\n\n" + d["notes"].strip() + "\n", text, flags=re.S | re.M)
        path.write_text(text)
        print(f"{f.stem}: {d.get('status')} {d.get('date') or ''}")


def posts() -> list[dict]:
    return [parse(p) for p in sorted(HERE.glob("*.md")) if p.name != "README.md"]


def x_length(t: str) -> int:
    urls = re.findall(r"\b(?:https?://)?[a-z0-9-]+(?:\.[a-z0-9-]+)*\.(?:com|org|io|net|dev|app|gg)(?:/\S*)?", t, re.I)
    for u in urls:
        t = t.replace(u, "")
    return 23 * len(urls) + sum(2 if ord(c) > 0x10FF else 1 for c in t)


def main() -> None:
    if "--export" in sys.argv:
        return export(Path(sys.argv[sys.argv.index("--export") + 1]))
    if "--apply" in sys.argv:
        return apply(Path(sys.argv[sys.argv.index("--apply") + 1]))
    check = "--check" in sys.argv
    items = posts()
    dated = sorted((p for p in items if p.get("date")), key=lambda p: p["date"])
    drafts = [p for p in items if not p.get("date")]
    for p in dated:
        print(f"{p['date']}  {p.get('status', '?'):8}  {p.get('angle', ''):9}  {p['slug']}")
    if drafts:
        print("\nno date:")
        for p in drafts:
            print(f"            {p.get('status', '?'):8}  {p.get('angle', ''):9}  {p['slug']}")
    if not check:
        return
    warn = []
    for day, n in Counter(p["date"] for p in dated).items():
        if n > 2:
            warn.append(f"{day}: {n} posts on one day")
    for a, b in zip(dated, dated[1:]):
        if a.get("angle") and a.get("angle") == b.get("angle"):
            warn.append(f"{a['slug']} and {b['slug']}: the same angle ({a['angle']}) twice in a row")
    for p in items:
        if p.get("error"):
            warn.append(f"{p['slug']}: {p['error']}")
            continue
        if p.get("status") not in ("proposed", "wip", "approved", "posted"):
            warn.append(f"{p['slug']}: status '{p.get('status')}'")
        if p.get("status") == "approved" and not p.get("date"):
            warn.append(f"{p['slug']}: approved without a date")
        video = p.get("video", "")
        if p.get("status") in ("approved", "posted") and (not video or video.startswith("record:") or not (REPO / video).exists()):
            warn.append(f"{p['slug']}: video '{video}' isn't there")
        if p.get("status") == "posted" and not p.get("posted"):
            warn.append(f"{p['slug']}: posted without its link")
        if x_length(p["post"]) > 700:
            warn.append(f"{p['slug']}: the post is {x_length(p['post'])} characters")
    print("\n" + ("\n".join(warn) if warn else "no warnings"))


if __name__ == "__main__":
    main()
