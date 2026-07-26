(() => {
  "use strict";

  const capturedScriptSource =
    typeof document !== "undefined" && document.currentScript
      ? document.currentScript.src
      : "";

  function createElement(tag, options = {}) {
    const element = document.createElement(tag);
    if (options.className) element.className = options.className;
    if (options.text !== undefined) {
      element.textContent = String(options.text);
    }
    if (options.type) element.type = options.type;
    if (options.attributes) {
      for (const [name, value] of Object.entries(options.attributes)) {
        if (value !== null && value !== undefined) {
          element.setAttribute(name, String(value));
        }
      }
    }
    return element;
  }

  function ensureStylesheet() {
    if (document.querySelector(
      "link[data-pamguard-project-level-meter-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-level-meter-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL(
          "project-level-meter-settings.css",
          capturedScriptSource).href
      : "/assets/project-level-meter-settings.css";
    document.head.append(link);
  }

  function canonicalSettings(value = {}) {
    const minLevel = Number.isInteger(Number(value.minLevel)) &&
      Number(value.minLevel) < 0
      ? Number(value.minLevel)
      : -80;
    const scaleReference = [0, 1, 2].includes(
      Number(value.scaleReference))
      ? Number(value.scaleReference)
      : 0;
    const scaleType = [0, 1].includes(Number(value.scaleType))
      ? Number(value.scaleType)
      : 0;
    return { minLevel, scaleReference, scaleType };
  }

  function labelledField(label, control, help = "") {
    const row = createElement("label", {
      className: "level-meter-settings-field"
    });
    row.append(
      createElement("span", {
        className: "level-meter-settings-label",
        text: label
      }),
      control);
    if (help) {
      row.append(createElement("small", {
        className: "level-meter-settings-help",
        text: help
      }));
    }
    return row;
  }

  function mountEditor(options) {
    ensureStylesheet();
    const {
      container,
      settings = {}
    } = options;
    const canonical = canonicalSettings(settings);
    const root = createElement("fieldset", {
      className: "level-meter-settings-editor",
      attributes: {
        "data-pamguard-level-meter-settings-editor": "true"
      }
    });
    root.append(createElement("legend", { text: "Scale selection" }));

    const typeGroup = createElement("div", {
      className: "level-meter-settings-type",
      attributes: {
        role: "radiogroup",
        "aria-label": "Level calculation"
      }
    });
    const peak = createElement("input", {
      type: "radio",
      attributes: {
        name: "level-meter-scale-type",
        value: "0",
        "data-setting-pointer": "/scaleType"
      }
    });
    peak.checked = canonical.scaleType === 0;
    const rms = createElement("input", {
      type: "radio",
      attributes: {
        name: "level-meter-scale-type",
        value: "1",
        "data-setting-pointer": "/scaleType"
      }
    });
    rms.checked = canonical.scaleType === 1;
    const peakLabel = createElement("label");
    peakLabel.append(peak, createElement("span", { text: "Peak" }));
    const rmsLabel = createElement("label");
    rmsLabel.append(rms, createElement("span", { text: "RMS" }));
    typeGroup.append(peakLabel, rmsLabel);

    const reference = createElement("select", {
      attributes: {
        "data-setting-pointer": "/scaleReference"
      }
    });
    [
      ["0", "Relative to full scale"],
      ["1", "Volts"],
      ["2", "Micropascal"]
    ].forEach(([value, label]) => {
      const option = createElement("option", {
        text: label,
        attributes: { value }
      });
      reference.append(option);
    });
    reference.value = String(canonical.scaleReference);

    const range = createElement("input", {
      type: "number",
      attributes: {
        min: "1",
        step: "1",
        value: String(Math.abs(canonical.minLevel)),
        "data-setting-pointer": "/minLevel"
      }
    });
    range.value = String(Math.abs(canonical.minLevel));
    const rangeRow = createElement("div", {
      className: "level-meter-settings-range"
    });
    rangeRow.append(
      labelledField(
        "Scale range",
        range,
        "Java stores this as a negative lower dB limit."),
      createElement("span", {
        className: "level-meter-settings-unit",
        text: "dB"
      }));

    root.append(
      typeGroup,
      labelledField("Reference", reference),
      rangeRow);
    container.append(root);

    return {
      collect() {
        // LevelMeterDialog parses a Double, truncates it to int, then stores
        // -abs(value). Preserve that slightly unusual Java behaviour.
        const enteredRange = Number(range.value);
        if (!Number.isFinite(enteredRange)) {
          throw new Error("The scale range must have a numeric value");
        }
        const storedRange = -Math.abs(Math.trunc(enteredRange));
        if (storedRange >= 0) {
          throw new Error("The scale range must be greater than zero");
        }
        const scaleReference = Number(reference.value);
        if (![0, 1, 2].includes(scaleReference)) {
          throw new Error("The scale reference is invalid");
        }
        return {
          minLevel: storedRange,
          scaleReference,
          scaleType: rms.checked ? 1 : 0
        };
      },
      cleanup() {}
    };
  }

  globalThis.PamguardProjectLevelMeterSettings = Object.freeze({
    mountEditor,
    canonicalSettings
  });
})();
