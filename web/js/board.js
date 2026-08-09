// OCGDB web UI -- a small, dependency-free chessboard renderer. It only
// ever displays a FEN the server already computed (per-ply FEN comes from
// /api/game/:id); it never generates moves itself.
'use strict';

(function () {
  const FILES = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'];
  const START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

  function parsePlacement(fen) {
    // Returns board[r][f], r=0 is rank 8 (FEN's first rank), matching FEN
    // row order directly; f=0 is file a.
    const placement = (fen || START_FEN).split(' ')[0];
    const rows = placement.split('/');
    const board = [];
    for (let r = 0; r < 8; r++) {
      const row = [];
      const rowStr = rows[r] || '8';
      for (const ch of rowStr) {
        if (/[1-8]/.test(ch)) {
          for (let i = 0; i < Number(ch); i++) row.push(null);
        } else {
          row.push(ch);
        }
      }
      while (row.length < 8) row.push(null);
      board.push(row);
    }
    return board;
  }

  class Chessboard {
    constructor(container, opts) {
      this.el = container;
      this.flipped = !!(opts && opts.flipped);
      this.squares = {};
      this._buildGrid();
      this.setFen((opts && opts.fen) || START_FEN);
    }

    _order() {
      return this.flipped ? [7, 6, 5, 4, 3, 2, 1, 0] : [0, 1, 2, 3, 4, 5, 6, 7];
    }

    _buildGrid() {
      this.el.innerHTML = '';
      this.el.classList.add('cboard');
      this.squares = {};

      const rOrder = this._order();
      const fOrder = this._order();

      rOrder.forEach((r) => {
        const rankNumber = 8 - r;
        fOrder.forEach((f) => {
          const name = FILES[f] + rankNumber;
          const light = (f + rankNumber) % 2 === 0;
          const sq = document.createElement('div');
          sq.className = 'cboard-sq ' + (light ? 'sq-light' : 'sq-dark');
          sq.dataset.square = name;

          // Coordinate labels along the two outer edges only.
          if (f === fOrder[0]) {
            const rankLabel = document.createElement('span');
            rankLabel.className = 'sq-coord sq-coord-rank';
            rankLabel.textContent = String(rankNumber);
            sq.appendChild(rankLabel);
          }
          if (r === rOrder[7]) {
            const fileLabel = document.createElement('span');
            fileLabel.className = 'sq-coord sq-coord-file';
            fileLabel.textContent = FILES[f];
            sq.appendChild(fileLabel);
          }

          this.el.appendChild(sq);
          this.squares[name] = sq;
        });
      });
    }

    setFlipped(flipped) {
      if (this.flipped === !!flipped) return;
      this.flipped = !!flipped;
      const fen = this.fen;
      this._buildGrid();
      this.setFen(fen);
    }

    setFen(fen) {
      this.fen = fen || START_FEN;
      const placement = parsePlacement(this.fen);
      Object.keys(this.squares).forEach((name) => {
        const sq = this.squares[name];
        const existing = sq.querySelector('.piece');
        if (existing) existing.remove();
      });
      for (let r = 0; r < 8; r++) {
        for (let f = 0; f < 8; f++) {
          const piece = placement[r][f];
          if (!piece) continue;
          const name = FILES[f] + (8 - r);
          const sq = this.squares[name];
          if (!sq) continue;
          sq.insertAdjacentHTML('beforeend', window.OcgdbPieces.pieceSvg(piece));
        }
      }
    }

    clearHighlight() {
      Object.values(this.squares).forEach((sq) => sq.classList.remove('sq-from', 'sq-to'));
    }

    highlight(fromSquare, toSquare) {
      this.clearHighlight();
      if (fromSquare && this.squares[fromSquare]) this.squares[fromSquare].classList.add('sq-from');
      if (toSquare && this.squares[toSquare]) this.squares[toSquare].classList.add('sq-to');
    }

    // Used by the intro page's PQL explainer to point at the squares an
    // example query mentions -- independent of the from/to move highlight.
    clearMarks() {
      Object.values(this.squares).forEach((sq) => sq.classList.remove('sq-marked'));
    }

    markSquares(names) {
      this.clearMarks();
      (names || []).forEach((name) => {
        if (this.squares[name]) this.squares[name].classList.add('sq-marked');
      });
    }
  }

  window.OcgdbBoard = { Chessboard, START_FEN };
})();
