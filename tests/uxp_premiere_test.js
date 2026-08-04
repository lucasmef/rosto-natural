const assert = require("assert");

global.window = {};
require("../uxp/js/premiere.js");
const premiere = window.RostoPremiere;

function tick(seconds) {
  return { seconds, ticks: String(Math.round(seconds * 1000)) };
}

function makeHarness() {
  let locked = 0;
  let timeVarying = false;
  let staticValue = 0;
  let keys = [];
  let transactionCount = 0;
  const action = apply => ({ apply });
  const param = {
    displayName: "_Capture Token",
    areKeyframesSupported: async () => true,
    isTimeVarying: () => timeVarying,
    getKeyframeListAsTickTimes: () => keys.map(key => key.position),
    getValueAtTime: async time => {
      if (!timeVarying || !keys.length) return { value: staticValue };
      const sorted = keys.slice().sort(
        (a, b) => Number(a.position.ticks) - Number(b.position.ticks));
      let selected = sorted[0];
      for (const key of sorted) {
        if (Number(key.position.ticks) > Number(time.ticks)) break;
        selected = key;
      }
      return { value: selected.value };
    },
    createKeyframe: value => ({ value, position: tick(0) }),
    createSetValueAction: keyframe => action(() => {
      assert(locked > 0);
      staticValue = keyframe.value;
    }),
    createSetTimeVaryingAction: value => action(() => {
      assert(locked > 0);
      timeVarying = value;
    }),
    createAddKeyframeAction: keyframe => action(() => {
      assert(locked > 0);
      keys.push({ ...keyframe });
    }),
    createRemoveKeyframeAction: time => action(() => {
      assert(locked > 0);
      keys = keys.filter(key => key.position.ticks !== time.ticks);
    }),
    createSetInterpolationAtKeyframeAction: (time, mode) => action(() => {
      assert(locked > 0);
      const key = keys.find(candidate =>
        candidate.position.ticks === time.ticks);
      assert(key, "interpolation action must target an installed keyframe");
      key.interpolation = mode;
    })
  };
  const component = {
    getParamCount: () => 1,
    getParam: () => param
  };
  const project = {
    lockedAccess(callback) {
      locked++;
      try { return callback(); } finally { locked--; }
    },
    executeTransaction(callback) {
      transactionCount++;
      const actions = [];
      callback({ addAction: item => actions.push(item) });
      for (const item of actions) item.apply();
      return true;
    }
  };
  const ctx = {
    project,
    ppro: {
      Constants: { InterpolationMode: { HOLD: 4 } },
      TickTime: { createWithSeconds: tick }
    }
  };
  return {
    ctx, component, param,
    get transactionCount() { return transactionCount; }
  };
}

async function keyframedCaptureUsesConstantUndoTransactions() {
  const harness = makeHarness();
  const entries = [
    { time: tick(10), token: 101 },
    { time: tick(11), token: 102 },
    { time: tick(12), token: 103 }
  ];
  await premiere.armCaptureTokens(harness.ctx, harness.component, entries);
  assert.strictEqual(harness.transactionCount, 3,
    "arming must use a constant number of Undo transactions");
  assert.strictEqual(
    (await harness.param.getValueAtTime(tick(10.5))).value, 101);
  assert.strictEqual(
    (await harness.param.getValueAtTime(tick(11.5))).value, 102);
  await premiere.resetCaptureTokens(
    harness.ctx, harness.component, tick(10));
  assert.strictEqual(harness.transactionCount, 5,
    "cleanup must also use a constant number of Undo transactions");
  assert.strictEqual(harness.param.isTimeVarying(), false);
  assert.strictEqual(harness.param.getKeyframeListAsTickTimes().length, 0);
  assert.strictEqual((await harness.param.getValueAtTime(tick(10))).value, 0);
}

async function duplicateLocalTimesFailClosed() {
  const harness = makeHarness();
  await assert.rejects(() => premiere.armCaptureTokens(
    harness.ctx, harness.component, [
      { time: tick(3), token: 1 },
      { time: tick(3), token: 2 }
    ]), /mesmo tempo local/);
  assert.strictEqual(harness.transactionCount, 0);
}

async function adobePrefixedMatchNameResolvesInstalledEffect() {
  const builtIn = { getMatchName: async () => "AE.ADBE Motion" };
  const rostoNatural = {
    getMatchName: async () => "AE.com.lucas.RostoNatural"
  };
  const item = {
    getComponentChain: async () => ({
      getComponentCount: () => 2,
      getComponentAtIndex: index => [builtIn, rostoNatural][index]
    })
  };
  assert.strictEqual(await premiere.findEffect(item), rostoNatural,
    "Premiere prefixes AE effect match names in an installed chain");
}

async function colorsUseThePremiereColorConstructor() {
  let captured = null;
  let capturedLips = null;
  class Color {}
  const colorParam = {
    displayName: "Cor dos l√°bios",
    getValueAtTime: () => {
      throw new Error("color values must not use getValueAtTime");
    },
    createKeyframe: value => {
      captured = value;
      return { value };
    },
    createSetValueAction: () => ({})
  };
  const lipsParam = {
    displayName: "L√°bios",
    createKeyframe: value => {
      capturedLips = value;
      return { value };
    },
    createSetValueAction: () => ({})
  };
  const component = {
    getParamCount: () => 2,
    getParam: index => [lipsParam, colorParam][index]
  };
  const ctx = {
    ppro: { Color },
    project: {
      lockedAccess: callback => callback(),
      executeTransaction(callback) {
        callback({ addAction() {} });
        return true;
      }
    }
  };
  await premiere.setValues(ctx, component, {
    lips: 31, lipColor: "#102030"
  });
  assert.strictEqual(capturedLips, 31,
    "the MacRoman-style host label must map back to Lábios");
  assert(captured instanceof Color);
  assert.strictEqual(captured.red, 0x10 / 255);
  assert.strictEqual(captured.green, 0x20 / 255);
  assert.strictEqual(captured.blue, 0x30 / 255);
  assert.strictEqual(captured.alpha, 1);
}

async function refusedApplyTransactionFailsClosed() {
  const chain = {
    getComponentCount: () => 0,
    getComponentAtIndex: () => null,
    createAppendComponentAction: () => ({})
  };
  const ctx = {
    item: { getComponentChain: async () => chain },
    ppro: {
      VideoFilterFactory: {
        createComponent: async () => ({
          getMatchName: async () => "AE.com.lucas.RostoNatural"
        })
      }
    },
    project: {
      lockedAccess: callback => callback(),
      executeTransaction: () => false
    }
  };
  await assert.rejects(() => premiere.applyEffect(ctx),
    /recusou a transação/);
}

async function factoryUsesRegisteredAdobeMatchName() {
  let requestedMatchName = null;
  let installed = null;
  const component = {
    getMatchName: async () => "AE.com.lucas.RostoNatural"
  };
  const chain = {
    getComponentCount: () => installed ? 1 : 0,
    getComponentAtIndex: () => installed,
    createAppendComponentAction: value => ({
      apply() { installed = value; }
    })
  };
  const ctx = {
    item: { getComponentChain: async () => chain },
    ppro: {
      VideoFilterFactory: {
        createComponent: async matchName => {
          requestedMatchName = matchName;
          return component;
        }
      }
    },
    project: {
      lockedAccess: callback => callback(),
      executeTransaction(callback) {
        const actions = [];
        callback({ addAction: action => actions.push(action) });
        for (const action of actions) action.apply();
        return true;
      }
    }
  };
  assert.strictEqual(await premiere.applyEffect(ctx), component);
  assert.strictEqual(requestedMatchName, "AE.com.lucas.RostoNatural",
    "the factory must receive the registered Adobe-prefixed match name");
}

async function missingApplyPostconditionFailsClosed() {
  const chain = {
    getComponentCount: () => 0,
    getComponentAtIndex: () => null,
    createAppendComponentAction: () => ({})
  };
  const ctx = {
    item: { getComponentChain: async () => chain },
    ppro: {
      VideoFilterFactory: {
        createComponent: async () => ({})
      }
    },
    project: {
      lockedAccess: callback => callback(),
      executeTransaction(callback) {
        callback({ addAction() {} });
        return true;
      }
    }
  };
  await assert.rejects(() => premiere.applyEffect(ctx),
    /não anexou o efeito/);
}

async function missingParametersFailBeforeTransaction() {
  let transactions = 0;
  const ctx = {
    ppro: {},
    project: {
      lockedAccess: callback => callback(),
      executeTransaction: () => {
        transactions++;
        return true;
      }
    }
  };
  const component = {
    getParamCount: () => 0,
    getParam: () => null
  };
  await assert.rejects(
    () => premiere.setValues(ctx, component, { lips: 22 }),
    /não expôs o parâmetro Lábios/);
  assert.strictEqual(transactions, 0,
    "a missing parameter must fail before opening an Undo transaction");
}

async function invalidCaptureTimesAndTokensFailClosed() {
  for (const entry of [
    { time: {}, token: 1 },
    { time: tick(1), token: 2000000000 }
  ]) {
    const harness = makeHarness();
    await assert.rejects(
      () => premiere.armCaptureTokens(
        harness.ctx, harness.component, [entry]),
      /Token ou tempo de captura inválido/);
    assert.strictEqual(harness.transactionCount, 0);
  }
}

(async () => {
  await keyframedCaptureUsesConstantUndoTransactions();
  await duplicateLocalTimesFailClosed();
  await adobePrefixedMatchNameResolvesInstalledEffect();
  await colorsUseThePremiereColorConstructor();
  await refusedApplyTransactionFailsClosed();
  await factoryUsesRegisteredAdobeMatchName();
  await missingApplyPostconditionFailsClosed();
  await missingParametersFailBeforeTransaction();
  await invalidCaptureTimesAndTokensFailClosed();
  console.log("uxp_premiere: 9 tests passed");
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
