(() => {
  const status = document.getElementById("status");
  const progress = document.getElementById("progress");
  const bar = document.getElementById("bar");
  const controls = [
    "master", "skin", "texture", "underEye", "tone", "shine",
    "eyes", "flyaways", "blush", "lips"
  ];
  const defaults = {
    master: 72, skin: 58, texture: 75, underEye: 42, tone: 24,
    shine: 30, eyes: 18, flyaways: 0, blush: 0, lips: 20,
    blushColor: "#db5766", lipColor: "#9e2933", beforeAfter: false
  };
  let active = null;
  let updateTimer = null;

  function show(message, error = false) {
    status.textContent = message;
    status.classList.toggle("error", error);
  }

  function values() {
    const result = {};
    for (const id of controls)
      result[id] = Number(document.getElementById(id).value);
    result.blushColor = document.getElementById("blushColor").value;
    result.lipColor = document.getElementById("lipColor").value;
    result.beforeAfter = document.getElementById("beforeAfter").checked;
    return result;
  }

  function setControls(next) {
    for (const id of controls) {
      document.getElementById(id).value = next[id];
      document.getElementById(`${id}Value`).value = next[id];
    }
    document.getElementById("blushColor").value = next.blushColor;
    document.getElementById("lipColor").value = next.lipColor;
    document.getElementById("beforeAfter").checked = next.beforeAfter;
  }

  function disable(value) {
    for (const button of document.querySelectorAll("button"))
      button.disabled = value;
  }

  async function connectAndApply() {
    const ctx = await window.RostoPremiere.context();
    const effect = await window.RostoPremiere.applyEffect(ctx);
    active = { ctx, effect };
    await window.RostoPremiere.setValues(ctx, effect, values());
  }

  function scheduleLiveUpdate() {
    if (!active) return;
    if (updateTimer) clearTimeout(updateTimer);
    updateTimer = setTimeout(async () => {
      updateTimer = null;
      try {
        await window.RostoPremiere.setValues(
          active.ctx, active.effect, values());
        show("Atualizado.");
      } catch (error) {
        active = null;
        show(error.message || String(error), true);
      }
    }, 120);
  }

  document.getElementById("apply").addEventListener("click", async () => {
    disable(true);
    try {
      await connectAndApply();
      show("Efeito aplicado. Os ajustes agora são ao vivo.");
    } catch (error) {
      active = null;
      show(error.message || String(error), true);
    } finally {
      disable(false);
    }
  });

  document.getElementById("analyze").addEventListener("click", async () => {
    disable(true);
    progress.hidden = false;
    bar.style.width = "0";
    try {
      const result = await window.RostoAnalysis.run(value => {
        const percent = Math.round(value * 100);
        bar.style.width = `${percent}%`;
        show(`Analisando movimento… ${percent}%`);
      });
      show(`${result.detectedCount}/${result.frameCount} quadros com rosto. ` +
           "Trilha estabilizada ativa.");
    } catch (error) {
      show(error.message || String(error), true);
    } finally {
      disable(false);
      progress.hidden = true;
    }
  });

  document.getElementById("reset").addEventListener("click", () => {
    setControls(defaults);
    scheduleLiveUpdate();
    show("Preset natural restaurado.");
  });

  for (const id of controls) {
    const input = document.getElementById(id);
    const output = document.getElementById(`${id}Value`);
    input.addEventListener("input", () => {
      output.value = input.value;
      scheduleLiveUpdate();
    });
  }
  for (const id of ["blushColor", "lipColor", "beforeAfter"])
    document.getElementById(id).addEventListener(
      "change", scheduleLiveUpdate);
})();
