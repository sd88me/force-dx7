/* DX7 algorithm routing data (all 32) — original data + original renderer.
 *
 * Provenance and confidence: see docs/ALGORITHMS.md. Short version:
 * transcribed from an independent hardware-analysis chart, not from any
 * software implementation's source code. Carrier positions and feedback
 * operator are high-confidence for all 32; the exact branch/merge shape on
 * the algorithms with more than one modulator feeding a single carrier
 * (roughly 9-15, 19-22, 26-27) is a best-effort single-source reading, not
 * independently re-verified -- flagged in ALGORITHMS.md, not asserted here
 * as beyond doubt.
 *
 * Data shape: ALGORITHMS[n] (1-indexed via array holes at 0) = {
 *   chains: [[modulator,...,carrier], ...],  // one array per signal path,
 *                                             // last element is always a carrier
 *   feedback: operatorNumber                 // which operator self-feeds
 * }
 */
window.DX7_ALGORITHMS = {
  1:  { chains: [[6,5,4,3],[2,1]],                 feedback: 6 },
  2:  { chains: [[6,5,4,3],[2,1]],                 feedback: 2 },
  3:  { chains: [[3,2,1],[6,5,4]],                 feedback: 6 },
  4:  { chains: [[3,2,1],[6,5,4]],                 feedback: 6 },
  5:  { chains: [[2,1],[4,3],[6,5]],               feedback: 6 },
  6:  { chains: [[2,1],[4,3],[6,5]],               feedback: 6 },
  7:  { chains: [[2,1],[4,3],[6,5]],               feedback: 6 },
  8:  { chains: [[2,1],[6,3],[4,5]],               feedback: 4 },
  9:  { chains: [[2,1],[4,3],[6,5,3]],             feedback: 2 },
  10: { chains: [[2,1],[5,4],[6,4],[3,4]],         feedback: 3 },
  11: { chains: [[2,1],[5,4],[6,4],[3,4]],         feedback: 6 },
  12: { chains: [[2,1],[4,3],[5,3],[6,3]],         feedback: 6 },
  13: { chains: [[2,1],[4,3],[5,3],[6,3]],         feedback: 6 },
  14: { chains: [[2,1],[4,3],[6,5,3]],             feedback: 6 },
  15: { chains: [[2,1],[4,3],[6,5,3]],             feedback: 6 },
  16: { chains: [[2,1],[3,1],[4,1],[6,5,1]],       feedback: 6 },
  17: { chains: [[2,1],[3,1],[4,1],[6,1]],         feedback: 3 },
  18: { chains: [[2,1],[3,1],[6,5,4,1]],           feedback: 6 },
  19: { chains: [[2,1],[3,1],[6,4],[6,5]],         feedback: 6 },
  20: { chains: [[3,1],[3,2],[5,4],[6,4]],         feedback: 6 },
  21: { chains: [[3,1],[3,2],[6,4],[6,5]],         feedback: 6 },
  22: { chains: [[2,1],[6,3],[6,4],[6,5]],         feedback: 6 },
  23: { chains: [[2,1],[3,6,4],[3,6,5]],           feedback: 6 },
  24: { chains: [[1],[2],[3],[6,4],[6,5]],         feedback: 6 },
  25: { chains: [[1],[2],[3],[6,4],[6,5]],         feedback: 6 },
  26: { chains: [[1],[2],[3,5,6,4]],               feedback: 6 },
  27: { chains: [[1],[2],[3,5,6,4]],               feedback: 6 },
  28: { chains: [[1],[3],[5,4],[6]],               feedback: 5 },
  29: { chains: [[1],[2],[3],[4,6,5]],             feedback: 6 },
  30: { chains: [[1],[2],[4,3],[6]],               feedback: 5 },
  31: { chains: [[1],[2],[3],[4],[6,5]],           feedback: 6 },
  32: { chains: [[1],[2],[3],[4],[5],[6]],         feedback: 6 },
};

/* Original renderer: a simple stacked-box diagram (carrier row at bottom,
 * modulators stacked above in their chain, a loop glyph on the feedback
 * operator) drawn with plain SVG in this project's own DX7-inspired amber/
 * brown palette. Not a copy of any specific editor's layout code or pixel
 * coordinates -- just a from-scratch way to show the same underlying facts. */
// Fixed canvas size across ALL 32 algorithms (computed once, from the
// widest/tallest algorithm in the table) so the panel never resizes as the
// user steps through algorithms -- only the content within it changes.
var ALGO_MAX_COLS, ALGO_MAX_ROWS;
(function(){
  var allAlgos = Object.keys(window.DX7_ALGORITHMS).map(function(k){ return window.DX7_ALGORITHMS[k]; });
  ALGO_MAX_COLS = Math.max.apply(null, allAlgos.map(function(a){ return a.chains.length; }));
  ALGO_MAX_ROWS = Math.max.apply(null, allAlgos.map(function(a){
    return Math.max.apply(null, a.chains.map(function(c){ return c.length; }));
  }));
})();

function renderAlgorithmDiagram(algoNum, host){
  var data = window.DX7_ALGORITHMS[algoNum];
  host.innerHTML = "";
  if (!data) { host.textContent = "Algorithm "+algoNum; return; }

  var BOX = 30, GAP_X = 8, GAP_Y = 8, PAD = 10;
  var maxHeight = ALGO_MAX_ROWS;                    // fixed, not per-algorithm
  var w = ALGO_MAX_COLS * (BOX + GAP_X) - GAP_X + PAD*2;  // fixed, not per-algorithm
  var h = maxHeight * (BOX + GAP_Y) - GAP_Y + PAD*2;

  var svg = document.createElementNS("http://www.w3.org/2000/svg","svg");
  svg.setAttribute("width", w); svg.setAttribute("height", h);
  svg.setAttribute("viewBox", "0 0 "+w+" "+h);

  data.chains.forEach(function(chain, ci){
    var x = PAD + ci*(BOX+GAP_X);
    // chain is modulator..carrier top-to-bottom; carrier (last) sits at the bottom row
    var startY = PAD + (maxHeight - chain.length) * (BOX+GAP_Y);
    chain.forEach(function(op, i){
      var y = startY + i*(BOX+GAP_Y);
      var isCarrier = (i === chain.length - 1);
      var rect = document.createElementNS(svg.namespaceURI,"rect");
      rect.setAttribute("x", x); rect.setAttribute("y", y);
      rect.setAttribute("width", BOX); rect.setAttribute("height", BOX);
      rect.setAttribute("rx", 3);
      rect.setAttribute("fill", isCarrier ? "#4ad6d6" : "#2f3336");
      rect.setAttribute("stroke", "#5a4a3c"); rect.setAttribute("stroke-width","1.5");
      svg.appendChild(rect);
      var label = document.createElementNS(svg.namespaceURI,"text");
      label.setAttribute("x", x+BOX/2); label.setAttribute("y", y+BOX/2+4);
      label.setAttribute("text-anchor","middle");
      label.setAttribute("font-size","13"); label.setAttribute("font-family","Courier New,monospace");
      label.setAttribute("fill", isCarrier ? "#141618" : "#d8dee2");
      label.textContent = op;
      svg.appendChild(label);
      if (i > 0){
        var line = document.createElementNS(svg.namespaceURI,"line");
        line.setAttribute("x1", x+BOX/2); line.setAttribute("y1", y-GAP_Y+2);
        line.setAttribute("x2", x+BOX/2); line.setAttribute("y2", y);
        line.setAttribute("stroke","#9c8b76"); line.setAttribute("stroke-width","1.5");
        svg.appendChild(line);
      }
      if (op === data.feedback){
        var loop = document.createElementNS(svg.namespaceURI,"path");
        loop.setAttribute("d", "M "+(x+BOX-4)+" "+(y+4)+" q 10 0 10 -10 q 0 -10 -10 -10");
        loop.setAttribute("fill","none"); loop.setAttribute("stroke","#e0a05c"); loop.setAttribute("stroke-width","1.5");
        svg.appendChild(loop);
      }
    });
  });
  host.appendChild(svg);
}
