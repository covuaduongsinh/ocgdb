// OCGDB web UI -- chess piece glyphs, drawn as inline SVG from plain
// geometric primitives (rect/circle/polygon), not traced from any existing
// piece set. Deliberately simple/original rather than illustrative: it only
// needs to read clearly at small board sizes and follow the app theme.
// Shape geometry is shared between white and black; colour comes from the
// `.piece-w` / `.piece-b` CSS classes in css/board.css.
'use strict';

(function () {
  const BASE = '<rect x="18" y="83" width="64" height="8" rx="2"/>';

  // A shared goblet-shaped body used (with different tops) by every piece.
  const BODY = '<path d="M36,83 L31,58 Q50,48 69,58 L64,83 Z"/>';
  const BODY_WIDE = '<path d="M32,83 L28,54 Q50,42 72,54 L68,83 Z"/>';

  // Queen: wide body + a crown of five points along the rim.
  const QUEEN_POINTS = [32, 41, 50, 59, 68]
    .map((x) => '<circle cx="' + x + '" cy="30" r="5.4"/>')
    .join('');

  const SHAPES = {
    P: BASE + BODY + '<circle cx="50" cy="40" r="13"/>',

    R: BASE +
      '<path d="M33,83 L33,55 L67,55 L67,83 Z"/>' +
      '<rect x="30" y="46" width="10" height="10"/>' +
      '<rect x="45" y="46" width="10" height="10"/>' +
      '<rect x="60" y="46" width="10" height="10"/>' +
      '<rect x="30" y="53" width="40" height="6"/>',

    N: BASE +
      '<polygon points="42,83 34,83 32,66 24,58 24,46 32,34 46,24 62,24 70,32 66,38 58,38 54,46 64,50 66,62 60,66 60,83"/>' +
      '<circle cx="40" cy="40" r="2.6" class="piece-eye"/>',

    B: BASE + BODY +
      '<circle cx="50" cy="38" r="15"/>' +
      '<line x1="42" y1="30" x2="58" y2="46" class="piece-slit"/>' +
      '<circle cx="50" cy="17" r="4.2"/>',

    Q: BASE + BODY_WIDE + '<rect x="34" y="36" width="32" height="9" rx="1"/>' + QUEEN_POINTS,

    K: BASE + BODY +
      '<rect x="39" y="34" width="22" height="10" rx="1"/>' +
      '<rect x="46.5" y="10" width="7" height="20" rx="1.5"/>' +
      '<rect x="40" y="16.5" width="20" height="7" rx="1.5"/>',
  };

  const TYPE_NAMES = { K: 'king', Q: 'queen', R: 'rook', B: 'bishop', N: 'knight', P: 'pawn' };

  // pieceSvg('K') -> white king, pieceSvg('k') -> black king.
  function pieceSvg(letter) {
    const upper = letter.toUpperCase();
    const shape = SHAPES[upper];
    if (!shape) return '';
    const side = letter === upper ? 'w' : 'b';
    const name = TYPE_NAMES[upper] || '';
    return (
      '<svg class="piece piece-' + side + '" viewBox="0 0 100 100" ' +
      'role="img" aria-label="' + side + ' ' + name + '" xmlns="http://www.w3.org/2000/svg">' +
      shape +
      '</svg>'
    );
  }

  window.OcgdbPieces = { pieceSvg };
})();
