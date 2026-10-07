import { check, enter, escapeHtml, head, listWithin, optionalTextWithin, rich, textWithin } from './shared.mjs';

const TONES = ['ok', 'warn', 'fail', 'info'];

export default {
  type: 'rules',
  validate(visual) {
    const errors = [];
    optionalTextWithin(errors, visual.kicker, 'kicker', 40);
    optionalTextWithin(errors, visual.title, 'title', 70);
    listWithin(errors, visual.rows, 'rows', 1, 6).forEach((row, index) => {
      textWithin(errors, row?.when, `rows[${index}].when`, 60);
      textWithin(errors, row?.result, `rows[${index}].result`, 26);
      check(errors, TONES.includes(row?.tone), `rows[${index}].tone must be one of ${TONES.join(', ')}`);
    });
    return errors;
  },
  html(visual) {
    const rows = visual.rows
      .map(
        (row, index) =>
          `<div class="row" ${enter(2 + index, 'left')}><span>${rich(row.when)}</span><span class="pill ${row.tone}">${escapeHtml(row.result)}</span></div>`,
      )
      .join('\n    ');
    return `<div class="panel">
  ${head(visual, { headingStep: 1 })}
  <div class="rules">
    ${rows}
  </div>
</div>`;
  },
};
