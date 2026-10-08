// Snippets for the X Article composer, run with the browser's javascript tool, one at a time.
// They drive the editor's own state (Draft.js) and let X's autosave store it: no request to X
// is made from here. Pass the text of one snippet, from its `// ==` line to the next.

// == find: locate the editor. Run first, and again after any page reload.
(() => {
  const ed = document.querySelector('[data-contents="true"]').closest('[contenteditable="true"]');
  let f = ed[Object.keys(ed).find(k => k.startsWith('__reactFiber'))];
  while (f && !(f.memoizedProps && f.memoizedProps.editorState && f.memoizedProps.onChange)) f = f.return;
  window.__ed = f;
  return f ? 'editor found, ' + f.memoizedProps.editorState.getCurrentContent().getBlockMap().size + ' blocks' : 'no editor';
})()

// == paste-target: a hidden textarea to paste the JSON into. Then focus is on it: press cmd+v
// (after `pbcopy < cs.json` on the Mac), then run `take`.
(() => {
  const t = document.createElement('textarea');
  t.id = '__in';
  t.style.cssText = 'position:fixed;top:0;left:0;width:10px;height:10px;opacity:0';
  document.body.appendChild(t);
  t.focus();
  return 'focused';
})()

// == take: parse the pasted JSON into window.__raw and remove the textarea.
(() => {
  const t = document.getElementById('__in');
  window.__raw = JSON.parse(t.value);
  t.remove();
  return __raw.blocks.length + ' blocks, ' + __raw.entity_map.length + ' entities';
})()

// == load: replace the whole body with __raw. The editor needs one non-empty text block to copy
// its classes from: type one word in the body first if it's empty.
(() => {
  const p = __ed.memoizedProps, es = p.editorState, ES = es.constructor;
  let c = es.getCurrentContent();
  const sample = c.getBlockMap().find(b => b.getCharacterList().size > 0 && b.getType() !== 'atomic');
  const CB = sample.constructor, List = sample.getCharacterList().constructor;
  const CM = sample.getCharacterList().first().constructor, OSet = sample.getCharacterList().first().getStyle().constructor;
  const IMap = sample.getData().constructor;
  const blocks = [];
  for (const rb of __raw.blocks) {
    const styles = Array.from({ length: rb.text.length }, () => []);
    for (const r of rb.inline_style_ranges)
      for (let i = r.offset; i < r.offset + r.length && i < rb.text.length; i++) styles[i].push('BOLD');
    let ent = null;
    if (rb.type === 'atomic') {
      const v = __raw.entity_map[rb.entity_ranges[0].key].value;
      c = c.createEntity('MARKDOWN', 'MUTABLE', { markdown: v.data.markdown });
      ent = c.getLastCreatedEntityKey();
    }
    const chars = List(styles.map(s => CM.create({ style: OSet(s), entity: ent })));
    blocks.push(new CB({ key: rb.key, type: rb.type, text: rb.text, characterList: chars, depth: 0, data: IMap() }));
  }
  const bm = c.getBlockMap().constructor(blocks.map(b => [b.getKey(), b]));
  const k = blocks[0].getKey();
  const sel = es.getSelection().merge({ anchorKey: k, focusKey: k, anchorOffset: 0, focusOffset: 0, isBackward: false });
  const nc = c.merge({ blockMap: bm, selectionBefore: es.getSelection(), selectionAfter: sel });
  p.onChange(ES.push(es, nc, 'insert-fragment'));
  return 'loaded ' + blocks.length + ' blocks';
})()

// == caret: put the caret at the end of an image marker, before Insert -> Media.
// Set window.__marker = 'pipeline' first.
(() => {
  const ed = document.querySelector('[data-contents="true"]');
  const el = [...ed.querySelectorAll('[data-block="true"]')].find(n => n.textContent === '[IMAGE: ' + __marker + ']');
  el.scrollIntoView({ block: 'center' });
  const span = el.querySelector('[data-text="true"]');
  const r = document.createRange();
  r.setStart(span.firstChild, span.firstChild.length);
  r.collapse(true);
  ed.closest('[contenteditable="true"]').focus();
  const s = getSelection(); s.removeAllRanges(); s.addRange(r);
  return 'caret after ' + el.textContent;
})()

// == unmark: delete an image marker block through the state (after its image is in).
// Set window.__marker = 'pipeline' first. Never delete a marker with Backspace: after an upload
// the editor's selection is stale and the key hits the block next to it.
(() => {
  const p = __ed.memoizedProps, es = p.editorState, ES = es.constructor;
  const c = es.getCurrentContent();
  const m = c.getBlocksAsArray().find(b => b.getText() === '[IMAGE: ' + __marker + ']');
  if (!m) return 'no marker ' + __marker;
  const prev = c.getKeyBefore(m.getKey());
  const sel = es.getSelection().merge({ anchorKey: prev, focusKey: prev, anchorOffset: 0, focusOffset: 0, isBackward: false, hasFocus: false });
  const nc = c.merge({ blockMap: c.getBlockMap().delete(m.getKey()), selectionBefore: es.getSelection(), selectionAfter: sel });
  p.onChange(ES.push(es, nc, 'remove-range'));
  return 'removed ' + __marker;
})()

// == check: compare the editor with __raw. Wants: no markers, no atomic without an entity,
// one MEDIA per image, MARKDOWN = __raw's entities, every text block equal to the source.
(() => {
  const c = __ed.memoizedProps.editorState.getCurrentContent();
  const bl = c.getBlocksAsArray(), types = {}, ents = {};
  let bare = 0;
  for (const b of bl) {
    types[b.getType()] = (types[b.getType()] || 0) + 1;
    if (b.getType() === 'atomic') {
      const k = b.getEntityAt(0);
      if (!k) bare++; else { const t = c.getEntity(k).getType(); ents[t] = (ents[t] || 0) + 1; }
    }
  }
  const mark = t => /^\[IMAGE: /.test(t);
  const cur = bl.filter(b => b.getType() !== 'atomic').map(b => b.getText()).filter(t => !mark(t));
  const src = __raw.blocks.filter(b => b.type !== 'atomic').map(b => b.text).filter(t => !mark(t));
  const diffs = [];
  for (let i = 0; i < Math.max(cur.length, src.length) && diffs.length < 3; i++)
    if (cur[i] !== src[i]) diffs.push({ i, src: (src[i] || '').slice(0, 60), cur: (cur[i] || '').slice(0, 60) });
  return JSON.stringify({ blocks: bl.length, types, ents, bare, markers: bl.filter(b => mark(b.getText())).length, diffs });
})()
