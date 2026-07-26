(() => {
  "use strict";

  const MAX_BITMAP = 4294967295;
  const FILTER_BANDS = Object.freeze([
    ["highPass", "High pass"],
    ["lowPass", "Low pass"],
    ["bandPass", "Band pass"],
    ["bandStop", "Band stop"]
  ]);
  const BASIC_SELECTIONS = Object.freeze({
    energy: 0x1,
    width: 0x2,
    peak: 0x4,
    mean: 0x8,
    length: 0x10
  });
  const SWEEP_CHANNEL_CHOICES = Object.freeze([
    ["requireAll", "Require every channel"],
    ["requireOne", "Require at least one channel"],
    ["useMeans", "Classify the channel mean"]
  ]);
  const SWEEP_RESTRICTED_BIN_TYPES = Object.freeze([
    ["clickCenter", "Bins around click centre"],
    ["clickStart", "Bins from click start"]
  ]);
  const DEFAULT_BASIC_CLASSIFIER_TYPE = Object.freeze({
    name: "New basic click type",
    speciesCode: 1,
    enabled: true,
    discard: false,
    whichSelections: 5,
    band1FreqHz: [0, 0],
    band2FreqHz: [0, 0],
    band1EnergyDb: [0, 0],
    band2EnergyDb: [0, 0],
    bandEnergyDifferenceDb: 0,
    peakFrequencySearchHz: [0, 0],
    peakFrequencyRangeHz: [0, 0],
    peakWidthHz: [0, 0],
    widthEnergyFraction: 0,
    meanSumRangeHz: [0, 0],
    meanSelectionRangeHz: [0, 0],
    clickLengthMs: [0, 0],
    lengthEnergyFraction: 0
  });
  const DEFAULT_SWEEP_CLASSIFIER_TYPE = Object.freeze({
    name: "New sweep click type",
    speciesCode: 1,
    discard: false,
    enabled: true,
    channelChoice: "requireAll",
    restrictLength: true,
    restrictedBins: 128,
    restrictedBinType: "clickCenter",
    enableLength: true,
    lengthSmoothing: 5,
    lengthDb: 6,
    lengthMs: [0, 1],
    enableEnergyBands: false,
    testEnergyBandHz: [0, 0],
    controlEnergyBand0Hz: [0, 0],
    controlEnergyBand1Hz: [0, 0],
    energyThreshold0Db: 0,
    energyThreshold1Db: 0,
    testAmplitude: false,
    amplitudeRangeDb: [0, 200],
    enableFftFilter: false,
    fftFilter: {
      band: "highPass",
      lowPassFreqHz: 0,
      highPassFreqHz: 0
    },
    enablePeak: false,
    enableWidth: false,
    enableMean: false,
    peakSearchRangeHz: [0, 0],
    peakRangeHz: [0, 0],
    peakWidthRangeHz: [0, 0],
    meanRangeHz: [0, 0],
    peakSmoothing: 5,
    peakWidthThresholdDb: 6,
    enableZeroCrossings: false,
    zeroCrossingCount: [0, 0],
    enableSweep: false,
    zeroCrossingSweepKhzPerMs: [0, 0],
    enableMinCrossCorrelation: false,
    enablePeakCrossCorrelation: false,
    minCorrelation: 0,
    correlationFactor: 1,
    enableBearingLimits: false,
    excludeBearingLimits: false,
    bearingLimitsRadians: [-Math.PI, Math.PI]
  });
  const capturedScriptSource =
    typeof document !== "undefined" && document.currentScript
      ? document.currentScript.src
      : "";
  let editorSequence = 0;

  const DEFAULT_SETTINGS = Object.freeze({
    detector: {
      channelBitmap: 3,
      groupingType: "all",
      channelGroups: [0, 0],
      triggerBitmap: 4294967295,
      minTriggerChannels: 1,
      thresholdDb: 10,
      longFilter: 0.00001,
      longFilter2: 0.000001,
      shortFilter: 0.1,
      preSample: 40,
      postSample: 40,
      minSep: 100,
      maxLength: 1024,
      sampleNoise: true,
      noiseSampleIntervalSeconds: 5,
      storeBackground: true,
      backgroundIntervalMilliseconds: 5000,
      publishTriggerFunction: false,
      preFilter: {
        type: "butterworth",
        band: "highPass",
        order: 4,
        lowPassFreqHz: 20000,
        highPassFreqHz: 500,
        passBandRippleDb: 2
      },
      triggerFilter: {
        type: "butterworth",
        band: "highPass",
        order: 2,
        lowPassFreqHz: 20000,
        highPassFreqHz: 2000,
        passBandRippleDb: 2
      },
      echo: {
        runOnline: false,
        discardEchoes: false,
        maxIntervalSeconds: 0.1
      }
    },
    features: {
      fftLength: 0,
      lengthEnergyFraction: 90,
      widthEnergyFraction: 90,
      energyBandsHz: [[1000, 6000], [6000, 14000]],
      peakFrequencySearchHz: [500, 20000],
      meanFrequencyRangeHz: [500, 20000]
    },
    classification: {
      runOnline: false,
      mode: "sweep",
      discardUnclassified: false,
      checkAllClassifiers: false,
      amplitudeDbOffsetByChannel: [],
      basicTypes: [],
      sweepTypes: []
    },
    localisation: {
      delayMeasurement: {
        filterBearings: false,
        filterBand: "highPass",
        filterHighPassHz: 0,
        filterLowPassHz: 0,
        envelopeBearings: false,
        useLeadingEdge: false,
        upSample: 1,
        useRestrictedBins: false,
        restrictedBins: 80
      },
      typeSettings: [],
      angleVetoes: [],
      trackedTrain: {
        isSelected: [true, false, false, false],
        maxRangeM: 20000,
        maxHeightM: 5,
        minHeightM: -5000,
        maxTimeMilliseconds: 200,
        limitPoints: false,
        maxPoints: 30
      }
    },
    train: {
      enabled: false,
      minIciSeconds: 0.1,
      maxIciSeconds: 2,
      maxIciChange: 1.2,
      okAngleErrorDegrees: 1,
      initialPerpendicularDistanceM: 100,
      minClicks: 6,
      minAngleChangeDegrees: 5,
      iciUpdateRatio: 0.5,
      minUpdateGapSeconds: 5
    },
    display: {
      channelBitmap: 3,
      timeWindowSeconds: 20,
      bearingLimitsDegrees: [0, 180],
      amplitudeLimitsDb: [0, 30],
      iciLimitsSeconds: [0.001, 3],
      showEchoes: true
    }
  });

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  function isObject(value) {
    return Boolean(value) &&
      typeof value === "object" &&
      !Array.isArray(value);
  }

  function mergeDefaults(defaultValue, suppliedValue) {
    if (Array.isArray(defaultValue)) {
      return Array.isArray(suppliedValue)
        ? clone(suppliedValue)
        : clone(defaultValue);
    }
    if (isObject(defaultValue)) {
      const supplied = isObject(suppliedValue) ? suppliedValue : {};
      const result = {};
      for (const [key, value] of Object.entries(defaultValue)) {
        result[key] = mergeDefaults(value, supplied[key]);
      }
      return result;
    }
    return suppliedValue === undefined
      ? defaultValue
      : clone(suppliedValue);
  }

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
      "link[data-pamguard-project-click-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-click-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL("project-click-settings.css", capturedScriptSource).href
      : "/assets/project-click-settings.css";
    document.head.append(link);
  }

  function field(label, control, options = {}) {
    const tagName = String(control?.tagName || "").toLowerCase();
    const rowTag = ["input", "select"].includes(tagName)
      ? "label"
      : "div";
    const row = createElement(rowTag, {
      className:
        `click-settings-field ${options.className || ""}`.trim()
    });
    row.append(createElement("span", {
      className: "click-settings-label",
      text: label
    }));
    row.append(control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "click-settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", {
        className: "click-settings-help",
        text: options.help
      }));
    }
    return row;
  }

  function checkboxField(label, control, help) {
    return field(label, control, {
      className: "click-settings-field-checkbox",
      help
    });
  }

  function numberInput(value, pointer, attributes = {}) {
    const input = createElement("input", {
      type: "number",
      attributes: {
        step: "any",
        "data-click-setting": pointer,
        ...attributes
      }
    });
    input.value = value;
    return input;
  }

  function checkboxInput(value, pointer) {
    const input = createElement("input", {
      type: "checkbox",
      attributes: {
        "data-click-setting": pointer
      }
    });
    input.checked = Boolean(value);
    return input;
  }

  function selectInput(value, pointer, definitions) {
    const select = createElement("select", {
      attributes: {
        "data-click-setting": pointer
      }
    });
    for (const [storedValue, label] of definitions) {
      select.append(createElement("option", {
        text: label,
        attributes: { value: storedValue }
      }));
    }
    select.value = String(value);
    return select;
  }

  function textInput(value, pointer, attributes = {}) {
    const input = createElement("input", {
      type: "text",
      attributes: {
        "data-click-setting": pointer,
        ...attributes
      }
    });
    input.value = value ?? "";
    return input;
  }

  function throwControlError(control, message) {
    if (control) {
      control.setCustomValidity(message);
      control.setAttribute("aria-invalid", "true");
      control.focus();
    }
    throw new Error(message);
  }

  function clearControlError(control) {
    control.setCustomValidity("");
    control.removeAttribute("aria-invalid");
  }

  function readNumber(control, label, options = {}) {
    clearControlError(control);
    const value = Number(control.value);
    let valid = control.value.trim() !== "" && Number.isFinite(value);
    if (valid && options.integer) valid = Number.isInteger(value);
    if (valid && options.min !== undefined) {
      valid = value >= options.min;
    }
    if (valid && options.exclusiveMin !== undefined) {
      valid = value > options.exclusiveMin;
    }
    if (valid && options.max !== undefined) {
      valid = value <= options.max;
    }
    if (!valid) {
      const constraint = [
        options.integer ? "an integer" : "a finite number",
        options.min !== undefined ? `at least ${options.min}` : "",
        options.exclusiveMin !== undefined
          ? `greater than ${options.exclusiveMin}`
          : "",
        options.max !== undefined ? `no greater than ${options.max}` : ""
      ].filter(Boolean).join(", ");
      throwControlError(control, `${label} must be ${constraint}`);
    }
    return value;
  }

  function readText(control, label) {
    clearControlError(control);
    const value = control.value.trim();
    if (!value) {
      throwControlError(control, `${label} cannot be empty`);
    }
    return value;
  }

  function setPath(target, path, value) {
    let current = target;
    for (let index = 0; index < path.length - 1; index += 1) {
      current = current[path[index]];
    }
    current[path[path.length - 1]] = value;
  }

  function setControlsEnabled(container, enabled) {
    for (const control of container.querySelectorAll(
      "input, select, button")) {
      control.disabled = !enabled;
    }
  }

  function section(title, help) {
    const wrapper = createElement("fieldset", {
      className: "click-settings-section"
    });
    wrapper.append(createElement("legend", { text: title }));
    if (help) {
      wrapper.append(createElement("p", {
        className: "click-settings-section-help",
        text: help
      }));
    }
    return wrapper;
  }

  function createTabs(container, instanceId, definitions, options = {}) {
    const tabList = createElement("div", {
      className: options.action
        ? "click-settings-action-list"
        : "click-settings-tab-list",
      attributes: {
        role: "tablist",
        "aria-label": options.label || "Click Detector settings"
      }
    });
    const panels = new Map();
    const buttons = [];

    const selectTab = (selectedIndex, focus = false) => {
      buttons.forEach((button, index) => {
        const selected = index === selectedIndex;
        button.setAttribute(
          "aria-selected",
          selected ? "true" : "false");
        button.tabIndex = selected ? 0 : -1;
        panels.get(definitions[index][0]).hidden = !selected;
      });
      if (focus) buttons[selectedIndex].focus();
    };

    definitions.forEach(([id, label], index) => {
      const panelId = `${instanceId}-${id}`;
      const button = createElement("button", {
        type: "button",
        className: options.action
          ? "click-settings-action"
          : "click-settings-tab",
        text: label,
        attributes: {
          role: "tab",
          "aria-selected": index === 0 ? "true" : "false",
          "aria-controls": panelId,
          tabindex: index === 0 ? "0" : "-1"
        }
      });
      const panel = createElement("section", {
        className: options.action
          ? "click-settings-action-panel"
          : "click-settings-tab-panel",
        attributes: {
          id: panelId,
          role: "tabpanel",
          "aria-labelledby": `${panelId}-tab`
        }
      });
      button.id = `${panelId}-tab`;
      panel.hidden = index !== 0;
      button.addEventListener("click", () => selectTab(index));
      button.addEventListener("keydown", (event) => {
        let nextIndex = index;
        if (event.key === "ArrowRight") {
          nextIndex = (index + 1) % definitions.length;
        }
        else if (event.key === "ArrowLeft") {
          nextIndex =
            (index - 1 + definitions.length) % definitions.length;
        }
        else if (event.key === "Home") nextIndex = 0;
        else if (event.key === "End") {
          nextIndex = definitions.length - 1;
        }
        else return;
        event.preventDefault();
        selectTab(nextIndex, true);
      });
      buttons.push(button);
      panels.set(id, panel);
      tabList.append(button);
    });
    container.append(tabList, ...panels.values());
    return { panels, buttons, selectTab };
  }

  function createBitmapEditor(value, pointer, label, options = {}) {
    const wrapper = createElement("div", {
      className: "click-settings-bitmap"
    });
    const bitmap = numberInput(value, pointer, {
      min: options.allowZero ? 0 : 1,
      max: MAX_BITMAP,
      step: 1,
      "aria-label": `${label} bitmap`
    });
    const channelToggle = createElement("button", {
      type: "button",
      className: "click-settings-disclosure",
      text: "Choose channels",
      attributes: {
        "aria-expanded": "false"
      }
    });
    const channels = createElement("div", {
      className: "click-settings-channel-grid"
    });
    channels.hidden = true;
    const checkboxes = [];

    const updateChecks = () => {
      const number = Number(bitmap.value);
      if (!Number.isInteger(number) ||
          number < 0 ||
          number > MAX_BITMAP) {
        return;
      }
      const bits = BigInt(number);
      checkboxes.forEach((checkbox, index) => {
        checkbox.checked =
          (bits & (BigInt(1) << BigInt(index))) !== BigInt(0);
      });
    };

    const updateBitmap = () => {
      let total = BigInt(0);
      checkboxes.forEach((checkbox, index) => {
        if (checkbox.checked) {
          total |= BigInt(1) << BigInt(index);
        }
      });
      bitmap.value = total.toString();
      bitmap.dispatchEvent(new Event("input", { bubbles: true }));
    };

    for (let channel = 0; channel < 32; channel += 1) {
      const checkbox = createElement("input", {
        type: "checkbox",
        attributes: {
          "aria-label": `Channel ${channel}`
        }
      });
      checkbox.addEventListener("change", updateBitmap);
      checkboxes.push(checkbox);
      const channelLabel = createElement("label", {
        className: "click-settings-channel"
      });
      channelLabel.append(
        checkbox,
        createElement("span", { text: `Ch ${channel}` }));
      channels.append(channelLabel);
    }

    channelToggle.addEventListener("click", () => {
      const expanded =
        channelToggle.getAttribute("aria-expanded") !== "true";
      channelToggle.setAttribute(
        "aria-expanded",
        expanded ? "true" : "false");
      channelToggle.textContent = expanded
        ? "Hide channels"
        : "Choose channels";
      channels.hidden = !expanded;
    });
    bitmap.addEventListener("input", updateChecks);
    updateChecks();
    wrapper.append(bitmap, channelToggle, channels);

    return {
      element: wrapper,
      input: bitmap,
      collect: () => readNumber(bitmap, label, {
        integer: true,
        min: options.allowZero ? 0 : 1,
        max: MAX_BITMAP
      })
    };
  }

  function createListEditor(options) {
    const wrapper = createElement("section", {
      className: "click-settings-list-editor"
    });
    const heading = createElement("div", {
      className: "click-settings-list-heading"
    });
    const headingText = createElement("div");
    headingText.append(createElement("h4", { text: options.title }));
    if (options.help) {
      headingText.append(createElement("p", {
        className: "click-settings-section-help",
        text: options.help
      }));
    }
    const addButton = createElement("button", {
      type: "button",
      className: "click-settings-add",
      text: options.addLabel || "Add"
    });
    heading.append(headingText, addButton);
    const items = createElement("div", {
      className: "click-settings-list"
    });
    const empty = createElement("p", {
      className: "click-settings-empty",
      text: options.emptyText || "No entries configured."
    });
    const entries = [];

    const updateEntryChrome = () => {
      empty.hidden = entries.length !== 0;
      addButton.disabled =
        options.maxItems !== undefined &&
        entries.length >= options.maxItems;
      entries.forEach((entry, index) => {
        entry.title.textContent =
          `${options.itemLabel || "Entry"} ${index + 1}`;
        entry.up.disabled = index === 0;
        entry.down.disabled = index === entries.length - 1;
        entry.remove.disabled =
          options.minItems !== undefined &&
          entries.length <= options.minItems;
      });
    };

    const moveEntry = (entry, offset) => {
      const oldIndex = entries.indexOf(entry);
      const newIndex = oldIndex + offset;
      if (oldIndex < 0 || newIndex < 0 ||
          newIndex >= entries.length) {
        return;
      }
      entries.splice(oldIndex, 1);
      entries.splice(newIndex, 0, entry);
      items.insertBefore(
        entry.card,
        newIndex + 1 < entries.length
          ? entries[newIndex + 1].card
          : null);
      updateEntryChrome();
    };

    const addEntry = (value) => {
      if (options.maxItems !== undefined &&
          entries.length >= options.maxItems) {
        return;
      }
      const card = createElement("article", {
        className: "click-settings-list-item"
      });
      const cardHeader = createElement("header", {
        className: "click-settings-list-item-header"
      });
      const title = createElement("h5");
      const actions = createElement("div", {
        className: "click-settings-list-item-actions"
      });
      const up = createElement("button", {
        type: "button",
        text: "Up",
        attributes: { "aria-label": "Move entry up" }
      });
      const down = createElement("button", {
        type: "button",
        text: "Down",
        attributes: { "aria-label": "Move entry down" }
      });
      const remove = createElement("button", {
        type: "button",
        className: "click-settings-remove",
        text: "Remove"
      });
      actions.append(up, down, remove);
      cardHeader.append(title, actions);
      const body = createElement("div", {
        className: "click-settings-list-item-body"
      });
      card.append(cardHeader, body);
      const editor = options.renderItem(body, clone(value));
      const entry = {
        card,
        title,
        up,
        down,
        remove,
        collect: editor.collect,
        focus: editor.focus || (() => {})
      };
      up.addEventListener("click", () => moveEntry(entry, -1));
      down.addEventListener("click", () => moveEntry(entry, 1));
      remove.addEventListener("click", () => {
        const index = entries.indexOf(entry);
        if (index < 0) return;
        entries.splice(index, 1);
        card.remove();
        updateEntryChrome();
      });
      entries.push(entry);
      items.append(card);
      updateEntryChrome();
      return entry;
    };

    addButton.addEventListener("click", () => {
      try {
        const entry = addEntry(options.createItem());
        entry?.focus();
      }
      catch (error) {
        options.reportError(error);
      }
    });
    (Array.isArray(options.values) ? options.values : [])
      .forEach(addEntry);
    updateEntryChrome();
    wrapper.append(heading, empty, items);

    return {
      element: wrapper,
      collect: () => entries.map((entry) => entry.collect()),
      setEnabled: (enabled) => {
        setControlsEnabled(wrapper, enabled);
        if (enabled) updateEntryChrome();
      }
    };
  }

  function createPairEditor(value, pointer, labels, options = {}) {
    const pair = Array.isArray(value) ? value : [0, 0];
    const grid = createElement("div", {
      className: "click-settings-pair-grid"
    });
    const low = numberInput(pair[0], `${pointer}/0`, {
      min: options.min,
      max: options.max,
      ...(options.integer ? { step: 1 } : {}),
      ...(options.exclusiveMin !== undefined
        ? { min: Number.MIN_VALUE }
        : {})
    });
    const high = numberInput(pair[1], `${pointer}/1`, {
      min: options.min,
      max: options.max,
      ...(options.integer ? { step: 1 } : {}),
      ...(options.exclusiveMin !== undefined
        ? { min: Number.MIN_VALUE }
        : {})
    });
    grid.append(
      field(labels[0], low, { unit: options.unit }),
      field(labels[1], high, { unit: options.unit }));
    return {
      element: grid,
      focus: () => low.focus(),
      collect: (collectOptions = {}) => {
        const readOptions = {
          ...(options.integer ? { integer: true } : {})
        };
        if (options.min !== undefined) readOptions.min = options.min;
        if (options.max !== undefined) readOptions.max = options.max;
        if (options.exclusiveMin !== undefined) {
          readOptions.exclusiveMin = options.exclusiveMin;
        }
        const result = [
          readNumber(low, labels[0], readOptions),
          readNumber(high, labels[1], readOptions)
        ];
        if (options.ordered !== false && result[0] > result[1]) {
          throwControlError(
            high,
            `${labels[1]} must be greater than or equal to ${labels[0]}`);
        }
        const strict = collectOptions.strict ?? options.strict;
        if (strict && result[0] === result[1]) {
          throwControlError(
            high,
            `${labels[1]} must be greater than ${labels[0]}`);
        }
        return result;
      }
    };
  }

  function createFilterEditor(value, pointer, instanceId) {
    const wrapper = section(
      "Digital filter",
      "PAMGuard applies this filter before the corresponding detector path.");
    const typeSuggestions = createElement("datalist", {
      attributes: { id: `${instanceId}-filter-types` }
    });
    ["butterworth", "chebyshev", "firwindow"].forEach((type) => {
      typeSuggestions.append(createElement("option", {
        attributes: { value: type }
      }));
    });
    const type = textInput(value.type, `${pointer}/type`, {
      list: typeSuggestions.id,
      autocomplete: "off"
    });
    const band = selectInput(
      value.band,
      `${pointer}/band`,
      FILTER_BANDS);
    const order = numberInput(value.order, `${pointer}/order`, {
      min: 1,
      max: 64,
      step: 1
    });
    const highPass = numberInput(
      value.highPassFreqHz,
      `${pointer}/highPassFreqHz`,
      { min: 0 });
    const lowPass = numberInput(
      value.lowPassFreqHz,
      `${pointer}/lowPassFreqHz`,
      { min: 0 });
    const ripple = numberInput(
      value.passBandRippleDb,
      `${pointer}/passBandRippleDb`);
    wrapper.append(
      field("Filter type", type, {
        help: "Canonical string; Java-backed values include butterworth, " +
          "chebyshev and firwindow."
      }),
      typeSuggestions,
      field("Filter band", band),
      field("Order", order),
      field("High-pass frequency", highPass, { unit: "Hz" }),
      field("Low-pass frequency", lowPass, { unit: "Hz" }),
      field("Pass-band ripple", ripple, { unit: "dB" }));
    return {
      element: wrapper,
      focus: () => type.focus(),
      collect: () => ({
        type: readText(type, "Filter type"),
        band: band.value,
        order: readNumber(order, "Filter order", {
          integer: true,
          min: 1,
          max: 64
        }),
        lowPassFreqHz: readNumber(
          lowPass,
          "Low-pass frequency",
          { min: 0 }),
        highPassFreqHz: readNumber(
          highPass,
          "High-pass frequency",
          { min: 0 }),
        passBandRippleDb: readNumber(
          ripple,
          "Pass-band ripple")
      })
    };
  }

  function createDelayEditor(value, pointer, options = {}) {
    const wrapper = createElement("div", {
      className: "click-settings-delay-editor"
    });
    let clickType = null;
    if (options.includeClickType) {
      clickType = numberInput(
        value.clickType,
        `${pointer}/clickType`,
        { min: 1, max: 255, step: 1 });
      wrapper.append(field("Click type", clickType, {
        help: "Classifier code in the Java/PAMGuard range 1–255."
      }));
    }
    const filterBearings = checkboxInput(
      value.filterBearings,
      `${pointer}/filterBearings`);
    const filterControls = createElement("div", {
      className: "click-settings-conditional"
    });
    const filterBand = selectInput(
      value.filterBand,
      `${pointer}/filterBand`,
      FILTER_BANDS);
    const highPass = numberInput(
      value.filterHighPassHz,
      `${pointer}/filterHighPassHz`,
      { min: 0 });
    const lowPass = numberInput(
      value.filterLowPassHz,
      `${pointer}/filterLowPassHz`,
      { min: 0 });
    filterControls.append(
      field("Delay filter band", filterBand),
      field("Delay high-pass frequency", highPass, { unit: "Hz" }),
      field("Delay low-pass frequency", lowPass, { unit: "Hz" }));
    const envelope = checkboxInput(
      value.envelopeBearings,
      `${pointer}/envelopeBearings`);
    const leadingEdge = checkboxInput(
      value.useLeadingEdge,
      `${pointer}/useLeadingEdge`);
    const upSample = numberInput(
      value.upSample,
      `${pointer}/upSample`,
      { min: 1, max: 32, step: 1 });
    const restricted = checkboxInput(
      value.useRestrictedBins,
      `${pointer}/useRestrictedBins`);
    const restrictedBins = numberInput(
      value.restrictedBins,
      `${pointer}/restrictedBins`,
      { min: 1, step: 1 });
    const restrictedRow = field(
      "Restricted correlation bins",
      restrictedBins);

    const updateFilter = () => {
      setControlsEnabled(filterControls, filterBearings.checked);
      filterControls.classList.toggle(
        "is-disabled",
        !filterBearings.checked);
    };
    const updateRestricted = () => {
      restrictedBins.disabled = !restricted.checked;
      restrictedRow.classList.toggle(
        "is-disabled",
        !restricted.checked);
    };
    filterBearings.addEventListener("change", updateFilter);
    restricted.addEventListener("change", updateRestricted);
    updateFilter();
    updateRestricted();

    wrapper.append(
      checkboxField(
        "Filter waveforms before measuring bearings",
        filterBearings),
      filterControls,
      checkboxField(
        "Use waveform envelope for bearings",
        envelope),
      checkboxField(
        "Use leading edge",
        leadingEdge),
      field("Upsample factor", upSample, {
        help: "Integer factor from 1 to 32."
      }),
      checkboxField(
        "Restrict correlation bins",
        restricted),
      restrictedRow);

    return {
      element: wrapper,
      focus: () => (clickType || filterBearings).focus(),
      collect: () => {
        const result = {
          filterBearings: filterBearings.checked,
          filterBand: filterBand.value,
          filterHighPassHz: readNumber(
            highPass,
            "Delay high-pass frequency",
            { min: 0 }),
          filterLowPassHz: readNumber(
            lowPass,
            "Delay low-pass frequency",
            { min: 0 }),
          envelopeBearings: envelope.checked,
          useLeadingEdge: leadingEdge.checked,
          upSample: readNumber(
            upSample,
            "Delay upsample factor",
            { integer: true, min: 1, max: 32 }),
          useRestrictedBins: restricted.checked,
          restrictedBins: readNumber(
            restrictedBins,
            "Restricted correlation bins",
            { integer: true, min: 1 })
        };
        if (clickType) {
          result.clickType = readNumber(
            clickType,
            "Click type",
            { integer: true, min: 1, max: 255 });
        }
        return options.includeClickType
          ? {
              clickType: result.clickType,
              filterBearings: result.filterBearings,
              filterBand: result.filterBand,
              filterHighPassHz: result.filterHighPassHz,
              filterLowPassHz: result.filterLowPassHz,
              envelopeBearings: result.envelopeBearings,
              useLeadingEdge: result.useLeadingEdge,
              upSample: result.upSample,
              useRestrictedBins: result.useRestrictedBins,
              restrictedBins: result.restrictedBins
            }
          : result;
      }
    };
  }

  function readStoredString(control, label, options = {}) {
    clearControlError(control);
    const value = String(control.value);
    if (options.required && value.length === 0) {
      throwControlError(control, `${label} cannot be empty`);
    }
    if (options.maxLength !== undefined &&
        value.length > options.maxLength) {
      throwControlError(
        control,
        `${label} cannot exceed ${options.maxLength} characters`);
    }
    return value;
  }

  function readOddPositiveInteger(control, label) {
    const value = readNumber(control, label, {
      integer: true,
      min: 1
    });
    if (value % 2 === 0) {
      throwControlError(control, `${label} must be odd`);
    }
    return value;
  }

  function readNonZeroNumber(control, label) {
    const value = readNumber(control, label);
    if (value === 0) {
      throwControlError(control, `${label} cannot be zero`);
    }
    return value;
  }

  function createCriterionSection(options) {
    const wrapper = section(options.title, options.help);
    const toggle = checkboxInput(options.enabled, options.pointer);
    toggle.setAttribute("data-click-criterion", options.criterion);
    const body = createElement("div", {
      className: "click-settings-criterion-body click-settings-conditional"
    });
    wrapper.append(
      checkboxField(options.label || "Use this criterion", toggle),
      body);
    const update = () => {
      setControlsEnabled(body, toggle.checked);
      body.classList.toggle("is-disabled", !toggle.checked);
    };
    toggle.addEventListener("change", update);
    update();
    return { element: wrapper, toggle, body, update };
  }

  function createBasicClassifierEditor(rawValue, pointer) {
    const value = mergeDefaults(
      DEFAULT_BASIC_CLASSIFIER_TYPE,
      rawValue);
    const wrapper = createElement("div", {
      className: "click-settings-classifier-editor"
    });

    const general = section(
      "Basic click type",
      "Fields map directly to ClickTypeParams and its inherited " +
        "ClickTypeCommonParams values.");
    const name = textInput(value.name, `${pointer}/name`, {
      maxlength: 128
    });
    const speciesCode = numberInput(
      value.speciesCode,
      `${pointer}/speciesCode`,
      { min: 1, max: 255, step: 1 });
    const enabled = checkboxInput(value.enabled, `${pointer}/enabled`);
    const discard = checkboxInput(value.discard, `${pointer}/discard`);
    general.append(
      field("Name", name),
      field("Species code", speciesCode, {
        help: "Portable project normalization: unique 1–255 code. Java " +
          "stores an int but later narrows it to a byte."
      }),
      checkboxField(
        "Enabled",
        enabled,
        "Stored and displayed for Java parity. The pinned Java " +
          "BasicClickIdentifier does not consult this inherited flag, so " +
          "switching it off does not stop Basic classification."),
      checkboxField(
        "Discard matching clicks",
        discard,
        "A matching type can classify a click and mark it for discard."));

    const energy = createCriterionSection({
      title: "Energy bands",
      help: "PAMGuard checks both in-band energy limits, then requires band " +
        "1 minus band 2 to meet the difference threshold.",
      label: "Use energy-band criterion",
      enabled:
        (value.whichSelections & BASIC_SELECTIONS.energy) !== 0,
      pointer: `${pointer}/whichSelections`,
      criterion: "energy"
    });
    const band1Freq = createPairEditor(
      value.band1FreqHz,
      `${pointer}/band1FreqHz`,
      ["Band 1 lower frequency", "Band 1 upper frequency"],
      { ordered: false, unit: "Hz" });
    const band1Energy = createPairEditor(
      value.band1EnergyDb,
      `${pointer}/band1EnergyDb`,
      ["Band 1 minimum energy", "Band 1 maximum energy"],
      { ordered: false, unit: "dB" });
    const band2Freq = createPairEditor(
      value.band2FreqHz,
      `${pointer}/band2FreqHz`,
      ["Band 2 lower frequency", "Band 2 upper frequency"],
      { ordered: false, unit: "Hz" });
    const band2Energy = createPairEditor(
      value.band2EnergyDb,
      `${pointer}/band2EnergyDb`,
      ["Band 2 minimum energy", "Band 2 maximum energy"],
      { ordered: false, unit: "dB" });
    const bandDifference = numberInput(
      value.bandEnergyDifferenceDb,
      `${pointer}/bandEnergyDifferenceDb`);
    energy.body.append(
      band1Freq.element,
      band1Energy.element,
      band2Freq.element,
      band2Energy.element,
      field("Minimum band 1 – band 2 difference", bandDifference, {
        unit: "dB"
      }));

    const peak = createCriterionSection({
      title: "Peak frequency",
      help: "Search for the spectral peak, then test its allowed frequency.",
      label: "Use peak-frequency-position criterion",
      enabled:
        (value.whichSelections & BASIC_SELECTIONS.peak) !== 0,
      pointer: `${pointer}/whichSelections`,
      criterion: "peak"
    });
    const peakSearch = createPairEditor(
      value.peakFrequencySearchHz,
      `${pointer}/peakFrequencySearchHz`,
      ["Search lower frequency", "Search upper frequency"],
      { ordered: false, unit: "Hz" });
    const peakRange = createPairEditor(
      value.peakFrequencyRangeHz,
      `${pointer}/peakFrequencyRangeHz`,
      ["Allowed lower frequency", "Allowed upper frequency"],
      { ordered: false, unit: "Hz" });
    peak.body.append(peakSearch.element, peakRange.element);

    const width = createCriterionSection({
      title: "Peak width",
      help: "The peak-width measurement reuses the peak search range above.",
      label: "Use peak-frequency-width criterion",
      enabled:
        (value.whichSelections & BASIC_SELECTIONS.width) !== 0,
      pointer: `${pointer}/whichSelections`,
      criterion: "width"
    });
    const peakWidth = createPairEditor(
      value.peakWidthHz,
      `${pointer}/peakWidthHz`,
      ["Minimum peak width", "Maximum peak width"],
      { ordered: false, unit: "Hz" });
    const widthFraction = numberInput(
      value.widthEnergyFraction,
      `${pointer}/widthEnergyFraction`);
    width.body.append(
      peakWidth.element,
      field("Width energy fraction", widthFraction, { unit: "%" }));

    const mean = createCriterionSection({
      title: "Mean frequency",
      help: "PAMGuard sums energy over one range and tests the resulting " +
        "mean against the selection range.",
      label: "Use mean-frequency criterion",
      enabled:
        (value.whichSelections & BASIC_SELECTIONS.mean) !== 0,
      pointer: `${pointer}/whichSelections`,
      criterion: "mean"
    });
    const meanSum = createPairEditor(
      value.meanSumRangeHz,
      `${pointer}/meanSumRangeHz`,
      ["Sum lower frequency", "Sum upper frequency"],
      { ordered: false, unit: "Hz" });
    const meanSelection = createPairEditor(
      value.meanSelectionRangeHz,
      `${pointer}/meanSelectionRangeHz`,
      ["Allowed lower frequency", "Allowed upper frequency"],
      { ordered: false, unit: "Hz" });
    mean.body.append(meanSum.element, meanSelection.element);

    const length = createCriterionSection({
      title: "Click length",
      help: "A maximum of zero retains Java's no-length-limit behaviour.",
      label: "Use click-length criterion",
      enabled:
        (value.whichSelections & BASIC_SELECTIONS.length) !== 0,
      pointer: `${pointer}/whichSelections`,
      criterion: "length"
    });
    const clickLength = createPairEditor(
      value.clickLengthMs,
      `${pointer}/clickLengthMs`,
      ["Minimum click length", "Maximum click length"],
      { ordered: false, unit: "ms" });
    const lengthFraction = numberInput(
      value.lengthEnergyFraction,
      `${pointer}/lengthEnergyFraction`);
    length.body.append(
      clickLength.element,
      field("Length energy fraction", lengthFraction, { unit: "%" }));

    wrapper.append(
      general,
      energy.element,
      peak.element,
      width.element,
      mean.element,
      length.element);

    return {
      element: wrapper,
      focus: () => name.focus(),
      collect: () => {
        let whichSelections = 0;
        for (const [criterion, criterionValue] of [
          [energy, BASIC_SELECTIONS.energy],
          [width, BASIC_SELECTIONS.width],
          [peak, BASIC_SELECTIONS.peak],
          [mean, BASIC_SELECTIONS.mean],
          [length, BASIC_SELECTIONS.length]
        ]) {
          if (criterion.toggle.checked) {
            whichSelections |= criterionValue;
          }
        }
        return {
          name: readStoredString(name, "Basic classifier name", {
            maxLength: 128
          }),
          speciesCode: readNumber(
            speciesCode,
            "Basic classifier species code",
            { integer: true, min: 1, max: 255 }),
          enabled: enabled.checked,
          discard: discard.checked,
          whichSelections,
          band1FreqHz: band1Freq.collect(),
          band2FreqHz: band2Freq.collect(),
          band1EnergyDb: band1Energy.collect(),
          band2EnergyDb: band2Energy.collect(),
          bandEnergyDifferenceDb: readNumber(
            bandDifference,
            "Band-energy difference"),
          peakFrequencySearchHz: peakSearch.collect(),
          peakFrequencyRangeHz: peakRange.collect(),
          peakWidthHz: peakWidth.collect(),
          widthEnergyFraction: readNumber(
            widthFraction,
            "Width energy fraction"),
          meanSumRangeHz: meanSum.collect(),
          meanSelectionRangeHz: meanSelection.collect(),
          clickLengthMs: clickLength.collect(),
          lengthEnergyFraction: readNumber(
            lengthFraction,
            "Length energy fraction")
        };
      }
    };
  }

  function createSweepClassifierEditor(rawValue, pointer) {
    const value = mergeDefaults(
      DEFAULT_SWEEP_CLASSIFIER_TYPE,
      rawValue);
    const wrapper = createElement("div", {
      className: "click-settings-classifier-editor"
    });

    const general = section(
      "Sweep classifier set",
      "Fields map directly to SweepClassifierSet. Unlike Basic mode, the " +
        "Sweep runtime honours the enabled flag.");
    const name = textInput(value.name, `${pointer}/name`, {
      maxlength: 128
    });
    const speciesCode = numberInput(
      value.speciesCode,
      `${pointer}/speciesCode`,
      { min: 1, max: 255, step: 1 });
    const enabled = checkboxInput(value.enabled, `${pointer}/enabled`);
    const discard = checkboxInput(value.discard, `${pointer}/discard`);
    const channelChoice = selectInput(
      value.channelChoice,
      `${pointer}/channelChoice`,
      SWEEP_CHANNEL_CHOICES);
    general.append(
      field("Name", name),
      field("Species code", speciesCode, {
        help: "Portable project normalization: unique 1–255 code. Java " +
          "stores an int but later narrows it to a byte."
      }),
      checkboxField(
        "Enabled",
        enabled,
        "Disabled Sweep sets are skipped by SweepClassifier."),
      checkboxField(
        "Discard matching clicks",
        discard,
        "A passing set can classify a click and mark it for discard."),
      field("Channel policy", channelChoice));

    const restriction = createCriterionSection({
      title: "Analysis-bin restriction",
      help: "Restrict the waveform/FFT analysis to a fixed number of bins.",
      label: "Restrict analysis length",
      enabled: value.restrictLength,
      pointer: `${pointer}/restrictLength`,
      criterion: "restrict-length"
    });
    const restrictedBins = numberInput(
      value.restrictedBins,
      `${pointer}/restrictedBins`,
      { min: 1, step: 1 });
    const restrictedBinType = selectInput(
      value.restrictedBinType,
      `${pointer}/restrictedBinType`,
      SWEEP_RESTRICTED_BIN_TYPES);
    restriction.body.append(
      field("Restricted bins", restrictedBins),
      field("Bin origin", restrictedBinType));

    const length = createCriterionSection({
      title: "Click length",
      label: "Use click-length criterion",
      enabled: value.enableLength,
      pointer: `${pointer}/enableLength`,
      criterion: "length"
    });
    const lengthSmoothing = numberInput(
      value.lengthSmoothing,
      `${pointer}/lengthSmoothing`,
      { min: 1, step: 2 });
    const lengthDb = numberInput(
      value.lengthDb,
      `${pointer}/lengthDb`);
    const lengthMs = createPairEditor(
      value.lengthMs,
      `${pointer}/lengthMs`,
      ["Minimum click length", "Maximum click length"],
      { ordered: false, unit: "ms" });
    length.body.append(
      field("Length smoothing", lengthSmoothing, {
        help: "Positive odd number of bins."
      }),
      field("Length threshold", lengthDb, {
        unit: "dB",
        help: "Non-zero level below the peak."
      }),
      lengthMs.element);

    const energy = createCriterionSection({
      title: "Energy bands",
      help: "Compare one test band with each of two control bands.",
      label: "Use energy-band criteria",
      enabled: value.enableEnergyBands,
      pointer: `${pointer}/enableEnergyBands`,
      criterion: "energy"
    });
    const testEnergyBand = createPairEditor(
      value.testEnergyBandHz,
      `${pointer}/testEnergyBandHz`,
      ["Test lower frequency", "Test upper frequency"],
      { ordered: false, unit: "Hz" });
    const controlEnergyBand0 = createPairEditor(
      value.controlEnergyBand0Hz,
      `${pointer}/controlEnergyBand0Hz`,
      ["Control 1 lower frequency", "Control 1 upper frequency"],
      { ordered: false, unit: "Hz" });
    const controlEnergyBand1 = createPairEditor(
      value.controlEnergyBand1Hz,
      `${pointer}/controlEnergyBand1Hz`,
      ["Control 2 lower frequency", "Control 2 upper frequency"],
      { ordered: false, unit: "Hz" });
    const energyThreshold0 = numberInput(
      value.energyThreshold0Db,
      `${pointer}/energyThreshold0Db`);
    const energyThreshold1 = numberInput(
      value.energyThreshold1Db,
      `${pointer}/energyThreshold1Db`);
    energy.body.append(
      testEnergyBand.element,
      controlEnergyBand0.element,
      field("Test minus control 1 threshold", energyThreshold0, {
        unit: "dB"
      }),
      controlEnergyBand1.element,
      field("Test minus control 2 threshold", energyThreshold1, {
        unit: "dB"
      }));

    const amplitude = createCriterionSection({
      title: "Amplitude",
      label: "Use amplitude criterion",
      enabled: value.testAmplitude,
      pointer: `${pointer}/testAmplitude`,
      criterion: "amplitude"
    });
    const amplitudeRange = createPairEditor(
      value.amplitudeRangeDb,
      `${pointer}/amplitudeRangeDb`,
      ["Minimum amplitude", "Maximum amplitude"],
      { ordered: false, unit: "dB" });
    amplitude.body.append(amplitudeRange.element);

    const fft = createCriterionSection({
      title: "Classifier FFT filter",
      help: "Optional Sweep-classifier filter using PAMGuard's FFT filter " +
        "band and edge fields.",
      label: "Filter before Sweep classification",
      enabled: value.enableFftFilter,
      pointer: `${pointer}/enableFftFilter`,
      criterion: "fft-filter"
    });
    const fftBand = selectInput(
      value.fftFilter.band,
      `${pointer}/fftFilter/band`,
      FILTER_BANDS);
    const fftHighPass = numberInput(
      value.fftFilter.highPassFreqHz,
      `${pointer}/fftFilter/highPassFreqHz`);
    const fftLowPass = numberInput(
      value.fftFilter.lowPassFreqHz,
      `${pointer}/fftFilter/lowPassFreqHz`);
    fft.body.append(
      field("Filter band", fftBand),
      field("High-pass edge", fftHighPass, { unit: "Hz" }),
      field("Low-pass edge", fftLowPass, { unit: "Hz" }));

    const spectral = section(
      "Peak, width, and mean frequency",
      "The enabled spectral tests share the peak-search range and odd-bin " +
        "smoothing setting.");
    const enablePeak = checkboxInput(
      value.enablePeak,
      `${pointer}/enablePeak`);
    const enableWidth = checkboxInput(
      value.enableWidth,
      `${pointer}/enableWidth`);
    const enableMean = checkboxInput(
      value.enableMean,
      `${pointer}/enableMean`);
    const spectralBody = createElement("div", {
      className: "click-settings-criterion-body click-settings-conditional"
    });
    const peakSearchRange = createPairEditor(
      value.peakSearchRangeHz,
      `${pointer}/peakSearchRangeHz`,
      ["Search lower frequency", "Search upper frequency"],
      { ordered: false, unit: "Hz" });
    const peakRange = createPairEditor(
      value.peakRangeHz,
      `${pointer}/peakRangeHz`,
      ["Allowed peak minimum", "Allowed peak maximum"],
      { ordered: false, unit: "Hz" });
    const peakWidthRange = createPairEditor(
      value.peakWidthRangeHz,
      `${pointer}/peakWidthRangeHz`,
      ["Allowed width minimum", "Allowed width maximum"],
      { ordered: false, unit: "Hz" });
    const meanRange = createPairEditor(
      value.meanRangeHz,
      `${pointer}/meanRangeHz`,
      ["Allowed mean minimum", "Allowed mean maximum"],
      { ordered: false, unit: "Hz" });
    const peakSmoothing = numberInput(
      value.peakSmoothing,
      `${pointer}/peakSmoothing`,
      { min: 1, step: 2 });
    const peakWidthThreshold = numberInput(
      value.peakWidthThresholdDb,
      `${pointer}/peakWidthThresholdDb`);
    spectralBody.append(
      peakSearchRange.element,
      peakRange.element,
      peakWidthRange.element,
      meanRange.element,
      field("Peak smoothing", peakSmoothing, {
        help: "Positive odd number of bins."
      }),
      field("Peak-width threshold", peakWidthThreshold, {
        unit: "dB",
        help: "Non-zero level below the peak."
      }));
    spectral.append(
      checkboxField("Test peak frequency", enablePeak),
      checkboxField("Test peak width", enableWidth),
      checkboxField("Test mean frequency", enableMean),
      spectralBody);
    const updateSpectral = () => {
      const active =
        enablePeak.checked || enableWidth.checked || enableMean.checked;
      setControlsEnabled(spectralBody, active);
      spectralBody.classList.toggle("is-disabled", !active);
    };
    enablePeak.addEventListener("change", updateSpectral);
    enableWidth.addEventListener("change", updateSpectral);
    enableMean.addEventListener("change", updateSpectral);
    updateSpectral();

    const zeroCrossings = createCriterionSection({
      title: "Zero crossings",
      label: "Use zero-crossing-count criterion",
      enabled: value.enableZeroCrossings,
      pointer: `${pointer}/enableZeroCrossings`,
      criterion: "zero-crossings"
    });
    const zeroCount = createPairEditor(
      value.zeroCrossingCount,
      `${pointer}/zeroCrossingCount`,
      ["Minimum zero crossings", "Maximum zero crossings"],
      { integer: true, ordered: false });
    const enableSweep = checkboxInput(
      value.enableSweep,
      `${pointer}/enableSweep`);
    const sweepRate = createPairEditor(
      value.zeroCrossingSweepKhzPerMs,
      `${pointer}/zeroCrossingSweepKhzPerMs`,
      ["Minimum sweep rate", "Maximum sweep rate"],
      { ordered: false, unit: "kHz/ms" });
    const sweepBody = createElement("div", {
      className: "click-settings-conditional"
    });
    sweepBody.append(sweepRate.element);
    zeroCrossings.body.append(zeroCount.element);
    zeroCrossings.element.append(
      checkboxField(
        "Use zero-crossing sweep-rate criterion",
        enableSweep,
        "The pinned Java worker can evaluate sweep rate independently. " +
          "The legacy Swing dialog visually couples the two switches."),
      sweepBody);
    const updateSweepRate = () => {
      setControlsEnabled(sweepBody, enableSweep.checked);
      sweepBody.classList.toggle("is-disabled", !enableSweep.checked);
    };
    enableSweep.addEventListener("change", updateSweepRate);
    updateSweepRate();

    const correlation = section(
      "Cross correlation",
      "Enable the minimum whole-waveform correlation and/or the " +
        "peak-correlation-factor criterion.");
    const enableMinCorrelation = checkboxInput(
      value.enableMinCrossCorrelation,
      `${pointer}/enableMinCrossCorrelation`);
    const minCorrelation = numberInput(
      value.minCorrelation,
      `${pointer}/minCorrelation`);
    const minCorrelationRow = field(
      "Minimum correlation",
      minCorrelation);
    const enablePeakCorrelation = checkboxInput(
      value.enablePeakCrossCorrelation,
      `${pointer}/enablePeakCrossCorrelation`);
    const correlationFactor = numberInput(
      value.correlationFactor,
      `${pointer}/correlationFactor`);
    const correlationFactorRow = field(
      "Peak correlation factor",
      correlationFactor,
      { help: "Non-zero multiplier." });
    correlation.append(
      checkboxField(
        "Use minimum cross-correlation criterion",
        enableMinCorrelation),
      minCorrelationRow,
      checkboxField(
        "Use peak cross-correlation criterion",
        enablePeakCorrelation),
      correlationFactorRow);
    const updateCorrelation = () => {
      minCorrelation.disabled = !enableMinCorrelation.checked;
      correlationFactor.disabled = !enablePeakCorrelation.checked;
      minCorrelationRow.classList.toggle(
        "is-disabled",
        !enableMinCorrelation.checked);
      correlationFactorRow.classList.toggle(
        "is-disabled",
        !enablePeakCorrelation.checked);
    };
    enableMinCorrelation.addEventListener("change", updateCorrelation);
    enablePeakCorrelation.addEventListener(
      "change",
      updateCorrelation);
    updateCorrelation();

    const bearing = createCriterionSection({
      title: "Bearing limits",
      help: "Canonical project values are radians. PAMGuard's dialog " +
        "presents the same limits in degrees.",
      label: "Use bearing-limit criterion",
      enabled: value.enableBearingLimits,
      pointer: `${pointer}/enableBearingLimits`,
      criterion: "bearing"
    });
    const excludeBearing = checkboxInput(
      value.excludeBearingLimits,
      `${pointer}/excludeBearingLimits`);
    const bearingRange = createPairEditor(
      value.bearingLimitsRadians,
      `${pointer}/bearingLimitsRadians`,
      ["Minimum bearing", "Maximum bearing"],
      { ordered: false, unit: "rad" });
    bearing.body.append(
      checkboxField(
        "Exclude bearings inside this range",
        excludeBearing),
      bearingRange.element);

    wrapper.append(
      general,
      restriction.element,
      length.element,
      energy.element,
      amplitude.element,
      fft.element,
      spectral,
      zeroCrossings.element,
      correlation,
      bearing.element);

    return {
      element: wrapper,
      focus: () => name.focus(),
      collect: () => {
        const highPassFreqHz = readNumber(
          fftHighPass,
          "Classifier high-pass edge");
        const lowPassFreqHz = readNumber(
          fftLowPass,
          "Classifier low-pass edge");
        const correlationFactorValue = readNumber(
          correlationFactor,
          "Peak correlation factor");
        const anySpectralTest =
          enablePeak.checked ||
          enableWidth.checked ||
          enableMean.checked;
        return {
          name: readStoredString(name, "Sweep classifier name", {
            required: true,
            maxLength: 128
          }),
          speciesCode: readNumber(
            speciesCode,
            "Sweep classifier species code",
            { integer: true, min: 1, max: 255 }),
          discard: discard.checked,
          enabled: enabled.checked,
          channelChoice: channelChoice.value,
          restrictLength: restriction.toggle.checked,
          restrictedBins: readNumber(
            restrictedBins,
            "Restricted bins",
            { integer: true, min: 1 }),
          restrictedBinType: restrictedBinType.value,
          enableLength: length.toggle.checked,
          lengthSmoothing: readOddPositiveInteger(
            lengthSmoothing,
            "Length smoothing"),
          lengthDb: readNonZeroNumber(lengthDb, "Length threshold"),
          lengthMs: lengthMs.collect({
            strict: length.toggle.checked
          }),
          enableEnergyBands: energy.toggle.checked,
          testEnergyBandHz: testEnergyBand.collect({
            strict: energy.toggle.checked
          }),
          controlEnergyBand0Hz: controlEnergyBand0.collect({
            strict: energy.toggle.checked
          }),
          controlEnergyBand1Hz: controlEnergyBand1.collect({
            strict: energy.toggle.checked
          }),
          energyThreshold0Db: readNumber(
            energyThreshold0,
            "Control 1 energy threshold"),
          energyThreshold1Db: readNumber(
            energyThreshold1,
            "Control 2 energy threshold"),
          testAmplitude: amplitude.toggle.checked,
          amplitudeRangeDb: amplitudeRange.collect({
            strict: amplitude.toggle.checked
          }),
          enableFftFilter: fft.toggle.checked,
          fftFilter: {
            band: fftBand.value,
            lowPassFreqHz,
            highPassFreqHz
          },
          enablePeak: enablePeak.checked,
          enableWidth: enableWidth.checked,
          enableMean: enableMean.checked,
          peakSearchRangeHz: peakSearchRange.collect({
            strict:
              enablePeak.checked ||
              enableWidth.checked ||
              enableMean.checked
          }),
          peakRangeHz: peakRange.collect({
            strict: enablePeak.checked
          }),
          peakWidthRangeHz: peakWidthRange.collect({
            strict: enableWidth.checked
          }),
          meanRangeHz: meanRange.collect({
            strict: enableMean.checked
          }),
          peakSmoothing: anySpectralTest
            ? readOddPositiveInteger(
                peakSmoothing,
                "Peak smoothing")
            : readNumber(
                peakSmoothing,
                "Peak smoothing",
                { integer: true }),
          peakWidthThresholdDb: enableWidth.checked
            ? readNonZeroNumber(
                peakWidthThreshold,
                "Peak-width threshold")
            : readNumber(
                peakWidthThreshold,
                "Peak-width threshold"),
          enableZeroCrossings: zeroCrossings.toggle.checked,
          zeroCrossingCount: zeroCount.collect({
            strict: zeroCrossings.toggle.checked
          }),
          enableSweep: enableSweep.checked,
          zeroCrossingSweepKhzPerMs: sweepRate.collect({
            strict: enableSweep.checked
          }),
          enableMinCrossCorrelation: enableMinCorrelation.checked,
          enablePeakCrossCorrelation: enablePeakCorrelation.checked,
          minCorrelation: readNumber(
            minCorrelation,
            "Minimum correlation"),
          correlationFactor: correlationFactorValue,
          enableBearingLimits: bearing.toggle.checked,
          excludeBearingLimits: excludeBearing.checked,
          bearingLimitsRadians: bearingRange.collect()
        };
      }
    };
  }

  function validateCollectedSettings(settings) {
    const detector = settings.detector;
    if (detector.groupingType === "user") {
      const selected = BigInt(detector.channelBitmap);
      for (let channel = 0; channel < 32; channel += 1) {
        const selectedChannel =
          (selected & (BigInt(1) << BigInt(channel))) !== BigInt(0);
        if (selectedChannel && detector.channelGroups.length <= channel) {
          throw new Error(
            "User grouping must assign every selected source channel");
        }
      }
    }
    if (settings.train.maxIciSeconds < settings.train.minIciSeconds) {
      throw new Error(
        "Maximum train ICI must be greater than or equal to minimum ICI");
    }
    for (const [label, types] of [
      ["Basic", settings.classification.basicTypes],
      ["Sweep", settings.classification.sweepTypes]
    ]) {
      const speciesCodes = new Set();
      for (const type of types) {
        if (speciesCodes.has(type.speciesCode)) {
          throw new Error(
            `${label} classifier species codes must be unique`);
        }
        speciesCodes.add(type.speciesCode);
      }
    }
    const tracked = settings.localisation.trackedTrain;
    if (!Array.isArray(tracked.isSelected) ||
        tracked.isSelected.length !== 4 ||
        tracked.isSelected.some((selected) =>
          typeof selected !== "boolean")) {
      throw new Error(
        "TrackedClickLocaliser isSelected must contain four " +
        "Java algorithm flags");
    }
    const trackedNumbers = [
      ["maxRangeM", false, undefined, 0],
      ["maxHeightM", false, undefined, undefined],
      ["minHeightM", false, undefined, undefined],
      ["maxTimeMilliseconds", true, 0, undefined],
      ["maxPoints", true, 1, undefined]
    ];
    for (const [key, integer, minimum, exclusiveMinimum] of trackedNumbers) {
      const value = tracked[key];
      const valid = typeof value === "number" &&
        Number.isFinite(value) &&
        (!integer || Number.isInteger(value)) &&
        (minimum === undefined || value >= minimum) &&
        (exclusiveMinimum === undefined || value > exclusiveMinimum);
      if (!valid) {
        throw new Error(
          `TrackedClickLocaliser value '${key}' is invalid`);
      }
    }
    if (typeof tracked.limitPoints !== "boolean") {
      throw new Error(
        "TrackedClickLocaliser limitPoints value is invalid");
    }
    if (tracked.minHeightM > tracked.maxHeightM) {
      throw new Error(
        "TrackedClickLocaliser height bounds are not ordered");
    }
    return settings;
  }

  function mountEditor(options) {
    if (!options?.container) {
      throw new Error("Click Detector editor requires a container");
    }
    if (typeof document === "undefined") {
      throw new Error("Click Detector editor requires a browser document");
    }
    const reportError = typeof options.reportError === "function"
      ? options.reportError
      : () => {};
    const draft = mergeDefaults(DEFAULT_SETTINGS, options.settings);
    const instanceId = `click-settings-${++editorSequence}`;
    ensureStylesheet();

    const root = createElement("div", {
      className: "click-settings-editor",
      attributes: {
        "data-pamguard-click-settings-editor": instanceId
      }
    });
    options.container.append(root);
    root.append(createElement("p", {
      className: "click-settings-intro",
      text: "This editor follows PAMGuard's Click Detection Parameters " +
        "workflow. The module graph owns the raw-data connection; these " +
        "controls own the detector's Java-authoritative settings."
    }));

    const primary = createTabs(
      root,
      `${instanceId}-primary`,
      [
        ["source", "Source"],
        ["trigger", "Trigger"],
        ["length", "Click Length"],
        ["delays", "Delays"],
        ["echoes", "Echoes"],
        ["noise", "Noise"]
      ],
      { label: "Click Detection Parameters" });
    const collectors = [];

    const bindNumber = (
      parent,
      label,
      value,
      path,
      validation = {},
      presentation = {}) => {
      const pointer = `/${path.join("/")}`;
      const attributes = {
        ...(validation.integer ? { step: 1 } : {}),
        ...(validation.min !== undefined
          ? { min: validation.min }
          : {}),
        ...(validation.max !== undefined
          ? { max: validation.max }
          : {})
      };
      const control = numberInput(value, pointer, attributes);
      parent.append(field(label, control, presentation));
      collectors.push(() => ({
        path,
        value: readNumber(control, label, validation)
      }));
      return control;
    };

    const bindCheckbox = (
      parent,
      label,
      value,
      path,
      help) => {
      const control = checkboxInput(value, `/${path.join("/")}`);
      parent.append(checkboxField(label, control, help));
      collectors.push(() => ({
        path,
        value: control.checked
      }));
      return control;
    };

    const bindSelect = (
      parent,
      label,
      value,
      path,
      definitions,
      help) => {
      const control = selectInput(
        value,
        `/${path.join("/")}`,
        definitions);
      parent.append(field(label, control, { help }));
      collectors.push(() => ({
        path,
        value: control.value
      }));
      return control;
    };

    const bindPair = (
      parent,
      title,
      value,
      path,
      labels,
      pairOptions = {}) => {
      const pairSection = section(title, pairOptions.help);
      const editor = createPairEditor(
        value,
        `/${path.join("/")}`,
        labels,
        pairOptions);
      pairSection.append(editor.element);
      parent.append(pairSection);
      collectors.push(() => ({
        path,
        value: editor.collect()
      }));
      return editor;
    };

    const sourcePanel = primary.panels.get("source");
    const sourceChannels = section(
      "Raw Data Source channels",
      "Choose the channels used by this Click Detector. Select the source " +
        "unit itself by connecting the module in the Data Model graph.");
    const sourceBitmap = createBitmapEditor(
      draft.detector.channelBitmap,
      "/detector/channelBitmap",
      "Source channel bitmap");
    sourceChannels.append(field(
      "Source channels",
      sourceBitmap.element,
      {
        help: "Unsigned 32-channel bitmap; at least one channel is required."
      }));
    collectors.push(() => ({
      path: ["detector", "channelBitmap"],
      value: sourceBitmap.collect()
    }));
    const grouping = bindSelect(
      sourceChannels,
      "Channel grouping",
      draft.detector.groupingType,
      ["detector", "groupingType"],
      [
        ["singles", "Individual channels"],
        ["all", "All selected channels together"],
        ["user", "User-defined groups"]
      ]);
    const groupEditor = createListEditor({
      title: "User channel groups",
      help: "Entry index is the channel number; value is its group number " +
        "(0–31). User grouping must cover every selected channel.",
      addLabel: "Add channel group",
      itemLabel: "Channel",
      emptyText: "No user channel groups.",
      values: draft.detector.channelGroups,
      maxItems: 32,
      createItem: () => 0,
      reportError,
      renderItem: (body, value) => {
        const control = numberInput(
          value,
          "/detector/channelGroups/*",
          { min: 0, max: 31, step: 1 });
        body.append(field("Group number", control));
        return {
          focus: () => control.focus(),
          collect: () => readNumber(
            control,
            "Channel group",
            { integer: true, min: 0, max: 31 })
        };
      }
    });
    const groupWrapper = createElement("div", {
      className: "click-settings-conditional"
    });
    groupWrapper.append(groupEditor.element);
    sourceChannels.append(groupWrapper);
    collectors.push(() => ({
      path: ["detector", "channelGroups"],
      value: groupEditor.collect()
    }));
    const updateGrouping = () => {
      const active = grouping.value === "user";
      groupEditor.setEnabled(active);
      groupWrapper.classList.toggle("is-disabled", !active);
    };
    grouping.addEventListener("change", updateGrouping);
    updateGrouping();
    sourcePanel.append(sourceChannels);

    const displaySection = section(
      "Click display defaults",
      "Canonical static Click display settings. Display panes created from " +
        "this detector can override their own layout later.");
    const displayBitmap = createBitmapEditor(
      draft.display.channelBitmap,
      "/display/channelBitmap",
      "Display channel bitmap",
      { allowZero: true });
    displaySection.append(field(
      "Displayed channels",
      displayBitmap.element,
      { help: "Zero hides all channels; otherwise use a 32-channel bitmap." }));
    collectors.push(() => ({
      path: ["display", "channelBitmap"],
      value: displayBitmap.collect()
    }));
    bindNumber(
      displaySection,
      "Time window",
      draft.display.timeWindowSeconds,
      ["display", "timeWindowSeconds"],
      { exclusiveMin: 0 },
      { unit: "s" });
    bindPair(
      displaySection,
      "Bearing axis",
      draft.display.bearingLimitsDegrees,
      ["display", "bearingLimitsDegrees"],
      ["Minimum bearing", "Maximum bearing"],
      { unit: "°" });
    bindPair(
      displaySection,
      "Amplitude axis",
      draft.display.amplitudeLimitsDb,
      ["display", "amplitudeLimitsDb"],
      ["Minimum amplitude", "Maximum amplitude"],
      { unit: "dB" });
    bindPair(
      displaySection,
      "Inter-click interval axis",
      draft.display.iciLimitsSeconds,
      ["display", "iciLimitsSeconds"],
      ["Minimum ICI", "Maximum ICI"],
      { unit: "s", exclusiveMin: 0 });
    bindCheckbox(
      displaySection,
      "Show echoes",
      draft.display.showEchoes,
      ["display", "showEchoes"]);
    sourcePanel.append(displaySection);

    const triggerPanel = primary.panels.get("trigger");
    const triggerSection = section(
      "Trigger",
      "These values mirror PAMGuard's signal/background trigger and " +
        "minimum-channel controls.");
    const triggerBitmap = createBitmapEditor(
      draft.detector.triggerBitmap,
      "/detector/triggerBitmap",
      "Trigger channel bitmap",
      { allowZero: true });
    triggerSection.append(field(
      "Trigger channels",
      triggerBitmap.element,
      {
        help: "Unsigned 32-channel bitmap. Zero disables triggering on all " +
          "channels."
      }));
    collectors.push(() => ({
      path: ["detector", "triggerBitmap"],
      value: triggerBitmap.collect()
    }));
    bindNumber(
      triggerSection,
      "Minimum triggered channels",
      draft.detector.minTriggerChannels,
      ["detector", "minTriggerChannels"],
      { integer: true, min: 1, max: 32 });
    bindNumber(
      triggerSection,
      "Threshold",
      draft.detector.thresholdDb,
      ["detector", "thresholdDb"],
      {},
      {
        unit: "dB",
        help: "Detection threshold: signal minus background."
      });
    bindNumber(
      triggerSection,
      "Long filter",
      draft.detector.longFilter,
      ["detector", "longFilter"],
      { min: 0, max: 1 },
      { help: "Background-noise update coefficient." });
    bindNumber(
      triggerSection,
      "Long filter 2",
      draft.detector.longFilter2,
      ["detector", "longFilter2"],
      { min: 0, max: 1 },
      {
        help: "Background-noise update coefficient while a click is active."
      });
    bindNumber(
      triggerSection,
      "Short filter",
      draft.detector.shortFilter,
      ["detector", "shortFilter"],
      { min: 0, max: 1 },
      { help: "Signal measurement coefficient." });
    bindCheckbox(
      triggerSection,
      "Publish trigger function",
      draft.detector.publishTriggerFunction,
      ["detector", "publishTriggerFunction"],
      "Expose the trigger-function output for downstream modules.");
    triggerPanel.append(triggerSection);

    const lengthPanel = primary.panels.get("length");
    const lengthSection = section(
      "Click Length",
      "Sample counts follow PAMGuard's pre/post capture, separation, and " +
        "maximum-click-length parameters.");
    bindNumber(
      lengthSection,
      "Minimum click separation",
      draft.detector.minSep,
      ["detector", "minSep"],
      { integer: true, min: 0 },
      { unit: "samples" });
    bindNumber(
      lengthSection,
      "Maximum click length",
      draft.detector.maxLength,
      ["detector", "maxLength"],
      { integer: true, min: 1 },
      { unit: "samples" });
    bindNumber(
      lengthSection,
      "Pre-samples",
      draft.detector.preSample,
      ["detector", "preSample"],
      { integer: true, min: 0 },
      { unit: "samples" });
    bindNumber(
      lengthSection,
      "Post-samples",
      draft.detector.postSample,
      ["detector", "postSample"],
      { integer: true, min: 0 },
      { unit: "samples" });
    lengthPanel.append(lengthSection);

    const featureSection = section(
      "Click feature measurements",
      "Frequency, duration, and bandwidth measurements used by the Click " +
        "Classifier. FFT length 0 retains PAMGuard's automatic/default path.");
    bindNumber(
      featureSection,
      "Feature FFT length",
      draft.features.fftLength,
      ["features", "fftLength"],
      { integer: true, min: 0 },
      { unit: "samples" });
    bindNumber(
      featureSection,
      "Length energy fraction",
      draft.features.lengthEnergyFraction,
      ["features", "lengthEnergyFraction"],
      { min: 0, max: 100 },
      { unit: "%" });
    bindNumber(
      featureSection,
      "Width energy fraction",
      draft.features.widthEnergyFraction,
      ["features", "widthEnergyFraction"],
      { min: 0, max: 100 },
      { unit: "%" });
    const energyBands = createListEditor({
      title: "Energy bands",
      help: "Each ordered, non-negative pair defines a measured energy band.",
      addLabel: "Add energy band",
      itemLabel: "Energy band",
      emptyText: "No energy bands.",
      values: draft.features.energyBandsHz,
      createItem: () => [0, 20000],
      reportError,
      renderItem: (body, value) => {
        const editor = createPairEditor(
          value,
          "/features/energyBandsHz/*",
          ["Lower frequency", "Upper frequency"],
          { min: 0, unit: "Hz" });
        body.append(editor.element);
        return editor;
      }
    });
    featureSection.append(energyBands.element);
    collectors.push(() => ({
      path: ["features", "energyBandsHz"],
      value: energyBands.collect()
    }));
    bindPair(
      featureSection,
      "Peak-frequency search",
      draft.features.peakFrequencySearchHz,
      ["features", "peakFrequencySearchHz"],
      ["Lower frequency", "Upper frequency"],
      { min: 0, unit: "Hz" });
    bindPair(
      featureSection,
      "Mean-frequency range",
      draft.features.meanFrequencyRangeHz,
      ["features", "meanFrequencyRangeHz"],
      ["Lower frequency", "Upper frequency"],
      { min: 0, unit: "Hz" });
    lengthPanel.append(featureSection);

    const delaysPanel = primary.panels.get("delays");
    delaysPanel.append(createElement("p", {
      className: "click-settings-section-help",
      text: "Default delay-measurement settings apply to all click types " +
        "unless a type-specific override is configured under Train " +
        "Localisation."
    }));
    const defaultDelay = createDelayEditor(
      draft.localisation.delayMeasurement,
      "/localisation/delayMeasurement");
    delaysPanel.append(defaultDelay.element);
    collectors.push(() => ({
      path: ["localisation", "delayMeasurement"],
      value: defaultDelay.collect()
    }));

    const echoesPanel = primary.panels.get("echoes");
    const echoSection = section(
      "Echo detection policy",
      "The maximum echo interval belongs to the port's simple Java-backed " +
        "echo settings.");
    const runEcho = bindCheckbox(
      echoSection,
      "Run echo detector online",
      draft.detector.echo.runOnline,
      ["detector", "echo", "runOnline"]);
    const discardEcho = bindCheckbox(
      echoSection,
      "Discard echoes",
      draft.detector.echo.discardEchoes,
      ["detector", "echo", "discardEchoes"]);
    bindNumber(
      echoSection,
      "Maximum echo interval",
      draft.detector.echo.maxIntervalSeconds,
      ["detector", "echo", "maxIntervalSeconds"],
      { min: 0 },
      { unit: "s" });
    const updateEcho = () => {
      discardEcho.disabled = !runEcho.checked;
      if (!runEcho.checked) discardEcho.checked = false;
    };
    runEcho.addEventListener("change", updateEcho);
    updateEcho();
    echoesPanel.append(echoSection);

    const noisePanel = primary.panels.get("noise");
    const noiseSection = section(
      "Noise Sampling",
      "Noise and detector-background sampling are independent canonical " +
        "streams. Legacy RainbowClick file-writing controls are intentionally " +
        "not included.");
    const sampleNoise = bindCheckbox(
      noiseSection,
      "Create sample noise measurements",
      draft.detector.sampleNoise,
      ["detector", "sampleNoise"]);
    const noiseInterval = bindNumber(
      noiseSection,
      "Noise sample interval",
      draft.detector.noiseSampleIntervalSeconds,
      ["detector", "noiseSampleIntervalSeconds"],
      { exclusiveMin: 0 },
      { unit: "s" });
    const storeBackground = bindCheckbox(
      noiseSection,
      "Store detector background",
      draft.detector.storeBackground,
      ["detector", "storeBackground"]);
    const backgroundInterval = bindNumber(
      noiseSection,
      "Background storage interval",
      draft.detector.backgroundIntervalMilliseconds,
      ["detector", "backgroundIntervalMilliseconds"],
      { integer: true, min: 0 },
      { unit: "ms" });
    const updateNoise = () => {
      noiseInterval.disabled = !sampleNoise.checked;
      backgroundInterval.disabled = !storeBackground.checked;
    };
    sampleNoise.addEventListener("change", updateNoise);
    storeBackground.addEventListener("change", updateNoise);
    updateNoise();
    noisePanel.append(noiseSection);

    const actionsHeader = createElement("div", {
      className: "click-settings-actions-header"
    });
    actionsHeader.append(
      createElement("h3", { text: "Additional Click Detector settings" }),
      createElement("p", {
        className: "click-settings-section-help",
        text: "These panes correspond to PAMGuard's separate filter, veto, " +
          "classifier, train-ID, and localisation settings actions."
      }));
    root.append(actionsHeader);
    const secondary = createTabs(
      root,
      `${instanceId}-secondary`,
      [
        ["pre-filter", "Digital pre-filter"],
        ["trigger-filter", "Digital trigger filter"],
        ["angle-vetoes", "Angle vetoes"],
        ["classification", "Classification"],
        ["train-id", "Train ID"],
        ["train-localisation", "Train Localisation"]
      ],
      {
        action: true,
        label: "Additional Click Detector settings"
      });

    const preFilter = createFilterEditor(
      draft.detector.preFilter,
      "/detector/preFilter",
      `${instanceId}-pre`);
    secondary.panels.get("pre-filter").append(preFilter.element);
    collectors.push(() => ({
      path: ["detector", "preFilter"],
      value: preFilter.collect()
    }));

    const triggerFilter = createFilterEditor(
      draft.detector.triggerFilter,
      "/detector/triggerFilter",
      `${instanceId}-trigger`);
    secondary.panels.get("trigger-filter").append(
      triggerFilter.element);
    collectors.push(() => ({
      path: ["detector", "triggerFilter"],
      value: triggerFilter.collect()
    }));

    const anglePanel = secondary.panels.get("angle-vetoes");
    anglePanel.append(createElement("p", {
      className: "click-settings-section-help",
      text: "Reject detections whose bearing falls inside a configured " +
        "angle interval for the selected channels."
    }));
    const angleVetoes = createListEditor({
      title: "Angle veto intervals",
      addLabel: "Add angle veto",
      itemLabel: "Angle veto",
      emptyText: "No angle vetoes.",
      values: draft.localisation.angleVetoes,
      createItem: () => ({
        channels: 3,
        startAngleDegrees: 0,
        endAngleDegrees: 180
      }),
      reportError,
      renderItem: (body, value) => {
        const channels = createBitmapEditor(
          value.channels,
          "/localisation/angleVetoes/*/channels",
          "Angle-veto channels",
          { allowZero: true });
        const start = numberInput(
          value.startAngleDegrees,
          "/localisation/angleVetoes/*/startAngleDegrees");
        const end = numberInput(
          value.endAngleDegrees,
          "/localisation/angleVetoes/*/endAngleDegrees");
        body.append(
          field("Channels", channels.element),
          field("Start angle", start, { unit: "°" }),
          field("End angle", end, { unit: "°" }));
        return {
          focus: () => channels.input.focus(),
          collect: () => ({
            channels: channels.collect(),
            startAngleDegrees: readNumber(start, "Veto start angle"),
            endAngleDegrees: readNumber(end, "Veto end angle")
          })
        };
      }
    });
    anglePanel.append(angleVetoes.element);
    collectors.push(() => ({
      path: ["localisation", "angleVetoes"],
      value: angleVetoes.collect()
    }));

    const classificationPanel =
      secondary.panels.get("classification");
    const classificationSection = section(
      "Click Classification",
      "Select PAMGuard's online classifier mode and policies. Classifier " +
        "definitions remain mode-specific ordered lists.");
    const runClassification = bindCheckbox(
      classificationSection,
      "Run classification online",
      draft.classification.runOnline,
      ["classification", "runOnline"]);
    const classificationControls = createElement("div", {
      className: "click-settings-conditional"
    });
    const classificationMode = bindSelect(
      classificationControls,
      "Classifier mode",
      draft.classification.mode,
      ["classification", "mode"],
      [
        ["none", "None"],
        ["basic", "Basic"],
        ["sweep", "Sweep"]
      ]);
    bindCheckbox(
      classificationControls,
      "Discard unclassified clicks",
      draft.classification.discardUnclassified,
      ["classification", "discardUnclassified"]);
    const checkAllClassifiers = bindCheckbox(
      classificationControls,
      "Check all classifiers",
      draft.classification.checkAllClassifiers,
      ["classification", "checkAllClassifiers"],
      "Sweep mode only: continue through every enabled set, retain the " +
        "first match as primary, and report all passing species codes.");
    classificationSection.append(classificationControls);
    const updateClassification = () => {
      setControlsEnabled(
        classificationControls,
        runClassification.checked);
      checkAllClassifiers.disabled =
        !runClassification.checked ||
        classificationMode.value !== "sweep";
      classificationControls.classList.toggle(
        "is-disabled",
        !runClassification.checked);
    };
    runClassification.addEventListener(
      "change",
      updateClassification);
    updateClassification();
    classificationPanel.append(classificationSection);

    const amplitudeOffsets = createListEditor({
      title: "Channel amplitude offsets",
      help: "Entry index is the channel number; value is its classifier " +
        "amplitude correction.",
      addLabel: "Add channel offset",
      itemLabel: "Channel",
      emptyText: "No channel amplitude offsets.",
      values: draft.classification.amplitudeDbOffsetByChannel,
      maxItems: 32,
      createItem: () => 0,
      reportError,
      renderItem: (body, value) => {
        const offset = numberInput(
          value,
          "/classification/amplitudeDbOffsetByChannel/*");
        body.append(field("Amplitude offset", offset, { unit: "dB" }));
        return {
          focus: () => offset.focus(),
          collect: () => readNumber(offset, "Channel amplitude offset")
        };
      }
    });
    classificationPanel.append(amplitudeOffsets.element);
    collectors.push(() => ({
      path: ["classification", "amplitudeDbOffsetByChannel"],
      value: amplitudeOffsets.collect()
    }));

    const basicTypes = createListEditor({
      title: "Basic classifier types",
      help: "Ordered ClickTypeParams definitions. Each PAMGuard criterion " +
        "has its own enable switch and structured controls.",
      addLabel: "Add basic type",
      itemLabel: "Basic classifier",
      emptyText: "No basic classifier definitions.",
      values: draft.classification.basicTypes,
      createItem: () => clone(DEFAULT_BASIC_CLASSIFIER_TYPE),
      reportError,
      renderItem: (body, value) => {
        const editor = createBasicClassifierEditor(
          value,
          "/classification/basicTypes/*");
        body.append(editor.element);
        return editor;
      }
    });
    const sweepTypes = createListEditor({
      title: "Sweep classifier types",
      help: "Ordered SweepClassifierSet definitions, including every " +
        "scientific test, shared measurement option, and common policy.",
      addLabel: "Add sweep type",
      itemLabel: "Sweep classifier",
      emptyText: "No sweep classifier definitions.",
      values: draft.classification.sweepTypes,
      createItem: () => clone(DEFAULT_SWEEP_CLASSIFIER_TYPE),
      reportError,
      renderItem: (body, value) => {
        const editor = createSweepClassifierEditor(
          value,
          "/classification/sweepTypes/*");
        body.append(editor.element);
        return editor;
      }
    });
    const updateClassifierLists = () => {
      basicTypes.element.hidden =
        classificationMode.value !== "basic";
      sweepTypes.element.hidden =
        classificationMode.value !== "sweep";
      updateClassification();
    };
    classificationMode.addEventListener(
      "change",
      updateClassifierLists);
    updateClassifierLists();
    classificationPanel.append(
      basicTypes.element,
      sweepTypes.element);
    collectors.push(
      () => ({
        path: ["classification", "basicTypes"],
        value: basicTypes.collect()
      }),
      () => ({
        path: ["classification", "sweepTypes"],
        value: sweepTypes.collect()
      }));

    const trainPanel = secondary.panels.get("train-id");
    const trainSection = section(
      "Click Train Identification",
      "This is PAMGuard's automatic simple click-train identification path. " +
        "It is separate from the manual tracked-event workflow shown under " +
        "Train Localisation.");
    const trainEnabled = bindCheckbox(
      trainSection,
      "Run automatic Click Train ID",
      draft.train.enabled,
      ["train", "enabled"]);
    const trainControls = createElement("div", {
      className: "click-settings-conditional"
    });
    bindNumber(
      trainControls,
      "Minimum ICI",
      draft.train.minIciSeconds,
      ["train", "minIciSeconds"],
      { exclusiveMin: 0 },
      { unit: "s" });
    bindNumber(
      trainControls,
      "Maximum ICI",
      draft.train.maxIciSeconds,
      ["train", "maxIciSeconds"],
      { exclusiveMin: 0 },
      { unit: "s" });
    bindNumber(
      trainControls,
      "Maximum ICI change ratio",
      draft.train.maxIciChange,
      ["train", "maxIciChange"],
      { min: 1 },
      { help: "Old/new or new/old ratio." });
    bindNumber(
      trainControls,
      "Maximum angle error",
      draft.train.okAngleErrorDegrees,
      ["train", "okAngleErrorDegrees"],
      { min: 0 },
      { unit: "°" });
    bindNumber(
      trainControls,
      "Initial perpendicular distance",
      draft.train.initialPerpendicularDistanceM,
      ["train", "initialPerpendicularDistanceM"],
      { min: 0 },
      { unit: "m" });
    bindNumber(
      trainControls,
      "Minimum clicks per train",
      draft.train.minClicks,
      ["train", "minClicks"],
      { integer: true, min: 1 });
    bindNumber(
      trainControls,
      "Minimum angle change for TMA",
      draft.train.minAngleChangeDegrees,
      ["train", "minAngleChangeDegrees"],
      { min: 0 },
      { unit: "°" });
    bindNumber(
      trainControls,
      "ICI update ratio",
      draft.train.iciUpdateRatio,
      ["train", "iciUpdateRatio"],
      { min: 0, max: 1 });
    bindNumber(
      trainControls,
      "Minimum interval between updates",
      draft.train.minUpdateGapSeconds,
      ["train", "minUpdateGapSeconds"],
      { min: 0 },
      { unit: "s" });
    trainSection.append(trainControls);
    const updateTrain = () => {
      setControlsEnabled(trainControls, trainEnabled.checked);
      trainControls.classList.toggle(
        "is-disabled",
        !trainEnabled.checked);
    };
    trainEnabled.addEventListener("change", updateTrain);
    updateTrain();
    trainPanel.append(trainSection);

    const localisationPanel =
      secondary.panels.get("train-localisation");
    localisationPanel.append(createElement("p", {
      className: "click-settings-section-help",
      text: "Type-specific delay overrides feed the implemented click " +
        "localiser runtime. Manual tracked-event membership is operated " +
        "from each Click display and is separate from automatic click trains."
    }));
    const typeSettings = createListEditor({
      title: "Type-specific delay settings",
      help: "Override the default Delays tab for a classifier click type.",
      addLabel: "Add click-type override",
      itemLabel: "Click-type override",
      emptyText: "No click-type delay overrides.",
      values: draft.localisation.typeSettings,
      createItem: () => ({
        clickType: 1,
        ...clone(DEFAULT_SETTINGS.localisation.delayMeasurement)
      }),
      reportError,
      renderItem: (body, value) => {
        const editor = createDelayEditor(
          value,
          "/localisation/typeSettings/*",
          { includeClickType: true });
        body.append(editor.element);
        return editor;
      }
    });
    localisationPanel.append(typeSettings.element);
    collectors.push(() => ({
      path: ["localisation", "typeSettings"],
      value: typeSettings.collect()
    }));

    const trackedNotice = createElement("aside", {
      className: "click-settings-unsupported",
      attributes: { role: "note" }
    });
    trackedNotice.append(
      createElement("strong", {
        text: "Manual tracked events / target motion"
      }),
      createElement("p", {
        text: "PAMGuard's TrackedClickLocaliser is a manual marking and " +
          "event-grouping workflow. Use a Click display to create, assign, " +
          "remove, and reassign events. The settings below are persisted " +
          "exactly as ClickLocParams. Numerical target-motion localisation " +
          "stays unavailable until the stream carries the moving array " +
          "origin and heading captured at every click."
      }));
    const trackedSection = section(
      "TrackedClickLocaliser / ClickLocParams");
    const algorithmDefinitions = [
      ["Least Squares", 0, "normal, mixed, viewer"],
      ["2D Simplex Optimization", 1, "normal, mixed, viewer"],
      ["3D Simplex Optimization", 2, "normal, mixed, viewer"],
      ["MCMC", 3, "viewer only"]
    ];
    const algorithmControls = [];
    for (const [label, index, modes] of algorithmDefinitions) {
      const control = checkboxInput(
        draft.localisation.trackedTrain.isSelected[index],
        `/localisation/trackedTrain/isSelected/${index}`);
      algorithmControls.push(control);
      trackedSection.append(checkboxField(
        label,
        control,
        `PAMGuard algorithm index ${index}; ${modes}.`));
    }
    const trackedControls = [
      ["Maximum range", "maxRangeM", "m"],
      ["Maximum height", "maxHeightM", "m"],
      ["Minimum height", "minHeightM", "m"],
      ["Maximum processing time", "maxTimeMilliseconds", "ms"],
      ["Maximum points", "maxPoints", ""]
    ];
    const trackedNumberControls = new Map();
    for (const [label, key, unit] of trackedControls) {
      const control = numberInput(
        draft.localisation.trackedTrain[key],
        `/localisation/trackedTrain/${key}`);
      trackedNumberControls.set(key, { label, control });
      trackedSection.append(field(label, control, { unit }));
    }
    const limitPoints = checkboxInput(
      draft.localisation.trackedTrain.limitPoints,
      "/localisation/trackedTrain/limitPoints");
    trackedSection.append(checkboxField(
      "Limit localisation points",
      limitPoints));
    trackedNotice.append(trackedSection);
    localisationPanel.append(trackedNotice);
    collectors.push(() => ({
      path: ["localisation", "trackedTrain"],
      value: {
        isSelected: algorithmControls.map((control) => control.checked),
        maxRangeM: readNumber(
          trackedNumberControls.get("maxRangeM").control,
          "Maximum range",
          { exclusiveMin: 0 }),
        maxHeightM: readNumber(
          trackedNumberControls.get("maxHeightM").control,
          "Maximum height"),
        minHeightM: readNumber(
          trackedNumberControls.get("minHeightM").control,
          "Minimum height"),
        maxTimeMilliseconds: readNumber(
          trackedNumberControls.get("maxTimeMilliseconds").control,
          "Maximum processing time",
          { integer: true, min: 0 }),
        limitPoints: limitPoints.checked,
        maxPoints: readNumber(
          trackedNumberControls.get("maxPoints").control,
          "Maximum points",
          { integer: true, min: 1 })
      }
    }));

    function collect() {
      const result = clone(DEFAULT_SETTINGS);
      for (const collector of collectors) {
        const collected = collector();
        setPath(result, collected.path, collected.value);
      }
      return validateCollectedSettings(result);
    }

    return {
      collect,
      focus: () => {
        primary.selectTab(0);
        sourceBitmap.input.focus();
      }
    };
  }

  globalThis.PamguardProjectClickSettings = Object.freeze({
    mountEditor
  });
})();
