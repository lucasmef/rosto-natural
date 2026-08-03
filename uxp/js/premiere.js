/* global require */
(() => {
  const MATCH_NAME = "com.lucas.RostoNatural";
  const PARAM_NAMES = {
    master: "Intensidade", skin: "Pele", texture: "Preservar textura",
    underEye: "Olheiras", tone: "Uniformizar tom", shine: "Brilho da pele",
    eyes: "Olhos", flyaways: "Cabelos rebeldes",
    blush: "Blush", blushColor: "Cor do blush",
    lips: "Lábios", lipColor: "Cor dos lábios", beforeAfter: "Antes / depois",
    analysisId: "_Analysis ID", captureMode: "_Capture Mode",
    cacheVersion: "_Cache Version"
  };

  function api() { return require("premierepro"); }

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
      if (await component.getMatchName() === MATCH_NAME) return component;
    }
    return null;
  }

  async function applyEffect(ctx) {
    let component = await findEffect(ctx.item);
    if (component) return component;
    component = await ctx.ppro.VideoFilterFactory.createComponent(MATCH_NAME);
    if (!component) throw new Error("Efeito nativo Rosto Natural não instalado.");
    const chain = await ctx.item.getComponentChain();
    ctx.project.lockedAccess(() => {
      const action = chain.createAppendComponentAction(component);
      ctx.project.executeTransaction(compound => compound.addAction(action),
                                     "Aplicar Rosto Natural");
    });
    return await findEffect(ctx.item) || component;
  }

  async function paramsByName(component) {
    const result = {};
    const count = component.getParamCount();
    for (let i = 0; i < count; i++) {
      const param = component.getParam(i);
      result[param.displayName] = param;
    }
    return result;
  }

  async function setValues(ctx, component, values, undoName = "Ajustar Rosto Natural") {
    const params = await paramsByName(component);
    const normalized = {};
    for (const [key, value] of Object.entries(values)) {
      const param = params[PARAM_NAMES[key]];
      if (!param) continue;
      if (typeof value === "string" && /^#[0-9a-f]{6}$/i.test(value)) {
        const color = await param.getValueAtTime(
          ctx.ppro.TickTime.createWithSeconds(0));
        color.red = parseInt(value.slice(1, 3), 16) / 255;
        color.green = parseInt(value.slice(3, 5), 16) / 255;
        color.blue = parseInt(value.slice(5, 7), 16) / 255;
        color.alpha = 1;
        normalized[key] = color;
      } else normalized[key] = value;
    }
    const actions = [];
    ctx.project.lockedAccess(() => {
      for (const [key, value] of Object.entries(normalized)) {
        const param = params[PARAM_NAMES[key]];
        const keyframe = param.createKeyframe(value);
        actions.push(param.createSetValueAction(keyframe, true));
      }
      ctx.project.executeTransaction(compound => {
        for (const action of actions) compound.addAction(action);
      }, undoName);
    });
  }

  window.RostoPremiere = { api, context, findEffect, applyEffect, setValues,
                           MATCH_NAME };
})();
