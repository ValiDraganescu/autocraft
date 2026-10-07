# X post calendar

One post per file. The `ringshadow-x-posts` skill writes drafts here, the
developer approves them and gives them a date, and a posted file keeps its
link. [../x-plan.md](../x-plan.md) has the strategy: two posts a day at most,
five hours apart, and a different kind of post each time.

```sh
uv run docs/x-posts/calendar.py          # the schedule, then the drafts
uv run docs/x-posts/calendar.py --check  # also warns on clashes
```

The vote page of the `ringshadow-x-posts` skill has a Calendar tab: the posts
with their videos, a status, a date and notes for each. `--export DIR` writes
the JSON the page loads; `--apply DIR` takes the status, date and notes the
developer set there back into these files (read the page's `calendar`
collection into DIR first).

## A post file

`docs/x-posts/<slug>.md`, the slug in kebab-case (`kstr-radio-ads.md`):

```markdown
---
status: proposed       # proposed, wip, approved, posted
date:                  # the day to post it (YYYY-MM-DD), once approved
angle: world           # play, build, world, unit, mechanic
video: docs/media/dive.gif
posted:                # the post's x.com link, once posted
---

## Post

The text, exactly as it goes on X.

## Reply

The first reply under it, usually the GitHub link to the skill involved.

## Notes

Why it's written this way, feedback, what's missing.
```

`video` is a file in the repo (`docs/media/…`, `video/renders/…`), or
`record: <what to record>` when the clip doesn't exist yet: a sim of
[the video skill](../../.claude/skills/ringshadow-video/sims.json) or a
description.

## Statuses

- **proposed**: a draft Claude wrote. Every new post starts here. No date,
  and it can miss its video.
- **wip**: the developer picked it. Claude makes its video (records the clip
  with the video skill) and reworks the text from the notes.
- **approved**: the developer read it and wants it out. It has a date and a
  video that exists.
- **posted**: on X. `posted` has the link, `date` the day it went out. Don't
  edit the text afterwards: it's the record of what was said. A post from
  before 2026-10-07 says Autocraft, the game's name until the rename to
  Ringshadow, and keeps it.

`--check` warns when a day has more than two posts, when two posts in a row
share an angle, when an approved post has no date or its video is missing,
and when a post is over 700 characters (the skill aims for 450 to 700, with the news inside the first 280, where X folds the post).
