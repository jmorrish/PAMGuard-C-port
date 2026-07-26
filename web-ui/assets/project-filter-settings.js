(() => {
  "use strict";

  const CHANNEL_COUNT = 32;
  const FILTER_TYPES = Object.freeze([
    ["none", "None"],
    ["butterworth", "IIR Butterworth"],
    ["chebyshev", "IIR Chebyshev"],
    ["firWindow", "FIR Filter (Window Method)"],
    ["firArbitrary", "Arbitrary FIR Filter"],
    ["fft", "FFT Filter"]
  ]);
  const FILTER_BANDS = Object.freeze([
    ["highPass", "High Pass"],
    ["bandPass", "Band Pass"],
    ["bandStop", "Band Stop"],
    ["lowPass", "Low Pass"]
  ]);
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
      "link[data-pamguard-project-filter-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-filter-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL("project-filter-settings.css", capturedScriptSource).href
      : "/assets/project-filter-settings.css";
    document.head.append(link);
  }

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  function finiteNumber(value, label, options = {}) {
    const result = Number(value);
    if (!Number.isFinite(result) ||
        (options.integer && !Number.isInteger(result)) ||
        (options.min !== undefined && result < options.min) ||
        (options.exclusiveMin !== undefined &&
          result <= options.exclusiveMin) ||
        (options.max !== undefined && result > options.max)) {
      throw new Error(`${label} has an invalid value`);
    }
    return result;
  }

  function filterDefaults() {
    return {
      type: "butterworth",
      band: "bandPass",
      order: 4,
      lowPassFreqHz: 20000,
      highPassFreqHz: 2000,
      passBandRippleDb: 2,
      stopBandRippleDb: 2,
      chebyGamma: 3,
      arbitraryFrequenciesHz: [],
      arbitraryGainsDb: []
    };
  }

  function canonicalFilterParams(value = {}) {
    const defaults = filterDefaults();
    const type = FILTER_TYPES.some(([candidate]) =>
      candidate === value.type)
      ? value.type
      : defaults.type;
    const band = FILTER_BANDS.some(([candidate]) =>
      candidate === value.band)
      ? value.band
      : defaults.band;
    const frequencies = Array.isArray(value.arbitraryFrequenciesHz)
      ? value.arbitraryFrequenciesHz.map((item, index) =>
          finiteNumber(item, `Arbitrary frequency ${index}`, { min: 0 }))
      : [];
    const gains = Array.isArray(value.arbitraryGainsDb)
      ? value.arbitraryGainsDb.map((item, index) =>
          finiteNumber(item, `Arbitrary gain ${index}`))
      : [];
    return {
      type,
      band,
      order: finiteNumber(
        value.order ?? defaults.order,
        "Filter order",
        { integer: true, min: 1, max: 32 }),
      lowPassFreqHz: finiteNumber(
        value.lowPassFreqHz ?? defaults.lowPassFreqHz,
        "Low-pass frequency",
        { min: 0 }),
      highPassFreqHz: finiteNumber(
        value.highPassFreqHz ?? defaults.highPassFreqHz,
        "High-pass frequency",
        { min: 0 }),
      passBandRippleDb: finiteNumber(
        value.passBandRippleDb ?? defaults.passBandRippleDb,
        "Pass-band ripple",
        { min: 0 }),
      stopBandRippleDb: finiteNumber(
        value.stopBandRippleDb ?? defaults.stopBandRippleDb,
        "Stop-band ripple",
        { min: 0 }),
      chebyGamma: finiteNumber(
        value.chebyGamma ?? defaults.chebyGamma,
        "FIR gamma",
        { exclusiveMin: 0 }),
      arbitraryFrequenciesHz: frequencies,
      arbitraryGainsDb: gains
    };
  }

  function portableBitmap(value) {
    const result = Number(value);
    return Number.isInteger(result) &&
      result >= 0 &&
      result <= 0xffffffff
      ? result
      : 0;
  }

  function channelsIn(bitmap) {
    const result = [];
    const normalized = portableBitmap(bitmap);
    for (let channel = 0; channel < CHANNEL_COUNT; channel++) {
      if (Math.floor(normalized / (2 ** channel)) % 2 === 1) {
        result.push(channel);
      }
    }
    return result;
  }

  function field(label, control, options = {}) {
    const row = createElement("label", {
      className: "filter-settings-field"
    });
    row.append(
      createElement("span", {
        className: "filter-settings-label",
        text: label
      }),
      control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "filter-settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", {
        className: "filter-settings-help",
        text: options.help
      }));
    }
    return row;
  }

  function pointerControl(tag, pointer, options = {}) {
    return createElement(tag, {
      type: options.type,
      attributes: {
        "data-setting-pointer": pointer,
        "data-filter-setting": pointer,
        ...options.attributes
      }
    });
  }

  function numberControl(value, pointer, attributes = {}) {
    const control = pointerControl("input", pointer, {
      type: "number",
      attributes: { step: "any", ...attributes }
    });
    control.value = value;
    return control;
  }

  function selectControl(value, pointer, values) {
    const select = pointerControl("select", pointer);
    for (const [stored, label] of values) {
      const option = createElement("option", {
        text: label,
        attributes: { value: stored }
      });
      option.selected = stored === value;
      select.append(option);
    }
    select.value = value;
    return select;
  }

  function mountChannelPicker(options) {
    const {
      container,
      initialBitmap,
      getAvailableChannelBitmap,
      sourceSelect
    } = options;
    let selectedBitmap = portableBitmap(initialBitmap);
    let controls = [];
    const section = createElement("fieldset", {
      className: "filter-settings-channels"
    });
    section.append(createElement("legend", {
      text: "Channels"
    }));
    const actions = createElement("div", {
      className: "filter-settings-actions"
    });
    const all = createElement("button", {
      type: "button",
      className: "secondary",
      text: "All available",
      attributes: { "data-filter-action": "all-channels" }
    });
    const none = createElement("button", {
      type: "button",
      className: "secondary",
      text: "None",
      attributes: { "data-filter-action": "no-channels" }
    });
    const choices = createElement("div", {
      className: "filter-settings-channel-list"
    });
    actions.append(all, none);
    section.append(actions, choices);
    container.append(section);

    const sync = () => {
      if (!controls.length) return;
      selectedBitmap = controls.reduce(
        (bitmap, item) =>
          bitmap + (item.control.checked ? 2 ** item.channel : 0),
        0);
    };
    const render = () => {
      sync();
      choices.replaceChildren();
      controls = [];
      for (const channel of channelsIn(
        getAvailableChannelBitmap())) {
        const control = pointerControl(
          "input",
          `/channelBitmap/${channel}`,
          { type: "checkbox" });
        control.checked =
          Math.floor(selectedBitmap / (2 ** channel)) % 2 === 1;
        const label = createElement("label", {
          className: "filter-settings-channel"
        });
        label.append(
          control,
          createElement("span", { text: `Channel ${channel}` }));
        choices.append(label);
        controls.push({ channel, control });
      }
      if (!controls.length) {
        choices.append(createElement("p", {
          className: "section-help",
          text: "Choose a raw-audio source to expose its channels."
        }));
      }
      all.disabled = controls.length === 0;
      none.disabled = controls.length === 0;
    };
    const sourceChanged = () => render();
    all.addEventListener("click", () => {
      controls.forEach((item) => {
        item.control.checked = true;
      });
    });
    none.addEventListener("click", () => {
      controls.forEach((item) => {
        item.control.checked = false;
      });
    });
    sourceSelect?.addEventListener("change", sourceChanged);
    render();

    return {
      collect() {
        sync();
        if (selectedBitmap === 0) {
          throw new Error("Select at least one channel");
        }
        return selectedBitmap;
      },
      cleanup() {
        sourceSelect?.removeEventListener?.("change", sourceChanged);
      }
    };
  }

  function validateFilter(params, sampleRateHz) {
    const isIir =
      params.type === "butterworth" ||
      params.type === "chebyshev";
    const isFir =
      params.type === "firWindow" ||
      params.type === "firArbitrary";
    if (isIir && params.order > 1 && params.order % 2 !== 0) {
      throw new Error(
        "PAMGuard IIR order must be 1 or an even number");
    }
    if (isFir && params.order > 16) {
      throw new Error("FIR order exponent must be between 1 and 16");
    }
    if (params.type === "chebyshev" &&
        params.passBandRippleDb <= 0) {
      throw new Error("Chebyshev pass-band ripple must be positive");
    }
    if (params.type === "none") return params;
    if (params.type === "firArbitrary") {
      if (params.arbitraryFrequenciesHz.length < 2 ||
          params.arbitraryFrequenciesHz.length !==
            params.arbitraryGainsDb.length) {
        throw new Error(
          "Arbitrary FIR needs at least two frequency/gain points");
      }
      params.arbitraryFrequenciesHz.forEach((frequency, index) => {
        if (index > 0 &&
            frequency < params.arbitraryFrequenciesHz[index - 1]) {
          throw new Error(
            "Arbitrary FIR frequencies must be ordered");
        }
        if (sampleRateHz > 0 && frequency > sampleRateHz / 2) {
          throw new Error(
            "Arbitrary FIR frequency exceeds the Nyquist frequency");
        }
      });
      return params;
    }
    const usesHigh = [
      "highPass",
      "bandPass",
      "bandStop"
    ].includes(params.band);
    const usesLow = [
      "lowPass",
      "bandPass",
      "bandStop"
    ].includes(params.band);
    if (usesHigh && params.highPassFreqHz <= 0) {
      throw new Error("Active high-pass frequency must be positive");
    }
    if (usesLow && params.lowPassFreqHz <= 0) {
      throw new Error("Active low-pass frequency must be positive");
    }
    if (usesHigh && usesLow &&
        params.highPassFreqHz > params.lowPassFreqHz) {
      throw new Error(
        "High-pass frequency must not exceed low-pass frequency");
    }
    if (sampleRateHz > 0 &&
        ((usesHigh && params.highPassFreqHz > sampleRateHz / 2) ||
         (usesLow && params.lowPassFreqHz > sampleRateHz / 2))) {
      throw new Error(
        "Active filter frequency exceeds the source Nyquist frequency");
    }
    return params;
  }

  function mountFilterPanel(options) {
    const {
      container,
      settings,
      pointerPrefix,
      getDesignSampleRate,
      reportError
    } = options;
    const draft = canonicalFilterParams(settings);
    const panel = createElement("section", {
      className: "filter-settings-design",
      attributes: {
        "data-filter-design": pointerPrefix || "/"
      }
    });
    const controls = createElement("div", {
      className: "filter-settings-controls"
    });
    const response = createElement("aside", {
      className: "filter-settings-response"
    });
    response.append(
      createElement("h4", { text: "Filter response" }),
      createElement("div", {
        className: "filter-settings-response-plot",
        attributes: {
          "data-filter-response-preview": ""
        }
      }),
      createElement("small", {
        className: "filter-settings-help",
        text:
          "Shape guide only. Runtime coefficients and fixture comparisons " +
          "use the exact PAMGuard-derived filter implementation."
      }));
    const responseText = createElement("p", {
      className: "section-help"
    });
    response.append(responseText);

    const pointer = (name) => `${pointerPrefix}/${name}`;
    const type = selectControl(
      draft.type,
      pointer("type"),
      FILTER_TYPES);
    const band = selectControl(
      draft.band,
      pointer("band"),
      FILTER_BANDS);
    const highPass = numberControl(
      draft.highPassFreqHz,
      pointer("highPassFreqHz"),
      { min: 0 });
    const lowPass = numberControl(
      draft.lowPassFreqHz,
      pointer("lowPassFreqHz"),
      { min: 0 });
    const order = numberControl(
      draft.order,
      pointer("order"),
      { min: 1, max: 32, step: 1 });
    const ripple = numberControl(
      draft.passBandRippleDb,
      pointer("passBandRippleDb"),
      { min: 0 });
    const stopRipple = numberControl(
      draft.stopBandRippleDb,
      pointer("stopBandRippleDb"),
      { min: 0 });
    const gamma = numberControl(
      draft.chebyGamma,
      pointer("chebyGamma"),
      { min: Number.MIN_VALUE });
    controls.append(
      field("Filter Type", type),
      field("Filter Response", band),
      field("High Pass", highPass, { unit: "Hz" }),
      field("Low Pass", lowPass, { unit: "Hz" }),
      field("Filter order", order),
      field("Pass band ripple", ripple, { unit: "dB" }),
      field("Stop band ripple", stopRipple, { unit: "dB" }),
      field("FIR gamma", gamma));

    const arbitrary = createElement("fieldset", {
      className: "filter-settings-arbitrary"
    });
    arbitrary.append(createElement("legend", {
      text: "Filter shape"
    }));
    const arbitraryRows = createElement("div", {
      className: "filter-settings-arbitrary-rows"
    });
    const addPoint = createElement("button", {
      type: "button",
      className: "secondary",
      text: "Add point",
      attributes: { "data-filter-action": "add-point" }
    });
    arbitrary.append(arbitraryRows, addPoint);
    controls.append(arbitrary);
    panel.append(controls, response);
    container.append(panel);

    let pointControls = [];
    const syncPoints = () => {
      draft.arbitraryFrequenciesHz = pointControls.map(
        (row, index) => finiteNumber(
          row.frequency.value,
          `Arbitrary frequency ${index}`,
          { min: 0 }));
      draft.arbitraryGainsDb = pointControls.map(
        (row, index) => finiteNumber(
          row.gain.value,
          `Arbitrary gain ${index}`));
    };
    const renderPoints = () => {
      arbitraryRows.replaceChildren();
      pointControls = [];
      draft.arbitraryFrequenciesHz.forEach((frequency, index) => {
        const row = createElement("div", {
          className: "filter-settings-arbitrary-row"
        });
        const frequencyControl = numberControl(
          frequency,
          `${pointer("arbitraryFrequenciesHz")}/${index}`,
          { min: 0 });
        const gainControl = numberControl(
          draft.arbitraryGainsDb[index] ?? 0,
          `${pointer("arbitraryGainsDb")}/${index}`);
        const remove = createElement("button", {
          type: "button",
          className: "secondary",
          text: "Remove",
          attributes: {
            "data-filter-action": `remove-point-${index}`
          }
        });
        remove.addEventListener("click", () => {
          try {
            syncPoints();
            draft.arbitraryFrequenciesHz.splice(index, 1);
            draft.arbitraryGainsDb.splice(index, 1);
            renderPoints();
            update();
          }
          catch (error) {
            reportError(error);
          }
        });
        row.append(
          field("Frequency", frequencyControl, { unit: "Hz" }),
          field("Gain", gainControl, { unit: "dB" }),
          remove);
        arbitraryRows.append(row);
        pointControls.push({
          frequency: frequencyControl,
          gain: gainControl
        });
      });
    };

    const readScalar = () => canonicalFilterParams({
      type: type.value,
      band: band.value,
      order: finiteNumber(
        order.value,
        "Filter order",
        { integer: true, min: 1, max: 32 }),
      lowPassFreqHz: finiteNumber(
        lowPass.value,
        "Low-pass frequency",
        { min: 0 }),
      highPassFreqHz: finiteNumber(
        highPass.value,
        "High-pass frequency",
        { min: 0 }),
      passBandRippleDb: finiteNumber(
        ripple.value,
        "Pass-band ripple",
        { min: 0 }),
      stopBandRippleDb: finiteNumber(
        stopRipple.value,
        "Stop-band ripple",
        { min: 0 }),
      chebyGamma: finiteNumber(
        gamma.value,
        "FIR gamma",
        { exclusiveMin: 0 }),
      arbitraryFrequenciesHz: draft.arbitraryFrequenciesHz,
      arbitraryGainsDb: draft.arbitraryGainsDb
    });
    const update = () => {
      const haveFilter = type.value !== "none";
      const isArbitrary = type.value === "firArbitrary";
      const usesOrder = [
        "butterworth",
        "chebyshev",
        "firWindow",
        "firArbitrary"
      ].includes(type.value);
      band.disabled = !haveFilter || isArbitrary;
      highPass.disabled = !haveFilter ||
        isArbitrary ||
        band.value === "lowPass";
      lowPass.disabled = !haveFilter ||
        isArbitrary ||
        band.value === "highPass";
      order.disabled = !usesOrder;
      ripple.disabled = type.value !== "chebyshev";
      stopRipple.disabled = true;
      gamma.disabled = ![
        "firWindow",
        "firArbitrary"
      ].includes(type.value);
      arbitrary.hidden = !isArbitrary;
      const sampleRate = Number(getDesignSampleRate()) || 0;
      const nyquist = sampleRate > 0 ? sampleRate / 2 : 0;
      responseText.textContent =
        `${FILTER_TYPES.find(([value]) =>
          value === type.value)?.[1] || type.value} · ` +
        `${FILTER_BANDS.find(([value]) =>
          value === band.value)?.[1] || band.value}` +
        (nyquist > 0
          ? ` · design Nyquist ${nyquist.toLocaleString()} Hz`
          : " · source sample rate unavailable");
      response.querySelector(
        "[data-filter-response-preview]").dataset.type = type.value;
      response.querySelector(
        "[data-filter-response-preview]").dataset.band = band.value;
    };
    const guardedUpdate = () => {
      try {
        update();
      }
      catch (error) {
        reportError(error);
      }
    };
    [type, band, highPass, lowPass, order, ripple, gamma]
      .forEach((control) =>
        control.addEventListener("change", guardedUpdate));
    addPoint.addEventListener("click", () => {
      try {
        syncPoints();
        const previous =
          draft.arbitraryFrequenciesHz.at(-1) ?? 0;
        draft.arbitraryFrequenciesHz.push(previous + 1000);
        draft.arbitraryGainsDb.push(0);
        renderPoints();
        update();
      }
      catch (error) {
        reportError(error);
      }
    });
    renderPoints();
    update();

    return {
      collect() {
        syncPoints();
        const result = readScalar();
        result.arbitraryFrequenciesHz =
          clone(draft.arbitraryFrequenciesHz);
        result.arbitraryGainsDb =
          clone(draft.arbitraryGainsDb);
        return validateFilter(
          result,
          Number(getDesignSampleRate()) || 0);
      },
      setDefaultLowPass(outputSampleRateHz, sourceSampleRateHz = 0) {
        const effectiveRate = sourceSampleRateHz > 0
          ? Math.min(outputSampleRateHz, sourceSampleRateHz)
          : outputSampleRateHz;
        type.value = "butterworth";
        band.value = "lowPass";
        order.value = "6";
        lowPass.value = String(effectiveRate / 2);
        update();
      },
      refresh: update
    };
  }

  function mountEditor(options) {
    ensureStylesheet();
    const {
      container,
      typeId,
      settings = {},
      getAvailableChannelBitmap = () => 0,
      getSourceSampleRate = () => 0,
      sourceSelect = null,
      reportError = () => {}
    } = options;
    const root = createElement("div", {
      className: "filter-settings-editor",
      attributes: {
        "data-pamguard-filter-settings-editor": typeId
      }
    });
    container.append(root);
    const isDecimator = typeId === "pamguard.decimator";
    if (!isDecimator && typeId !== "pamguard.filter") {
      throw new Error(`Unsupported filter settings type ${typeId}`);
    }

    const channelPicker = mountChannelPicker({
      container: root,
      initialBitmap: settings.channelBitmap,
      getAvailableChannelBitmap,
      sourceSelect
    });
    let outputRate = null;
    let interpolation = null;
    let sourceSummary = null;
    let defaultFilter = null;
    if (isDecimator) {
      const decimator = createElement("fieldset", {
        className: "filter-settings-decimator"
      });
      decimator.append(createElement("legend", {
        text: "Decimator settings"
      }));
      sourceSummary = createElement("p", {
        className: "section-help",
        attributes: {
          "data-decimator-source-rate": ""
        }
      });
      outputRate = numberControl(
        settings.outputSampleRateHz ?? 2000,
        "/outputSampleRateHz",
        { min: 1, max: 4294967295, step: 1 });
      interpolation = selectControl(
        String(settings.interpolation ?? 0),
        "/interpolation",
        [
          ["0", "None"],
          ["1", "Linear"],
          ["2", "Quadratic"]
        ]);
      defaultFilter = createElement("button", {
        type: "button",
        className: "secondary",
        text: "Default Filter",
        attributes: {
          "data-filter-action": "decimator-default-filter"
        }
      });
      decimator.append(
        sourceSummary,
        field("Output sample rate", outputRate, { unit: "Hz" }),
        field(
          "Interpolation",
          interpolation,
          {
            help:
              "PAMGuard recommends interpolation for a non-integer " +
              "input/output rate ratio."
          }),
        defaultFilter);
      root.append(decimator);
    }

    const filterContainer = createElement("fieldset", {
      className: "filter-settings-filter"
    });
    filterContainer.append(createElement("legend", {
      text: isDecimator
        ? "Anti-aliasing filter"
        : "Filter settings"
    }));
    root.append(filterContainer);
    const filterPanel = mountFilterPanel({
      container: filterContainer,
      settings: isDecimator ? settings.filter : settings,
      pointerPrefix: isDecimator ? "/filter" : "",
      getDesignSampleRate: () => {
        const source = Number(getSourceSampleRate()) || 0;
        if (!isDecimator) return source;
        const output = Number(outputRate?.value) || 0;
        return Math.max(source, output);
      },
      reportError
    });

    const refreshSource = () => {
      if (sourceSummary) {
        const rate = Number(getSourceSampleRate()) || 0;
        sourceSummary.textContent = rate > 0
          ? `Source sample rate ${rate.toLocaleString()} Hz`
          : "Source sample rate unavailable";
      }
      filterPanel.refresh();
    };
    sourceSelect?.addEventListener("change", refreshSource);
    outputRate?.addEventListener("change", refreshSource);
    defaultFilter?.addEventListener("click", () => {
      try {
        const output = finiteNumber(
          outputRate.value,
          "Output sample rate",
          { integer: true, min: 1, max: 4294967295 });
        filterPanel.setDefaultLowPass(
          output,
          Number(getSourceSampleRate()) || 0);
      }
      catch (error) {
        reportError(error);
      }
    });
    refreshSource();

    return {
      collect() {
        const channelBitmap = channelPicker.collect();
        const filter = filterPanel.collect();
        if (!isDecimator) {
          return {
            channelBitmap,
            ...filter
          };
        }
        const outputSampleRateHz = finiteNumber(
          outputRate.value,
          "Output sample rate",
          { integer: true, min: 1, max: 4294967295 });
        let interpolationValue = finiteNumber(
          interpolation.value,
          "Interpolation",
          { integer: true, min: 0, max: 2 });
        const sourceRate = Number(getSourceSampleRate()) || 0;
        const higherRate = Math.max(sourceRate, outputSampleRateHz);
        const lowerRate = Math.min(sourceRate, outputSampleRateHz);
        if (lowerRate > 0 &&
            higherRate % lowerRate === 0 &&
            interpolationValue > 0) {
          interpolationValue = 0;
        }
        return {
          outputSampleRateHz,
          filter,
          interpolation: interpolationValue,
          channelBitmap
        };
      },
      cleanup() {
        channelPicker.cleanup();
        sourceSelect?.removeEventListener?.("change", refreshSource);
      }
    };
  }

  globalThis.PamguardProjectFilterSettings = Object.freeze({
    mountEditor,
    canonicalFilterParams,
    validateFilter
  });
})();
