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
        '<button type="button" class="btn btn-small" data-act="run-job">' + t().t('pql.runAsJob') + '</button>' +
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

    form.querySelector('[data-act="run-job"]').addEventListener('click', () => {
      const q = input.value.trim();
      if (q) runAsJob(panel, q);
    });

    function run() {
      const q = input.value.trim();
      const limit = Number(form.querySelector('[name="limit"]').value) || 100;
      window.OcgdbNav.navigate('pql', q ? { pql: q } : {});
      if (q) runQuery(panel, q, limit);
    }

    if (initialQ) runQuery(panel, initialQ, 100);
  }

  // Submits the current query as a background job (Phase 2.5) instead of
  // the synchronous GET /api/query above -- for a scan too large to
  // finish inside one HTTP request/browser tab lifetime on a big,
  // unfiltered database. Requires the admin token (job submission is an
  // admin-only action); tracks it via the same streamed connection the
  // Admin tab's job modal uses (adminJobStream, api.js) rather than
  // reimplementing polling here, then paginates the matched GameIDs from
  // AdminStore's QueryResults table once the job finishes.
  async function runAsJob(panel, q) {
    const resultsEl = panel.querySelector('.pql-results');
    if (!window.OcgdbApi.hasAdminToken()) {
      resultsEl.innerHTML = '<div class="panel-error">' + t().t('pql.jobNeedsToken') + '</div>';
      return;
    }
    resultsEl.innerHTML = '<div class="panel-loading">' + t().t('common.loading') + '</div>';

    let info;
    try { info = await window.OcgdbApi.info(); } catch (e) { /* fall through to the not-loaded message below */ }
    if (!info || !info.dbPath) {
      resultsEl.innerHTML = '<div class="panel-error">' + t().t('common.dbNotLoaded') + '</div>';
      return;
    }

    let jobId;
    try {
      const res = await window.OcgdbApi.adminSubmitJob({ task: 'query', db: info.dbPath, pql: q });
      jobId = res.id;
    } catch (err) {
      resultsEl.innerHTML = '<div class="panel-error">' + esc(err.message) + '</div>';
      return;
    }

    resultsEl.innerHTML =
      '<div class="pql-job-status" data-role="job-status">' + t().t('pql.jobRunning', { id: jobId }) + '</div>';
    const statusEl = resultsEl.querySelector('[data-role="job-status"]');

    // This view has no unmount hook -- app.js's router only toggles
    // `panel.hidden` when switching tabs (the panel element itself, and
    // whatever it last rendered, stays in the DOM) -- so `panel.hidden` is
    // exactly how admin.js's own polling loop detects "the user left this
    // tab" (see startPolling() there), and `document.body.contains(
    // statusEl)` catches the other stale case: the user re-ran a query in
    // this same view, which replaced panel's children (a fresh mount()/
    // runAsJob() call) out from under this stream. The job itself keeps
    // running server-side regardless of whether anything is still
    // listening; it's also tracked in the Admin tab either way.
    const stream = window.OcgdbApi.adminJobStream(jobId, 0, (evt) => {
      if (evt.type !== 'status') return;
      if (panel.hidden || !document.body.contains(statusEl)) { stream.stop(); return; }
      if (!evt.final) {
        statusEl.textContent = t().t('pql.jobRunning', { id: jobId }) +
          (evt.gameCnt ? ' (' + Number(evt.gameCnt).toLocaleString() + ')' : '');
        return;
      }
      if (evt.state === 'succeeded') {
        renderJobResults(resultsEl, jobId, 0);
      } else {
        resultsEl.innerHTML = '<div class="panel-error">' +
          t().t('pql.jobFailed', { state: evt.state }) + (evt.error ? ': ' + esc(evt.error) : '') + '</div>';
      }
    });
  }

  async function renderJobResults(el, jobId, offset) {
    const limit = 200;
    let data;
    try {
      data = await window.OcgdbApi.adminJobResults(jobId, offset, limit);
    } catch (err) {
      el.innerHTML = '<div class="panel-error">' + esc(err.message) + '</div>';
      return;
    }
    let html = '<div class="pql-summary"><span><strong>' + data.total + '</strong> ' + t().t('pql.matched') + '</span></div>';
    if (!data.gameIds.length) {
      html += '<div class="panel-empty">' + t().t('pql.noResults') + '</div>';
    } else {
      html += '<div class="pql-job-ids">' +
        data.gameIds.map((id) => '<a href="#" class="pql-job-id-link" data-id="' + id + '">#' + id + '</a>').join(' ') +
        '</div>';
      if (offset + data.gameIds.length < data.total) {
        html += '<button type="button" class="btn btn-small" data-act="more">' + t().t('common.loadMore') + '</button>';
      }
    }
    el.innerHTML = html;
    el.querySelectorAll('.pql-job-id-link').forEach((a) => {
      a.addEventListener('click', (e) => {
        e.preventDefault();
        window.OcgdbNav.openGameViewer(Number(a.dataset.id));
      });
    });
    const moreBtn = el.querySelector('[data-act="more"]');
    if (moreBtn) moreBtn.addEventListener('click', () => renderJobResults(el, jobId, offset + limit));
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
