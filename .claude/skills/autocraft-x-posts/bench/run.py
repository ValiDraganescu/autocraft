# /// script
# requires-python = ">=3.11"
# ///
"""One benchmark round: a post per brief written by `claude -p` with the
skill as its system prompt, plus a few blind posts written without it.

    uv run run.py ROUND [--baseline N] [--jobs 6]

Writes rounds/ROUND.json: [{id, round, brief, arm, text}]. The arm
("skill" or "baseline") is hidden on the voting page until revealed."""
import argparse
import json
import random
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
SKILL = HERE.parent / "SKILL.md"
MODEL = "claude-opus-5-5"
BASELINE = "You write posts for X (Twitter). Output only the post text."


def ask(system: str, prompt: str) -> str:
    r = subprocess.run(["claude", "-p", "--model", MODEL, "--system-prompt", system, "--tools", "",
                        "--no-session-persistence", "--strict-mcp-config", "--setting-sources", "", prompt],
                       capture_output=True, text=True, timeout=600, cwd=HERE)
    if r.returncode != 0:
        raise RuntimeError(r.stderr or r.stdout)
    return r.stdout.strip()


def prompt(author: str, b: dict) -> str:
    facts = "\n".join(f"- {f}" for f in b["facts"])
    return (f"Author: {author}\n\nWrite one X post for this brief.\n\nAttached media: {b['attachment']}\n\n"
            f"Facts for this post:\n{facts}\n\nThe link for the reply: {b['link']}\n\nOutput only the post text.")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("round", type=int)
    ap.add_argument("--baseline", type=int, default=5)
    ap.add_argument("--jobs", type=int, default=6)
    a = ap.parse_args()
    spec = json.loads((HERE / "briefs.json").read_text())
    skill = SKILL.read_text()
    rnd = random.Random(a.round)
    jobs = [("skill", b) for b in spec["briefs"]]
    jobs += [("baseline", b) for b in rnd.sample(spec["briefs"], a.baseline)]

    def one(job):
        arm, b = job
        text = ask(skill if arm == "skill" else BASELINE, prompt(spec["author"], b))
        print(f"{arm:8} {b['id']}: {text[:70]!r}", flush=True)
        return {"brief": b["id"], "arm": arm, "text": text, "video": b["video"], "angle": b["angle"], "attachment": b["attachment"]}

    with ThreadPoolExecutor(a.jobs) as ex:
        posts = list(ex.map(one, jobs))
    rnd.shuffle(posts)
    for i, p in enumerate(posts):
        p.update(id=f"r{a.round}-{i:02d}", round=a.round)
    out = HERE / "rounds" / f"{a.round}.json"
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(posts, indent=1, ensure_ascii=False))
    print(f"wrote {out} ({len(posts)} posts)")


if __name__ == "__main__":
    main()
