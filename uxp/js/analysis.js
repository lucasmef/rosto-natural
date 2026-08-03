(() => {
  const GEOMETRY_VERSION = 1;
  const MESH_VERSION = 2;
  const LANDMARK_COUNT = 478;
  const MAX_GAP = 8;
  const MEDIAN_RADIUS = 2;
  const HALF_LIFE = .080;

  function median(values) {
    const sorted = values.slice().sort((a, b) => a - b);
    return sorted[Math.floor(sorted.length / 2)];
  }

  function smoothstep(a, b, value) {
    const t = Math.max(0, Math.min(1, (value - a) / (b - a)));
    return t * t * (3 - 2 * t);
  }

  function adaptiveAlpha(dt, motion) {
    const base = 1 - Math.pow(2, -dt / HALF_LIFE);
    const release = smoothstep(.0015, .020, motion);
    return base + (.92 - base) * release;
  }

  function clonePoints(points) {
    return points.map(point => [point[0], point[1]]);
  }

  function interpolatePoints(a, b, t) {
    return a.map((point, index) => [
      point[0] + (b[index][0] - point[0]) * t,
      point[1] + (b[index][1] - point[1]) * t
    ]);
  }

  function interpolateMeshGaps(frames, maxGap = MAX_GAP) {
    const out = frames.map(frame => ({
      ...frame,
      mesh: frame.mesh ? {
        confidence: frame.mesh.confidence,
        points: clonePoints(frame.mesh.points)
      } : null
    }));
    for (let i = 0; i < out.length;) {
      if (out[i].mesh) { i++; continue; }
      const begin = i;
      while (i < out.length && !out[i].mesh) i++;
      const gap = i - begin;
      if (begin && i < out.length && gap <= maxGap) {
        for (let k = 0; k < gap; k++) {
          const t = (k + 1) / (gap + 1);
          const a = out[begin - 1].mesh, b = out[i].mesh;
          out[begin + k].mesh = {
            confidence: a.confidence +
              (b.confidence - a.confidence) * t,
            points: interpolatePoints(a.points, b.points, t)
          };
        }
      }
    }
    return out;
  }

  function meshPose(mesh) {
    const left = mesh.points[234], right = mesh.points[454];
    const eyeLeft = mesh.points[33], eyeRight = mesh.points[263];
    const width = Math.max(Math.hypot(
      right[0] - left[0], right[1] - left[1]), 1e-5);
    return {
      x: (left[0] + right[0]) * .5,
      y: (left[1] + right[1]) * .5,
      width,
      roll: Math.atan2(eyeRight[1] - eyeLeft[1],
                       eyeRight[0] - eyeLeft[0])
    };
  }

  function meshMotion(a, b) {
    const pa = meshPose(a), pb = meshPose(b);
    const scale = Math.max((pa.width + pb.width) * .5, 1e-5);
    return Math.hypot(pa.x - pb.x, pa.y - pb.y) / scale +
      Math.abs(pa.width - pb.width) / scale * .50 +
      Math.abs(pa.roll - pb.roll) * .25;
  }

  function stabilizeMeshes(input, radius = MEDIAN_RADIUS) {
    const filled = interpolateMeshGaps(input);
    const med = filled.map((frame, index) => {
      if (!frame.mesh) return frame;
      const neighbors = filled.slice(
        Math.max(0, index - radius), index + radius + 1
      ).filter(candidate => candidate.mesh);
      const points = Array.from({ length: LANDMARK_COUNT }, (_, point) => [
        median(neighbors.map(item => item.mesh.points[point][0])),
        median(neighbors.map(item => item.mesh.points[point][1]))
      ]);
      return { ...frame, mesh: {
        confidence: median(neighbors.map(item => item.mesh.confidence)),
        points
      }};
    });
    const motionReference = med.map(frame => ({
      ...frame,
      mesh: frame.mesh ? {
        confidence: frame.mesh.confidence,
        points: clonePoints(frame.mesh.points)
      } : null
    }));
    for (const direction of [1, -1]) {
      let state = null;
      let previous = null;
      const start = direction > 0 ? 0 : med.length - 1;
      for (let n = 0; n < med.length; n++) {
        const index = start + n * direction;
        if (!med[index].mesh) {
          state = null;
          previous = null;
          continue;
        }
        if (!state) {
          state = {
            confidence: med[index].mesh.confidence,
            points: clonePoints(med[index].mesh.points)
          };
        } else {
          const dt = Math.abs(
            med[index].seconds - med[previous].seconds);
          const alpha = adaptiveAlpha(
            dt, meshMotion(motionReference[index].mesh,
                           motionReference[previous].mesh));
          state.confidence +=
            (med[index].mesh.confidence - state.confidence) * alpha;
          for (let point = 0; point < LANDMARK_COUNT; point++) {
            state.points[point][0] +=
              (med[index].mesh.points[point][0] -
               state.points[point][0]) * alpha;
            state.points[point][1] +=
              (med[index].mesh.points[point][1] -
               state.points[point][1]) * alpha;
          }
          med[index].mesh = {
            confidence: state.confidence,
            points: clonePoints(state.points)
          };
        }
        previous = index;
      }
    }
    return med;
  }

  function interpolateGeometryGaps(frames, maxGap = MAX_GAP) {
    const out = frames.map(frame => ({
      ...frame, face: frame.face ? { ...frame.face } : null
    }));
    for (let i = 0; i < out.length;) {
      if (out[i].face) { i++; continue; }
      const begin = i;
      while (i < out.length && !out[i].face) i++;
      const gap = i - begin;
      if (begin && i < out.length && gap <= maxGap) {
        for (let k = 0; k < gap; k++) {
          const t = (k + 1) / (gap + 1);
          const a = out[begin - 1].face, b = out[i].face;
          const face = {};
          for (const key of [
            "x", "y", "width", "height", "roll", "confidence"
          ]) face[key] = a[key] + (b[key] - a[key]) * t;
          out[begin + k].face = face;
        }
      }
    }
    return out;
  }

  function geometryMotion(a, b) {
    const scale = Math.max((a.width + b.width) * .5, 1e-5);
    return Math.hypot(a.x - b.x, a.y - b.y) / scale +
      Math.abs(a.width - b.width) / scale * .50 +
      Math.abs(a.height - b.height) /
        Math.max((a.height + b.height) * .5, 1e-5) * .35 +
      Math.abs(a.roll - b.roll) * .25;
  }

  function stabilizeGeometry(input, radius = MEDIAN_RADIUS) {
    const fields = ["x", "y", "width", "height", "roll", "confidence"];
    const filled = interpolateGeometryGaps(input);
    const med = filled.map((frame, index) => {
      if (!frame.face) return frame;
      const neighbors = filled.slice(
        Math.max(0, index - radius), index + radius + 1
      ).filter(candidate => candidate.face);
      const face = {};
      for (const key of fields)
        face[key] = median(neighbors.map(item => item.face[key]));
      return { ...frame, face };
    });
    const motionReference = med.map(frame => ({
      ...frame, face: frame.face ? { ...frame.face } : null
    }));
    for (const direction of [1, -1]) {
      let state = null;
      let previous = null;
      const start = direction > 0 ? 0 : med.length - 1;
      for (let n = 0; n < med.length; n++) {
        const index = start + n * direction;
        if (!med[index].face) {
          state = null;
          previous = null;
          continue;
        }
        if (!state) state = { ...med[index].face };
        else {
          const dt = Math.abs(
            med[index].seconds - med[previous].seconds);
          const alpha = adaptiveAlpha(
            dt, geometryMotion(motionReference[index].face,
                               motionReference[previous].face));
          for (const key of fields)
            state[key] += (med[index].face[key] - state[key]) * alpha;
          med[index].face = { ...state };
        }
        previous = index;
      }
    }
    return med;
  }

  function encodeGeometry(frames) {
    const header = 16, row = 40;
    const buffer = new ArrayBuffer(header + frames.length * row);
    const bytes = new Uint8Array(buffer);
    "ROSTOLMK".split("").forEach((ch, i) => {
      bytes[i] = ch.charCodeAt(0);
    });
    const view = new DataView(buffer);
    view.setUint32(8, GEOMETRY_VERSION, true);
    view.setUint32(12, frames.length, true);
    frames.forEach((frame, index) => {
      const offset = header + index * row;
      view.setFloat64(offset, frame.seconds, true);
      for (const [fieldIndex, key] of [
        "x", "y", "width", "height", "roll", "confidence"
      ].entries()) view.setFloat32(
        offset + 8 + fieldIndex * 4,
        frame.face ? frame.face[key] : 0, true);
      view.setUint8(offset + 32, frame.face ? 1 : 0);
    });
    return buffer;
  }

  function encodeMeshes(frames) {
    const header = 16;
    const row = 16 + LANDMARK_COUNT * 8;
    const buffer = new ArrayBuffer(header + frames.length * row);
    const bytes = new Uint8Array(buffer);
    "ROSTMESH".split("").forEach((ch, index) => {
      bytes[index] = ch.charCodeAt(0);
    });
    const view = new DataView(buffer);
    view.setUint32(8, MESH_VERSION, true);
    view.setUint32(12, frames.length, true);
    frames.forEach((frame, frameIndex) => {
      const offset = header + frameIndex * row;
      const valid = Boolean(frame.mesh);
      view.setFloat64(offset, frame.seconds, true);
      view.setFloat32(
        offset + 8, valid ? frame.mesh.confidence : 0, true);
      view.setUint8(offset + 12, valid ? 1 : 0);
      for (let point = 0; point < LANDMARK_COUNT; point++) {
        const coordinates = valid ? frame.mesh.points[point] : [0, 0];
        view.setFloat32(offset + 16 + point * 8, coordinates[0], true);
        view.setFloat32(offset + 20 + point * 8, coordinates[1], true);
      }
    });
    return buffer;
  }

  function parseCapture(data) {
    const source = Array.isArray(data.points) ? data.points :
      (Array.isArray(data.landmarks) ? data.landmarks : null);
    if (source && source.length === LANDMARK_COUNT) {
      const points = source.map(point => Array.isArray(point) ?
        [Number(point[0]), Number(point[1])] :
        [Number(point.x), Number(point.y)]);
      if (points.every(point =>
        Number.isFinite(point[0]) && Number.isFinite(point[1]))) {
        return { mesh: {
          confidence: Number(data.confidence) || 0,
          points
        }};
      }
    }
    const fields = ["x", "y", "width", "height", "roll", "confidence"];
    if (fields.every(key => Number.isFinite(Number(data[key]))))
      return { face: Object.fromEntries(
        fields.map(key => [key, Number(data[key])])) };
    return {};
  }

  async function run(onProgress) {
    const premiere = window.RostoPremiere;
    const ctx = await premiere.context();
    const effect = await premiere.applyEffect(ctx);
    const start = await ctx.item.getStartTime();
    const end = await ctx.item.getEndTime();
    const settings = await ctx.sequence.getSettings();
    const frameRate = settings.getVideoFrameRate();
    const fps = frameRate && frameRate.value ? frameRate.value : 30;
    const frameCount = Math.max(
      1, Math.ceil((end.seconds - start.seconds) * fps));
    const analysisId = Math.floor(Date.now() % 2000000000);
    const { storage } = require("uxp");
    const fs = storage.localFileSystem;
    try {
      await fs.createEntryWithUrl("file:/private/tmp/RostoNatural",
        { type: storage.types.folder });
    } catch (_) { /* Parent already exists. */ }
    const folder = await fs.createEntryWithUrl(
      `file:/private/tmp/RostoNatural/${analysisId}`,
      { type: storage.types.folder });
    await premiere.setValues(ctx, effect, {
      analysisId, captureMode: true, cacheVersion: MESH_VERSION
    }, "Preparar análise facial");

    const captures = [];
    let mode = null;
    try {
      for (let index = 0; index < frameCount; index++) {
        const seconds = start.seconds + index / fps;
        const time = ctx.ppro.TickTime.createWithSeconds(seconds);
        const filename = `frame-${String(index).padStart(6, "0")}.jpg`;
        const ok = await ctx.ppro.Exporter.exportSequenceFrame(
          ctx.sequence, time, filename, `${folder.nativePath}/`, 640, 360);
        if (!ok)
          throw new Error(`Falha ao analisar quadro ${index + 1}.`);
        const capture = await folder.getEntry("capture.json");
        const parsed = parseCapture(JSON.parse(await capture.read()));
        try {
          const exportedFrame = await folder.getEntry(filename);
          await exportedFrame.delete();
        } catch (_) { /* Temporary frame cleanup is best-effort. */ }
        const detectedMode =
          parsed.mesh && parsed.mesh.confidence > .35 ? "mesh" :
          (parsed.face && parsed.face.confidence > .35 ?
            "geometry" : null);
        if (!mode && detectedMode) mode = detectedMode;
        if (mode && detectedMode && mode !== detectedMode)
          throw new Error("O detector mudou de formato durante a análise.");
        captures.push({
          seconds: index / fps,
          mesh: mode === "mesh" && parsed.mesh.confidence > .35 ?
            parsed.mesh : null,
          face: mode === "geometry" && parsed.face.confidence > .35 ?
            parsed.face : null
        });
        onProgress((index + 1) / frameCount);
      }
      const detectedCount = captures.filter(frame =>
        mode === "mesh" ? frame.mesh : frame.face).length;
      if (!mode || !detectedCount)
        throw new Error("Nenhum rosto foi detectado.");
      const stable = mode === "mesh" ?
        stabilizeMeshes(captures) : stabilizeGeometry(captures);
      const encoded = mode === "mesh" ?
        encodeMeshes(stable) : encodeGeometry(stable);
      const version = mode === "mesh" ? MESH_VERSION : GEOMETRY_VERSION;
      const bin = await folder.createFile(
        "landmarks.bin", { overwrite: true });
      await bin.write(encoded, { format: storage.formats.binary });
      const metadata = await folder.createFile(
        "metadata.json", { overwrite: true });
      await metadata.write(JSON.stringify({
        schema: "com.lucas.RostoNatural.analysis",
        version, mode, analysisId,
        sequenceGuid: ctx.sequence.guid.toString(),
        fps, frameCount, createdAt: new Date().toISOString(),
        smoothing: {
          medianWindowFrames: 5,
          halfLifeMilliseconds: 80,
          adaptiveMotionRelease: true,
          maxGapFrames: 8
        }
      }, null, 2));
      // ANALYSIS_ID is a numeric native-effect slider. Keeping the original
      // number here is essential; passing the filesystem path is a type error.
      await premiere.setValues(ctx, effect, {
        analysisId, captureMode: false, cacheVersion: version
      }, "Usar análise facial");
      return {
        frameCount, detectedCount, fps,
        nativePath: folder.nativePath, mode
      };
    } catch (error) {
      await premiere.setValues(ctx, effect, { captureMode: false },
        "Cancelar análise facial");
      throw error;
    }
  }

  const api = {
    GEOMETRY_VERSION, MESH_VERSION, LANDMARK_COUNT,
    interpolateGeometryGaps, interpolateMeshGaps,
    stabilizeGeometry, stabilizeMeshes,
    encodeGeometry, encodeMeshes, parseCapture, run
  };
  if (typeof window !== "undefined") window.RostoAnalysis = api;
  if (typeof module !== "undefined") module.exports = api;
})();
