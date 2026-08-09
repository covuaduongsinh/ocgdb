// OCGDB web UI -- PQL query console: input + examples + results table.
'use strict';

(function () {
  const t = () => window.I18N;

  const EXAMPLES = [
    { q: 'Q=3', label: '3 white queens' },
    { q: 'r[e4, e5, d4, d5] = 2', label: '2 rooks in the centre' },
    { q: 'P[d4, e5, f4, g4] = 4 and kb7', label: '4 white pawns + black king b7' },
    { q: 'B[c-f] + b[c-f] == 2', label: '2 bishops between files c-f' },
    { q: 'white6 = 5', label: '5 white pieces on rank 6' },
    { q: 'fen[rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1]', label: '1.e4 reached' },
  ];

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  function mount(panel, params) {
    const initialQ = (params && params.pql) || '';
    panel.innerHTML =
      '<h1>' + t().t('pql.title') + '</h1>' +
      '<p class="muted">' + t().t('pql.desc') + '</p>' +
      '<form class="pql-form">' +
        '<input type="text" name="pql" class="pql-input" value="' + esc(initialQ) + '" ' +
          'data-i18n-ph="pql.placeholder" placeholder="' + t().t('pql.placeholder') + '" spellcheck="false">' +
        '<label class="field pql-limit-field">' + t().t('pql.limit') +
          '<input type="number" name="limit" value="100" min="1" max="2000"></label>' +
        '<button type="submit" class="btn btn-primary">' + t().t('pql.run') + '</button>' +
      '</form>' +
      '<div class="pql-examples">' +
        '<span class="muted">' + t().t('pql.examples') + ':</span> ' +
        EXAMPLES.map((ex) => '<button type="button" class="chip" data-q="' + esc(ex.q) + '">' + esc(ex.label) + '</button>').join(' ') +
      '</div>' +
      '<div class="pql-results"></div>';

    const form = panel.querySelector('.pql-form');
    const input = form.querySelector('[name="pql"]');

    panel.querySelectorAll('.chip').forEach((chip) => {
      chip.addEventListener('click', () => {
        input.value = chip.dataset.q;
        run();
      });
    });

    form.addEventListener('submit', (e) => {
      e.preventDefault();
      run();
    });

    function run() {
      const q = input.value.trim();
      const limit = Number(form.querySelector('[name="limit"]').value) || 100;
      window.OcgdbNav.navigate('pql', q ? { pql: q } : {});
      if (q) runQuery(panel, q, limit);
    }

    if (initialQ) runQuery(panel, initialQ, 100);
  }

  async function runQuery(panel, q, limit) {
    const resultsEl = panel.querySelector('.pql-results');
    resultsEl.innerHTML = '<div class="panel-loading">' + t().t('common.loading') + '</div>';
    try {
      const data = await window.OcgdbApi.query(q, limit);
      if (!data.ok) {
        resultsEl.innerHTML = '<div class="panel-error">' + t().t('pql.parseError') + ': ' + esc(data.error || '') + '</div>';
        return;
      }
      renderResults(resultsEl, data);
    } catch (e) {
      resultsEl.innerHTML = '<div class="panel-error">' + t().t('common.dbNotLoaded') + '</div>';
    }
  }

  function renderResults(el, data) {
    let html = '<div class="pql-summary">' +
      '<span><strong>' + data.scanned + '</strong> ' + t().t('pql.scanned') + '</span>' +
      '<span><strong>' + data.matched + '</strong> ' + t().t('pql.matched') + '</span>' +
      '<span>' + t().t('pql.elapsed') + ': <strong>' + data.elapsedMs + 'ms</strong></span>' +
      '</div>';
    if (data.truncated) {
      html += '<div class="pql-truncated muted">' + t().t('pql.truncated') + '</div>';
    }
    if (!data.games.length) {
      html += '<div class="panel-empty">' + t().t('pql.noResults') + '</div>';
    } else {
      html += '<div class="table-scroll"><table class="games-table"><thead><tr>' +
        '<th>' + t().t('browse.colId') + '</th><th>' + t().t('browse.colWhite') + '</th>' +
        '<th>' + t().t('browse.colBlack') + '</th><th>' + t().t('browse.colResult') + '</th>' +
        '<th>' + t().t('browse.colEvent') + '</th><th>' + t().t('browse.colDate') + '</th></tr></thead><tbody>';
      data.games.forEach((g) => {
        html += '<tr class="game-row" data-id="' + g.id + '">' +
          '<td>' + g.id + '</td><td>' + esc(g.white) + '</td><td>' + esc(g.black) + '</td>' +
          '<td>' + esc(g.result || '') + '</td><td>' + esc(g.event) + '</td><td>' + esc(g.date || '') + '</td></tr>';
      });
      html += '</tbody></table></div>';
    }
    el.innerHTML = html;
    el.querySelectorAll('.game-row').forEach((tr) => {
      tr.addEventListener('click', () => window.OcgdbNav.openGameViewer(Number(tr.dataset.id)));
    });
  }

  window.OcgdbPql = { mount };
})();
