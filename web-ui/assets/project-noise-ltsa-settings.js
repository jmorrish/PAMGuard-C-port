(() => {
  "use strict";

  const CHANNEL_COUNT = 32;
  const FFT_STANDARD_BANDS = Object.freeze([
    ["thirdOctave", "Third Octave", "ThirdOctave"],
    ["decidecade", "Deci Decade", "DeciDecade"],
    ["octave", "Octave", "Octave"],
    ["decade", "Decade", "Decade"]
  ]);
  const NOISE_BAND_TYPES = Object.freeze([
    ["octave", "Octave"],
    ["thirdOctave", "Third Octave"],
    ["decidecade", "Deci Decade"],
    ["decade", "Decade"],
    ["tenthOctave", "Tenth Octave"],
    ["twelfthOctave", "Twelth Octave"]
  ]);
  const BAND_RATIOS = Object.freeze({
    octave: 2,
    thirdOctave: 2 ** (1 / 3),
    decidecade: 10 ** 0.1,
    decade: 10,
    tenthOctave: 2 ** 0.1,
    twelfthOctave: 2 ** (1 / 12)
  });
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
      "link[data-pamguard-project-noise-ltsa-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-noise-ltsa-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL(
          "project-noise-ltsa-settings.css",
          capturedScriptSource).href
      : "/assets/project-noise-ltsa-settings.css";
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

  function fftNoiseDefaults() {
    return {
      channelBitmap: 1,
      measurementIntervalSeconds: 60,
      nMeasures: 100,
      useAll: true,
      bands: []
    };
  }

  function noiseBandDefaults() {
    return {
      channelBitmap: 1,
      bandType: "thirdOctave",
      filterType: "butterworth",
      iirOrder: 6,
      firOrder: 7,
      firGamma: 2.5,
      outputIntervalSeconds: 10,
      minimumFrequencyHz: 1.7925856629456591,
      maximumFrequencyHz: 1133.6866687924667,
      referenceFrequencyHz: 1000
    };
  }

  function ltsaDefaults() {
    return {
      channelBitmap: 0,
      intervalSeconds: 60,
      longerFactor: 10
    };
  }

  function canonicalFftNoiseSettings(value = {}) {
    const defaults = fftNoiseDefaults();
    const bands = Array.isArray(value.bands)
      ? value.bands.map((band, index) => {
          const type = band.bandType === null ||
            FFT_STANDARD_BANDS.some(([candidate]) =>
              candidate === band.bandType)
            ? band.bandType
            : null;
          const low = finiteNumber(
            band.lowFrequencyHz,
            `Measurement band ${index + 1} lower frequency`,
            { min: 0 });
          const high = finiteNumber(
            band.highFrequencyHz,
            `Measurement band ${index + 1} upper frequency`,
            { exclusiveMin: 0 });
          if (high <= low) {
            throw new Error(
              `Measurement band ${index + 1} frequencies are not ordered`);
          }
          return {
            name: String(band.name ?? ""),
            lowFrequencyHz: low,
            highFrequencyHz: high,
            bandType: type
          };
        })
      : [];
    return {
      channelBitmap: portableBitmap(
        value.channelBitmap ?? defaults.channelBitmap),
      measurementIntervalSeconds: finiteNumber(
        value.measurementIntervalSeconds ??
          defaults.measurementIntervalSeconds,
        "Interval between measurements",
        { integer: true, min: 1 }),
      nMeasures: finiteNumber(
        value.nMeasures ?? defaults.nMeasures,
        "Number of measures in interval",
        { integer: true, min: 1 }),
      useAll: typeof value.useAll === "boolean"
        ? value.useAll
        : defaults.useAll,
      bands
    };
  }

  function canonicalNoiseBandSettings(value = {}) {
    const defaults = noiseBandDefaults();
    const bandType = NOISE_BAND_TYPES.some(([candidate]) =>
      candidate === value.bandType)
      ? value.bandType
      : defaults.bandType;
    const filterType = ["butterworth", "firWindow"].includes(
      value.filterType)
      ? value.filterType
      : defaults.filterType;
    return {
      channelBitmap: portableBitmap(
        value.channelBitmap ?? defaults.channelBitmap),
      bandType,
      filterType,
      iirOrder: finiteNumber(
        value.iirOrder ?? defaults.iirOrder,
        "IIR filter order",
        { integer: true, min: 2, max: 20 }),
      firOrder: finiteNumber(
        value.firOrder ?? defaults.firOrder,
        "FIR filter order",
        { integer: true, min: 2, max: 20 }),
      firGamma: finiteNumber(
        value.firGamma ?? defaults.firGamma,
        "FIR filter gamma",
        { exclusiveMin: 0 }),
      outputIntervalSeconds: finiteNumber(
        value.outputIntervalSeconds ??
          defaults.outputIntervalSeconds,
        "Output interval",
        { integer: true, min: 1 }),
      minimumFrequencyHz: finiteNumber(
        value.minimumFrequencyHz ??
          defaults.minimumFrequencyHz,
        "Minimum frequency",
        { exclusiveMin: 0 }),
      maximumFrequencyHz: finiteNumber(
        value.maximumFrequencyHz ??
          defaults.maximumFrequencyHz,
        "Maximum frequency",
        { exclusiveMin: 0 }),
      referenceFrequencyHz: finiteNumber(
        value.referenceFrequencyHz ??
          defaults.referenceFrequencyHz,
        "Reference frequency",
        { exclusiveMin: 0 })
    };
  }

  function canonicalLtsaSettings(value = {}) {
    const defaults = ltsaDefaults();
    return {
      channelBitmap: portableBitmap(
        value.channelBitmap ?? defaults.channelBitmap),
      intervalSeconds: finiteNumber(
        value.intervalSeconds ?? defaults.intervalSeconds,
        "Measurement interval",
        { integer: true, min: 1 }),
      longerFactor: finiteNumber(
        value.longerFactor ?? defaults.longerFactor,
        "Longer average factor",
        { integer: true, min: 1 })
    };
  }

  /**
   * Exact translation of NoiseControl.createBands and
   * NoiseDialog.addBands from the pinned PAMGuard Java authority.
   */
  function createStandardFftBands(
    bandType,
    sampleRateHz,
    fftLength) {
    const ratio = BAND_RATIOS[bandType];
    const name = FFT_STANDARD_BANDS.find(
      ([candidate]) => candidate === bandType)?.[2];
    const sampleRate = finiteNumber(
      sampleRateHz,
      "FFT source sample rate",
      { exclusiveMin: 0 });
    const length = finiteNumber(
      fftLength,
      "FFT source length",
      { integer: true, min: 2 });
    if (!ratio || !name) {
      throw new Error("Unknown standard Noise Monitor band type");
    }
    const frequencyResolution = sampleRate / length;
    const minimum =
      Math.max(frequencyResolution * 2 / (ratio - 1), 1);
    const maximum = sampleRate / 2;
    let lower = 1000 / Math.sqrt(ratio);
    while (lower > minimum) lower /= ratio;
    const bands = [];
    while (lower < maximum / ratio) {
      const upper = lower * ratio;
      if (lower < minimum) {
        lower *= ratio;
        continue;
      }
      if (upper > maximum) break;
      bands.push({
        name,
        lowFrequencyHz: lower,
        highFrequencyHz: upper,
        bandType
      });
      lower *= ratio;
    }
    return bands;
  }

  function pointerControl(tag, pointer, options = {}) {
    return createElement(tag, {
      type: options.type,
      attributes: {
        "data-setting-pointer": pointer,
        ...options.attributes
      }
    });
  }

  function numberControl(value, pointer, attributes = {}) {
    const control = pointerControl("input", pointer, {
      type: "number",
      attributes: { step: "any", ...attributes }
    });
    control.value = String(value);
    return control;
  }

  function selectControl(value, pointer, values) {
    const control = pointerControl("select", pointer);
    for (const [stored, label] of values) {
      const option = createElement("option", {
        text: label,
        attributes: { value: stored }
      });
      option.selected = stored === value;
      control.append(option);
    }
    control.value = value;
    return control;
  }

  function field(label, control, options = {}) {
    const row = createElement("label", {
      className: "noise-ltsa-settings-field"
    });
    row.append(
      createElement("span", {
        className: "noise-ltsa-settings-label",
        text: label
      }),
      control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "noise-ltsa-settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", {
        className: "noise-ltsa-settings-help",
        text: options.help
      }));
    }
    return row;
  }

  function actionButton(label, action) {
    return createElement("button", {
      type: "button",
      className: "secondary",
      text: label,
      attributes: { "data-noise-ltsa-action": action }
    });
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
    const root = createElement("fieldset", {
      className: "noise-ltsa-settings-channels"
    });
    root.append(createElement("legend", { text: "Channels" }));
    const actions = createElement("div", {
      className: "noise-ltsa-settings-actions"
    });
    const all = actionButton("All available", "all-channels");
    const none = actionButton("None", "no-channels");
    const choices = createElement("div", {
      className: "noise-ltsa-settings-channel-list"
    });
    actions.append(all, none);
    root.append(actions, choices);
    container.append(root);

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
          className: "noise-ltsa-settings-channel"
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
          text: "Choose a compatible source to expose its channels."
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

  function mountFftResolution(options) {
    const {
      container,
      getSourceSampleRate,
      getSourceFftLength,
      getSourceFftHop
    } = options;
    const root = createElement("fieldset", {
      className: "noise-ltsa-settings-resolution"
    });
    root.append(createElement("legend", { text: "FFT Resolution" }));
    const values = new Map();
    for (const [key, label, unit] of [
      ["sample-rate", "Sample Rate", "Hz"],
      ["fft-length", "FFT Length", "bins"],
      ["frequency-resolution", "Frequency Resolution", "Hz"],
      ["time-resolution", "Time Resolution", "ms"],
      ["time-step", "Time Step Size", "ms"]
    ]) {
      const value = createElement("output", {
        attributes: { "data-fft-resolution-value": key }
      });
      values.set(key, value);
      root.append(field(label, value, { unit }));
    }
    container.append(root);
    const refresh = () => {
      const sampleRate = Number(getSourceSampleRate()) || 0;
      const length = Number(getSourceFftLength()) || 0;
      const hop = Number(getSourceFftHop()) || 0;
      const present = sampleRate > 0 && length > 0;
      values.get("sample-rate").textContent =
        present ? String(Math.trunc(sampleRate)) : "N/A";
      values.get("fft-length").textContent =
        present ? String(length) : "N/A";
      values.get("frequency-resolution").textContent =
        present ? (sampleRate / length).toFixed(2) : "N/A";
      values.get("time-resolution").textContent =
        present ? (length / sampleRate * 1000).toFixed(2) : "N/A";
      values.get("time-step").textContent =
        present && hop > 0
          ? (hop / sampleRate * 1000).toFixed(2)
          : "N/A";
    };
    refresh();
    return { refresh };
  }

  function mountFftNoiseEditor(options) {
    const {
      container,
      settings,
      sourceSelect,
      getAvailableChannelBitmap,
      getSourceSampleRate,
      getSourceFftLength,
      getSourceFftHop,
      reportError
    } = options;
    const draft = canonicalFftNoiseSettings(settings);
    const root = createElement("div", {
      className: "noise-ltsa-settings-editor",
      attributes: {
        "data-pamguard-noise-ltsa-settings-editor":
          "pamguard.fft-noise-monitor"
      }
    });
    container.append(root);
    const channelPicker = mountChannelPicker({
      container: root,
      initialBitmap: draft.channelBitmap,
      getAvailableChannelBitmap,
      sourceSelect
    });
    const resolution = mountFftResolution({
      container: root,
      getSourceSampleRate,
      getSourceFftLength,
      getSourceFftHop
    });

    const measurements = createElement("fieldset", {
      className: "noise-ltsa-settings-measurements"
    });
    measurements.append(createElement("legend", {
      text: "Measurements"
    }));
    const interval = numberControl(
      draft.measurementIntervalSeconds,
      "/measurementIntervalSeconds",
      { min: 1, step: 1 });
    const count = numberControl(
      draft.nMeasures,
      "/nMeasures",
      { min: 1, step: 1 });
    const useAll = pointerControl(
      "input",
      "/useAll",
      { type: "checkbox" });
    useAll.checked = draft.useAll;
    const useAllLabel = createElement("label", {
      className: "noise-ltsa-settings-check"
    });
    useAllLabel.append(
      useAll,
      createElement("span", { text: "Use all FFT data" }));
    measurements.append(
      field(
        "Interval between measurements",
        interval,
        { unit: "s" }),
      field("Number of measures in interval", count),
      useAllLabel);

    const standard = createElement("div", {
      className: "noise-ltsa-settings-standard-bands",
      attributes: { role: "group", "aria-label": "Standard bands" }
    });
    const standardControls = new Map();
    for (const [type, label] of FFT_STANDARD_BANDS) {
      const control = createElement("input", {
        type: "checkbox",
        attributes: {
          "data-noise-standard-band": type
        }
      });
      control.checked = draft.bands.some(
        (band) => band.bandType === type);
      const wrapper = createElement("label");
      wrapper.append(control, createElement("span", { text: label }));
      standard.append(wrapper);
      standardControls.set(type, control);
    }
    const bandActions = createElement("div", {
      className: "noise-ltsa-settings-actions"
    });
    const addCustom = actionButton(
      "Add Other bands",
      "add-custom-band");
    bandActions.append(addCustom);
    const bandsHost = createElement("div", {
      className: "noise-ltsa-settings-band-table-host"
    });

    const customEditor = createElement("fieldset", {
      className: "noise-ltsa-settings-custom-band"
    });
    customEditor.hidden = true;
    const customLegend = createElement("legend", {
      text: "Add Other bands"
    });
    const customName = pointerControl(
      "input",
      "/bands/draft/name",
      { type: "text" });
    const customLow = numberControl(
      "",
      "/bands/draft/lowFrequencyHz",
      { min: 0 });
    const customHigh = numberControl(
      "",
      "/bands/draft/highFrequencyHz",
      { min: Number.MIN_VALUE });
    const saveCustom = actionButton("Save band", "save-custom-band");
    const cancelCustom = actionButton(
      "Cancel",
      "cancel-custom-band");
    const customActions = createElement("div", {
      className: "noise-ltsa-settings-actions"
    });
    customActions.append(saveCustom, cancelCustom);
    customEditor.append(
      customLegend,
      field("Name", customName),
      field("Range", customLow, { unit: "Hz lower" }),
      field("to", customHigh, { unit: "Hz upper" }),
      customActions);
    measurements.append(
      standard,
      bandActions,
      bandsHost,
      customEditor);
    root.append(measurements);

    let customEditIndex = null;
    const autoMeasures = () => {
      count.disabled = useAll.checked;
      if (!useAll.checked) return;
      const seconds = Number(interval.value);
      const sampleRate = Number(getSourceSampleRate()) || 0;
      const hop = Number(getSourceFftHop()) || 0;
      if (Number.isInteger(seconds) &&
          seconds > 0 &&
          sampleRate > 0 &&
          hop > 0) {
        count.value = String(Math.trunc(
          seconds * sampleRate / hop));
      }
    };
    const renderBands = () => {
      bandsHost.replaceChildren();
      const table = createElement("table", {
        className: "noise-ltsa-settings-band-table"
      });
      const head = createElement("thead");
      const heading = createElement("tr");
      for (const label of [
        "Name",
        "F1 (Hz)",
        "Centre (Hz)",
        "F2 (Hz)",
        "Actions"
      ]) {
        heading.append(createElement("th", { text: label }));
      }
      head.append(heading);
      const body = createElement("tbody");
      draft.bands.forEach((band, index) => {
        const row = createElement("tr", {
          attributes: { "data-noise-band-row": index }
        });
        for (const value of [
          band.name,
          Number(band.lowFrequencyHz).toPrecision(7),
          Math.sqrt(
            band.lowFrequencyHz *
              band.highFrequencyHz).toPrecision(7),
          Number(band.highFrequencyHz).toPrecision(7)
        ]) {
          row.append(createElement("td", { text: value }));
        }
        const actions = createElement("td");
        if (band.bandType === null) {
          const edit = actionButton(
            "Edit ...",
            `edit-custom-band-${index}`);
          const remove = actionButton(
            "Remove",
            `remove-custom-band-${index}`);
          edit.addEventListener("click", () => {
            customEditIndex = index;
            customLegend.textContent = "Edit Other band";
            customName.value = band.name;
            customLow.value = String(band.lowFrequencyHz);
            customHigh.value = String(band.highFrequencyHz);
            customEditor.hidden = false;
          });
          remove.addEventListener("click", () => {
            draft.bands.splice(index, 1);
            renderBands();
          });
          actions.append(edit, remove);
        }
        else {
          actions.append(createElement("span", {
            className: "noise-ltsa-settings-generated",
            text: "Standard"
          }));
        }
        row.append(actions);
        body.append(row);
      });
      if (!draft.bands.length) {
        const empty = createElement("tr");
        const cell = createElement("td", {
          text: "No measurement bands configured.",
          attributes: { colspan: 5 }
        });
        empty.append(cell);
        body.append(empty);
      }
      table.append(head, body);
      bandsHost.append(table);
    };
    const openCustom = () => {
      customEditIndex = null;
      customLegend.textContent = "Add Other bands";
      customName.value = "";
      customLow.value = "";
      customHigh.value = "";
      customEditor.hidden = false;
    };
    const closeCustom = () => {
      customEditIndex = null;
      customEditor.hidden = true;
    };
    addCustom.addEventListener("click", openCustom);
    cancelCustom.addEventListener("click", closeCustom);
    saveCustom.addEventListener("click", () => {
      try {
        const low = finiteNumber(
          customLow.value,
          "Custom band lower frequency",
          { min: 0 });
        const high = finiteNumber(
          customHigh.value,
          "Custom band upper frequency",
          { exclusiveMin: 0 });
        if (high <= low) {
          throw new Error(
            "Custom band upper frequency must exceed its lower frequency");
        }
        const sampleRate = Number(getSourceSampleRate()) || 0;
        const length = Number(getSourceFftLength()) || 0;
        if (sampleRate > 0 && high > sampleRate / 2) {
          throw new Error(
            "Custom band upper frequency exceeds the source Nyquist frequency");
        }
        if (sampleRate > 0 &&
            length > 0 &&
            high - low <
              Number((sampleRate / length).toFixed(2))) {
          throw new Error(
            "Custom band range is smaller than the FFT frequency resolution");
        }
        const band = {
          name: String(customName.value),
          lowFrequencyHz: low,
          highFrequencyHz: high,
          bandType: null
        };
        if (customEditIndex === null) {
          draft.bands.push(band);
        }
        else {
          draft.bands[customEditIndex] = band;
        }
        closeCustom();
        renderBands();
      }
      catch (error) {
        reportError(error);
      }
    });
    for (const [type, control] of standardControls) {
      control.addEventListener("change", () => {
        try {
          if (control.checked) {
            const generatedBands = createStandardFftBands(
              type,
              getSourceSampleRate(),
              getSourceFftLength());
            draft.bands = draft.bands.filter(
              (band) => band.bandType === null);
            for (const [otherType, otherControl] of standardControls) {
              if (otherType !== type) otherControl.checked = false;
            }
            draft.bands.push(...generatedBands);
          }
          else {
            draft.bands = draft.bands.filter(
              (band) => band.bandType !== type);
          }
          renderBands();
        }
        catch (error) {
          control.checked = false;
          reportError(error);
        }
      });
    }
    const refreshSource = () => {
      resolution.refresh();
      const available = Number(getSourceSampleRate()) > 0 &&
        Number(getSourceFftLength()) > 0;
      standardControls.forEach((control) => {
        control.disabled = !available;
      });
      addCustom.disabled = !available;
      autoMeasures();
    };
    interval.addEventListener("input", autoMeasures);
    useAll.addEventListener("change", autoMeasures);
    sourceSelect?.addEventListener("change", refreshSource);
    renderBands();
    refreshSource();

    return {
      collect() {
        const channelBitmap = channelPicker.collect();
        const result = {
          channelBitmap,
          measurementIntervalSeconds: finiteNumber(
            interval.value,
            "Interval between measurements",
            { integer: true, min: 1 }),
          nMeasures: finiteNumber(
            count.value,
            "Number of measures in interval",
            { integer: true, min: 1 }),
          useAll: useAll.checked,
          bands: clone(draft.bands)
        };
        if (!result.bands.length) {
          throw new Error(
            "Create at least one measurement frequency band");
        }
        return canonicalFftNoiseSettings(result);
      },
      cleanup() {
        channelPicker.cleanup();
        sourceSelect?.removeEventListener?.("change", refreshSource);
      }
    };
  }

  function mountNoiseBandEditor(options) {
    const {
      container,
      settings,
      sourceSelect,
      getAvailableChannelBitmap,
      getSourceSampleRate,
      reportError
    } = options;
    const draft = canonicalNoiseBandSettings(settings);
    const root = createElement("div", {
      className: "noise-ltsa-settings-editor",
      attributes: {
        "data-pamguard-noise-ltsa-settings-editor":
          "pamguard.noise-band-monitor"
      }
    });
    container.append(root);
    const channelPicker = mountChannelPicker({
      container: root,
      initialBitmap: draft.channelBitmap,
      getAvailableChannelBitmap,
      sourceSelect
    });

    const output = createElement("fieldset");
    output.append(createElement("legend", { text: "Output" }));
    const outputInterval = numberControl(
      draft.outputIntervalSeconds,
      "/outputIntervalSeconds",
      { min: 1, step: 1 });
    output.append(field(
      "Output Interval",
      outputInterval,
      { unit: "s" }));

    const measurement = createElement("fieldset");
    measurement.append(createElement("legend", {
      text: "Measurement Bands"
    }));
    const bandType = selectControl(
      draft.bandType,
      "/bandType",
      NOISE_BAND_TYPES);
    const reference = numberControl(
      draft.referenceFrequencyHz,
      "/referenceFrequencyHz",
      { min: Number.MIN_VALUE });
    const minimum = numberControl(
      draft.minimumFrequencyHz,
      "/minimumFrequencyHz",
      { min: Number.MIN_VALUE });
    const maximum = numberControl(
      draft.maximumFrequencyHz,
      "/maximumFrequencyHz",
      { min: Number.MIN_VALUE });
    const defaultReference = actionButton(
      "Default",
      "default-reference-frequency");
    const maxFrequency = actionButton(
      "Max",
      "maximum-source-frequency");
    const referenceRow = createElement("div", {
      className: "noise-ltsa-settings-inline-action"
    });
    referenceRow.append(
      field("Reference Frequency", reference, { unit: "Hz" }),
      defaultReference);
    const maximumRow = createElement("div", {
      className: "noise-ltsa-settings-inline-action"
    });
    maximumRow.append(
      field("Maximum Frequency", maximum, { unit: "Hz" }),
      maxFrequency);
    measurement.append(
      field("Band Type", bandType),
      referenceRow,
      maximumRow,
      field("Minimum Frequency", minimum, { unit: "Hz" }));

    const filters = createElement("fieldset");
    filters.append(createElement("legend", { text: "Filters" }));
    const filterType = selectControl(
      draft.filterType,
      "/filterType",
      [
        ["butterworth", "Butterworth"],
        ["firWindow", "FIR Filter"]
      ]);
    const order = numberControl(
      filterType.value === "butterworth"
        ? draft.iirOrder
        : draft.firOrder,
      "/filterOrder",
      { min: 2, max: 20, step: 1 });
    const gamma = numberControl(
      draft.firGamma,
      "/firGamma",
      { min: Number.MIN_VALUE });
    const retained = createElement("div", {
      className: "noise-ltsa-settings-retained-orders"
    });
    const retainedIir = createElement("output", {
      attributes: {
        "data-retained-filter-order": "iir",
        "data-setting-pointer": "/iirOrder"
      }
    });
    const retainedFir = createElement("output", {
      attributes: {
        "data-retained-filter-order": "fir",
        "data-setting-pointer": "/firOrder"
      }
    });
    retained.append(
      field("Stored IIR order", retainedIir),
      field("Stored FIR order", retainedFir));
    filters.append(
      field("Filter Type", filterType),
      field("Filter Order", order),
      field(
        "Filter Gamma",
        gamma,
        {
          help:
            "Used by the FIR Window design; retained when Butterworth " +
            "is selected."
        }),
      retained);
    root.append(output, measurement, filters);

    let activeFilterType = filterType.value;
    const syncActiveOrder = () => {
      const parsed = finiteNumber(
        order.value,
        "Filter order",
        { integer: true, min: 2, max: 20 });
      if (activeFilterType === "butterworth") {
        draft.iirOrder = parsed;
      }
      else {
        draft.firOrder = parsed;
      }
    };
    const updateFilterControls = () => {
      gamma.disabled = filterType.value !== "firWindow";
      retainedIir.textContent = String(draft.iirOrder);
      retainedFir.textContent = String(draft.firOrder);
    };
    filterType.addEventListener("change", () => {
      try {
        syncActiveOrder();
        activeFilterType = filterType.value;
        order.value = String(
          activeFilterType === "butterworth"
            ? draft.iirOrder
            : draft.firOrder);
        updateFilterControls();
      }
      catch (error) {
        filterType.value = activeFilterType;
        reportError(error);
      }
    });
    order.addEventListener("change", () => {
      try {
        syncActiveOrder();
        updateFilterControls();
      }
      catch (error) {
        reportError(error);
      }
    });
    defaultReference.addEventListener("click", () => {
      reference.value = "1000";
    });
    const refreshSource = () => {
      const sampleRate = Number(getSourceSampleRate()) || 0;
      maxFrequency.disabled = sampleRate <= 0;
    };
    maxFrequency.addEventListener("click", () => {
      const sampleRate = Number(getSourceSampleRate()) || 0;
      if (sampleRate > 0) {
        maximum.value = String(sampleRate / 2);
      }
    });
    sourceSelect?.addEventListener("change", refreshSource);
    updateFilterControls();
    refreshSource();

    return {
      collect() {
        syncActiveOrder();
        if (draft.iirOrder % 2 !== 0) {
          throw new Error(
            "The IIR filter order must be even; 6 or greater is recommended");
        }
        const minimumFrequencyHz = finiteNumber(
          minimum.value,
          "Minimum frequency",
          { exclusiveMin: 0 });
        const maximumFrequencyHz = finiteNumber(
          maximum.value,
          "Maximum frequency",
          { exclusiveMin: 0 });
        if (minimumFrequencyHz > maximumFrequencyHz) {
          throw new Error(
            "The minimum frequency must not exceed the maximum frequency");
        }
        return canonicalNoiseBandSettings({
          channelBitmap: channelPicker.collect(),
          bandType: bandType.value,
          filterType: filterType.value,
          iirOrder: draft.iirOrder,
          firOrder: draft.firOrder,
          firGamma: finiteNumber(
            gamma.value,
            "FIR filter gamma",
            { exclusiveMin: 0 }),
          outputIntervalSeconds: finiteNumber(
            outputInterval.value,
            "Output interval",
            { integer: true, min: 1 }),
          minimumFrequencyHz,
          maximumFrequencyHz,
          referenceFrequencyHz: finiteNumber(
            reference.value,
            "Reference frequency",
            { exclusiveMin: 0 })
        });
      },
      cleanup() {
        channelPicker.cleanup();
        sourceSelect?.removeEventListener?.("change", refreshSource);
      }
    };
  }

  function mountLtsaEditor(options) {
    const {
      container,
      settings,
      sourceSelect,
      getAvailableChannelBitmap
    } = options;
    const draft = canonicalLtsaSettings(settings);
    const root = createElement("div", {
      className: "noise-ltsa-settings-editor",
      attributes: {
        "data-pamguard-noise-ltsa-settings-editor": "pamguard.ltsa"
      }
    });
    container.append(root);
    const channelPicker = mountChannelPicker({
      container: root,
      initialBitmap: draft.channelBitmap,
      getAvailableChannelBitmap,
      sourceSelect
    });
    const measurement = createElement("fieldset");
    measurement.append(createElement("legend", {
      text: "Measurement"
    }));
    const interval = numberControl(
      draft.intervalSeconds,
      "/intervalSeconds",
      { min: 1, step: 1 });
    measurement.append(field(
      "Measurement interval",
      interval,
      { unit: "seconds" }));

    const advanced = createElement("details", {
      className: "noise-ltsa-settings-advanced"
    });
    advanced.append(createElement("summary", {
      text: "Advanced persisted settings"
    }));
    const longerFactor = numberControl(
      draft.longerFactor,
      "/longerFactor",
      { min: 1, step: 1 });
    advanced.append(
      field(
        "Longer average factor (persisted, dormant)",
        longerFactor),
      createElement("p", {
        className: "section-help",
        text:
          "PAMGuard 2.02.18e persists this value, but its longer-average " +
          "process and output are commented out. It has no runtime effect."
      }));
    root.append(measurement, advanced);

    return {
      collect() {
        return canonicalLtsaSettings({
          channelBitmap: channelPicker.collect(),
          intervalSeconds: finiteNumber(
            interval.value,
            "Measurement interval",
            { integer: true, min: 1 }),
          longerFactor: finiteNumber(
            longerFactor.value,
            "Longer average factor",
            { integer: true, min: 1 })
        });
      },
      cleanup() {
        channelPicker.cleanup();
      }
    };
  }

  function mountEditor(options) {
    ensureStylesheet();
    const normalized = {
      settings: {},
      getAvailableChannelBitmap: () => 0,
      getSourceSampleRate: () => 0,
      getSourceFftLength: () => 0,
      getSourceFftHop: () => 0,
      reportError: () => {},
      ...options
    };
    if (normalized.typeId === "pamguard.fft-noise-monitor") {
      return mountFftNoiseEditor(normalized);
    }
    if (normalized.typeId === "pamguard.noise-band-monitor") {
      return mountNoiseBandEditor(normalized);
    }
    if (normalized.typeId === "pamguard.ltsa") {
      return mountLtsaEditor(normalized);
    }
    throw new Error(
      `Unsupported Noise/LTSA editor type ${
        normalized.typeId || ""}`);
  }

  globalThis.PamguardProjectNoiseLtsaSettings = Object.freeze({
    mountEditor,
    canonicalFftNoiseSettings,
    canonicalNoiseBandSettings,
    canonicalLtsaSettings,
    createStandardFftBands
  });
})();
