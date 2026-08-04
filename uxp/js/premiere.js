/* global require */
(() => {
  const MATCH_NAME = "com.lucas.RostoNatural";
  const FACTORY_MATCH_NAME = `AE.${MATCH_NAME}`;
  const PARAM_NAMES = {
    master: "Intensidade", skin: "Pele", texture: "Preservar textura",
    underEye: "Olheiras", tone: "Uniformizar tom", shine: "Brilho da pele",
    eyes: "Olhos", flyaways: "Cabelos rebeldes",
    blush: "Blush", blushColor: "Cor do blush",
    lips: "Lábios", lipColor: "Cor dos lábios", beforeAfter: "Antes / depois",
    analysisId: "_Analysis ID", captureMode: "_Capture Mode",
    cacheVersion: "_Cache Version", captureToken: "_Capture Token",
    captureSession: "_Capture Session"
  };

  function api() { return require("premierepro"); }

  function canonicalMatchName(value) {
    const name = String(value || "");
    return name.startsWith("AE.") ? name.slice(3) : name;
  }

  function canonicalParamName(value) {
    const name = String(value || "");
    if (name === "L√°bios") return "Lábios";
    if (name === "Cor dos l√°bios") return "Cor dos lábios";
    return name;
  }

  async function context() {
    const ppro = api();
    const project = await ppro.Project.getActiveProject();
    if (!project) throw new Error("Abra um projeto no Premiere.");
    const sequence = await project.getActiveSequence();
    if (!sequence) throw new Error("Abra uma sequência.");
    const selection = await sequence.getSelection();
    const items = await selection.getTrackItems();
    // Linked audio is commonly selected together with video. Both expose a
    // component chain, so use the transition method unique to video items.
    const videoItems = items.filter(item =>
      typeof item.createAddVideoTransitionAction === "function");
    if (videoItems.length !== 1)
      throw new Error("Selecione exatamente um clipe de vídeo.");
    return { ppro, project, sequence, item: videoItems[0] };
  }

  async function findEffect(item) {
    const chain = await item.getComponentChain();
    const count = chain.getComponentCount();
    for (let i = 0; i < count; i++) {
      const component = chain.getComponentAtIndex(i);
      if (canonicalMatchName(await component.getMatchName()) === MATCH_NAME)
        return component;
    }
    return null;
  }

  async function applyEffect(ctx) {
    let component = await findEffect(ctx.item);
    if (component) return component;
    component = await ctx.ppro.VideoFilterFactory.createComponent(
      FACTORY_MATCH_NAME);
    if (!component) throw new Error("Efeito nativo Rosto Natural não instalado.");
    const chain = await ctx.item.getComponentChain();
    transaction(ctx, "Aplicar Rosto Natural", compound => {
      const action = chain.createAppendComponentAction(component);
      compound.addAction(action);
    });
    const installed = await findEffect(ctx.item);
    if (!installed)
      throw new Error("O Premiere não anexou o efeito Rosto Natural ao clipe.");
    return installed;
  }

  async function paramsByName(component) {
    const result = {};
    const count = component.getParamCount();
    for (let i = 0; i < count; i++) {
      const param = component.getParam(i);
      result[canonicalParamName(param.displayName)] = param;
    }
    return result;
  }

  function transaction(ctx, undoName, addActions) {
    let result;
    ctx.project.lockedAccess(() => {
      result = ctx.project.executeTransaction(addActions, undoName);
    });
    if (result === false)
      throw new Error(`O Premiere recusou a transação: ${undoName}.`);
  }

  async function keyframeTimes(param) {
    const times = await Promise.resolve(param.getKeyframeListAsTickTimes());
    return Array.isArray(times) ? times : [];
  }

  async function scalarValueAtTime(param, time) {
    const result = await param.getValueAtTime(time);
    if (result && typeof result === "object" &&
        Object.prototype.hasOwnProperty.call(result, "value"))
      return result.value;
    return result;
  }

  async function captureTokenParam(component) {
    const params = await paramsByName(component);
    const param = params[PARAM_NAMES.captureToken];
    if (!param)
      throw new Error("O efeito nativo não expôs o token de captura.");
    return param;
  }

  async function setValues(ctx, component, values, undoName = "Ajustar Rosto Natural") {
    const params = await paramsByName(component);
    const normalized = {};
    for (const [key, value] of Object.entries(values)) {
      if (!Object.prototype.hasOwnProperty.call(PARAM_NAMES, key))
        throw new Error(`Controle Rosto Natural desconhecido: ${key}.`);
      const displayName = PARAM_NAMES[key];
      const param = params[displayName];
      if (!param)
        throw new Error(`O efeito nativo não expôs o parâmetro ${displayName}.`);
      if (typeof value === "string" && /^#[0-9a-f]{6}$/i.test(value)) {
        const color = new ctx.ppro.Color();
        color.red = parseInt(value.slice(1, 3), 16) / 255;
        color.green = parseInt(value.slice(3, 5), 16) / 255;
        color.blue = parseInt(value.slice(5, 7), 16) / 255;
        color.alpha = 1;
        normalized[key] = color;
      } else normalized[key] = value;
    }
    transaction(ctx, undoName, compound => {
      for (const [key, value] of Object.entries(normalized)) {
        const param = params[PARAM_NAMES[key]];
        const keyframe = param.createKeyframe(value);
        compound.addAction(param.createSetValueAction(keyframe, true));
      }
    });
  }

  async function resetCaptureTokens(
      ctx, component, sampleTime, undoPrefix = "Limpar tokens de captura") {
    const param = await captureTokenParam(component);
    const existing = await keyframeTimes(param);
    if (existing.length) {
      transaction(ctx, `${undoPrefix}: remover quadros`, compound => {
        for (const time of existing)
          compound.addAction(param.createRemoveKeyframeAction(time, false));
      });
    }
    if (await Promise.resolve(param.isTimeVarying())) {
      transaction(ctx, `${undoPrefix}: desativar animação`, compound => {
        compound.addAction(param.createSetTimeVaryingAction(false));
      });
    }
    const probe = sampleTime || ctx.ppro.TickTime.createWithSeconds(0);
    const current = Number(await scalarValueAtTime(param, probe));
    if (!Number.isFinite(current) || current !== 0) {
      transaction(ctx, `${undoPrefix}: zerar`, compound => {
        const keyframe = param.createKeyframe(0);
        compound.addAction(param.createSetValueAction(keyframe, true));
      });
    }
    if ((await keyframeTimes(param)).length ||
        await Promise.resolve(param.isTimeVarying()) ||
        Number(await scalarValueAtTime(param, probe)) !== 0)
      throw new Error("O Premiere não conseguiu limpar os tokens de captura.");
  }

  async function armCaptureTokens(
      ctx, component, entries, undoPrefix = "Programar capturas faciais") {
    if (!Array.isArray(entries) || !entries.length)
      throw new Error("Nenhum quadro foi programado para análise.");
    const param = await captureTokenParam(component);
    if (!await param.areKeyframesSupported())
      throw new Error(
        "Esta versão do Premiere não anima o token nativo de captura.");
    const hold = ctx.ppro.Constants && ctx.ppro.Constants.InterpolationMode ?
      ctx.ppro.Constants.InterpolationMode.HOLD : undefined;
    if (typeof hold !== "number")
      throw new Error("O Premiere não expôs interpolação HOLD para captura.");
    const uniqueTimes = new Set();
    for (const entry of entries) {
      if (!entry || !entry.time || !Number.isInteger(entry.token) ||
          entry.token <= 0 || entry.token > 1999999999)
        throw new Error("Token ou tempo de captura inválido.");
      const rawTicks = entry.time.ticks;
      if ((typeof rawTicks !== "string" &&
           typeof rawTicks !== "number" &&
           typeof rawTicks !== "bigint") ||
          !/^-?\d+$/.test(String(rawTicks)))
        throw new Error("Token ou tempo de captura inválido.");
      const ticks = String(rawTicks);
      if (uniqueTimes.has(ticks))
        throw new Error(
          "Dois quadros mapearam para o mesmo tempo local do clipe.");
      uniqueTimes.add(ticks);
    }

    await resetCaptureTokens(ctx, component, entries[0].time, undoPrefix);
    transaction(ctx, `${undoPrefix}: ativar animação`, compound => {
      compound.addAction(param.createSetTimeVaryingAction(true));
    });

    // Enabling animation may create a host-owned default keyframe. Remove it
    // in the same transaction that installs the complete token schedule.
    const automatic = await keyframeTimes(param);
    transaction(ctx, `${undoPrefix}: adicionar quadros`, compound => {
      for (const time of automatic)
        compound.addAction(param.createRemoveKeyframeAction(time, false));
      for (const entry of entries) {
        const keyframe = param.createKeyframe(entry.token);
        keyframe.position = entry.time;
        compound.addAction(param.createAddKeyframeAction(keyframe));
      }
    });
    transaction(ctx, `${undoPrefix}: manter valores`, compound => {
      for (const entry of entries)
        compound.addAction(param.createSetInterpolationAtKeyframeAction(
          entry.time, hold, false));
    });

    const installed = await keyframeTimes(param);
    if (!await Promise.resolve(param.isTimeVarying()) ||
        installed.length !== entries.length)
      throw new Error("O Premiere não instalou todos os tokens de captura.");
    for (const entry of entries) {
      const actual = Number(await scalarValueAtTime(param, entry.time));
      if (actual !== entry.token)
        throw new Error(
          "O Premiere alterou um token ao programar a análise facial.");
    }
  }

  window.RostoPremiere = {
    api, context, findEffect, applyEffect, setValues,
    armCaptureTokens, resetCaptureTokens, MATCH_NAME, FACTORY_MATCH_NAME
  };
})();
