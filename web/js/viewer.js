// OCGDB web UI -- single game viewer: board + move list + navigation.
// Mounted into any container by browse.js (and, for the intro page's demo,
// could be reused directly). Fully self-contained: fetches its own data.
'use strict';

(function () {
  const t = () => window.I18N;

  class GameViewer {
    constructor(container) {
      this.root = container;
      this.game = null;
      this.cur = 0; // ply pointer: 0..moves.length
      this.flipped = false;
      this._onKeydown = this._onKeydown.bind(this);
      this.engines = [];
      this.analysisStream = null; // { stop() } from adminAnalyse(), while a search is running
      this.analysisEngineId = null;
    }

    async load(gameId) {
      this.root.innerHTML = '<div class="panel-loading">' + t().t('common.loading') + '</div>';
      try {
        const game = await window.OcgdbApi.game(gameId);
        if (!game) {
          this.root.innerHTML = '<div class="panel-error">' + t().t('viewer.loadError') + '</div>';
          return;
        }
        this.game = game;
        this.cur = 0;
        this._render();
        document.addEventListener('keydown', this._onKeydown);
      } catch (e) {
        this.root.innerHTML = '<div class="panel-error">' + t().t('viewer.loadError') + '</div>';
      }
    }

    unmount() {
      document.removeEventListener('keydown', this._onKeydown);
      this._stopAnalysis();
      this.root.innerHTML = '';
    }

    _onKeydown(e) {
      if (!this.game) return;
      if (e.key === 'ArrowLeft') { this._goto(this.cur - 1); e.preventDefault(); }
      else if (e.key === 'ArrowRight') { this._goto(this.cur + 1); e.preventDefault(); }
      else if (e.key === 'Home') { this._goto(0); e.preventDefault(); }
      else if (e.key === 'End') { this._goto(this.game.moves.length); e.preventDefault(); }
    }

    _fenAtPly(k) {
      const moves = this.game.moves;
      if (!moves.length) return this.game.startFen || window.OcgdbBoard.START_FEN;
      if (k <= 0) return moves[0].fen;
      if (k >= moves.length) return this.game.finalFen;
      return moves[k].fen;
    }

    _goto(k) {
      const max = this.game.moves.length;
      this.cur = Math.max(0, Math.min(max, k));
      this._update();
    }

    _render() {
      const g = this.game;
      const tag = (name) => (g.tags && g.tags[name]) || '';
      const white = tag('White') || '?';
      const black = tag('Black') || '?';

      this.root.innerHTML =
        '<div class="viewer">' +
          '<div class="viewer-header">' +
            '<div class="viewer-players">' +
              '<strong>' + esc(white) + '</strong>' +
              (tag('WhiteElo') ? ' <span class="muted">(' + esc(tag('WhiteElo')) + ')</span>' : '') +
              ' &ndash; <strong>' + esc(black) + '</strong>' +
              (tag('BlackElo') ? ' <span class="muted">(' + esc(tag('BlackElo')) + ')</span>' : '') +
              ' <span class="viewer-result">' + esc(tag('Result')) + '</span>' +
            '</div>' +
            '<div class="viewer-meta muted">' +
              esc(tag('Event')) + (tag('Site') ? ' &middot; ' + esc(tag('Site')) : '') +
              (tag('Date') ? ' &middot; ' + esc(tag('Date')) : '') +
              (tag('ECO') ? ' &middot; ' + esc(tag('ECO')) : '') +
            '</div>' +
          '</div>' +
          '<div class="viewer-body">' +
            '<div class="viewer-board-col">' +
              '<div class="viewer-board"></div>' +
              '<div class="viewer-controls">' +
                '<button type="button" class="btn" data-act="start" title="' + t().t('viewer.start') + '">|&laquo;</button>' +
                '<button type="button" class="btn" data-act="prev" title="' + t().t('viewer.prev') + '">&laquo;</button>' +
                '<button type="button" class="btn" data-act="next" title="' + t().t('viewer.next') + '">&raquo;</button>' +
                '<button type="button" class="btn" data-act="end" title="' + t().t('viewer.end') + '">&raquo;|</button>' +
                '<button type="button" class="btn" data-act="flip" title="' + t().t('viewer.flip') + '">&#8645;</button>' +
                '<span class="viewer-plycount muted"></span>' +
              '</div>' +
              '<div class="viewer-comment muted"></div>' +
            '</div>' +
            '<div class="viewer-side-col">' +
              '<div class="viewer-moves"></div>' +
              (window.OcgdbApi.hasAdminToken() ?
                '<details class="viewer-analysis-wrap">' +
                  '<summary>' + t().t('viewer.analysis') + '</summary>' +
                  '<div class="viewer-analysis" data-role="analysis">' +
                    '<div class="panel-loading">' + t().t('common.loading') + '</div>' +
                  '</div>' +
                '</details>' : '') +
              '<details class="viewer-pgn-wrap">' +
                '<summary>' + t().t('viewer.pgn') + '</summary>' +
                '<pre class="viewer-pgn"></pre>' +
                '<div class="viewer-pgn-actions">' +
                  '<button type="button" class="btn" data-act="copy">' + t().t('viewer.copyPgn') + '</button>' +
                  '<a class="btn" data-act="dl" download="game-' + g.id + '.pgn">' + t().t('common.download') + '</a>' +
                '</div>' +
              '</details>' +
            '</div>' +
          '</div>' +
        '</div>';

      this.boardEl = this.root.querySelector('.viewer-board');
      this.board = new window.OcgdbBoard.Chessboard(this.boardEl, { fen: this._fenAtPly(0), flipped: this.flipped });

      this._renderMoveList();
      this.root.querySelector('.viewer-pgn').textContent = g.pgn || '';
      const dl = this.root.querySelector('[data-act="dl"]');
      dl.href = window.OcgdbApi.pgnUrl(g.id);

      this.root.querySelectorAll('[data-act]').forEach((btn) => {
        btn.addEventListener('click', () => this._onAction(btn.dataset.act));
      });

      if (window.OcgdbApi.hasAdminToken()) this._initAnalysisPanel();

      this._update();
    }

    _renderMoveList() {
      const g = this.game;
      const wrap = this.root.querySelector('.viewer-moves');
      if (!g.moves.length) {
        wrap.innerHTML = '<div class="muted">' + t().t('common.noData') + '</div>';
        return;
      }
      let html = '';
      for (let i = 0; i < g.moves.length; i += 2) {
        const no = Math.floor(i / 2) + 1;
        html += '<span class="move-no">' + no + '.</span>';
        html += '<span class="move" data-ply="' + (i + 1) + '">' + esc(g.moves[i].san) + '</span>';
        if (g.moves[i + 1]) {
          html += '<span class="move" data-ply="' + (i + 2) + '">' + esc(g.moves[i + 1].san) + '</span>';
        }
      }
      wrap.innerHTML = html;
      wrap.querySelectorAll('.move').forEach((el) => {
        el.addEventListener('click', () => this._goto(Number(el.dataset.ply)));
      });
    }

    _onAction(act) {
      const g = this.game;
      if (act === 'start') this._goto(0);
      else if (act === 'prev') this._goto(this.cur - 1);
      else if (act === 'next') this._goto(this.cur + 1);
      else if (act === 'end') this._goto(g.moves.length);
      else if (act === 'flip') {
        this.flipped = !this.flipped;
        this.board.setFlipped(this.flipped);
        this._update();
      } else if (act === 'copy') {
        navigator.clipboard && navigator.clipboard.writeText(g.pgn || '').then(() => {
          const btn = this.root.querySelector('[data-act="copy"]');
          const old = btn.textContent;
          btn.textContent = t().t('common.copied');
          setTimeout(() => { btn.textContent = old; }, 1200);
        });
      }
    }

    _update() {
      const g = this.game;
      this.board.setFen(this._fenAtPly(this.cur));

      if (this.cur > 0 && g.moves[this.cur - 1]) {
        const uci = g.moves[this.cur - 1].uci || '';
        this.board.highlight(uci.slice(0, 2), uci.slice(2, 4));
      } else {
        this.board.clearHighlight();
      }

      this.root.querySelectorAll('.move').forEach((el) => {
        el.classList.toggle('move-active', Number(el.dataset.ply) === this.cur);
      });
      const active = this.root.querySelector('.move-active');
      if (active && active.scrollIntoView) active.scrollIntoView({ block: 'nearest' });

      const plyEl = this.root.querySelector('.viewer-plycount');
      if (plyEl) plyEl.textContent = this.cur + ' / ' + g.moves.length;

      const commentEl = this.root.querySelector('.viewer-comment');
      if (commentEl) {
        const comment = this.cur === 0 ? (g.firstComment || '') : ((g.moves[this.cur - 1] || {}).comment || '');
        commentEl.textContent = comment;
        commentEl.style.display = comment ? '' : 'none';
      }

      // A running analysis session tracks the board: every navigation
      // re-issues it against the new position (Phase 5.2's "attach to the
      // board in the game viewer" -- the engine always analyses whatever
      // position is currently on screen, like Lichess's analysis board).
      if (this.analysisEngineId != null) this._startAnalysis(this.analysisEngineId);
    }

    // ------------------------------------------------------------ analysis

    async _initAnalysisPanel() {
      const panel = this.root.querySelector('[data-role="analysis"]');
      if (!panel) return;
      try {
        const data = await window.OcgdbApi.adminEngines();
        this.engines = data.engines || [];
      } catch (e) {
        panel.innerHTML = '<div class="panel-error">' + esc(e.message) + '</div>';
        return;
      }
      if (!this.engines.length) {
        panel.innerHTML = '<div class="panel-empty">' + t().t('viewer.noEngines') + '</div>';
        return;
      }
      panel.innerHTML =
        '<div class="viewer-analysis-controls">' +
          '<select data-role="engine-select">' +
            this.engines.map((e) => '<option value="' + e.id + '">' + esc(e.name) + '</option>').join('') +
          '</select>' +
          '<button type="button" class="btn btn-small" data-role="analysis-toggle">' + t().t('viewer.analyseStart') + '</button>' +
        '</div>' +
        '<div class="viewer-analysis-output muted" data-role="analysis-output"></div>';

      panel.querySelector('[data-role="analysis-toggle"]').addEventListener('click', () => {
        if (this.analysisEngineId != null) {
          this._stopAnalysis();
        } else {
          const engineId = Number(panel.querySelector('[data-role="engine-select"]').value);
          this._startAnalysis(engineId);
        }
      });
    }

    _startAnalysis(engineId) {
      this._stopAnalysis(); // supersede any in-flight search for the previous position
      this.analysisEngineId = engineId;

      const toggleBtn = this.root.querySelector('[data-role="analysis-toggle"]');
      if (toggleBtn) toggleBtn.textContent = t().t('viewer.analyseStop');
      const outEl = this.root.querySelector('[data-role="analysis-output"]');
      if (outEl) outEl.textContent = t().t('viewer.analysing');

      const fen = this._fenAtPly(this.cur);
      this.analysisStream = window.OcgdbApi.adminAnalyse(engineId, fen, { depth: 20 }, (evt) => {
        if (!outEl || this.analysisEngineId !== engineId) return; // stale response from a superseded search
        if (evt.type === 'error') {
          outEl.textContent = evt.error;
        } else if (evt.type === 'info') {
          const score = evt.mate != null
            ? t().t('viewer.mateIn', { n: Math.abs(evt.mate) })
            : (evt.scoreCp >= 0 ? '+' : '') + (evt.scoreCp / 100).toFixed(2);
          outEl.textContent = t().t('viewer.depth') + ' ' + evt.depth + ': ' + score + '  ' + (evt.pv || '');
        } else if (evt.type === 'bestmove') {
          outEl.textContent += '  (' + t().t('viewer.bestMove') + ': ' + evt.move + ')';
        }
      });
    }

    _stopAnalysis() {
      if (this.analysisStream) { this.analysisStream.stop(); this.analysisStream = null; }
      this.analysisEngineId = null;
      const toggleBtn = this.root.querySelector('[data-role="analysis-toggle"]');
      if (toggleBtn) toggleBtn.textContent = t().t('viewer.analyseStart');
    }
  }

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  window.OcgdbViewer = { GameViewer };
})();
