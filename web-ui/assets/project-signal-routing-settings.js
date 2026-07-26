(() => {
  "use strict";

  const CHANNEL_COUNT = 32;
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

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  function ensureStylesheet() {
    if (document.querySelector(
      "link[data-pamguard-project-signal-routing-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-signal-routing-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL(
          "project-signal-routing-settings.css",
          capturedScriptSource).href
      : "/assets/project-signal-routing-settings.css";
    document.head.append(link);
  }

  function finiteNumber(value, label) {
    const result = Number(value);
    if (!Number.isFinite(result)) {
      throw new Error(`${label} must be a finite number`);
    }
    return result;
  }

  function amplifierDefaults() {
    return {
      channelSettings: Array.from(
        { length: CHANNEL_COUNT },
        () => ({ gainDb: 0, invert: false }))
    };
  }

  function patchDefaults() {
    return {
      routingMatrix: Array.from(
        { length: CHANNEL_COUNT },
        (_, input) => Array.from(
          { length: CHANNEL_COUNT },
          (_, output) => input === output)),
      advancedGainMatrix: null
    };
  }

  function canonicalAmplifierSettings(settings) {
    const supplied = settings?.channelSettings;
    if (!Array.isArray(supplied) ||
        supplied.length !== CHANNEL_COUNT) {
      return amplifierDefaults();
    }
    return {
      channelSettings: supplied.map((row, channel) => ({
        gainDb: finiteNumber(
          row?.gainDb,
          `Channel ${channel} gain`),
        invert: Boolean(row?.invert)
      }))
    };
  }

  function canonicalBooleanMatrix(value, label) {
    if (!Array.isArray(value) || value.length !== CHANNEL_COUNT) {
      throw new Error(`${label} must contain 32 input rows`);
    }
    return value.map((row, input) => {
      if (!Array.isArray(row) || row.length !== CHANNEL_COUNT) {
        throw new Error(
          `${label} input ${input} must contain 32 outputs`);
      }
      return row.map(Boolean);
    });
  }

  function canonicalGainMatrix(value) {
    if (!Array.isArray(value) || value.length !== CHANNEL_COUNT) {
      throw new Error(
        "Advanced gain matrix must contain 32 input rows");
    }
    return value.map((row, input) => {
      if (!Array.isArray(row) || row.length !== CHANNEL_COUNT) {
        throw new Error(
          `Advanced gain matrix input ${input} must contain 32 outputs`);
      }
      return row.map((gain, output) => finiteNumber(
        gain,
        `Advanced gain ${input} to ${output}`));
    });
  }

  function canonicalPatchSettings(settings) {
    const defaults = patchDefaults();
    const routing = settings?.routingMatrix === undefined
      ? defaults.routingMatrix
      : canonicalBooleanMatrix(
          settings.routingMatrix,
          "Routing matrix");
    const advanced = settings?.advancedGainMatrix == null
      ? null
      : canonicalGainMatrix(settings.advancedGainMatrix);
    return {
      routingMatrix: routing,
      advancedGainMatrix: advanced
    };
  }

  function portableBitmap(value) {
    const bitmap = Number(value);
    return Number.isInteger(bitmap) &&
      bitmap >= 0 &&
      bitmap <= 0xffffffff
      ? bitmap
      : 0;
  }

  function selectedChannels(bitmap) {
    const normalized = portableBitmap(bitmap) || 3;
    return Array.from(
      { length: CHANNEL_COUNT },
      (_, channel) => channel)
      .filter((channel) =>
        Math.floor(normalized / (2 ** channel)) % 2 === 1);
  }

  function sectionHeading(title, description) {
    const heading = createElement("div", {
      className: "signal-routing-heading"
    });
    heading.append(
      createElement("h3", { text: title }),
      createElement("p", {
        className: "section-help",
        text: description
      }));
    return heading;
  }

  function actionButton(text, action) {
    return createElement("button", {
      type: "button",
      className: "secondary",
      text,
      attributes: {
        "data-signal-routing-action": action
      }
    });
  }

  function matrixCellControl(type, pointer) {
    return createElement("input", {
      type,
      attributes: {
        "data-setting-pointer": pointer,
        "data-signal-routing-setting": pointer,
        "aria-label": pointer
      }
    });
  }

  function mountAmplifierEditor(options) {
    const {
      container,
      settings,
      getAvailableChannelBitmap = () => 0,
      sourceSelect = null,
      reportError = () => {}
    } = options;
    const draft = canonicalAmplifierSettings(settings);
    const editor = createElement("div", {
      className: "signal-routing-editor",
      attributes: {
        "data-signal-routing-editor": "amplifier"
      }
    });
    const actions = createElement("div", {
      className: "signal-routing-actions"
    });
    const reset = actionButton("Restore 0 dB defaults", "reset-amplifier");
    actions.append(reset);
    const tableHost = createElement("div", {
      className: "signal-routing-table-host"
    });
    editor.append(
      sectionHeading(
        "Channel Gains",
        "PAMGuard applies one signed gain to each absolute channel. " +
          "Only channels published by the selected raw-data source are shown."),
      actions,
      tableHost);
    container.append(editor);

    let visibleControls = new Map();
    const syncVisible = () => {
      for (const [channel, controls] of visibleControls) {
        draft.channelSettings[channel] = {
          gainDb: finiteNumber(
            controls.gain.value,
            `Channel ${channel} gain`),
          invert: controls.invert.checked
        };
      }
    };
    const render = () => {
      syncVisible();
      visibleControls = new Map();
      const table = createElement("table", {
        className: "signal-routing-table amplifier-gain-table"
      });
      const head = createElement("thead");
      const headRow = createElement("tr");
      ["Channel", "Gain (dB)", "Invert"].forEach((label) =>
        headRow.append(createElement("th", { text: label })));
      head.append(headRow);
      const body = createElement("tbody");
      for (const channel of selectedChannels(
        getAvailableChannelBitmap())) {
        const row = createElement("tr", {
          attributes: {
            "data-signal-routing-channel": channel
          }
        });
        const gain = matrixCellControl(
          "number",
          `/channelSettings/${channel}/gainDb`);
        gain.step = "any";
        gain.value = draft.channelSettings[channel].gainDb;
        const invert = matrixCellControl(
          "checkbox",
          `/channelSettings/${channel}/invert`);
        invert.checked = draft.channelSettings[channel].invert;
        row.append(
          createElement("th", {
            text: channel,
            attributes: { scope: "row" }
          }),
          (() => {
            const cell = createElement("td");
            cell.append(gain);
            return cell;
          })(),
          (() => {
            const cell = createElement("td");
            cell.append(invert);
            return cell;
          })());
        body.append(row);
        visibleControls.set(channel, { gain, invert });
      }
      table.append(head, body);
      tableHost.replaceChildren(table);
    };
    const guardedRender = () => {
      try {
        render();
      }
      catch (error) {
        reportError(error);
      }
    };
    const restoreDefaults = () => {
      draft.channelSettings = amplifierDefaults().channelSettings;
      visibleControls = new Map();
      guardedRender();
    };
    reset.addEventListener("click", restoreDefaults);
    sourceSelect?.addEventListener("change", guardedRender);
    render();

    return {
      collect() {
        syncVisible();
        return canonicalAmplifierSettings(draft);
      },
      cleanup() {
        sourceSelect?.removeEventListener?.("change", guardedRender);
      }
    };
  }

  function checkboxMatrixTable(
    matrix,
    inputChannels,
    controls,
    pointerRoot) {
    const table = createElement("table", {
      className: "signal-routing-table patch-routing-matrix"
    });
    const head = createElement("thead");
    const heading = createElement("tr");
    heading.append(createElement("th", {
      text: "In \\ Out",
      attributes: { scope: "col" }
    }));
    for (let output = 0; output < CHANNEL_COUNT; output++) {
      heading.append(createElement("th", {
        text: output,
        attributes: { scope: "col" }
      }));
    }
    heading.append(createElement("th", {
      text: "Input",
      attributes: { scope: "col" }
    }));
    head.append(heading);
    const body = createElement("tbody");
    for (const input of inputChannels) {
      const row = createElement("tr", {
        attributes: {
          "data-signal-routing-input": input
        }
      });
      row.append(createElement("th", {
        text: input,
        attributes: { scope: "row" }
      }));
      for (let output = 0; output < CHANNEL_COUNT; output++) {
        const pointer = `${pointerRoot}/${input}/${output}`;
        const control = matrixCellControl("checkbox", pointer);
        control.checked = matrix[input][output];
        controls.set(`${input}/${output}`, control);
        const cell = createElement("td");
        cell.append(control);
        row.append(cell);
      }
      row.append(createElement("th", {
        text: input,
        attributes: { scope: "row" }
      }));
      body.append(row);
    }
    table.append(head, body);
    return table;
  }

  function gainMatrixTable(
    matrix,
    inputChannels,
    controls) {
    const table = createElement("table", {
      className:
        "signal-routing-table patch-routing-matrix patch-gain-matrix"
    });
    const head = createElement("thead");
    const heading = createElement("tr");
    heading.append(createElement("th", {
      text: "In \\ Out",
      attributes: { scope: "col" }
    }));
    for (let output = 0; output < CHANNEL_COUNT; output++) {
      heading.append(createElement("th", {
        text: output,
        attributes: { scope: "col" }
      }));
    }
    head.append(heading);
    const body = createElement("tbody");
    for (const input of inputChannels) {
      const row = createElement("tr", {
        attributes: {
          "data-signal-routing-gain-input": input
        }
      });
      row.append(createElement("th", {
        text: input,
        attributes: { scope: "row" }
      }));
      for (let output = 0; output < CHANNEL_COUNT; output++) {
        const pointer = `/advancedGainMatrix/${input}/${output}`;
        const control = matrixCellControl("number", pointer);
        control.step = "any";
        control.value = matrix[input][output];
        controls.set(`${input}/${output}`, control);
        const cell = createElement("td");
        cell.append(control);
        row.append(cell);
      }
      body.append(row);
    }
    table.append(head, body);
    return table;
  }

  function mountPatchPanelEditor(options) {
    const {
      container,
      settings,
      getAvailableChannelBitmap = () => 0,
      sourceSelect = null,
      reportError = () => {}
    } = options;
    const draft = canonicalPatchSettings(settings);
    const editor = createElement("div", {
      className: "signal-routing-editor",
      attributes: {
        "data-signal-routing-editor": "patch-panel"
      }
    });
    const actions = createElement("div", {
      className: "signal-routing-actions"
    });
    const identity = actionButton("Restore identity", "identity");
    const clear = actionButton("Clear routes", "clear");
    actions.append(identity, clear);
    const matrixHost = createElement("div", {
      className:
        "signal-routing-table-host signal-routing-matrix-host"
    });

    const advanced = createElement("details", {
      className: "signal-routing-advanced"
    });
    const advancedSummary = createElement("summary", {
      text: "Advanced gain matrix (C++ extension)"
    });
    const advancedEnable = matrixCellControl(
      "checkbox",
      "/advancedGainMatrix/enabled");
    const advancedEnableLabel = createElement("label", {
      className: "signal-routing-advanced-enable"
    });
    advancedEnableLabel.append(
      advancedEnable,
      createElement("span", {
        text: "Use arbitrary coefficients instead of Java's unit routes"
      }));
    const advancedHelp = createElement("p", {
      className: "section-help",
      text:
        "This optional matrix is not a PAMGuard Java setting. When enabled, " +
        "its signed coefficients replace the 0/1 routing matrix at runtime."
    });
    const advancedHost = createElement("div", {
      className:
        "signal-routing-table-host signal-routing-matrix-host"
    });
    advanced.append(
      advancedSummary,
      advancedEnableLabel,
      advancedHelp,
      advancedHost);
    editor.append(
      sectionHeading(
        "Channel Connections",
        "Rows are available input channels and columns are output " +
          "channels, matching PAMGuard's Patch Panel dialog."),
      actions,
      matrixHost,
      advanced);
    container.append(editor);

    let routeControls = new Map();
    let gainControls = new Map();
    const syncRoutes = () => {
      for (const [key, control] of routeControls) {
        const [input, output] = key.split("/").map(Number);
        draft.routingMatrix[input][output] = control.checked;
      }
    };
    const syncGains = () => {
      if (!draft.advancedGainMatrix) return;
      for (const [key, control] of gainControls) {
        const [input, output] = key.split("/").map(Number);
        draft.advancedGainMatrix[input][output] = finiteNumber(
          control.value,
          `Advanced gain ${input} to ${output}`);
      }
    };
    const activeInputs = () =>
      selectedChannels(getAvailableChannelBitmap());
    const renderRoutes = () => {
      syncRoutes();
      routeControls = new Map();
      matrixHost.replaceChildren(checkboxMatrixTable(
        draft.routingMatrix,
        activeInputs(),
        routeControls,
        "/routingMatrix"));
    };
    const renderAdvanced = () => {
      syncGains();
      advancedEnable.checked = draft.advancedGainMatrix !== null;
      advancedHost.replaceChildren();
      if (!draft.advancedGainMatrix) {
        advancedHost.append(createElement("p", {
          className: "section-help",
          text: "Advanced coefficients are disabled."
        }));
        return;
      }
      gainControls = new Map();
      advancedHost.append(gainMatrixTable(
        draft.advancedGainMatrix,
        activeInputs(),
        gainControls));
    };
    const guardedRender = () => {
      try {
        renderRoutes();
        renderAdvanced();
      }
      catch (error) {
        reportError(error);
      }
    };

    identity.addEventListener("click", () => {
      draft.routingMatrix = patchDefaults().routingMatrix;
      routeControls = new Map();
      guardedRender();
    });
    clear.addEventListener("click", () => {
      draft.routingMatrix = Array.from(
        { length: CHANNEL_COUNT },
        () => Array(CHANNEL_COUNT).fill(false));
      routeControls = new Map();
      guardedRender();
    });
    advancedEnable.addEventListener("change", () => {
      try {
        syncRoutes();
        syncGains();
        draft.advancedGainMatrix = advancedEnable.checked
          ? draft.routingMatrix.map((row) =>
              row.map((selected) => selected ? 1 : 0))
          : null;
        renderAdvanced();
      }
      catch (error) {
        reportError(error);
      }
    });
    sourceSelect?.addEventListener("change", guardedRender);
    renderRoutes();
    renderAdvanced();

    return {
      collect() {
        syncRoutes();
        syncGains();
        const available = new Set(activeInputs());
        const result = canonicalPatchSettings(draft);
        for (let input = 0; input < CHANNEL_COUNT; input++) {
          if (!available.has(input)) {
            result.routingMatrix[input].fill(false);
          }
        }
        return result;
      },
      cleanup() {
        sourceSelect?.removeEventListener?.("change", guardedRender);
      }
    };
  }

  function mountEditor(options) {
    ensureStylesheet();
    if (options?.typeId === "pamguard.amplifier") {
      return mountAmplifierEditor(options);
    }
    if (options?.typeId === "pamguard.patch-panel") {
      return mountPatchPanelEditor(options);
    }
    throw new Error(
      `Unsupported signal-routing editor type ${options?.typeId || ""}`);
  }

  globalThis.PamguardProjectSignalRoutingSettings = Object.freeze({
    mountEditor,
    canonicalAmplifierSettings,
    canonicalPatchSettings
  });
})();
