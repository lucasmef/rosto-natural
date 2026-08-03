const assert = require("assert");
const analysis = require("../uxp/js/analysis.js");

function mesh(offset = 0, jitter = 0) {
  const points = Array.from(
    { length: analysis.LANDMARK_COUNT },
    () => [.40 + offset + jitter, .50 + jitter]
  );
  points[234] = [.30 + offset + jitter, .50 + jitter];
  points[454] = [.70 + offset + jitter, .50 + jitter];
  points[33] = [.42 + offset + jitter, .45 + jitter];
  points[263] = [.58 + offset + jitter, .45 + jitter];
  return { confidence: .95, points };
}

function binaryEncodingMatchesCoreLayout() {
  const frames = [
    { seconds: 0, mesh: mesh() },
    { seconds: 1 / 60, mesh: null }
  ];
  const buffer = analysis.encodeMeshes(frames);
  const bytes = new Uint8Array(buffer);
  const view = new DataView(buffer);
  assert.strictEqual(
    String.fromCharCode(...bytes.slice(0, 8)), "ROSTMESH");
  assert.strictEqual(view.getUint32(8, true), 2);
  assert.strictEqual(view.getUint32(12, true), 2);
  assert.strictEqual(buffer.byteLength, 16 + 2 * (16 + 478 * 8));
  assert.strictEqual(view.getUint8(16 + 12), 1);
  assert.strictEqual(view.getUint8(16 + (16 + 478 * 8) + 12), 0);
}

function meshGapAndAdaptiveMotion() {
  const frames = [];
  for (let index = 0; index < 24; index++) {
    const offset = index < 12 ? 0 : .15;
    const jitter = index % 2 ? -.0015 : .0015;
    frames.push({
      seconds: index / 60,
      mesh: mesh(offset, jitter)
    });
  }
  frames[5].mesh = null;
  frames[6].mesh = null;
  const stable = analysis.stabilizeMeshes(frames);
  assert(stable[5].mesh && stable[6].mesh);
  assert(stable[12].mesh.points[10][0] > .52,
    "adaptive release should follow fast motion");
  const rawDelta = Math.abs(
    frames[8].mesh.points[10][0] - frames[9].mesh.points[10][0]);
  const stableDelta = Math.abs(
    stable[8].mesh.points[10][0] - stable[9].mesh.points[10][0]);
  assert(stableDelta < rawDelta * .25,
    "static landmark jitter should be attenuated");
}

function captureParsing() {
  const points = mesh().points.map(([x, y]) => ({ x, y }));
  const parsedMesh = analysis.parseCapture({
    confidence: .9, points
  });
  assert.strictEqual(parsedMesh.mesh.points.length, 478);
  const parsedFace = analysis.parseCapture({
    x: .5, y: .48, width: .4, height: .55, roll: 0, confidence: .8
  });
  assert.strictEqual(parsedFace.face.width, .4);
}

binaryEncodingMatchesCoreLayout();
meshGapAndAdaptiveMotion();
captureParsing();
console.log("uxp_analysis: 3 tests passed");
