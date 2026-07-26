(() => {
  "use strict";

  let editorSequence = 0;

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

  function field(label, control, options = {}) {
    const row = createElement("label", {
      className: `settings-field ${options.className || ""}`.trim()
    });
    row.append(
      createElement("span", {
        className: "settings-label",
        text: label
      }),
      control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", { text: options.help }));
    }
    return row;
  }

  function textInput(value, attributes = {}) {
    const input = createElement("input", { attributes });
    input.value = value ?? "";
    return input;
  }

  function numberInput(value, attributes = {}) {
    const input = createElement("input", {
      type: "number",
      attributes: { step: "any", ...attributes }
    });
    input.value = value;
    return input;
  }

  function requiredText(control, label) {
    const value = control.value.trim();
    if (!value) throw new Error(`${label} cannot be empty`);
    return value;
  }

  function nullableText(control) {
    const value = control.value.trim();
    return value ? value : null;
  }

  function finiteNumber(control, label, options = {}) {
    const value = Number(control.value);
    if (!Number.isFinite(value) ||
        (options.integer && !Number.isInteger(value)) ||
        (options.min !== undefined && value < options.min) ||
        (options.exclusiveMin !== undefined &&
          value <= options.exclusiveMin) ||
        (options.max !== undefined && value > options.max)) {
      throw new Error(`${label} has an invalid value`);
    }
    return value;
  }

  function nullableNumber(control, label) {
    if (!control.value.trim()) return null;
    return finiteNumber(control, label);
  }

  function interpolationSelect(value, label) {
    const select = createElement("select", {
      attributes: { "aria-label": label }
    });
    [
      [0, "Use latest"],
      [1, "Interpolate"],
      [2, "Use preceding"]
    ].forEach(([storedValue, text]) => {
      select.append(createElement("option", {
        text,
        attributes: { value: storedValue }
      }));
    });
    select.value = String(value);
    return select;
  }

  function coordinateFields(parent, controls, prefix) {
    const grid = createElement("div", {
      className: "array-coordinate-grid"
    });
    [
      ["X", "xM"],
      ["Y", "yM"],
      ["Z", "zM"]
    ].forEach(([axis, key]) => {
      const control = numberInput(controls.value[key]);
      controls[key] = control;
      grid.append(field(`${prefix} ${axis}`, control, { unit: "m" }));
    });
    parent.append(grid);
  }

  function errorFields(parent, controls, prefix) {
    const grid = createElement("div", {
      className: "array-coordinate-grid"
    });
    [
      ["X error", "xErrorM"],
      ["Y error", "yErrorM"],
      ["Z error", "zErrorM"]
    ].forEach(([axis, key]) => {
      const control = numberInput(controls.value[key], { min: 0 });
      controls[key] = control;
      grid.append(field(`${prefix} ${axis}`, control, { unit: "m" }));
    });
    parent.append(grid);
  }

  function createTabs(container, instanceId) {
    const definitions = [
      ["identity", "Instrument Identity"],
      ["environment", "Environment"],
      ["streamers", "Streamers"],
      ["hydrophones", "Hydrophone Elements"],
      ["mapping", "Channel Mapping"]
    ];
    const tabList = createElement("div", {
      className: "settings-tab-list",
      attributes: {
        role: "tablist",
        "aria-label": "Array Manager settings"
      }
    });
    const panels = new Map();
    definitions.forEach(([id, label], index) => {
      const selected = index === 0;
      const panelId = `${instanceId}-${id}`;
      const button = createElement("button", {
        type: "button",
        className: "settings-tab",
        text: label,
        attributes: {
          role: "tab",
          "aria-selected": selected ? "true" : "false",
          "aria-controls": panelId,
          "data-array-settings-tab": id
        }
      });
      const panel = createElement("section", {
        className: "settings-tab-panel",
        attributes: {
          id: panelId,
          role: "tabpanel",
          "data-array-settings-panel": id
        }
      });
      panel.hidden = !selected;
      button.addEventListener("click", () => {
        for (const candidate of
          tabList.querySelectorAll("[role='tab']")) {
          candidate.setAttribute(
            "aria-selected",
            candidate === button ? "true" : "false");
        }
        for (const [candidateId, candidate] of panels) {
          candidate.hidden = candidateId !== id;
        }
      });
      tabList.append(button);
      panels.set(id, panel);
    });
    container.append(tabList, ...panels.values());
    return panels;
  }

  function mountEditor(options) {
    if (!options?.container) {
      throw new Error("Array Manager editor requires a container");
    }
    const reportError = typeof options.reportError === "function"
      ? options.reportError
      : () => {};
    const draft = clone(options.settings || {});
    draft.streamers = Array.isArray(draft.streamers)
      ? draft.streamers
      : [];
    draft.hydrophones = Array.isArray(draft.hydrophones)
      ? draft.hydrophones
      : [];

    const instanceId = `array-settings-${++editorSequence}`;
    const panels = createTabs(options.container, instanceId);

    const arrayName = textInput(draft.arrayName, {
      required: "required",
      maxlength: 128,
      autocomplete: "off",
      "data-array-setting": "arrayName"
    });
    const instrumentType = textInput(draft.instrumentType, {
      maxlength: 128,
      autocomplete: "off",
      "data-array-setting": "instrumentType"
    });
    const instrumentId = textInput(draft.instrumentId, {
      maxlength: 128,
      autocomplete: "off",
      "data-array-setting": "instrumentId"
    });
    panels.get("identity").append(
      createElement("p", {
        className: "section-help",
        text: "Array identity and instrument metadata are shared by every " +
          "Acquisition and localisation unit in this project."
      }),
      field("Array name", arrayName),
      field("Instrument type", instrumentType, {
        help: "Optional make or instrument family."
      }),
      field("Instrument ID", instrumentId, {
        help: "Optional serial number or deployment identifier."
      }));

    const speedOfSound = numberInput(draft.speedOfSoundMps, {
      min: Number.MIN_VALUE,
      "data-array-setting": "speedOfSoundMps"
    });
    const speedError = numberInput(draft.speedOfSoundErrorMps, {
      min: 0,
      "data-array-setting": "speedOfSoundErrorMps"
    });
    const originInterpolation = interpolationSelect(
      draft.originInterpolation,
      "Streamer-origin interpolation");
    originInterpolation.dataset.arraySetting = "originInterpolation";
    const hydrophoneInterpolation = interpolationSelect(
      draft.hydrophoneInterpolation,
      "Hydrophone interpolation");
    hydrophoneInterpolation.dataset.arraySetting = "hydrophoneInterpolation";
    panels.get("environment").append(
      createElement("p", {
        className: "section-help",
        text: "These values are the authoritative PAMGuard environment used " +
          "when converting click delays into bearings and positions."
      }),
      field("Speed of sound", speedOfSound, { unit: "m/s" }),
      field("Speed-of-sound error", speedError, { unit: "m/s" }),
      field("Streamer origin", originInterpolation, {
        help: "PAMGuard PamArray originInterpolation."
      }),
      field("Hydrophone position", hydrophoneInterpolation, {
        help: "PAMGuard PamArray hydrophoneInterpolation."
      }));

    const streamerList = createElement("div", {
      className: "array-item-list",
      attributes: { "data-array-streamers": "" }
    });
    const streamerActions = createElement("div", {
      className: "array-editor-actions"
    });
    const addStreamer = createElement("button", {
      type: "button",
      className: "secondary",
      text: "Add streamer",
      attributes: { "data-array-add-streamer": "" }
    });
    streamerActions.append(addStreamer);
    panels.get("streamers").append(
      createElement("p", {
        className: "section-help",
        text: "Streamer IDs follow list order, matching PAMGuard's array " +
          "model. A referenced streamer cannot be removed."
      }),
      streamerList,
      streamerActions);

    const hydrophoneList = createElement("div", {
      className: "array-item-list",
      attributes: { "data-array-hydrophones": "" }
    });
    const hydrophoneActions = createElement("div", {
      className: "array-editor-actions"
    });
    const addHydrophone = createElement("button", {
      type: "button",
      className: "secondary",
      text: "Add hydrophone",
      attributes: { "data-array-add-hydrophone": "" }
    });
    hydrophoneActions.append(addHydrophone);
    panels.get("hydrophones").append(
      createElement("p", {
        className: "section-help",
        text: "Hydrophone channel is its stable zero-based position in the " +
          "PAMGuard array. Sensitivity is stored in dB re 1 V/\u00b5Pa."
      }),
      hydrophoneList,
      hydrophoneActions);

    const mappingBody = createElement("div", {
      className: "array-channel-map",
      attributes: { "data-array-channel-map": "" }
    });
    panels.get("mapping").append(
      createElement("p", {
        className: "section-help",
        text: "This is the global static geometry mapping. Sound " +
          "Acquisition maps each software input to one of these hydrophone " +
          "channels in its own Sampling settings."
      }),
      mappingBody);

    let streamerControls = [];
    let hydrophoneControls = [];

    function renderMapping() {
      mappingBody.replaceChildren();
      const table = createElement("table", {
        className: "array-mapping-table"
      });
      const head = createElement("thead");
      const headingRow = createElement("tr");
      ["Channel", "Hydrophone type", "Streamer", "Position (m)"].forEach(
        (heading) => headingRow.append(
          createElement("th", { text: heading })));
      head.append(headingRow);
      const body = createElement("tbody");
      hydrophoneControls.forEach((controls, channel) => {
        const streamerId = Number(controls.streamerId.value);
        const streamerName =
          streamerControls[streamerId]?.name?.value.trim() ||
          `Streamer ${streamerId}`;
        const row = createElement("tr");
        [
          channel,
          controls.type.value.trim() || "Unknown",
          streamerName,
          [
            controls.xM.value,
            controls.yM.value,
            controls.zM.value
          ].join(", ")
        ].forEach((value) => row.append(
          createElement("td", { text: value })));
        body.append(row);
      });
      table.append(head, body);
      mappingBody.append(table);
    }

    function renderStreamers() {
      streamerList.replaceChildren();
      streamerControls = [];
      draft.streamers.forEach((streamer, index) => {
        const controls = { value: streamer };
        const card = createElement("fieldset", {
          className: "settings-object array-item",
          attributes: { "data-array-streamer": index }
        });
        card.append(createElement("legend", {
          text: `Streamer ${index}`
        }));
        controls.name = textInput(streamer.name, {
          maxlength: 128,
          autocomplete: "off"
        });
        card.append(field("Name", controls.name, {
          help: "Optional operator-friendly streamer name."
        }));
        coordinateFields(card, controls, "Position");

        const advanced = createElement("details", {
          className: "authoritative-sections"
        });
        advanced.append(createElement("summary", {
          text: "Uncertainty, orientation and origin methods"
        }));
        const advancedBody = createElement("div", {
          className: "array-advanced-body"
        });
        errorFields(advancedBody, controls, "Position");
        [
          ["Heading", "headingDegrees"],
          ["Pitch", "pitchDegrees"],
          ["Roll", "rollDegrees"]
        ].forEach(([label, key]) => {
          const control = numberInput(streamer[key] ?? "");
          controls[key] = control;
          advancedBody.append(field(label, control, { unit: "\u00b0" }));
        });
        controls.locatorClass = textInput(streamer.locatorClass, {
          required: "required",
          autocomplete: "off"
        });
        controls.originClass = textInput(streamer.originClass, {
          required: "required",
          autocomplete: "off"
        });
        advancedBody.append(
          field("Locator class", controls.locatorClass),
          field("Origin class", controls.originClass));
        advanced.append(advancedBody);
        card.append(advanced);

        const remove = createElement("button", {
          type: "button",
          className: "secondary danger",
          text: "Remove streamer",
          attributes: {
            "data-array-remove-streamer": index
          }
        });
        const isLast = index === draft.streamers.length - 1;
        remove.disabled =
          draft.streamers.length <= 1 || !isLast;
        remove.title = !isLast
          ? "Only the last streamer can be removed because IDs follow order."
          : "";
        remove.addEventListener("click", () => {
          try {
            syncDraft();
            if (draft.hydrophones.some(
              (hydrophone) => hydrophone.streamerId === index)) {
              throw new Error(
                `Move hydrophones off streamer ${index} before removing it`);
            }
            draft.streamers.pop();
            renderStreamers();
            renderHydrophones();
          }
          catch (error) {
            reportError(error);
          }
        });
        card.append(remove);
        for (const control of card.querySelectorAll("input, select")) {
          control.addEventListener("input", renderMapping);
          control.addEventListener("change", renderMapping);
        }
        streamerControls.push(controls);
        streamerList.append(card);
      });
      addStreamer.disabled = draft.streamers.length >= 32;
      renderMapping();
    }

    function renderHydrophones() {
      hydrophoneList.replaceChildren();
      hydrophoneControls = [];
      draft.hydrophones.forEach((hydrophone, index) => {
        const controls = { value: hydrophone };
        const card = createElement("fieldset", {
          className: "settings-object array-item",
          attributes: { "data-array-hydrophone": index }
        });
        card.append(createElement("legend", {
          text: `Hydrophone channel ${index}`
        }));
        controls.streamerId = createElement("select", {
          attributes: {
            required: "required",
            "aria-label": `Streamer for hydrophone ${index}`
          }
        });
        draft.streamers.forEach((streamer, streamerId) => {
          controls.streamerId.append(createElement("option", {
            text: streamer.name || `Streamer ${streamerId}`,
            attributes: { value: streamerId }
          }));
        });
        controls.streamerId.value = String(hydrophone.streamerId);
        controls.type = textInput(hydrophone.type, {
          required: "required",
          maxlength: 128,
          autocomplete: "off"
        });
        card.append(
          field("Streamer", controls.streamerId),
          field("Type", controls.type));
        coordinateFields(card, controls, "Position");
        controls.sensitivityDb = numberInput(hydrophone.sensitivityDb);
        controls.preampGainDb = numberInput(hydrophone.preampGainDb);
        controls.bandwidthLow = numberInput(
          hydrophone.bandwidthHz?.[0], { min: 0 });
        controls.bandwidthHigh = numberInput(
          hydrophone.bandwidthHz?.[1], { min: 0 });
        card.append(
          field("Sensitivity", controls.sensitivityDb, {
            unit: "dB re 1 V/\u00b5Pa"
          }),
          field("Hydrophone preamplifier gain", controls.preampGainDb, {
            unit: "dB"
          }),
          field("Bandwidth low", controls.bandwidthLow, { unit: "Hz" }),
          field("Bandwidth high", controls.bandwidthHigh, { unit: "Hz" }));

        const advanced = createElement("details", {
          className: "authoritative-sections"
        });
        advanced.append(createElement("summary", {
          text: "Position uncertainty"
        }));
        const advancedBody = createElement("div", {
          className: "array-advanced-body"
        });
        errorFields(advancedBody, controls, "Position");
        advanced.append(advancedBody);
        card.append(advanced);

        const remove = createElement("button", {
          type: "button",
          className: "secondary danger",
          text: "Remove hydrophone",
          attributes: {
            "data-array-remove-hydrophone": index
          }
        });
        remove.disabled =
          draft.hydrophones.length <= 1 ||
          index !== draft.hydrophones.length - 1;
        remove.title = index !== draft.hydrophones.length - 1
          ? "Only the last hydrophone can be removed because channels " +
            "follow list order."
          : "";
        remove.addEventListener("click", () => {
          try {
            syncDraft();
            draft.hydrophones.pop();
            renderHydrophones();
          }
          catch (error) {
            reportError(error);
          }
        });
        card.append(remove);
        for (const control of card.querySelectorAll("input, select")) {
          control.addEventListener("input", renderMapping);
          control.addEventListener("change", renderMapping);
        }
        hydrophoneControls.push(controls);
        hydrophoneList.append(card);
      });
      addHydrophone.disabled = draft.hydrophones.length >= 32;
      renderMapping();
    }

    function readStreamers() {
      return streamerControls.map((controls, index) => ({
        id: index,
        name: nullableText(controls.name),
        xM: finiteNumber(controls.xM, `Streamer ${index} X position`),
        yM: finiteNumber(controls.yM, `Streamer ${index} Y position`),
        zM: finiteNumber(controls.zM, `Streamer ${index} Z position`),
        xErrorM: finiteNumber(
          controls.xErrorM,
          `Streamer ${index} X error`,
          { min: 0 }),
        yErrorM: finiteNumber(
          controls.yErrorM,
          `Streamer ${index} Y error`,
          { min: 0 }),
        zErrorM: finiteNumber(
          controls.zErrorM,
          `Streamer ${index} Z error`,
          { min: 0 }),
        headingDegrees: nullableNumber(
          controls.headingDegrees,
          `Streamer ${index} heading`),
        pitchDegrees: nullableNumber(
          controls.pitchDegrees,
          `Streamer ${index} pitch`),
        rollDegrees: nullableNumber(
          controls.rollDegrees,
          `Streamer ${index} roll`),
        locatorClass: requiredText(
          controls.locatorClass,
          `Streamer ${index} locator class`),
        originClass: requiredText(
          controls.originClass,
          `Streamer ${index} origin class`)
      }));
    }

    function readHydrophones(streamers) {
      return hydrophoneControls.map((controls, index) => {
        const streamerId = finiteNumber(
          controls.streamerId,
          `Hydrophone ${index} streamer`,
          { integer: true, min: 0, max: streamers.length - 1 });
        const low = finiteNumber(
          controls.bandwidthLow,
          `Hydrophone ${index} bandwidth low`,
          { min: 0 });
        const high = finiteNumber(
          controls.bandwidthHigh,
          `Hydrophone ${index} bandwidth high`,
          { min: 0 });
        if (high < low) {
          throw new Error(
            `Hydrophone ${index} bandwidth must be ordered`);
        }
        return {
          channel: index,
          streamerId,
          type: requiredText(
            controls.type,
            `Hydrophone ${index} type`),
          xM: finiteNumber(
            controls.xM,
            `Hydrophone ${index} X position`),
          yM: finiteNumber(
            controls.yM,
            `Hydrophone ${index} Y position`),
          zM: finiteNumber(
            controls.zM,
            `Hydrophone ${index} Z position`),
          xErrorM: finiteNumber(
            controls.xErrorM,
            `Hydrophone ${index} X error`,
            { min: 0 }),
          yErrorM: finiteNumber(
            controls.yErrorM,
            `Hydrophone ${index} Y error`,
            { min: 0 }),
          zErrorM: finiteNumber(
            controls.zErrorM,
            `Hydrophone ${index} Z error`,
            { min: 0 }),
          sensitivityDb: finiteNumber(
            controls.sensitivityDb,
            `Hydrophone ${index} sensitivity`),
          preampGainDb: finiteNumber(
            controls.preampGainDb,
            `Hydrophone ${index} preamplifier gain`),
          bandwidthHz: [low, high]
        };
      });
    }

    function collect() {
      const streamers = readStreamers();
      if (!streamers.length) {
        throw new Error("Array Manager requires at least one streamer");
      }
      const hydrophones = readHydrophones(streamers);
      if (!hydrophones.length) {
        throw new Error("Array Manager requires at least one hydrophone");
      }
      return {
        arrayName: requiredText(arrayName, "Array name"),
        instrumentType: nullableText(instrumentType),
        instrumentId: nullableText(instrumentId),
        speedOfSoundMps: finiteNumber(
          speedOfSound,
          "Speed of sound",
          { exclusiveMin: 0 }),
        speedOfSoundErrorMps: finiteNumber(
          speedError,
          "Speed-of-sound error",
          { min: 0 }),
        originInterpolation: finiteNumber(
          originInterpolation,
          "Streamer-origin interpolation",
          { integer: true, min: 0, max: 2 }),
        hydrophoneInterpolation: finiteNumber(
          hydrophoneInterpolation,
          "Hydrophone interpolation",
          { integer: true, min: 0, max: 2 }),
        streamers,
        hydrophones
      };
    }

    function syncDraft() {
      const next = collect();
      Object.assign(draft, next);
    }

    addStreamer.addEventListener("click", () => {
      try {
        syncDraft();
        const id = draft.streamers.length;
        draft.streamers.push({
          id,
          name: null,
          xM: 0,
          yM: 0,
          zM: 0,
          xErrorM: 0.1,
          yErrorM: 0.1,
          zErrorM: 0.1,
          headingDegrees: null,
          pitchDegrees: null,
          rollDegrees: null,
          locatorClass: "Array.ThreadingHydrophoneLocator",
          originClass: "Array.streamerOrigin.GPSOriginMethod"
        });
        renderStreamers();
        renderHydrophones();
      }
      catch (error) {
        reportError(error);
      }
    });

    addHydrophone.addEventListener("click", () => {
      try {
        syncDraft();
        const channel = draft.hydrophones.length;
        draft.hydrophones.push({
          channel,
          streamerId: 0,
          type: "Unknown",
          xM: 0,
          yM: channel === 0 ? 0 : -3 * channel,
          zM: -5,
          xErrorM: 0,
          yErrorM: 0,
          zErrorM: 0,
          sensitivityDb: -170,
          preampGainDb: 0,
          bandwidthHz: [0, 20000]
        });
        renderHydrophones();
      }
      catch (error) {
        reportError(error);
      }
    });

    renderStreamers();
    renderHydrophones();

    return {
      collect,
      focus: () => arrayName.focus()
    };
  }

  globalThis.PamguardProjectArraySettings = Object.freeze({
    mountEditor
  });
})();
