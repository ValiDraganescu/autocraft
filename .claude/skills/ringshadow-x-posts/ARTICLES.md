# X Articles

An X Article is a long-form page on X: a title, a 5:2 header image, headings, lists, code blocks, tables and images. A post is the share that links to it. The first one was `docs/articles/ringshadow-release-builds.md` (2026-10-08); read it for the shape.

The article goes into X's composer through the editor's own state, and X's autosave stores it. That way the whole body, with its headings, bold text, code blocks and tables, goes in with one snippet, and the only clicks are the three image uploads. The developer wants X's own UI to save the draft, so every write goes through the page's editor. Requests to X's API stay with the page (each one carries a signed `x-client-transaction-id`).

| Path | What |
|---|---|
| `docs/articles/<slug>.md` | The article, the source of truth |
| `docs/articles/<slug>/` | Its images: `thumb.jpg` (2000×800) and the inline ones |
| `scripts/md2draft.py` | Markdown to the editor's content: `{blocks, entity_map}` |
| `scripts/x-article.js` | The browser snippets: `find`, `paste-target`, `take`, `load`, `caret`, `unmark`, `check` |
| `scripts/article-art/` | The image kit: `shot.sh` (page to PNG) and `base.css` (HUD palette and fonts) |
| `scripts/article-art/examples/` | The first article's four pages, one per layout; copy one and replace its content |

## 1. Write the markdown

Same voice and banned list as a post (SKILL.md). Then, for an article:

- **The facts are measured.** Every number comes from a log, a bill or a command's output, and the same number reads the same everywhere it appears. Costs are given per month at real usage (for example 2 to 3 releases a week), with the per-run cost beside them. List the sources in an HTML comment at the end: `md2draft.py` stops at the first `<!--`.
- **Reproducible.** Every step has the command that does it and a "Done when …" line. Code blocks hold real commands and real file contents. A reader who wants to do the same can follow along.
- **The file:** the H1 title (specific, with the payoff: "One Mac, One Rented Windows Box: …"), a `>` note for the composer (header image, inline images), then the body. A hook line, then 3 to 4 bold numbers, then the sections. Subheadings every 3 to 5 paragraphs, bold the key line of each section, short paragraphs. End with what's still open and a question to reply to.
- **The markdown the converter reads:** `## ` headings, `> ` quotes, `- ` and `1. ` lists, `**bold**`, fenced code (with a language), `|` tables, and `![alt](<slug>/NAME.jpg)` on its own line. Inline backticks are dropped. The editor has no inline code, so write `build_release.sh` and similar names so they read as plain text too.

Done when every number traces to a source in the closing comment, and a search finds no em dash, en dash or banned word.

## 2. Make the images

Render HTML pages to PNG with headless Chrome, in the HUD look (ink #06121e, cyan #59d9ff, ice #ebf7ff, Barlow Condensed):

```bash
cp .claude/skills/ringshadow-x-posts/scripts/article-art/examples/bill.html <scratch>/art/chart.html
.claude/skills/ringshadow-x-posts/scripts/article-art/shot.sh <scratch>/art/chart.html 1600 770
sips -s format jpeg -s formatOptions 85 <scratch>/art/chart.png --out docs/articles/<slug>/chart.jpg
```

- **The thumbnail** is the scroll stopper: 2000×800 (5:2), a game frame on the right (`bg.jpg`, from a clip with ffmpeg `-ss T -frames:v 1`), the headline on the left over a fade to ink.
- **Inline images** at 1600 wide: a diagram of the setup, a table of what happened, a chart of the bill. Charts are drawn to scale from the article's own numbers. Write the numbers in the HTML as data with a comment naming their source.
- Start each image from the example with the closest layout, and replace all of its text and numbers:
  - `thumb.html`: the 2000×800 header, a game frame with the headline over a fade.
  - `pipeline.html`: a flow diagram of boxes and arrows.
  - `rounds.html`: a table of attempts, each row coloured by its result.
  - `bill.html`: a stacked bar chart drawn to scale from data in the page.
- Read every PNG before using it: text fits, numbers match the article.

## 3. Load it into the composer

Publishing stays the developer's. Claude writes and checks the draft, and the developer presses Publish. Load the Chrome tools in one `ToolSearch`: `tabs_context_mcp`, `tabs_create_mcp`, `navigate`, `computer`, `find`, `file_upload`, `javascript_tool`.

1. `uv run .claude/skills/ringshadow-x-posts/scripts/md2draft.py docs/articles/<slug>.md <scratch>/cs.json`. It prints the block counts: the MARKDOWN entities are code blocks plus tables, and there's one image marker per inline image.
2. Open a new tab at x.com → Articles → Write (or the draft's `https://x.com/compose/articles/edit/<id>`). Click "Add a title" and `type` the title. Click the body and type one word, so the editor has a text block to copy from.
3. Run the snippets of `scripts/x-article.js` with `javascript_tool`, in this order:
   - `find`;
   - `pbcopy < <scratch>/cs.json` in Bash;
   - `paste-target`, then `cmd+v` with `computer`;
   - `take`, which prints the block and entity counts;
   - `load`.

   Within seconds the page shows "Last saved just now".
4. **The header image:** `find` "file input before the 5:2 aspect ratio text", `file_upload` `thumb.jpg` to it, then Apply in the "Edit media" dialog.
5. **Each inline image:**
   - set `window.__marker = 'NAME'` and run `caret`;
   - then Insert (toolbar) → Media;
   - `find` "file input inside the Insert media dialog (not the header image)", and `file_upload` `NAME.jpg`. The image lands after the marker's block. If it lands somewhere else, click the end of the marker line and insert again.
   - once it's in, run `unmark` for that marker. Remove markers through the state only: after an upload, Backspace hits the table or code block beside the marker and empties it.
6. Run `check`. Expect:
   - `markers: 0` and `bare: 0` (every atomic block has its entity);
   - `ents.MEDIA` equal to the inline images;
   - `ents.MARKDOWN` equal to the converter's count;
   - `diffs: []`.
7. Open Preview and scroll through it: headings, bold text, code blocks, tables and images render.

Done when `check` is clean and Preview shows the whole article. Tell the developer the draft's URL and that it's theirs to publish.

**After an edit to the markdown:** run step 1 again, then `find`, `take` and `load`. The header image stays. `load` replaces the body, so the inline images go in again (step 5).

### How the editor stores an article

This is useful when something breaks. The editor is Draft.js, and the draft is its raw state. The block types are `unstyled`, `header-two`, `unordered-list-item`, `ordered-list-item`, `blockquote` and `atomic`. Bold is the inline style `BOLD`, and X's saved JSON spells it `Bold`. Code blocks, tables and LaTeX are all `atomic` blocks with one entity type, `MARKDOWN`, whose `data.markdown` is a fenced block or a markdown table. Images are `MEDIA` entities that point at an upload, which is why they go in through the UI. The editor component is the React fiber 3 levels above the `contenteditable` whose props have `editorState` and `onChange`. `find` walks up to it.

### Traps

| Symptom | Fix |
|---|---|
| An HTML paste turned tables and code into plain paragraphs | Use `load`. The composer's paste keeps headings, lists and bold, and flattens the rest. |
| Insert → Code didn't open, and the code landed in the body | Use `load`. Code blocks come from the converter. |
| The image went into the header slot | Two file inputs exist. Name the one you want in `find`: the header's is before the 5:2 text, the inline one is inside the Insert media dialog. |
| A table or code block went blank | It was hit by a key after an upload. Run step 1, then `find`, `take` and `load`, and insert the images again. |
| `find` says "no editor" | The page is still loading. Wait 2 s and run it again. |
| `load` fails on `sample` | The body has no text block. Type one word in it first. |

## 4. The share post

The post that links the article follows SKILL.md, with three changes:

- It's at most 256 characters.
- It's plain text, so the developer can copy it.
- It has the article's one-line promise and its headline number ("about $15 a month").

Like every post, it needs the developer's yes to the exact text before it goes out (WORKFLOW.md).
