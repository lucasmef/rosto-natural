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
    version: 2, captureToken: 77,
    hostTime: 1234, hostTimeScale: 600,
    renderWidth: 640, renderHeight: 360, quality: 1,
    highQuality: true, field: 0,
    confidence: .9, points
  });
  assert.strictEqual(parsedMesh.mesh.points.length, 478);
  assert.strictEqual(parsedMesh.hostTime, 1234);
  assert.strictEqual(parsedMesh.hostTimeScale, 600);
  const parsedFace = analysis.parseCapture({
    version: 1, captureToken: 78,
    hostTime: 1235, hostTimeScale: 600,
    renderWidth: 640, renderHeight: 360, quality: 1,
    highQuality: true, field: 0,
    x: .5, y: .48, width: .4, height: .55, roll: 0, confidence: .8
  });
  assert.strictEqual(parsedFace.face.width, .4);
  assert.deepStrictEqual(analysis.parseCapture({
    version: 2, captureToken: 79,
    hostTime: 1236, hostTimeScale: 0,
    renderWidth: 640, renderHeight: 360, quality: 1,
    highQuality: true, field: 0,
    confidence: .9, points
  }), {});
  assert.deepStrictEqual(analysis.parseCapture(null), {});
  assert.deepStrictEqual(analysis.parseCapture({
    version: 2, captureToken: 80,
    hostTime: 1237, hostTimeScale: 600,
    renderWidth: 640, renderHeight: 360, quality: 1,
    highQuality: true, field: 0,
    confidence: .9, points: [null, ...points.slice(1)]
  }), {});
  assert.deepStrictEqual(analysis.parseCapture({
    version: 2, captureToken: 81,
    hostTime: Number.MAX_SAFE_INTEGER + 1, hostTimeScale: 600,
    renderWidth: 640, renderHeight: 360, quality: 1,
    highQuality: true, field: 0,
    confidence: .9, points
  }), {});
}

function analysisDimensionsPreserveAspectRatio() {
  assert.deepStrictEqual(
    analysis.analysisDimensions(3840, 2160), { width: 640, height: 360 });
  assert.deepStrictEqual(
    analysis.analysisDimensions(1080, 1920), { width: 360, height: 640 });
  assert.deepStrictEqual(
    analysis.analysisDimensions(1080, 1080), { width: 640, height: 640 });
}

function timingUsesIntegerTicks() {
  assert.strictEqual(analysis.frameCountFromTicks(100 * 1001, 1001), 100);
  assert.strictEqual(analysis.frameCountFromTicks(0, 1001), 1);
  assert.throws(() => analysis.frameCountFromTicks(100, 0));
}

function capturesRemainCorrelatedWhenOutOfOrderOrReversed() {
  const captures = [
    { captureToken: 3, sequenceSeconds: 12, hostTime: 100,
      hostTimeScale: 100, mesh: mesh(.03), renderWidth: 640,
      renderHeight: 360, quality: 1, highQuality: true, field: 0 },
    { captureToken: 1, sequenceSeconds: 10, hostTime: 300,
      hostTimeScale: 100, mesh: mesh(.01), renderWidth: 640,
      renderHeight: 360, quality: 1, highQuality: true, field: 0 },
    { captureToken: 2, sequenceSeconds: 11, hostTime: 200,
      hostTimeScale: 100, mesh: mesh(.02), renderWidth: 640,
      renderHeight: 360, quality: 1, highQuality: true, field: 0 },
    { captureToken: 99, sequenceSeconds: 11.5, hostTime: 200,
      hostTimeScale: 100, mesh: { ...mesh(.2), confidence: .4 },
      renderWidth: 640, renderHeight: 360, quality: 0,
      highQuality: false, field: 0 }
  ];
  const sequence = analysis.capturesInSequenceOrder(captures, "mesh");
  assert.deepStrictEqual(sequence.map(frame => frame.captureToken), [1, 2, 3]);
  assert.deepStrictEqual(sequence.map(frame => frame.hostSeconds), [3, 2, 1]);
  const host = analysis.framesInHostOrder(sequence);
  assert.deepStrictEqual(host.map(frame => frame.seconds), [1, 2, 3]);
  assert.deepStrictEqual(host.map(frame => frame.captureToken), [3, 2, 1]);
  const issued = new Map([
    [1, { sequenceSeconds: 10, expectedHostSeconds: 3,
      hostToleranceSeconds: .001 }],
    [2, { sequenceSeconds: 11, expectedHostSeconds: 2,
      hostToleranceSeconds: .001 }],
    [3, { sequenceSeconds: 12, expectedHostSeconds: 1,
      hostToleranceSeconds: .001 }]
  ]);
  analysis.validateCaptureResponses(captures.slice(0, 3), issued);
  assert.throws(() => analysis.validateCaptureResponses([
    ...captures.slice(0, 3),
    { ...captures[1], hostTime: 250 }
  ], issued));
  assert.throws(() => analysis.validateCaptureResponses(
    captures.slice(0, 2), issued));
}

function multipleRenderVariantsPreferHighQuality() {
  const issued = new Map([[41, {
    sequenceSeconds: 5, expectedHostSeconds: 2,
    hostToleranceSeconds: .001
  }]]);
  const low = {
    captureToken: 41, sequenceSeconds: 5,
    hostTime: 1200, hostTimeScale: 600,
    mesh: { ...mesh(.1), confidence: .99 },
    renderWidth: 1280, renderHeight: 720,
    quality: 0, highQuality: false, field: 0
  };
  const high = {
    ...low, mesh: { ...mesh(.2), confidence: .90 },
    renderWidth: 640, renderHeight: 360,
    quality: 1, highQuality: true
  };
  analysis.validateCaptureResponses([low, high], issued);
  const selected = analysis.capturesInSequenceOrder([low, high], "mesh");
  assert.strictEqual(selected.length, 1);
  assert.strictEqual(selected[0].highQuality, true);
  assert.strictEqual(selected[0].mesh.points[10][0], mesh(.2).points[10][0]);
}

function variableSpeedResponsesFailClosed() {
  const issued = new Map([[51, {
    sequenceSeconds: 8, expectedHostSeconds: 4,
    hostToleranceSeconds: .01
  }]]);
  const capture = {
    captureToken: 51, hostTime: 425, hostTimeScale: 100,
    mesh: mesh(), renderWidth: 640, renderHeight: 360,
    quality: 1, highQuality: true, field: 0
  };
  assert.throws(() => analysis.validateCaptureResponses([capture], issued),
    /remapeamento temporal/);
}

async function expiredCaptureSessionsAreScavengedSafely() {
  const now = Date.UTC(2026, 7, 3, 12, 0, 0);
  const rootPath = "/private/tmp/RostoNatural";
  const entriesByPath = new Map();
  function register(entry, kind, symbolicLink = false) {
    entry._kind = kind;
    entry._symbolicLink = symbolicLink;
    entriesByPath.set(entry.nativePath, entry);
    return entry;
  }
  const pathFs = {
    lstat: async nativePath => {
      const entry = entriesByPath.get(nativePath);
      if (!entry) throw new Error("missing path");
      return {
        isSymbolicLink: () => entry._symbolicLink,
        isDirectory: () => entry._kind === "folder",
        isFile: () => entry._kind === "file"
      };
    }
  };
  function session(name, ageSeconds, options = {}) {
    let deleted = false;
    let deleteAttempts = 0;
    const sessionPath = `${rootPath}/${name}`;
    const markerName = options.markerOnly ?
      "session.marker" : "capture.lease";
    const childNames = options.completed ?
      ["capture.lease", "metadata.json", "landmarks.bin"] :
      (options.childNames || [
        markerName, "frame-000001.jpg",
        "capture-42-100-600-640x360-q1-f0-p1-s1.pending",
        "capture-42--100-600-640x360-q1-f0-p1-s2.json",
        "landmarks.bin"
      ]);
    const children = new Map();
    function createChild(childName, materialized = true) {
      const nested = childName === "nested";
      let content = "";
      let dateModified = new Date(now - ageSeconds * 1000);
      const child = {
        name: childName, isFolder: nested, isFile: !nested,
        nativePath: `${sessionPath}/${childName}`,
        read: async () => content,
        write: async value => {
          if (!materialized) {
            if (children.has(childName) ||
                entriesByPath.has(`${sessionPath}/${childName}`))
              throw new Error("path appeared before first write");
            materialized = true;
            register(child, nested ? "folder" : "file",
              options.symbolicLinkChild === childName);
            children.set(childName, child);
          }
          content = value;
          dateModified = new Date(now);
        },
        getMetadata: async () => ({ dateModified }),
        delete: async () => {
          children.delete(childName);
          entriesByPath.delete(`${sessionPath}/${childName}`);
        }
      };
      if (materialized) {
        register(child, nested ? "folder" : "file",
          options.symbolicLinkChild === childName);
        children.set(childName, child);
      }
      return child;
    }
    for (const childName of childNames) createChild(childName);
    for (const candidate of ["capture.lease", "session.marker"]) {
      const marker = children.get(candidate);
      if (!marker) continue;
      const schema = candidate === "capture.lease" ?
        (options.schema || "com.lucas.RostoNatural.captureLease") :
        "com.lucas.RostoNatural.captureSession";
      marker.read = async () =>
        options.invalidLease && candidate === "capture.lease" ? "{" :
          JSON.stringify({ schema, session: Number(name) });
      const markerAge = candidate === "session.marker" &&
        Number.isFinite(options.markerAgeSeconds) ? options.markerAgeSeconds :
        (candidate === "capture.lease" &&
         Number.isFinite(options.leaseAgeSeconds) ?
          options.leaseAgeSeconds : ageSeconds);
      marker.getMetadata = async () => ({
        dateModified: new Date(now - markerAge * 1000)
      });
      if (candidate === "capture.lease" && options.leaseMetadataError)
        marker.getMetadata = async () => { throw new Error("metadata"); };
    }
    return register({
      name, isFolder: true, isFile: false, nativePath: sessionPath,
      getEntry: async entryName => {
        if (children.has(entryName)) return children.get(entryName);
        throw new Error("missing");
      },
      getEntries: async () => Array.from(children.values()),
      createFile: async (entryName, createOptions = {}) => {
        if (children.has(entryName) && !createOptions.overwrite)
          throw new Error("already exists");
        return children.get(entryName) || createChild(entryName, false);
      },
      delete: async () => {
        if (children.size) throw new Error("folder must be empty");
        deleteAttempts++;
        if (options.failFolderDeleteOnce && deleteAttempts === 1)
          throw new Error("simulated folder delete failure");
        deleted = true;
        entriesByPath.delete(sessionPath);
      },
      get childCount() { return children.size; },
      get deleted() { return deleted; }
    }, "folder", Boolean(options.symbolicLinkFolder));
  }
  const stale = session("123", 31);
  const active = session("124", 5);
  const completed = session("125", 60, { completed: true });
  const foreign = session("126", 60, { schema: "other" });
  const markerOnly = session("127", 60, { markerOnly: true });
  const unexpected = session("128", 60, {
    childNames: ["capture.lease", "capture-1-do-not-delete.json"]
  });
  const linkedChild = session("129", 60, {
    childNames: ["capture.lease", "landmarks.bin"],
    symbolicLinkChild: "landmarks.bin"
  });
  const nested = session("130", 60, {
    childNames: ["capture.lease", "nested"]
  });
  const linkedFolder = session("131", 60, { symbolicLinkFolder: true });
  const invalidLeaseWithMarker = session("132", 60, {
    childNames: ["capture.lease", "session.marker", "landmarks.bin"],
    invalidLease: true
  });
  const deleteFailure = session("133", 60, {
    childNames: ["capture.lease", "landmarks.bin"],
    failFolderDeleteOnce: true
  });
  const suffixImpostor = session("134", 60, {
    childNames: ["capture.lease",
      "capture-42-100-600-640x360-q1-f0-p1-s1.json.evil"]
  });
  const freshInvalidLease = session("135", 60, {
    childNames: ["capture.lease", "session.marker", "landmarks.bin"],
    invalidLease: true, leaseAgeSeconds: 5, markerAgeSeconds: 60
  });
  const unknownLeaseAge = session("136", 60, {
    childNames: ["capture.lease", "session.marker", "landmarks.bin"],
    leaseMetadataError: true, markerAgeSeconds: 60
  });
  const rootEntries = [stale, active, completed, foreign, markerOnly,
    unexpected, linkedChild, nested, linkedFolder, invalidLeaseWithMarker,
    deleteFailure, suffixImpostor, freshInvalidLease, unknownLeaseAge,
    { name: "not-a-session", isFolder: true }];
  const root = register({
    name: "RostoNatural", isFolder: true, isFile: false,
    nativePath: rootPath,
    getEntries: async () => rootEntries
  }, "folder");
  const linkedRootFs = {
    lstat: async nativePath => nativePath === rootPath ? {
      isSymbolicLink: () => true,
      isDirectory: () => true,
      isFile: () => false
    } : pathFs.lstat(nativePath)
  };
  assert.strictEqual(await analysis.scavengeExpiredCaptureSessions(
    root, now, 30, linkedRootFs), 0);
  assert.strictEqual(
    await analysis.scavengeExpiredCaptureSessions(root, now, 30, pathFs), 3);
  assert.strictEqual(stale.deleted, true);
  assert.strictEqual(stale.childCount, 0);
  assert.strictEqual(markerOnly.deleted, true);
  assert.strictEqual(active.deleted, false);
  assert.strictEqual(completed.deleted, false);
  assert.strictEqual(foreign.deleted, false);
  assert.strictEqual(unexpected.deleted, false);
  assert.strictEqual(unexpected.childCount, 2);
  assert.strictEqual(linkedChild.deleted, false);
  assert.strictEqual(nested.deleted, false);
  assert.strictEqual(linkedFolder.deleted, false);
  assert.strictEqual(invalidLeaseWithMarker.deleted, true);
  assert.strictEqual(deleteFailure.deleted, false);
  assert.strictEqual(deleteFailure.childCount, 1);
  assert((await deleteFailure.getEntry("session.marker")).isFile);
  assert.strictEqual(suffixImpostor.deleted, false);
  assert.strictEqual(freshInvalidLease.deleted, false);
  assert.strictEqual(unknownLeaseAge.deleted, false);
}

async function uxpModeBitsKeepScratchValidationFailClosed() {
  const folder = {
    nativePath: "/private/tmp/RostoNatural",
    isFolder: true,
    isFile: false
  };
  const regularFolderFs = {
    lstat: async () => ({
      mode: 0o040700,
      isDirectory: () => true,
      isFile: () => false
    })
  };
  assert.strictEqual(await analysis.secureScratchEntry(
    folder, folder.nativePath, "folder", regularFolderFs), true,
  "Premiere UXP 26.3 exposes POSIX mode without isSymbolicLink()");

  const symbolicLinkFs = {
    lstat: async () => ({
      mode: 0o120777,
      isDirectory: () => true,
      isFile: () => false
    })
  };
  assert.strictEqual(await analysis.secureScratchEntry(
    folder, folder.nativePath, "folder", symbolicLinkFs), false);
  for (const mode of [0o000700, 0o010700, 0o100600]) {
    assert.strictEqual(await analysis.secureScratchEntry(
      folder, folder.nativePath, "folder", {
        lstat: async () => ({
          mode,
          isSymbolicLink: () => false,
          isDirectory: () => true,
          isFile: () => false
        })
      }), false,
    "unknown, special, and mismatched POSIX types must be rejected");
  }
  for (const reported of [undefined, false]) {
    assert.strictEqual(await analysis.secureScratchEntry(
      folder, folder.nativePath, "folder", {
        lstat: async () => ({
          mode: 0o120777,
          isSymbolicLink: () => reported,
          isDirectory: () => true,
          isFile: () => false
        })
      }), false,
    "a symlink mode must override an absent or contradictory method result");
  }
  assert.strictEqual(await analysis.secureScratchEntry(
    folder, folder.nativePath, "folder", {
      lstat: async () => ({
        mode: 0o040700,
        isSymbolicLink: () => true,
        isDirectory: () => true,
        isFile: () => false
      })
    }), false,
  "a positive symlink method must override regular-looking mode bits");
  assert.strictEqual(await analysis.secureScratchEntry(
    folder, folder.nativePath, "folder", {
      lstat: async () => ({
        mode: 0o040700,
        isSymbolicLink: () => undefined,
        isDirectory: () => true,
        isFile: () => false
      })
    }), true,
  "valid POSIX mode is the fallback for UXP's unsupported link method");
  assert.strictEqual(await analysis.secureScratchEntry(
    folder, folder.nativePath, "folder", {
      lstat: async () => ({
        isDirectory: () => true,
        isFile: () => false
      })
    }), false,
  "unknown link status must remain fail-closed");
}

async function scratchEnumerationStabilizesOnlyTransientRaces() {
  let quickCalls = 0;
  const firstValidSnapshot = await analysis.getScratchEntriesWithRetry({
    getEntries: async () => {
      quickCalls++;
      return quickCalls === 1 ? [] : [{ name: "later.json" }];
    }
  });
  assert.deepStrictEqual(firstValidSnapshot, []);
  assert.strictEqual(quickCalls, 1,
    "active drains must not wait for global directory stability");

  assert.deepStrictEqual(await analysis.getScratchEntriesWithRetry({
    getEntries: async () => {
      if (quickCalls++ < 2)
        throw new Error("Could not find an entry of 'file:///private/tmp/x'");
      return [];
    }
  }, 2), [], "Premiere UXP's missing-entry wording is transient");

  let calls = 0;
  const recovered = await analysis.getStableScratchEntries({
    getEntries: async () => {
      calls++;
      if (calls < 3) throw new Error("no such file or directory");
      return [{ name: "capture-1.json" }];
    }
  }, 4);
  assert.deepStrictEqual(recovered, [{ name: "capture-1.json" }]);
  assert.strictEqual(calls, 4,
    "a recovered snapshot must repeat before it is trusted");

  calls = 0;
  await assert.rejects(() => analysis.getStableScratchEntries({
    getEntries: async () => {
      calls++;
      throw new Error("persistent enumeration failure");
    }
  }, 3), /persistent enumeration failure/);
  assert.strictEqual(calls, 1,
    "non-transient failures must remain fail-closed without retries");

  calls = 0;
  const renamed = await analysis.getStableScratchEntries({
    getEntries: async () => {
      calls++;
      if (calls === 1) return [];
      return [{ name: "capture-2.json" }];
    }
  }, 3);
  assert.deepStrictEqual(renamed, [{ name: "capture-2.json" }]);
  assert.strictEqual(calls, 3,
    "an empty snapshot before an atomic rename must not be trusted");
}

async function completedCacheRemovesOnlyKnownExportFrames() {
  const sessionPath = "/private/tmp/RostoNatural/123";
  const entries = ["session.marker", "frame-000000.jpg", "frame-000001.jpg"]
    .map(name => ({
      name,
      nativePath: `${sessionPath}/${name}`,
      isFolder: false,
      isFile: true,
      deleted: false,
      async delete() { this.deleted = true; }
    }));
  const folder = {
    nativePath: sessionPath,
    isFolder: true,
    isFile: false,
    async getEntries() { return entries.filter(entry => !entry.deleted); },
    async getEntry(name) {
      const found = entries.find(entry => entry.name === name && !entry.deleted);
      if (!found) throw new Error("no such file or directory");
      return found;
    }
  };
  const pathFs = {
    async lstat(path) {
      const isFolder = path === sessionPath;
      return {
        mode: isFolder ? 0o040700 : 0o100600,
        isDirectory: () => isFolder,
        isFile: () => !isFolder
      };
    }
  };
  assert.strictEqual(await analysis.removeExportedFrameArtifacts(
    folder, sessionPath, pathFs,
    ["frame-000000.jpg", "frame-000001.jpg"], 4, 2), 2);
  assert.strictEqual(await analysis.assertScratchContents(
    folder, sessionPath, pathFs, ["session.marker"]), true);

  const unexpected = {
    name: "foreign.txt",
    nativePath: `${sessionPath}/foreign.txt`,
    isFolder: false,
    isFile: true,
    deleted: false,
    async delete() { this.deleted = true; }
  };
  entries.push(unexpected);
  await assert.rejects(() => analysis.assertScratchContents(
    folder, sessionPath, pathFs, ["session.marker"]),
  /arquivos temporários/);
  assert.strictEqual(unexpected.deleted, false,
    "unexpected content must never be deleted by final-cache cleanup");

  const linkedFrame = {
    name: "frame-000002.jpg",
    nativePath: `${sessionPath}/frame-000002.jpg`,
    isFolder: false,
    isFile: true,
    deleted: false,
    async delete() { this.deleted = true; }
  };
  entries.push(linkedFrame);
  await assert.rejects(() => analysis.removeOptionalExportedFrame(
    folder, sessionPath, {
      lstat: async path => path === linkedFrame.nativePath ? {
        mode: 0o120777,
        isDirectory: () => false,
        isFile: () => true
      } : pathFs.lstat(path)
    }, linkedFrame.name), /inseguro/);
  assert.strictEqual(linkedFrame.deleted, false,
    "an unsafe frame entry must be preserved");
}

(async () => {
  binaryEncodingMatchesCoreLayout();
  meshGapAndAdaptiveMotion();
  captureParsing();
  analysisDimensionsPreserveAspectRatio();
  timingUsesIntegerTicks();
  capturesRemainCorrelatedWhenOutOfOrderOrReversed();
  multipleRenderVariantsPreferHighQuality();
  variableSpeedResponsesFailClosed();
  await expiredCaptureSessionsAreScavengedSafely();
  await uxpModeBitsKeepScratchValidationFailClosed();
  await scratchEnumerationStabilizesOnlyTransientRaces();
  await completedCacheRemovesOnlyKnownExportFrames();
  console.log("uxp_analysis: 12 tests passed");
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
