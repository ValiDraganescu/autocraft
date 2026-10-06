# Sharing Autocraft on X

The goal is to build in public: share the game's progress and its tooling
(Claude Code skills, the multi-agent workflow, the Orchestrator, Suno
prompts, renders, perf wins, bugs) in large amounts, and give the tools away.
There is no Steam page and nothing to sell.

Research by Grok on 2026-10-05, at high effort. The figures come from web and
X content that Grok cited, and they haven't been checked.

## The short version

- **One account**, aimed at Claude Code users. Autocraft is the proof. A
  split account (game / tools) means two empty audiences. Bio, one sentence:
  building a Mac RTS in Unreal by driving coding agents, and open-sourcing
  the skills. Bio link: the GitHub repo.
- **What travels:** a tool shown working that a stranger can install. Use a
  short screen recording where the result appears, plus one number.
  Bookmarks run at 1–3× likes on these posts. That ratio is the target.
  Pipeline posts without footage flop (a careful 7-step Unreal + Claude Code
  list got 62 likes).
- **The angle nobody else has:** a turnaround sheet goes in and a procedural
  Geometry Script mesh comes out, inside Unreal, on a Mac, in a real game.
  Scenario already spent "we open-sourced a game studio" (64 skills,
  image-to-mesh tools).
- **Cadence:** two original posts a day, at least 5 hours apart (about 10
  a.m. and after 4 p.m. US Eastern). Add a third only when it's a different
  kind of thing (a render, then a cost, then a bug). Replies are the
  high-volume channel: 20–40 a day, specific. One thread a week, on a topic
  whose short post already earned bookmarks. One X Article a month.
- **Why not more:** the ranker discounts each later post by the same author in
  one feed refresh (×1, ×0.625, ×0.44, …, floor ×0.25). A new post gets a
  cold-start test only while it's an original, under 200 impressions and
  under 2 hours old. Replies never get that test. Near-copy captions ("Day 14,
  new skill") trip the duplicate-text filters, so every post needs its own
  first sentence and its own artifact.
- **Giving it away:** one MIT repo, one folder per skill (`SKILL.md`, a
  sample input, the command you run). Use gists for Suno prompts. The post
  holds the proof and the raw `github.com/...` URL goes in the first reply.
  After two weeks, try the URL in the post itself for five posts and compare
  stars. Pin the post that produced the most stars.
- **Don't:**
  - "Comment X and I'll DM it". X has been writing rules against soliciting
    replies since July–August 2026, and a DM never becomes a star.
  - Follow-gates, "first 50 people", "bookmark this".
  - `#gamedev` and `#indiedev`. Those tags reach the audience most likely to
    mute you, and a mute weighs −58.8 against +0.5 for a like.
  - Arguing about AI art.
- **AI art:** label the frames "Grok turnaround, reference" and
  "Geometry Script, in-engine". If someone asks, the agent wrote the mesh
  code, the mesh is procedural and the picture was a reference. Then stop.

## Formats, best first

1. **The agent finishes:** 10–20 s, prompt → Unreal viewport → mesh. No
   title card.
2. **Side-by-side:** turnaround sheet next to the in-engine model, labelled, as
   a 6 s loop. This is the most ownable frame.
3. **One number with a receipt:** "building skill, 14 min, $1.80, 6,200
   triangles". The template is @tvykruta's "$7 to $1 per PR".
4. **The twenty lines:** a screenshot of the rule that fixed a real failure
   ("stop welding the treads to the hull"), not the whole file.
5. **The Orchestrator:** three terminal panes, one sentence each on what
   that agent owns.
6. **The track:** a Suno prompt on screen, with 15 s of the cue under a
   unit. At most once a week.
7. **The bug:** the problem in one frame, the one-line fix in the caption.
8. **The perf win:** frame time before and after, both numbers on screen.

Skip "day N" templates, long silent agent sessions, and threads that only
restate the caption. Cut recordings to 15–45 s.

## Accounts to study and reply under

- @shiri_shh: a 15 s skill video that got 7.3k bookmarks.
- @emmanuel_2m and @Scenario_gg: GameDev OS. Study the shape, don't copy the
  pitch.
- @tvykruta: before/after numbers, with the AGENTS.md at the end of the thread.
- @JorgeCastilloPr and @tom_doerr: one sentence, the output, the URL.
- @makevoid: a music video made by a Claude Code skill with Suno.
- @genex_games: an open-source AI game-making app.
- @bcherny and @claudeai: when Boris asks "what's missing", reply with one
  concrete gap the Orchestrator hits. Not a trailer.

## Daily routine

- Record clips while you work. Post the best one around 10 a.m. US Eastern,
  then stay 45 minutes and answer the replies.
- Reply under 2–3 accounts from the list, and under whoever posted a skill
  that morning.
- Post a second original after 4 p.m. ET, but only if the afternoon produced
  a different kind of thing.
- Push the repo the same day you post about it.
- Use Premium so you can post longer videos and leave the unverified
  low-reach bucket.

## 30-day starter (6 Oct – 2 Nov 2026)

The repo goes up on day 1, even if it holds only one skill and a README.

- **Week 1: the model pipeline skill.**
  - Monday: a side-by-side of one unit, with the URL in the reply.
  - Tuesday: the twenty lines that encode a constraint.
  - Wednesday: the cost, time and triangle numbers.
  - Thursday: the agent's bad mesh and the rule you added.
  - Friday: the Orchestrator taking one unit from sheet to viewport.
  - Weekend: replies only.
- **Week 2: the building visuals skill.**
  - Same formats, a different building each day.
  - Wednesday: a Suno post.
  - Friday: quote Monday's mesh post, if the building makes it look like
    chapter one.
- **Week 3: the testing skill and perf.**
  - Monday: a test the agent wrote, failing on screen.
  - Wednesday: frame time before and after.
  - Friday: the bug you would have shipped.
- **Week 4: an X Article, "Turnaround sheet to Geometry Script",** linking
  the three skills.
  - Monday: a 30 s cut of the whole path.
  - Thursday: the reply under Boris.
  - Friday: review the sheet.

## Metrics

Track four numbers per post at 24 hours: bookmarks, replies with a
technical question, follows, and GitHub stars or clones that came from
`x.com`. Likes are vanity.

- About 30 bookmarks on a week-1 post: repeat that format.
- 200 likes and 4 bookmarks: drop that format.
- Bookmarks but no stars: the URL is hard to find or the repo doesn't run.
  Fix that before writing another skill.

By 2 Nov, a reasonable result is a few hundred followers, a handful of posts
past 100 bookmarks, and stars you can trace to specific posts.

## Sources

- Skill posts: https://x.com/shiri_shh/status/2103521939134550246,
  https://x.com/emmanuel_2m/status/2103097017073361137,
  https://x.com/Scenario_gg/status/2103460573895659982,
  https://x.com/tvykruta/status/2106435001340621147,
  https://x.com/JorgeCastilloPr/status/2091486889937154120,
  https://x.com/makevoid/status/2104216237794550018,
  https://x.com/tom_doerr/status/2085267066756493601,
  https://x.com/genex_games/status/2107148754633507237
- The pipeline post that flopped: https://x.com/rewind02/status/2106014352021086596
- Boris's "what's missing" post: https://x.com/bcherny/status/2106566247869763743
- Ranking weights and cold start: https://github.com/xai-org/x-algorithm/blob/main/home-mixer/params/param.rs
- Author diversity: https://x.com/pirwot/status/2105621308889256243, https://ombrise.com/work/x-algorithm,
  https://github.com/xai-org/x-algorithm/pull/212
- Replies in For You: https://autotraction.ai/blog/why-replies-beat-posts-on-x/
- Engagement-bait rules: https://www.like.tg/library/x-updates-creator-program-curb-engagement-bait,
  https://www.complex.com/pop-culture/a/markelibert/x-original-content-rewards-program-creator-payouts
- Links and reach: https://playersells.com/insights/links-and-reach
- X Articles: https://xpert.so/blog/x-articles-the-long-form-feature-almost-nobody-uses-57938
