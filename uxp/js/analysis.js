(() => {
  const GEOMETRY_VERSION = 1;
  const MESH_VERSION = 2;
  const LANDMARK_COUNT = 478;
  const MAX_GAP = 8;
  const MEDIAN_RADIUS = 2;
  const HALF_LIFE = .080;
  const MAX_ANALYSIS_FRAMES = 7200;
  const CAPTURE_ROOT_NATIVE_PATH = "/private/tmp/RostoNatural";

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
    if (!data || typeof data !== "object" || Array.isArray(data)) return {};
    const version = Number(data.version);
    const timing = {
      version,
      captureToken: Number(data.captureToken),
      hostTime: Number(data.hostTime),
      hostTimeScale: Number(data.hostTimeScale),
      renderWidth: Number(data.renderWidth),
      renderHeight: Number(data.renderHeight),
      quality: Number(data.quality),
      highQuality: data.highQuality,
      field: Number(data.field)
    };
    if (!Number.isSafeInteger(timing.captureToken) ||
        timing.captureToken <= 0 ||
        !Number.isSafeInteger(timing.hostTime) ||
        !Number.isSafeInteger(timing.hostTimeScale) ||
        timing.hostTimeScale <= 0 ||
        !Number.isInteger(timing.renderWidth) || timing.renderWidth <= 0 ||
        !Number.isInteger(timing.renderHeight) || timing.renderHeight <= 0 ||
        !Number.isSafeInteger(timing.quality) ||
        typeof timing.highQuality !== "boolean" ||
        !Number.isSafeInteger(timing.field))
      return {};
    const source = Array.isArray(data.points) ? data.points :
      (Array.isArray(data.landmarks) ? data.landmarks : null);
    if (version === MESH_VERSION &&
        source && source.length === LANDMARK_COUNT) {
      const points = source.map(point => {
        if (Array.isArray(point))
          return [Number(point[0]), Number(point[1])];
        if (point && typeof point === "object")
          return [Number(point.x), Number(point.y)];
        return [NaN, NaN];
      });
      const confidence = Number(data.confidence);
      if (points.every(point =>
        Number.isFinite(point[0]) && Number.isFinite(point[1])) &&
          Number.isFinite(confidence)) {
        return { ...timing, mesh: {
          confidence,
          points
        }};
      }
    }
    const fields = ["x", "y", "width", "height", "roll", "confidence"];
    if (version === GEOMETRY_VERSION &&
        fields.every(key => Number.isFinite(Number(data[key]))))
      return { ...timing, face: Object.fromEntries(
        fields.map(key => [key, Number(data[key])])) };
    return {};
  }

  function analysisDimensions(width, height, maximumSide = 640) {
    if (!(width > 0) || !(height > 0) || !(maximumSide > 0))
      throw new Error("Dimensões inválidas para análise.");
    const scale = Math.min(1, maximumSide / Math.max(width, height));
    const even = value => Math.max(2, Math.round(value * scale / 2) * 2);
    return { width: even(width), height: even(height) };
  }

  function frameCountFromTicks(durationTicks, ticksPerFrame) {
    if (!(durationTicks >= 0) || !(ticksPerFrame > 0))
      throw new Error("Base de tempo inválida para análise.");
    return Math.max(1, Math.round(durationTicks / ticksPerFrame));
  }

  function capturesInSequenceOrder(input, mode) {
    if (mode !== "mesh" && mode !== "geometry")
      throw new Error("Formato de captura inválido.");
    const payload = mode === "mesh" ? "mesh" : "face";
    const byHostTime = new Map();
    for (const capture of input) {
      if (!Number.isFinite(capture.sequenceSeconds)) continue;
      const hostSeconds = capture.hostTime / capture.hostTimeScale;
      if (!Number.isFinite(hostSeconds)) continue;
      const key = hostSeconds.toFixed(12);
      const confidence = capture[payload] ?
        Number(capture[payload].confidence) || 0 : 0;
      const previous = byHostTime.get(key);
      const previousConfidence = previous && previous[payload] ?
        Number(previous[payload].confidence) || 0 : -1;
      const area = capture.renderWidth * capture.renderHeight;
      const previousArea = previous ?
        previous.renderWidth * previous.renderHeight : -1;
      const preferred = !previous ||
        (capture.highQuality && !previous.highQuality) ||
        (capture.highQuality === previous.highQuality &&
          (area > previousArea ||
           (area === previousArea && confidence > previousConfidence)));
      if (preferred) {
        byHostTime.set(key, {
          sequenceSeconds: capture.sequenceSeconds,
          hostSeconds,
          captureToken: capture.captureToken,
          hostTime: capture.hostTime,
          hostTimeScale: capture.hostTimeScale,
          renderWidth: capture.renderWidth,
          renderHeight: capture.renderHeight,
          quality: capture.quality,
          highQuality: capture.highQuality,
          field: capture.field,
          mesh: mode === "mesh" && confidence > .35 ?
            capture.mesh : null,
          face: mode === "geometry" && confidence > .35 ?
            capture.face : null
        });
      }
    }
    return Array.from(byHostTime.values())
      .sort((a, b) => a.sequenceSeconds - b.sequenceSeconds)
      .map(frame => ({ ...frame, seconds: frame.sequenceSeconds }));
  }

  function validateCaptureResponses(captures, issuedTokens) {
    const responses = new Map();
    for (const capture of captures) {
      if (!issuedTokens.has(capture.captureToken)) continue;
      if (!responses.has(capture.captureToken))
        responses.set(capture.captureToken, new Set());
      responses.get(capture.captureToken).add(
        (capture.hostTime / capture.hostTimeScale).toFixed(12));
    }
    const missingTokens = [];
    const ambiguousTokens = [];
    for (const [token, requestValue] of issuedTokens.entries()) {
      const times = responses.get(token);
      if (!times) {
        missingTokens.push(token);
        continue;
      }
      if (times.size !== 1) {
        ambiguousTokens.push(token);
        continue;
      }
      const request = typeof requestValue === "number" ?
        { sequenceSeconds: requestValue } : requestValue;
      if (request && Number.isFinite(request.expectedHostSeconds)) {
        const observedHostSeconds = Number(times.values().next().value);
        const tolerance = Number.isFinite(request.hostToleranceSeconds) ?
          Math.max(0, request.hostToleranceSeconds) : 1e-6;
        if (Math.abs(observedHostSeconds - request.expectedHostSeconds) >
            tolerance)
          throw new Error(
            "O clipe usa remapeamento temporal incompatível com a análise.");
      }
    }
    if (missingTokens.length || ambiguousTokens.length)
      throw new Error(
        "O Premiere retornou capturas ausentes ou fora de ordem " +
        `(${responses.size}/${issuedTokens.size}; ` +
        `${missingTokens.length} ausentes; ` +
        `${ambiguousTokens.length} ambíguas).`);
  }

  function framesInHostOrder(frames) {
    return frames.map(frame => ({ ...frame, seconds: frame.hostSeconds }))
      .sort((a, b) => a.seconds - b.seconds);
  }

  function normalizedScratchPath(value) {
    if (typeof value !== "string" || !value.length || value.includes("\0"))
      return null;
    return value.length > 1 && value.endsWith("/") ?
      value.slice(0, -1) : value;
  }

  async function secureScratchEntry(entry, expectedPath, kind, pathFs) {
    const nativePath = normalizedScratchPath(entry && entry.nativePath);
    if (!nativePath || nativePath !== expectedPath || !pathFs ||
        typeof pathFs.lstat !== "function") return false;
    if (kind === "folder" && !entry.isFolder) return false;
    if (kind === "file" && !entry.isFile) return false;
    let stats;
    try { stats = await pathFs.lstat(nativePath); }
    catch (_) { return false; }
    if (!stats) return false;
    let symbolicLink = null;
    if (typeof stats.isSymbolicLink === "function") {
      try {
        const reported = stats.isSymbolicLink();
        if (typeof reported === "boolean") symbolicLink = reported;
      } catch (_) { /* POSIX mode remains an independent fallback. */ }
    }
    if (Number.isInteger(stats.mode)) {
      const modeType = stats.mode & 0o170000;
      const expectedType = kind === "folder" ? 0o040000 : 0o100000;
      if (modeType !== expectedType || symbolicLink === true) return false;
    } else if (symbolicLink !== false) return false;
    if (kind === "folder")
      return typeof stats.isDirectory === "function" && stats.isDirectory();
    return typeof stats.isFile === "function" && stats.isFile();
  }

  function isMissingScratchEntryError(error) {
    const code = String(error && error.code || "").toUpperCase();
    const message = String(error && error.message || error || "");
    return code === "ENOENT" ||
      /no such file|not found|could not find an entry/i.test(message);
  }

  async function getScratchEntriesWithRetry(folder, attempts = 8) {
    if (!folder || typeof folder.getEntries !== "function" ||
        !Number.isInteger(attempts) || attempts <= 0)
      throw new Error("Pasta temporária de análise inválida.");
    let lastError;
    for (let attempt = 0; attempt < attempts; attempt++) {
      try {
        const entries = await folder.getEntries();
        if (!Array.isArray(entries))
          throw new Error("O Premiere retornou uma listagem temporária inválida.");
        return entries;
      } catch (error) {
        if (!isMissingScratchEntryError(error)) throw error;
        lastError = error;
      }
      if (attempt + 1 < attempts)
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    throw lastError || new Error("Falha ao listar capturas temporárias.");
  }

  async function getStableScratchEntries(folder, attempts = 8) {
    let previousSignature = null;
    for (let attempt = 0; attempt < attempts; attempt++) {
      try {
        const entries = await getScratchEntriesWithRetry(folder);
        const signature = entries.map(entry => entry && entry.name || "")
          .sort().join("\0");
        if (previousSignature === signature) return entries;
        previousSignature = signature;
      }
      catch (error) {
        if (!isMissingScratchEntryError(error)) throw error;
        previousSignature = null;
      }
      if (attempt + 1 < attempts)
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    throw new Error("A listagem das capturas temporárias não estabilizou.");
  }

  async function removeOptionalExportedFrame(
      folder, sessionPath, pathFs, name) {
    if (!/^frame-\d{6}\.jpg$/.test(name))
      throw new Error("Nome de quadro temporário inválido.");
    let entry;
    try { entry = await folder.getEntry(name); }
    catch (error) {
      if (isMissingScratchEntryError(error)) return false;
      throw error;
    }
    if (!await secureScratchEntry(
      entry, `${sessionPath}/${name}`, "file", pathFs))
      throw new Error("Quadro temporário de análise inseguro.");
    await entry.delete();
    return true;
  }

  async function removeExportedFrameArtifacts(
      folder, sessionPath, pathFs, expectedNames,
      attempts = 12, requiredQuietPasses = 3) {
    if (!await secureScratchEntry(folder, sessionPath, "folder", pathFs) ||
        !Array.isArray(expectedNames) ||
        !Number.isInteger(attempts) || attempts <= 0 ||
        !Number.isInteger(requiredQuietPasses) || requiredQuietPasses <= 0)
      throw new Error("Sessão temporária de análise inválida.");
    const expected = new Set(expectedNames);
    if (expected.size !== expectedNames.length ||
        expectedNames.some(name => !/^frame-\d{6}\.jpg$/.test(name)))
      throw new Error("Lista de quadros temporários inválida.");
    let quietPasses = 0;
    let removed = 0;
    for (let attempt = 0; attempt < attempts; attempt++) {
      const entries = await getStableScratchEntries(folder);
      const frames = entries.filter(entry =>
        expected.has(entry && entry.name || ""));
      for (const frame of frames) {
        if (!await secureScratchEntry(
          frame, `${sessionPath}/${frame.name}`, "file", pathFs))
          throw new Error("Quadro temporário de análise inseguro.");
        await frame.delete();
        removed++;
      }
      quietPasses = frames.length ? 0 : quietPasses + 1;
      if (quietPasses >= requiredQuietPasses) return removed;
      if (attempt + 1 < attempts)
        await new Promise(resolve => setTimeout(resolve, 25));
    }
    throw new Error("O Premiere não concluiu a limpeza dos quadros temporários.");
  }

  async function assertScratchContents(
      folder, sessionPath, pathFs, expectedNames) {
    if (!await secureScratchEntry(folder, sessionPath, "folder", pathFs) ||
        !Array.isArray(expectedNames))
      throw new Error("Sessão temporária de análise inválida.");
    const expected = new Set(expectedNames);
    if (expected.size !== expectedNames.length || expectedNames.some(name =>
      !expectedScratchFileName(name, true)))
      throw new Error("Conteúdo esperado da análise inválido.");
    const entries = await getStableScratchEntries(folder);
    if (entries.length !== expected.size)
      throw new Error("A pasta da análise contém arquivos temporários.");
    for (const entry of entries) {
      const name = entry && entry.name || "";
      if (!expected.has(name) || !await secureScratchEntry(
        entry, `${sessionPath}/${name}`, "file", pathFs))
        throw new Error("A pasta da análise contém arquivo inseguro.");
    }
    return true;
  }

  function expectedScratchFileName(name, allowMetadata = false) {
    return name === "capture.lease" || name === "session.marker" ||
      name === "landmarks.bin" ||
      (allowMetadata && name === "metadata.json") ||
      /^frame-\d{6}\.jpg$/.test(name) ||
      /^capture-\d+--?\d+-\d+-\d+x\d+-q\d+-f0-p\d+-s\d+\.(?:json|pending)$/
        .test(name);
  }

  async function deleteValidatedScratchFolder(
      entry, pathFs, allowMetadata = false) {
    const sessionName = entry && entry.name || "";
    if (!/^\d{1,10}$/.test(sessionName)) return false;
    const sessionPath = `${CAPTURE_ROOT_NATIVE_PATH}/${sessionName}`;
    if (!await secureScratchEntry(entry, sessionPath, "folder", pathFs) ||
        typeof entry.getEntries !== "function") return false;
    const children = await entry.getEntries();
    if (!children.length) return false;
    for (const child of children) {
      const name = child && child.name || "";
      if (!expectedScratchFileName(name, allowMetadata) ||
          !await secureScratchEntry(
            child, `${sessionPath}/${name}`, "file", pathFs)) return false;
    }
    // Keep ownership markers until every data file has been removed. If a
    // deletion fails, a future pass can still identify and retry the session.
    const markerNames = new Set(["capture.lease", "session.marker"]);
    let deletionStarted = false;
    async function restoreRecoveryMarker() {
      try {
        if (!await secureScratchEntry(
          entry, sessionPath, "folder", pathFs) ||
          typeof entry.createFile !== "function") return;
        try {
          const existing = await entry.getEntry("session.marker");
          if (await secureScratchEntry(
            existing, `${sessionPath}/session.marker`, "file", pathFs))
            return;
          return;
        } catch (_) { /* Marker is absent and may be recreated safely. */ }
        const recovery = await entry.createFile(
          "session.marker", { overwrite: false });
        // UXP materializes a createFile Entry only on its first write.
        await recovery.write(JSON.stringify({
          schema: "com.lucas.RostoNatural.captureSession",
          session: Number(sessionName),
          refreshedAt: new Date().toISOString(),
          recoveredAfterCleanupFailure: true
        }));
        if (!await secureScratchEntry(
          recovery, `${sessionPath}/session.marker`, "file", pathFs)) return;
      } catch (_) { /* Best-effort retry marker; never broaden deletion. */ }
    }
    try {
      deletionStarted = true;
      for (const child of children) {
        if (!markerNames.has(child.name)) await child.delete();
      }
      const markers = await entry.getEntries();
      if (!markers.length || markers.some(child =>
          !markerNames.has(child.name)))
        throw new Error("A sessão mudou durante a limpeza.");
      for (const child of markers) {
        if (!await secureScratchEntry(
          child, `${sessionPath}/${child.name}`, "file", pathFs))
          throw new Error("Marcador da sessão mudou durante a limpeza.");
      }
      // The native lease is removed before the durable panel marker.
      for (const name of ["capture.lease", "session.marker"]) {
        const marker = markers.find(child => child.name === name);
        if (marker) await marker.delete();
      }
      if ((await entry.getEntries()).length)
        throw new Error("A sessão recebeu arquivos durante a limpeza.");
      await entry.delete();
      return true;
    } catch (error) {
      if (deletionStarted) await restoreRecoveryMarker();
      throw error;
    }
  }

  async function scavengeExpiredCaptureSessions(
      rootFolder, nowMilliseconds = Date.now(), staleSeconds = 30,
      pathFileSystem = null) {
    if (!rootFolder || typeof rootFolder.getEntries !== "function") return 0;
    const pathFs = pathFileSystem || require("fs");
    if (!await secureScratchEntry(rootFolder, CAPTURE_ROOT_NATIVE_PATH,
                                  "folder", pathFs)) return 0;
    let removed = 0;
    for (const entry of await rootFolder.getEntries()) {
      if (!entry || !entry.isFolder || !/^\d{1,10}$/.test(entry.name || ""))
        continue;
      const sessionPath = `${CAPTURE_ROOT_NATIVE_PATH}/${entry.name}`;
      if (!await secureScratchEntry(entry, sessionPath, "folder", pathFs))
        continue;
      const safeMarkerTimes = [];
      const validMarkerTimes = [];
      let markerAgeUnknown = false;
      for (const candidate of [
        ["capture.lease", "com.lucas.RostoNatural.captureLease"],
        ["session.marker", "com.lucas.RostoNatural.captureSession"]
      ]) {
        let marker;
        try { marker = await entry.getEntry(candidate[0]); }
        catch (_) { continue; }
        if (!await secureScratchEntry(
          marker, `${sessionPath}/${candidate[0]}`, "file", pathFs)) continue;
        let modified;
        try {
          const metadata = await marker.getMetadata();
          modified = new Date(metadata.dateModified).getTime();
        } catch (_) {
          markerAgeUnknown = true;
          continue;
        }
        if (!Number.isFinite(modified)) {
          markerAgeUnknown = true;
          continue;
        }
        safeMarkerTimes.push(modified);
        try {
          const payload = JSON.parse(await marker.read());
          if (payload && payload.schema === candidate[1] &&
              Number(payload.session) === Number(entry.name))
            validMarkerTimes.push(modified);
        } catch (_) { /* A known-fresh invalid lease still blocks above. */ }
      }
      // A fresh lease blocks cleanup even if a concurrent write was torn: the
      // native side intentionally treats freshness, not JSON, as authority.
      if (markerAgeUnknown || safeMarkerTimes.some(modified =>
          nowMilliseconds - modified <= staleSeconds * 1000) ||
          !validMarkerTimes.length || validMarkerTimes.some(modified =>
          nowMilliseconds - modified <= staleSeconds * 1000)) continue;
      // A completed cache is never scavenged, even if an exceptional lease
      // deletion failed after metadata was committed.
      try {
        await entry.getEntry("metadata.json");
        continue;
      } catch (_) { /* Incomplete capture session. */ }
      try {
        if (await deleteValidatedScratchFolder(entry, pathFs)) removed++;
      } catch (_) { /* Best-effort cleanup in the private temp namespace. */ }
    }
    return removed;
  }

  async function run(onProgress) {
    const premiere = window.RostoPremiere;
    const ctx = await premiere.context();
    const effect = await premiere.applyEffect(ctx);
    const start = await ctx.item.getStartTime();
    const end = await ctx.item.getEndTime();
    const inPoint = await ctx.item.getInPoint();
    const outPoint = await ctx.item.getOutPoint();
    const speed = Number(await ctx.item.getSpeed());
    const reversed = Boolean(await ctx.item.isSpeedReversed());
    if (!Number.isFinite(speed) || speed <= 0)
      throw new Error("A velocidade do clipe não é compatível com a análise.");
    const settings = await ctx.sequence.getSettings();
    const frameRate = settings.getVideoFrameRate();
    const fps = frameRate && frameRate.value ? frameRate.value : 30;
    const frameRect = await settings.getVideoFrameRect();
    const pixelAspectRatio = await settings.getVideoPixelAspectRatio();
    const squarePixelAspectRatio = ctx.ppro.Constants &&
      ctx.ppro.Constants.PixelAspectRatio ?
      ctx.ppro.Constants.PixelAspectRatio.SQUARE : null;
    if (squarePixelAspectRatio &&
        pixelAspectRatio !== squarePixelAspectRatio)
      throw new Error(
        "A análise facial exige uma sequência com pixels quadrados.");
    const videoFieldType = await settings.getVideoFieldType();
    const progressive = ctx.ppro.Constants &&
      ctx.ppro.Constants.VideoFieldType ?
      ctx.ppro.Constants.VideoFieldType.PROGRESSIVE : undefined;
    if (typeof progressive !== "number" || videoFieldType !== progressive)
      throw new Error(
        "A análise facial exige uma sequência de varredura progressiva.");
    const analysisSize = analysisDimensions(
      frameRect.width, frameRect.height);
    const duration = end.subtract(start);
    const frameDuration = ctx.ppro.TickTime.createWithFrameAndFrameRate(
      1, frameRate);
    const sourceSpan = Math.abs(outPoint.subtract(inPoint).seconds);
    const expectedSourceSpan = Math.abs(duration.seconds * speed);
    const speedTolerance = Math.max(frameDuration.seconds * speed * 2, .001);
    if (Math.abs(sourceSpan - expectedSourceSpan) > speedTolerance)
      throw new Error(
        "A análise ainda não suporta rampas de velocidade no clipe.");
    const requestedFrameCount = frameCountFromTicks(
      duration.ticksNumber, frameRate.ticksPerFrame);
    if (requestedFrameCount > MAX_ANALYSIS_FRAMES)
      throw new Error(
        "Divida o clipe: a análise aceita até 7.200 quadros por execução.");
    const maximumToken = 1999999999;
    const analysisId = Math.floor(Math.random() * maximumToken) + 1;
    const tokenSeed = Math.floor(Math.random() * maximumToken) + 1;
    const { storage } = require("uxp");
    const pathFs = require("fs");
    const fs = storage.localFileSystem;
    let rootFolder;
    try {
      rootFolder = await fs.createEntryWithUrl("file:/private/tmp/RostoNatural",
        { type: storage.types.folder });
    } catch (_) {
      rootFolder = await fs.getEntryWithUrl("file:/private/tmp/RostoNatural");
    }
    if (!await secureScratchEntry(rootFolder, CAPTURE_ROOT_NATIVE_PATH,
                                  "folder", pathFs))
      throw new Error("Diretório temporário de análise inseguro.");
    await scavengeExpiredCaptureSessions(
      rootFolder, Date.now(), 30, pathFs);
    const folder = await fs.createEntryWithUrl(
      `file:/private/tmp/RostoNatural/${analysisId}`,
      { type: storage.types.folder });
    const sessionPath = `${CAPTURE_ROOT_NATIVE_PATH}/${analysisId}`;
    if (!await secureScratchEntry(folder, sessionPath, "folder", pathFs))
      throw new Error("Sessão temporária de análise insegura.");
    let sessionMarkerEntry = await folder.createFile(
      "session.marker", { overwrite: true });
    async function refreshSessionMarker() {
      await sessionMarkerEntry.write(JSON.stringify({
        schema: "com.lucas.RostoNatural.captureSession",
        session: analysisId,
        refreshedAt: new Date().toISOString()
      }));
    }
    await refreshSessionMarker();
    if (!await secureScratchEntry(
      sessionMarkerEntry, `${sessionPath}/session.marker`, "file", pathFs))
      throw new Error("Marcador temporário de análise inseguro.");
    let leaseEntry = null;
    async function refreshCaptureLease() {
      if (!leaseEntry)
        leaseEntry = await folder.createFile(
          "capture.lease", { overwrite: true });
      await leaseEntry.write(JSON.stringify({
        schema: "com.lucas.RostoNatural.captureLease",
        session: analysisId,
        refreshedAt: new Date().toISOString()
      }));
    }
    async function removeCaptureLease() {
      if (!leaseEntry) return;
      try { await leaseEntry.delete(); } catch (_) { /* Already gone. */ }
      leaseEntry = null;
    }
    let leaseHeartbeatTimer = null;
    let leaseHeartbeatChain = Promise.resolve();
    let leaseHeartbeatError = null;
    function startCaptureLeaseHeartbeat() {
      if (leaseHeartbeatTimer !== null) return;
      leaseHeartbeatTimer = setInterval(() => {
        leaseHeartbeatChain = leaseHeartbeatChain.then(async () => {
          if (leaseHeartbeatTimer !== null) await refreshCaptureLease();
        }).catch(error => {
          if (!leaseHeartbeatError) leaseHeartbeatError = error;
        });
      }, 2000);
    }
    async function stopCaptureLeaseHeartbeat() {
      if (leaseHeartbeatTimer !== null) {
        clearInterval(leaseHeartbeatTimer);
        leaseHeartbeatTimer = null;
      }
      await leaseHeartbeatChain;
      if (leaseHeartbeatError) {
        const error = leaseHeartbeatError;
        leaseHeartbeatError = null;
        throw error;
      }
    }
    async function removeFailedAnalysisFolder() {
      try {
        await deleteValidatedScratchFolder(folder, pathFs, true);
      } catch (_) { /* Lease expiry still stops native publication. */ }
    }
    const issuedTokens = new Map();
    const schedule = [];
    const exportedFrameNames = [];
    for (let index = 0; index < requestedFrameCount; index++) {
      const offset = ctx.ppro.TickTime.createWithFrameAndFrameRate(
        index, frameRate);
      const sequenceTime = start.add(offset);
      // Component keyframes use project-item time. Account for trim, constant
      // speed and reverse playback so the native parameter value follows
      // PF_InData.current_time even when Premiere renders speculatively.
      const sourceOffset = reversed ?
        offset.add(frameDuration).multiply(speed) : offset.multiply(speed);
      const parameterTime = reversed ?
        outPoint.subtract(sourceOffset) : inPoint.add(sourceOffset);
      const captureToken = (tokenSeed + index) % maximumToken + 1;
      issuedTokens.set(captureToken, {
        sequenceSeconds: sequenceTime.seconds,
        expectedHostSeconds: parameterTime.seconds,
        hostToleranceSeconds: Math.max(
          frameDuration.seconds * speed * .25, 1e-6)
      });
      schedule.push({
        index, sequenceTime, parameterTime, captureToken
      });
      exportedFrameNames.push(
        `frame-${String(index).padStart(6, "0")}.jpg`);
    }
    const rawCaptures = [];
    const consumedCaptureNames = new Set();
    async function drainCaptures() {
      const parsedCaptures = [];
      const entries = await getScratchEntriesWithRetry(folder);
      for (const entry of entries) {
        const name = entry && entry.name || "";
        const match =
          /^capture-(\d+)--?\d+-\d+-\d+x\d+-q\d+-f0-p\d+-s\d+\.json$/
            .exec(name);
        if (!match) continue;
        const filenameToken = Number(match[1]);
        if (!issuedTokens.has(filenameToken))
          throw new Error("Arquivo de captura com token inesperado.");
        if (!await secureScratchEntry(
          entry, `${sessionPath}/${name}`, "file", pathFs))
          throw new Error("Arquivo temporário de captura inseguro.");
        if (consumedCaptureNames.has(name)) {
          try { await entry.delete(); } catch (_) { /* Retried on next drain. */ }
          continue;
        }
        const parsed = parseCapture(JSON.parse(await entry.read()));
        if (parsed.captureToken !== filenameToken)
          throw new Error("Resposta nativa com token de captura inválido.");
        consumedCaptureNames.add(name);
        try { await entry.delete(); } catch (_) { /* Retried on next drain. */ }
        const expectedAspect = frameRect.width / frameRect.height;
        const renderAspect = parsed.renderWidth / parsed.renderHeight;
        if (!Number.isFinite(renderAspect) ||
            Math.abs(renderAspect / expectedAspect - 1) > .01)
          continue;
        parsed.sequenceSeconds =
          issuedTokens.get(filenameToken).sequenceSeconds;
        parsedCaptures.push(parsed);
      }
      return parsedCaptures;
    }

    let captureModeArmed = false;
    let tokensArmed = false;
    let sessionArmed = false;
    let analysisCommitted = false;
    try {
      sessionArmed = true;
      await premiere.setValues(ctx, effect, {
        captureSession: analysisId, captureMode: false
      }, "Preparar sessão de análise facial");
      tokensArmed = true;
      await premiere.armCaptureTokens(ctx, effect, schedule.map(entry => ({
        time: entry.parameterTime, token: entry.captureToken
      })));
      await refreshCaptureLease();
      captureModeArmed = true;
      await premiere.setValues(ctx, effect, { captureMode: true },
        "Ativar captura facial");
      startCaptureLeaseHeartbeat();
      for (const entry of schedule) {
        await refreshCaptureLease();
        const filename =
          `frame-${String(entry.index).padStart(6, "0")}.jpg`;
        let ok = false;
        try {
          ok = await ctx.ppro.Exporter.exportSequenceFrame(
            ctx.sequence, entry.sequenceTime, filename,
            `${folder.nativePath}/`,
            analysisSize.width, analysisSize.height);
        } finally {
          await removeOptionalExportedFrame(
            folder, sessionPath, pathFs, filename);
        }
        if (!ok)
          throw new Error(`Falha ao analisar quadro ${entry.index + 1}.`);
        rawCaptures.push(...await drainCaptures());
        if (typeof onProgress === "function")
          onProgress((entry.index + 1) / requestedFrameCount);
      }
      // Keep the short native lease alive while already-requested renders
      // settle. A panel unload/crash stops refreshing it, so publication
      // becomes inert automatically even if project parameters cannot reset.
      let quietPolls = 0;
      let captureRenderSettled = false;
      for (let attempt = 0; attempt < 300; attempt++) {
        await refreshCaptureLease();
        await new Promise(resolve => setTimeout(resolve, 50));
        const drained = await drainCaptures();
        rawCaptures.push(...drained);
        quietPolls = drained.length ? 0 : quietPolls + 1;
        const covered = new Set(rawCaptures.map(capture =>
          capture.captureToken)).size >= issuedTokens.size;
        if (covered && quietPolls >= 2 &&
            await ctx.sequence.isDoneAnalyzingForVideoEffects()) {
          captureRenderSettled = true;
          break;
        }
      }
      if (!captureRenderSettled)
        throw new Error(
          "O Premiere não concluiu as renderizações da análise a tempo.");
      await stopCaptureLeaseHeartbeat();
      await premiere.setValues(ctx, effect, {
        captureMode: false, captureSession: 0
      }, "Finalizar capturas faciais");
      captureModeArmed = false;
      sessionArmed = false;
      // Let renders that already captured the old parameter snapshot finish
      // while the lease is still valid, then close publication and drain once
      // more to cover the final check/rename window in the native side.
      let closedRenderSettled = false;
      for (let attempt = 0; attempt < 40; attempt++) {
        await new Promise(resolve => setTimeout(resolve, 50));
        const drained = await drainCaptures();
        rawCaptures.push(...drained);
        if (!drained.length &&
            await ctx.sequence.isDoneAnalyzingForVideoEffects()) {
          closedRenderSettled = true;
          break;
        }
      }
      if (!closedRenderSettled)
        throw new Error(
          "O Premiere não encerrou as capturas faciais a tempo.");
      await refreshSessionMarker();
      await removeCaptureLease();
      let closedQuietPolls = 0;
      for (let attempt = 0; attempt < 20; attempt++) {
        await new Promise(resolve => setTimeout(resolve, 50));
        const drained = await drainCaptures();
        rawCaptures.push(...drained);
        const entries = await getStableScratchEntries(folder);
        const pending = entries.some(entry =>
          /^capture-.*\.(?:json|pending)$/.test(entry && entry.name || ""));
        closedQuietPolls = !drained.length && !pending &&
          await ctx.sequence.isDoneAnalyzingForVideoEffects() ?
          closedQuietPolls + 1 : 0;
        if (closedQuietPolls >= 3) break;
      }
      if (closedQuietPolls < 3)
        throw new Error(
          "O Premiere não estabilizou as capturas faciais a tempo.");
      await premiere.resetCaptureTokens(
        ctx, effect, schedule[0].parameterTime, "Finalizar tokens de captura");
      tokensArmed = false;
      await removeExportedFrameArtifacts(
        folder, sessionPath, pathFs, exportedFrameNames);
      await assertScratchContents(
        folder, sessionPath, pathFs, ["session.marker"]);
      if (!rawCaptures.length)
        throw new Error("O efeito nativo não respondeu à análise facial.");
      validateCaptureResponses(rawCaptures, issuedTokens);
      const detectedModes = new Set(rawCaptures.map(capture =>
        capture.mesh && capture.mesh.confidence > .35 ? "mesh" :
        (capture.face && capture.face.confidence > .35 ?
          "geometry" : null)).filter(Boolean));
      if (detectedModes.size > 1)
        throw new Error("O detector mudou de formato durante a análise.");
      const mode = detectedModes.values().next().value || null;
      if (!mode) throw new Error("Nenhum rosto foi detectado.");
      const sequenceFrames = capturesInSequenceOrder(rawCaptures, mode);
      const detectedCount = sequenceFrames.filter(frame =>
        mode === "mesh" ? frame.mesh : frame.face).length;
      const stable = mode === "mesh" ?
        stabilizeMeshes(sequenceFrames) : stabilizeGeometry(sequenceFrames);
      const hostFrames = framesInHostOrder(stable);
      const encoded = mode === "mesh" ?
        encodeMeshes(hostFrames) : encodeGeometry(hostFrames);
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
        fps, frameCount: hostFrames.length, requestedFrameCount,
        pixelAspectRatio, createdAt: new Date().toISOString(),
        captureProtocol: {
          version: 1,
          requestParameter: "_Capture Token",
          sessionParameter: "_Capture Session",
          expiringLeaseSeconds: 10,
          keyframedHoldSchedule: true,
          exactlyOneHostTimePerToken: true,
          timestamps: "PF_InData.current_time/time_scale"
        },
        hostTimeSamples: sequenceFrames.slice(0, 8).map(frame => ({
          sequenceSeconds: frame.sequenceSeconds,
          hostSeconds: frame.hostSeconds,
          captureToken: frame.captureToken,
          hostTime: frame.hostTime,
          hostTimeScale: frame.hostTimeScale
        })),
        smoothing: {
          medianWindowFrames: 5,
          halfLifeMilliseconds: 80,
          adaptiveMotionRelease: true,
          maxGapFrames: 8
        }
      }, null, 2));
      if (!await secureScratchEntry(
        bin, `${sessionPath}/landmarks.bin`, "file", pathFs) ||
          !await secureScratchEntry(
            metadata, `${sessionPath}/metadata.json`, "file", pathFs))
        throw new Error("Cache facial final inseguro.");
      await sessionMarkerEntry.delete();
      await assertScratchContents(
        folder, sessionPath, pathFs, ["landmarks.bin", "metadata.json"]);
      // ANALYSIS_ID is a numeric native-effect slider. Keeping the original
      // number here is essential; passing the filesystem path is a type error.
      await premiere.setValues(ctx, effect, {
        analysisId, cacheVersion: version,
        captureSession: 0, captureMode: false, captureToken: 0
      }, "Usar análise facial");
      analysisCommitted = true;
      return {
        frameCount: hostFrames.length, requestedFrameCount,
        detectedCount, fps,
        nativePath: folder.nativePath, mode
      };
    } finally {
      try {
        await stopCaptureLeaseHeartbeat();
      } catch (_) { /* Preserve the original analysis error. */ }
      await removeCaptureLease();
      if (captureModeArmed || sessionArmed) {
        try {
          await premiere.setValues(ctx, effect, {
            captureMode: false, captureSession: 0
          }, "Cancelar análise facial");
        } catch (_) { /* Preserve the original analysis error. */ }
      }
      if (tokensArmed) {
        try {
          await premiere.resetCaptureTokens(
            ctx, effect, schedule[0].parameterTime,
            "Cancelar tokens de captura");
        } catch (_) { /* Preserve the original analysis error. */ }
      }
      if (!analysisCommitted) await removeFailedAnalysisFolder();
    }
  }

  const api = {
    GEOMETRY_VERSION, MESH_VERSION, LANDMARK_COUNT,
    interpolateGeometryGaps, interpolateMeshGaps,
    stabilizeGeometry, stabilizeMeshes,
    encodeGeometry, encodeMeshes, parseCapture, analysisDimensions,
    frameCountFromTicks, capturesInSequenceOrder, validateCaptureResponses,
    framesInHostOrder, scavengeExpiredCaptureSessions,
    secureScratchEntry, getScratchEntriesWithRetry, getStableScratchEntries,
    removeOptionalExportedFrame, removeExportedFrameArtifacts,
    assertScratchContents, run
  };
  if (typeof window !== "undefined") window.RostoAnalysis = api;
  if (typeof module !== "undefined") module.exports = api;
})();
