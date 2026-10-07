import { CANVAS, FORMAT } from '../layout.mjs';
import { check, enter, escapeHtml, head, listWithin, optionalTextWithin, plain, textWithin } from './shared.mjs';

const NODE_CHAR = 14; // Barlow Condensed SemiBold at 30px
const NODE_PADDING = 64;
const ARROW = 44 + 44;
const ROW_WIDTH = 1440;
// Vertical formats: each row is a column, top to bottom, the columns side by side.
const COLUMN_GAP = 40;
const VERTICAL_CHAR = 16; // 34px
const COLUMN_NODES = 4;

const node = (entry) => (typeof entry === 'string' ? { label: entry, focus: false } : { label: entry?.label, focus: Boolean(entry?.focus) });

export default {
  type: 'flow',
  validate(visual) {
    const errors = [];
    optionalTextWithin(errors, visual.kicker, 'kicker', 40);
    textWithin(errors, visual.title, 'title', 70);
    listWithin(errors, visual.rows, 'rows', 1, 3).forEach((row, rowIndex) => {
      const nodes = listWithin(errors, row, `rows[${rowIndex}]`, 1, 5).map(node);
      nodes.forEach((entry, index) => textWithin(errors, entry.label, `rows[${rowIndex}][${index}]`, 28));
      if (FORMAT.vertical) {
        // A label may wrap to two lines in its column.
        const column = Math.floor((CANVAS.width - 2 * FORMAT.pad - (visual.rows.length - 1) * COLUMN_GAP) / visual.rows.length);
        const longest = Math.max(...nodes.map((entry) => plain(entry.label ?? '').length));
        check(errors, nodes.length <= COLUMN_NODES, `rows[${rowIndex}] has ${nodes.length} nodes; a vertical video fits ${COLUMN_NODES}`);
        check(errors, longest * VERTICAL_CHAR <= 2 * (column - NODE_PADDING), `rows[${rowIndex}]: a label is too long for a ${column}px column; shorten it or use fewer rows`);
        return;
      }
      const width = nodes.reduce((sum, entry) => sum + plain(entry.label ?? '').length * NODE_CHAR + NODE_PADDING, 0) + (nodes.length - 1) * ARROW;
      check(errors, width <= ROW_WIDTH, `rows[${rowIndex}] is about ${width}px wide; the most is ${ROW_WIDTH}px. Shorten the labels or split the row`);
    });
    return errors;
  },
  html(visual) {
    let step = 2;
    const rows = visual.rows
      .map((row) => {
        const parts = row.map(node).map((entry, index) => {
          const box = `<div class="node${entry.focus ? ' focus' : ''}" ${enter(step, entry.focus ? 'pop' : 'left')}>${escapeHtml(entry.label)}</div>`;
          const arrow = index > 0 ? `<div class="arrow" ${enter(step, 'left')}></div>` : '';
          step += 1;
          return arrow + box;
        });
        return `<div class="row">${parts.join('')}</div>`;
      })
      .join('\n    ');
    return `<div class="panel">
  ${head(visual, { headingStep: 1 })}
  <div class="flow">
    ${rows}
  </div>
</div>`;
  },
};
