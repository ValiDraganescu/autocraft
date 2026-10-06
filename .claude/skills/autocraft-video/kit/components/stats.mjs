import { FORMAT } from '../layout.mjs';
import { check, enter, escapeHtml, head, listWithin, optionalTextWithin, rich, textWithin } from './shared.mjs';

export default {
  type: 'stats',
  validate(visual) {
    const errors = [];
    optionalTextWithin(errors, visual.kicker, 'kicker', 40);
    textWithin(errors, visual.title, 'title', 70);
    listWithin(errors, visual.items, 'items', 1, 4).forEach((item, index) => {
      check(errors, /^\S{1,7}$/.test(String(item?.value ?? '')), `items[${index}].value must be 1 to 7 characters with no space`);
      textWithin(errors, item?.label, `items[${index}].label`, 70);
    });
    return errors;
  },
  html(visual) {
    const items = visual.items
      .map((item, index) => {
        const value = String(item.value);
        const count = /^\d+$/.test(value) && Number(value) > 9 ? ' data-count' : '';
        return `<div class="stat" ${enter(2 + index)}><div class="value"${count}>${escapeHtml(value)}</div><div class="label">${rich(item.label)}</div></div>`;
      })
      .join('\n    ');
    // Vertical: two across for two or four numbers, else one under the other.
    const columns = FORMAT.vertical ? (visual.items.length % 2 === 0 ? 2 : 1) : visual.items.length;
    return `<div class="panel">
  ${head(visual, { headingStep: 1 })}
  <div class="stats" style="grid-template-columns: repeat(${columns}, 1fr);">
    ${items}
  </div>
</div>`;
  },
};
