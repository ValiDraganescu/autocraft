# From idea to posted

The whole path of a post, as the developer and Claude run it: proposals on the review page, the developer's picks and notes, rework, the video, the preview, approval with a date, posting, and the record. Each step says who acts and what changes.

| Status | Who moves it there | What it means |
|---|---|---|
| `proposed` | Claude | A draft. Every new post starts here. |
| `wip` | the developer | Picked. Claude reworks it from the notes and makes its video. |
| `approved` | the developer, with a date | Ready: the text is final and the video exists. |
| `posted` | Claude, after posting | On X. `posted` holds the link. Nobody edits it again. |

The files in `docs/x-posts/` are the record ([README](../../../docs/x-posts/README.md)); the review page is where the developer reads, picks and comments. Keep them in step: every change made on one goes to the other in the same session.

## The review page

One artifact holds everything the developer looks at: https://claude.ai/artifact/D7aLFrMwDbY8rFoVCTWhdV ("Post Lab"). Its source is a single HTML file (keep a copy in the scratchpad when you change it, and publish with the same file path or pass the URL). Its database:

| Collection | Doc id | Fields | Written by |
|---|---|---|---|
| `calendar` | the post's slug | `status`, `date`, `angle`, `video`, `posted`, `post`, `reply`, `notes`, `changed` | Claude seeds it; the developer changes `status`, `date`, `notes` (and `changed: true`) |
| `posts` | `rN-NN` | `round`, `brief`, `arm`, `text`, `video`, `attachment`, `vote`, `note` | benchmark rounds (below) |
| `rounds` | `N` | `changes`, `note`, `submitted`, `satisfied` | the developer presses "Round done" |

Media shown on the cards are files published with the page under `media/<file name>`: the page takes the `video` path's file name. A published file may be at most 15 MB, so publish the X encode (under 10 MB), never the full render. Publishing more media keeps the files already there.

## 1. Propose

1. Write the posts with this skill (SKILL.md), one idea each, with a proposed video: an existing file (`docs/media/…`, `video/renders/…`) or `record: <what to shoot>`.
2. Save each as `docs/x-posts/<slug>.md` with `status: proposed`.
3. Put them on the page: `uv run docs/x-posts/calendar.py --export <scratch>/cal`, then one `ArtifactData` batch of `set` writes into `calendar` (one per file, `file_path` = the exported JSON). A doc that exists needs its `if_version`.
4. Tell the developer how many are new and that they're on the Calendar tab.

## 2. The developer picks and comments

On the Calendar tab the developer sets `wip` on the posts they want, writes notes (what to change, what the video should show), and later `approved` with a date. Nothing reaches the files until a sync.

**Sync** whenever the developer says so, and before any commit that touches `docs/x-posts/`: read the collection with `ArtifactData` `list` (`collection: calendar`, `out_dir: <scratch>/calsync`), then `uv run docs/x-posts/calendar.py --apply <scratch>/calsync`. It takes `status`, `date` and `notes` of every doc marked `changed`; the text stays as it is in the file. Then `calendar.py --check`.

## 3. Rework each wip post

For every `wip` post, read its notes and rework the text with this skill: what the notes ask for, nothing else. Notes about the video go to step 4. Write the file, then re-export that post to the page (`set` with its `if_version`), so the card shows the new text.

## 4. Make its video

Every post video has the same shape, with the gas giant as the game's signature:

1. **The first frame is the thumbnail and has to be striking**: the gas giant over the scene, and the opening card at the lower left, over a dark gradient, clear of the gas giant and above X's player bar: RINGSHADOW in large white Exo 2 Black, the video's title in dark type on a cyan bar, and "A REAL-TIME STRATEGY GAME IN UNREAL ENGINE 5" under it. On screen from frame 0. (The developer picked this look on 2026-10-07; small cyan text on the sand was unreadable.)
2. **The gas giant comes down into the scene**: the same shot's pull-out played in reverse, faster (2×), so it lands where the scene happens.
3. **The scene**: what the post describes, with the game's own sound.
4. **Back to the gas giant**: the eject. The HUD fades out over half a second while the camera shoots out of the cockpit, keeps the unit in view, and turns up to the gas giant. Then the end card comes up in the same style: the name, "A real-time strategy game in Unreal Engine 5." on the cyan bar, "Drive any unit. Open source, public domain.", the GitHub link.

Music under all of it (a track from `unreal/Resources/Sounds/music/`), the game's sound over the scene, loudness at -16 LUFS.

How:

1. Write the sim in the video skill's `sims.json`: the moment plus `-AcPilotPullOut=S,L` at its end. Useful staging: `-AcPilotPath` with `aim+act:N` (the view stays on the nearest enemy's chest while firing), `-AcPilotFoeHp=N` (the staged enemy dies, so the unit lives to the pull-out), `-AcPilotDiveAt=1` (opens on the RTS view and dives in), `-AcPilotAt=`, `-AcHour=` (night shows the gas giant best).
2. Record it with the video skill's `record.py` (hidden, silent to the room, about 25 s per 10 s).
3. Look at it before anything else: the contact sheet, and a tile of 6 to 8 frames across the clip (ffmpeg `select` + `tile`), full-size crops where aim or text matter. Read the log's `pilot:` lines for when the unit left, died or pulled out. Fix the staging and record again until the frames show what the post says. The usual failures: the unit walks into the enemy, shots go into the ground, the unit dies before the pull-out, the pull-out never reaches the gas giant.
4. Cut it with the video skill's `scripts/giantcut.py`: `--down S,E` is the pull-out in the clip, from the frame the cockpit is gone (the HUD fade starts about 0.3 s before it) to where the camera settles; `--end` the scene's end; `--title` the opening card's title (about 25 characters at most); `--music`, `--music-at`; `--x` also writes the X file, a 2-pass x264 encode under 10 MB (`video/renders/<slug>-x.mp4`; the browser upload to X takes at most 10 MB). Find S and E in a tile of the pull-out at 4 to 8 frame steps. Check frame 0 and the end card at full size.
5. Keep the cut's command line in the post's `## Notes`, so it can be cut again.
6. Set the post's `video` to the X file and note in `## Notes` how it was made (sim, cut script, length, size).

## 5. Preview

Publish the X file with the page (`files: {"media/<slug>-x.mp4": "<abs path>"}`), re-export the post to `calendar`, and tell the developer the card has its video. When the developer comments on the video, go back to step 4.

## 6. Approve and schedule

The developer sets `approved` and a date on the page. Sync, then `calendar.py --check`: at most two posts a day (at least five hours apart, about 10 a.m. and after 4 p.m. US Eastern, docs/x-plan.md), no angle twice in a row, the video exists, the post under 700 characters. Report any clash and let the developer move a date.

## 7. Post

On the day, and only after the developer says to post it:

1. Show the exact text, the reply and the video file in the chat, and wait for a clear yes. Every post needs its own yes.
2. In Chrome (the developer's own X account), open `https://x.com/compose/post`. Upload the file to that dialog's file input (the inline composer of the home page swallows it), type the text, wait for the video to finish processing ("Ready"), then post. If processing stalls, discard the draft and start a fresh compose window.
3. Open the new post, reply with the reply text, check the thread shows both.
4. Set `status: posted`, `date` to the day, `posted` to the post's link, in the file and on the page. Never edit a posted file's text again.

## 8. After

At 24 hours, note the post's numbers in its `## Notes` when the developer shares them (bookmarks, replies with a real question, follows, stars from x.com; docs/x-plan.md "Metrics"). Formats that earn bookmarks get repeated.

## Tuning the skill (benchmark rounds)

When posts keep missing, run a round: `bench/run.py N` writes a post per brief in `bench/briefs.json` with `claude -p` and the skill, plus a few blind posts without it; `bench/seed.py N` and one `ArtifactData` batch put them on the page's Round N tab. The developer votes each up or down with a note and presses "Round done". Read the votes (`posts` where `round == N`, and `rounds/N`), ask Grok (`ask_grok`) how to fix what was voted down, change SKILL.md, and run the next round. Up-voted posts go to the calendar as `proposed`. Stop when the developer ticks "I'm satisfied".
