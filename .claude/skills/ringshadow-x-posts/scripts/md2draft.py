# /// script
# requires-python = ">=3.11"
# ///
"""An article's markdown -> the X Article editor's content (Draft.js raw: blocks + entity_map).

Body: everything after the H1 and the `>` composer note under it, up to the first `<!--`.
Blocks: `## ` header-two, `> ` blockquote, `- ` unordered-list-item, `1. ` ordered-list-item,
paragraphs (lines joined until a blank line) unstyled. Fenced code and `|` tables become atomic
blocks with a MARKDOWN entity. `![..](dir/NAME.jpg)` becomes an `[IMAGE: NAME]` paragraph, the
marker where the image is uploaded by hand. `**bold**` becomes a Bold range (UTF-16 offsets);
inline backticks are dropped (the editor has no inline code).

Usage: uv run md2draft.py ARTICLE.md OUT.json
"""
import json
import random
import re
import string
import sys

src = open(sys.argv[1]).read().split('<!--')[0]
lines = src.split('\n')
i = 0
while i < len(lines) and not lines[i].startswith('# '):
    i += 1
i += 1
while i < len(lines) and (not lines[i].strip() or lines[i].startswith('>')):
    i += 1
lines = lines[i:]

blocks, entities = [], []


def key():
    return ''.join(random.choice(string.ascii_lowercase + string.digits) for _ in range(5))


def u16(s):
    return len(s.encode('utf-16-le')) // 2


def inline(text):
    text = re.sub(r'`([^`]*)`', r'\1', text)
    out, ranges = '', []
    for part in re.split(r'(\*\*.+?\*\*)', text):
        if part.startswith('**') and part.endswith('**') and len(part) > 4:
            inner = part[2:-2]
            ranges.append({'offset': u16(out), 'length': u16(inner), 'style': 'Bold'})
            out += inner
        else:
            out += part
    return out, ranges


def add(kind, text):
    t, r = inline(text)
    blocks.append({'data': {}, 'text': t, 'key': key(), 'type': kind,
                   'entity_ranges': [], 'inline_style_ranges': r})


def atomic(markdown):
    entities.append({'key': str(len(entities)),
                     'value': {'data': {'markdown': markdown}, 'type': 'MARKDOWN', 'mutability': 'Mutable'}})
    blocks.append({'data': {}, 'text': ' ', 'key': key(), 'type': 'atomic',
                   'entity_ranges': [{'key': len(entities) - 1, 'offset': 0, 'length': 1}],
                   'inline_style_ranges': []})


para = []


def flush():
    global para
    if para:
        add('unstyled', ' '.join(para))
        para = []


i = 0
while i < len(lines):
    raw = lines[i]
    s = raw.strip()
    m = re.match(r'^(\s*)```(\w*)\s*$', raw)
    if m:
        flush()
        ind, lang = m.group(1), m.group(2)
        code = []
        i += 1
        while not re.match(r'^\s*```\s*$', lines[i]):
            l = lines[i]
            code.append(l[len(ind):] if l.startswith(ind) else l)
            i += 1
        atomic('```' + lang + '\n' + '\n'.join(code) + '\n```')
        i += 1
        continue
    if s.startswith('|'):
        flush()
        tab = []
        while i < len(lines) and lines[i].strip().startswith('|'):
            tab.append(lines[i].strip())
            i += 1
        atomic('\n'.join(tab))
        continue
    if not s:
        flush()
    elif s.startswith('## '):
        flush(); add('header-two', s[3:])
    elif s.startswith('> '):
        flush(); add('blockquote', s[2:])
    elif s.startswith('!['):
        flush(); add('unstyled', '[IMAGE: ' + re.search(r'([\w-]+)\.(?:jpe?g|png|gif|webp)\)', s).group(1) + ']')
    elif s.startswith('- ') and not raw.startswith(' '):
        flush(); add('unordered-list-item', s[2:])
    elif re.match(r'^\d+\. ', s) and not raw.startswith(' '):
        flush(); add('ordered-list-item', re.sub(r'^\d+\. ', '', s))
    else:
        para.append(s)
    i += 1
flush()

json.dump({'blocks': blocks, 'entity_map': entities}, open(sys.argv[2], 'w'), ensure_ascii=False)
kinds = {}
for b in blocks:
    kinds[b['type']] = kinds.get(b['type'], 0) + 1
images = sum(b['text'].startswith('[IMAGE: ') for b in blocks)
print(len(blocks), 'blocks', kinds, len(entities), 'MARKDOWN entities', images, 'image markers')
