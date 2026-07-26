(() => {
  "use strict";

  function byId(id) {
    return document.getElementById(id);
  }

  function formatNumber(value, digits = 2) {
    if (typeof value !== "number" || !Number.isFinite(value)) {
      return "n/a";
    }
    return value.toFixed(digits);
  }

  function resizeCanvas(canvas) {
    const ratio = window.devicePixelRatio || 1;
    const width = Math.max(
      1,
      Math.floor(canvas.clientWidth * ratio));
    const height = Math.max(
      1,
      Math.floor(canvas.clientHeight * ratio));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    return ratio;
  }

  function heatRgb(unit) {
    const value = Math.max(0, Math.min(1, unit));
    return [
      Math.round(8 + 237 * Math.pow(value, 1.7)),
      Math.round(
        27 + 187 * Math.sin(value * Math.PI * 0.72)),
      Math.round(25 + 78 * (1 - value))
    ];
  }

  function heatColor(unit) {
    const [red, green, blue] = heatRgb(unit);
    return `rgb(${red}, ${green}, ${blue})`;
  }

  const platform = globalThis.PamguardPlatform || {};
  globalThis.PamguardPlatform = Object.freeze({
    ...platform,
    dom: Object.freeze({
      byId,
      formatNumber,
      resizeCanvas,
      heatRgb,
      heatColor
    })
  });
})();
