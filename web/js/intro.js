// OCGDB web UI -- visual introduction / landing view. Every number here
// comes from GET /api/info on the currently open database; nothing is
// hardcoded (see intro.serverDesc in i18n.js for the same claim made to
// the user).
'use strict';

(function () {
  const t = () => window.I18N;

  const FIXED_COLUMNS = {
    Info: ['Name', 'Value'],
    Events: ['ID', 'Name'],
    Sites: ['ID', 'Name'],
    Players: ['ID', 'Name', 'Elo'],
    Comments: ['ID', 'GameID', 'Ply', 'Comment'],
  };

  const PQL_EXAMPLES = [
    { q: 'Q=3', squares: [], text: { vi: 'Tổng số Hậu trắng trên bàn phải đúng 3.', en: 'Exactly three white queens on the board.' } },
    { q: 'P[d4, e5, f4, g4] = 4 and kb7', squares: ['d4', 'e5', 'f4', 'g4', 'b7'], text: {
      vi: '4 Tốt trắng ở đúng 4 ô này, VÀ Vua đen đứng ở b7.',
      en: 'Four white pawns on exactly these squares, AND the black king on b7.' } },
    { q: 'B[c-f] + b[c-f] == 2', squares: ['c1', 'd1', 'e1', 'f1', 'c8', 'd8', 'e8', 'f8'], text: {
      vi: 'Đúng 2 Tượng (bất kỳ màu) đứng ở các cột từ c đến f.',
      en: 'Exactly two bishops (either colour) somewhere on files c through f.' } },
  ];

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  function schemaBox(x, y, w, h, key, rows) {
    return '<g class="schema-box" data-table="' + key + '" transform="translate(' + x + ',' + y + ')">' +
      '<rect width="' + w + '" height="' + h + '" rx="8"/>' +
      '<text x="' + (w / 2) + '" y="' + (h / 2 - 4) + '" text-anchor="middle" class="schema-box-title">' + key + '</text>' +
      '<text x="' + (w / 2) + '" y="' + (h / 2 + 14) + '" text-anchor="middle" class="schema-box-count">' +
        (rows == null ? '' : Number(rows).toLocaleString()) +
      '</text>' +
      '</g>';
  }

  function link(x1, y1, x2, y2) {
    return '<line x1="' + x1 + '" y1="' + y1 + '" x2="' + x2 + '" y2="' + y2 + '" class="schema-link"/>';
  }

  function renderSchema(info) {
    const counts = (info && info.info) || {};
    const gamesCols = (info && info.gamesColumns) || [];
    const boxes = [
      schemaBox(20, 20, 130, 56, 'Players', counts.PlayerCount),
      schemaBox(20, 130, 130, 56, 'Events', counts.EventCount),
      schemaBox(20, 240, 130, 56, 'Sites', counts.SiteCount),
      schemaBox(280, 130, 170, 76, 'Games', counts.GameCount),
      schemaBox(560, 130, 150, 56, 'Comments', counts.CommentCount),
      schemaBox(560, 240, 150, 56, 'Info', null),
    ].join('');
    const links =
      link(150, 48, 280, 150) +
      link(150, 158, 280, 168) +
      link(150, 268, 280, 186) +
      link(450, 168, 560, 158);

    const svg = '<svg viewBox="0 0 760 320" class="schema-svg" role="img">' + links + boxes + '</svg>';

    return '<div class="schema-wrap">' + svg +
      '<div class="schema-detail" data-role="schema-detail">' +
        '<span class="muted">' + t().t('intro.schemaDesc') + '</span>' +
      '</div></div>';
  }

  function wireSchemaHover(panel, info) {
    const gamesCols = (info && info.gamesColumns) || [];
    const cols = Object.assign({}, FIXED_COLUMNS, { Games: gamesCols.length ? gamesCols : ['ID', '...'] });
    const detail = panel.querySelector('[data-role="schema-detail"]');
    panel.querySelectorAll('.schema-box').forEach((g) => {
      g.addEventListener('mouseenter', () => {
        const key = g.dataset.table;
        detail.innerHTML = '<strong>' + key + '</strong><br>' + (cols[key] || []).map(esc).join(', ');
      });
      g.addEventListener('mouseleave', () => {
        detail.innerHTML = '<span class="muted">' + t().t('intro.schemaDesc') + '</span>';
      });
    });
  }

  function renderMovesCompare(info) {
    const current = (info && info.moveField) || '';
    const kinds = ['Moves', 'Moves1', 'Moves2'];
    return '<div class="moves-cards">' + kinds.map((k) => {
      const active = k === current;
      return '<div class="moves-card' + (active ? ' moves-card-active' : '') + '">' +
        '<h3>' + t().t('intro.moves' + k + 'Title') + '</h3>' +
        '<p class="muted">' + t().t('intro.moves' + k + 'Desc') + '</p>' +
        (active ? '<span class="badge">' + k + '</span>' : '') +
        '</div>';
    }).join('') + '</div>';
  }

  function renderPqlDemo(panel) {
    const boardHost = document.createElement('div');
    boardHost.className = 'intro-pql-board';
    const board = new window.OcgdbBoard.Chessboard(boardHost, { fen: window.OcgdbBoard.START_FEN });

    const list = document.createElement('div');
    list.className = 'pql-demo-list';
    PQL_EXAMPLES.forEach((ex, i) => {
      const row = document.createElement('div');
      row.className = 'pql-demo-row' + (i === 0 ? ' active' : '');
      row.innerHTML =
        '<code>' + esc(ex.q) + '</code>' +
        '<span class="muted">' + esc(ex.text[t().lang()] || ex.text.en) + '</span>' +
        '<button type="button" class="btn btn-small" data-act="try">' + t().t('intro.pqlTryIt') + '</button>';
      row.addEventListener('mouseenter', () => board.markSquares(ex.squares));
      row.querySelector('[data-act="try"]').addEventListener('click', () => {
        window.OcgdbNav.navigate('pql', { pql: ex.q });
      });
      list.appendChild(row);
    });
    if (PQL_EXAMPLES[0]) board.markSquares(PQL_EXAMPLES[0].squares);
    list.addEventListener('mouseleave', () => {
      board.markSquares(PQL_EXAMPLES[0] ? PQL_EXAMPLES[0].squares : []);
    });

    const wrap = panel.querySelector('[data-role="pql-demo"]');
    wrap.appendChild(boardHost);
    wrap.appendChild(list);
  }

  function archDiagram() {
    const rows = [
      ['Core', 'thread pool, stats, run()'],
      ['DbCore', 'SQLite connection, transactions'],
      ['DbRead', 'read games from an existing .ocgdb.db3'],
      ['Search / Exporter / Extract / Duplicate / WebServer', 'one task each, sharing everything above'],
    ];
    return '<div class="arch-diagram">' + rows.map((r, i) =>
      '<div class="arch-row" style="margin-left:' + (i * 28) + 'px">' +
        '<span class="arch-box">' + esc(r[0]) + '</span>' +
        '<span class="muted arch-note">' + esc(r[1]) + '</span>' +
      '</div>').join('<div class="arch-arrow">&#8595;</div>') + '</div>';
  }

  function mount(panel) {
    const info = window.OcgdbApp.info;
    if (!info) {
      panel.innerHTML = '<div class="panel-error">' + t().t('common.dbNotLoaded') + '</div>';
      const onInfo = () => { if (window.OcgdbApp.info) mount(panel); };
      document.addEventListener('ocgdb:info', onInfo, { once: true });
      return;
    }
    const c = info.info || {};

    panel.innerHTML =
      '<section class="intro-hero">' +
        '<h1>' + t().t('intro.heroTitle') + '</h1>' +
        '<p>' + t().t('intro.heroDesc') + '</p>' +
        '<div class="intro-stats">' +
          statChip(c.GameCount, 'intro.statGames') +
          statChip(c.PlayerCount, 'intro.statPlayers') +
          statChip(c.EventCount, 'intro.statEvents') +
          statChip(c.SiteCount, 'intro.statSites') +
        '</div>' +
      '</section>' +

      '<section class="intro-section">' +
        '<h2>' + t().t('intro.compareTitle') + '</h2>' +
        renderCompareTable() +
      '</section>' +

      '<section class="intro-section">' +
        '<h2>' + t().t('intro.schemaTitle') + '</h2>' +
        renderSchema(info) +
      '</section>' +

      '<section class="intro-section">' +
        '<h2>' + t().t('intro.movesTitle') + '</h2>' +
        '<p class="muted">' + t().t('intro.movesDesc') + ' <strong>' + esc(info.moveField || '?') + '</strong></p>' +
        renderMovesCompare(info) +
      '</section>' +

      '<section class="intro-section">' +
        '<h2>' + t().t('intro.pqlTitle') + '</h2>' +
        '<p class="muted">' + t().t('intro.pqlDesc') + '</p>' +
        '<div class="intro-pql-demo" data-role="pql-demo"></div>' +
      '</section>' +

      '<section class="intro-section">' +
        '<h2>' + t().t('intro.archTitle') + '</h2>' +
        '<p class="muted">' + t().t('intro.archDesc') + '</p>' +
        archDiagram() +
      '</section>' +

      '<section class="intro-section intro-footnote">' +
        '<h2>' + t().t('intro.serverTitle') + '</h2>' +
        '<p class="muted">' + t().t('intro.serverDesc') + '</p>' +
      '</section>';

    wireSchemaHover(panel, info);
    renderPqlDemo(panel);
  }

  function statChip(value, labelKey) {
    return '<div class="stat-chip"><span class="stat-chip-value">' +
      (value == null ? '?' : Number(value).toLocaleString()) + '</span>' +
      '<span class="stat-chip-label">' + t().t(labelKey) + '</span></div>';
  }

  function renderCompareTable() {
    const rows = [
      ['intro.compareReadable', 'intro.no', 'intro.yes', 'intro.yes'],
      ['intro.compareRelational', 'intro.yes', 'intro.no', 'intro.yes'],
      ['intro.compareSpeed', 'intro.fast', 'intro.slow', 'intro.fast'],
    ];
    let html = '<div class="table-scroll"><table class="compare-table"><thead><tr><th></th>' +
      '<th>' + t().t('intro.compareBinary') + '</th>' +
      '<th>' + t().t('intro.comparePgn') + '</th>' +
      '<th class="compare-col-ocgdb">' + t().t('intro.compareOcgdb') + '</th>' +
      '</tr></thead><tbody>';
    rows.forEach((r) => {
      html += '<tr><th>' + t().t(r[0]) + '</th><td>' + t().t(r[1]) + '</td><td>' + t().t(r[2]) +
        '</td><td class="compare-col-ocgdb">' + t().t(r[3]) + '</td></tr>';
    });
    html += '</tbody></table></div>';
    return html;
  }

  window.OcgdbIntro = { mount };
})();
