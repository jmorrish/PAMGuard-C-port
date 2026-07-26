(() => {
  "use strict";

  const CHANNEL_COUNT = 32;
  const MAX_KERNEL_HISTORY = 10;
  const TYPE_ENERGY = "pamguard.ishmael-energy-sum";
  const TYPE_SGRAM = "pamguard.ishmael-sgram-corr";
  const TYPE_MATCH = "pamguard.ishmael-match-filter";
  const SUPPORTED_TYPES = Object.freeze([
    TYPE_ENERGY,
    TYPE_SGRAM,
    TYPE_MATCH
  ]);
  const GROUPING_TYPES = Object.freeze([
    ["singles", "No grouping"],
    ["all", "One group"],
    ["user", "User groups"]
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

  function createSvgElement(tag, attributes = {}) {
    const element = typeof document.createElementNS === "function"
      ? document.createElementNS(
          "http://www.w3.org/2000/svg",
          tag)
      : document.createElement(tag);
    for (const [name, value] of Object.entries(attributes)) {
      element.setAttribute(name, String(value));
    }
    return element;
  }

  function ensureStylesheet() {
    if (document.querySelector(
      "link[data-pamguard-project-ishmael-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-ishmael-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL(
          "project-ishmael-settings.css",
          capturedScriptSource).href
      : "/assets/project-ishmael-settings.css";
    document.head.append(link);
  }

  function requireType(typeId) {
    if (!SUPPORTED_TYPES.includes(typeId)) {
      throw new Error(`Unsupported Ishmael settings type ${typeId}`);
    }
    return typeId;
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

  function booleanValue(value, fallback) {
    if (value === undefined || value === null) return fallback;
    if (typeof value !== "boolean") {
      throw new Error("Boolean setting has an invalid value");
    }
    return value;
  }

  function portableBitmap(value, fallback = 0) {
    const result = Number(value);
    if (Number.isInteger(result) &&
        result >= 0 &&
        result <= 0xffffffff) {
      return result;
    }
    if (value === undefined || value === null) return fallback;
    throw new Error("Channel bitmap has an invalid value");
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

  function bitmapFor(channels) {
    return channels.reduce(
      (bitmap, channel) => bitmap + 2 ** channel,
      0);
  }

  function commonDefaults() {
    return {
      channelBitmap: 0,
      groupingType: "all",
      channelGroups: [],
      threshold: 1,
      minTimeSeconds: 0,
      maxTimeSeconds: 99999,
      refractoryTimeSeconds: 0
    };
  }

  function defaultSettings(typeId) {
    requireType(typeId);
    const common = commonDefaults();
    if (typeId === TYPE_ENERGY) {
      return {
        ...common,
        f0Hz: 0,
        f1Hz: 1000,
        ratioF0Hz: 1000,
        ratioF1Hz: 2000,
        useRatio: false,
        adaptiveThreshold: false,
        longFilter: 0.0001,
        useLog: false,
        spikeDecay: 100,
        outputSmoothing: false,
        shortFilter: 0.1
      };
    }
    if (typeId === TYPE_SGRAM) {
      return {
        ...common,
        segments: [],
        spreadHz: 100,
        useLog: false
      };
    }
    return {
      ...common,
      kernelFilenameList: [],
      kernelSamples: []
    };
  }

  function canonicalCommon(value, defaults) {
    const groupingType =
      value.groupingType === undefined
        ? defaults.groupingType
        : value.groupingType;
    if (!GROUPING_TYPES.some(
      ([candidate]) => candidate === groupingType)) {
      throw new Error(
        "Grouping type must be singles, all, or user");
    }
    const channelGroups = value.channelGroups === undefined
      ? clone(defaults.channelGroups)
      : Array.isArray(value.channelGroups)
        ? value.channelGroups.map((group, index) =>
            finiteNumber(
              group,
              `Channel ${index} group`,
              { integer: true, min: 0, max: 31 }))
        : null;
    if (!channelGroups || channelGroups.length > CHANNEL_COUNT) {
      throw new Error(
        "Channel groups must contain at most 32 assignments");
    }
    const result = {
      channelBitmap: portableBitmap(
        value.channelBitmap,
        defaults.channelBitmap),
      groupingType,
      channelGroups,
      threshold: finiteNumber(
        value.threshold ?? defaults.threshold,
        "Threshold",
        { min: 0 }),
      minTimeSeconds: finiteNumber(
        value.minTimeSeconds ?? defaults.minTimeSeconds,
        "Minimum time over threshold",
        { min: 0 }),
      maxTimeSeconds: finiteNumber(
        value.maxTimeSeconds ?? defaults.maxTimeSeconds,
        "Maximum time over threshold",
        { min: 0 }),
      refractoryTimeSeconds: finiteNumber(
        value.refractoryTimeSeconds ??
          defaults.refractoryTimeSeconds,
        "Minimum IDI",
        { min: 0 })
    };
    if (result.maxTimeSeconds !== 0 &&
        result.maxTimeSeconds < result.minTimeSeconds) {
      throw new Error(
        "Maximum time must be zero (disabled) or at least the minimum time");
    }
    validateUserGrouping(result);
    return result;
  }

  function canonicalSettings(typeId, value = {}) {
    requireType(typeId);
    const defaults = defaultSettings(typeId);
    const common = canonicalCommon(value, defaults);
    if (typeId === TYPE_ENERGY) {
      const result = {
        ...common,
        f0Hz: finiteNumber(
          value.f0Hz ?? defaults.f0Hz,
          "Lower frequency bound",
          { min: 0 }),
        f1Hz: finiteNumber(
          value.f1Hz ?? defaults.f1Hz,
          "Upper frequency bound",
          { min: 0 }),
        ratioF0Hz: finiteNumber(
          value.ratioF0Hz ?? defaults.ratioF0Hz,
          "Lower ratio bound",
          { min: 0 }),
        ratioF1Hz: finiteNumber(
          value.ratioF1Hz ?? defaults.ratioF1Hz,
          "Upper ratio bound",
          { min: 0 }),
        useRatio: booleanValue(
          value.useRatio,
          defaults.useRatio),
        adaptiveThreshold: booleanValue(
          value.adaptiveThreshold,
          defaults.adaptiveThreshold),
        longFilter: finiteNumber(
          value.longFilter ?? defaults.longFilter,
          "Long filter",
          { min: 0 }),
        useLog: booleanValue(
          value.useLog,
          defaults.useLog),
        spikeDecay: finiteNumber(
          value.spikeDecay ?? defaults.spikeDecay,
          "Spike threshold",
          { min: 1 }),
        outputSmoothing: booleanValue(
          value.outputSmoothing,
          defaults.outputSmoothing),
        shortFilter: finiteNumber(
          value.shortFilter ?? defaults.shortFilter,
          "Short filter",
          { min: 0 })
      };
      if (result.f0Hz > result.f1Hz) {
        throw new Error(
          "Lower frequency bound must not exceed the upper frequency bound");
      }
      if (result.ratioF0Hz > result.ratioF1Hz) {
        throw new Error(
          "Lower ratio bound must not exceed the upper ratio bound");
      }
      if (result.useRatio && result.adaptiveThreshold) {
        throw new Error(
          "Energy ratio and adaptive threshold are mutually exclusive");
      }
      return result;
    }
    if (typeId === TYPE_SGRAM) {
      if (!Array.isArray(value.segments ?? defaults.segments)) {
        throw new Error(
          "Spectrogram segments must be an array");
      }
      const segments =
        (value.segments ?? defaults.segments)
          .map((segment, index) => {
            if (!Array.isArray(segment) || segment.length !== 4) {
              throw new Error(
                `Segment ${index + 1} must contain t0, f0, t1, and f1`);
            }
            const decoded = segment.map((field, fieldIndex) =>
              finiteNumber(
                field,
                `Segment ${index + 1} ${[
                  "t0", "f0", "t1", "f1"
                ][fieldIndex]}`,
                { min: 0 }));
            if (decoded[0] > decoded[2]) {
              throw new Error(
                `Segment ${index + 1} start time must not exceed its end time`);
            }
            return decoded;
          });
      return {
        ...common,
        segments,
        spreadHz: finiteNumber(
          value.spreadHz ?? defaults.spreadHz,
          "Kernel width",
          { exclusiveMin: 0 }),
        useLog: booleanValue(
          value.useLog,
          defaults.useLog)
      };
    }
    const namesValue =
      value.kernelFilenameList ?? defaults.kernelFilenameList;
    if (!Array.isArray(namesValue) ||
        namesValue.length > MAX_KERNEL_HISTORY) {
      throw new Error(
        "Kernel filename history must contain at most ten entries");
    }
    const names = namesValue.map((name, index) => {
      const portable = String(name);
      if (!portable ||
          portable === "." ||
          portable === ".." ||
          /[\\/:]/.test(portable)) {
        throw new Error(
          `Kernel filename ${index + 1} must be a portable basename`);
      }
      return portable;
    });
    if (new Set(names).size !== names.length) {
      throw new Error(
        "Kernel filename history must not contain duplicates");
    }
    const samplesValue =
      value.kernelSamples ?? defaults.kernelSamples;
    if (!Array.isArray(samplesValue)) {
      throw new Error("Kernel samples must be an array");
    }
    return {
      ...common,
      kernelFilenameList: names,
      kernelSamples: samplesValue.map((sample, index) =>
        finiteNumber(
          sample,
          `Kernel sample ${index + 1}`))
    };
  }

  function validateUserGrouping(settings) {
    if (settings.groupingType !== "user") return;
    for (const channel of channelsIn(settings.channelBitmap)) {
      if (channel >= settings.channelGroups.length ||
          !Number.isInteger(settings.channelGroups[channel])) {
        throw new Error(
          `Assign selected channel / sequence ${channel} to a group`);
      }
    }
  }

  function validateReady(typeId, settings) {
    const canonical = canonicalSettings(typeId, settings);
    if ((typeId === TYPE_ENERGY ||
        typeId === TYPE_SGRAM) &&
        canonical.channelBitmap === 0) {
      throw new Error(
        "Select at least one FFT channel or sequence");
    }
    if (typeId === TYPE_SGRAM &&
        canonical.segments.length === 0) {
      throw new Error(
        "Add at least one time-frequency segment");
    }
    if (typeId === TYPE_MATCH &&
        (canonical.kernelFilenameList.length === 0 ||
          canonical.kernelSamples.length === 0)) {
      throw new Error(
        "Select a kernel sound file with first-channel samples");
    }
    return canonical;
  }

  function pointerControl(tag, pointer, options = {}) {
    return createElement(tag, {
      type: options.type,
      className: options.className,
      attributes: {
        "data-setting-pointer": pointer,
        ...options.attributes
      }
    });
  }

  function numberControl(value, pointer, attributes = {}) {
    const control = pointerControl("input", pointer, {
      type: "number",
      attributes: {
        step: "any",
        ...attributes
      }
    });
    control.value = String(value);
    return control;
  }

  function checkControl(value, pointer) {
    const control = pointerControl("input", pointer, {
      type: "checkbox"
    });
    control.checked = value;
    return control;
  }

  function field(label, control, options = {}) {
    const row = createElement("label", {
      className: "ishmael-settings-field"
    });
    row.append(
      createElement("span", {
        className: "ishmael-settings-label",
        text: label
      }),
      control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "ishmael-settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", {
        className: "ishmael-settings-help",
        text: options.help
      }));
    }
    return row;
  }

  function checkboxField(label, control, help = "") {
    const wrapper = createElement("label", {
      className: "ishmael-settings-check"
    });
    wrapper.append(
      control,
      createElement("span", { text: label }));
    if (help) {
      wrapper.append(createElement("small", {
        className: "ishmael-settings-help",
        text: help
      }));
    }
    return wrapper;
  }

  function actionButton(label, action) {
    return createElement("button", {
      type: "button",
      className: "secondary",
      text: label,
      attributes: {
        "data-ishmael-action": action
      }
    });
  }

  function mountGroupedSource(options) {
    const {
      container,
      typeId,
      settings,
      sourceSelect,
      getAvailableChannelBitmap,
      getSourceSampleRate
    } = options;
    const sourceKind =
      typeId === TYPE_MATCH ? "rawAudio" : "fft";
    const sourceDescription =
      sourceKind === "fft"
        ? "FFT source channels / sequences"
        : "Raw-audio source channels";
    let selectedBitmap = portableBitmap(settings.channelBitmap);
    const assignments = Array.from(
      { length: CHANNEL_COUNT },
      (_, channel) => {
        if (Number.isInteger(settings.channelGroups[channel])) {
          return settings.channelGroups[channel];
        }
        if (settings.groupingType === "all") return 0;
        return channel;
      });
    const section = createElement("fieldset", {
      className: "ishmael-settings-section ishmael-settings-source",
      attributes: {
        "data-ishmael-section": "source",
        "data-ishmael-source-kind": sourceKind
      }
    });
    section.append(createElement("legend", {
      text: sourceDescription
    }));
    const sourceSummary = createElement("p", {
      className: "section-help",
      attributes: {
        "data-ishmael-source-summary": ""
      }
    });
    const grouping = createElement("div", {
      className: "ishmael-settings-grouping",
      attributes: {
        role: "radiogroup",
        "aria-label": "Auto Grouping"
      }
    });
    grouping.append(createElement("strong", {
      text: "Auto Grouping"
    }));
    const groupingControls = new Map();
    for (const [value, label] of GROUPING_TYPES) {
      const control = pointerControl(
        "input",
        "/groupingType",
        {
          type: "radio",
          attributes: {
            name: `ishmael-${typeId}-grouping`,
            value
          }
        });
      control.value = value;
      control.checked = settings.groupingType === value;
      grouping.append(checkboxField(label, control));
      groupingControls.set(value, control);
    }
    const actions = createElement("div", {
      className: "ishmael-settings-actions"
    });
    const all = actionButton("All available", "all-channels");
    const none = actionButton("None", "no-channels");
    actions.append(all, none);
    const channelHost = createElement("div", {
      className: "ishmael-settings-channel-host"
    });
    section.append(
      sourceSummary,
      grouping,
      actions,
      channelHost);
    container.append(section);

    let channelControls = [];
    let groupControls = [];
    let availableBitmap = 0;
    const currentGrouping = () =>
      GROUPING_TYPES.find(([value]) =>
        groupingControls.get(value).checked)?.[0] || "all";
    const sync = () => {
      if (channelControls.length) {
        selectedBitmap = bitmapFor(
          channelControls
            .filter((item) => item.control.checked)
            .map((item) => item.channel));
      }
      for (const item of groupControls) {
        assignments[item.channel] = finiteNumber(
          item.control.value,
          `Channel ${item.channel} group`,
          { integer: true, min: 0, max: 31 });
      }
    };
    const updateGroupAvailability = () => {
      const user = currentGrouping() === "user";
      for (const item of groupControls) {
        const channel = channelControls.find(
          (candidate) => candidate.channel === item.channel);
        item.control.disabled =
          !user || !channel?.control.checked;
      }
    };
    const selectGrouping = (type) => {
      sync();
      if (type === "all") {
        assignments.fill(0);
      }
      else if (type === "singles") {
        assignments.forEach((_, channel) => {
          assignments[channel] = channel;
        });
      }
      groupControls.forEach((item) => {
        item.control.value = String(assignments[item.channel]);
      });
      updateGroupAvailability();
    };
    const render = () => {
      sync();
      availableBitmap = portableBitmap(
        getAvailableChannelBitmap());
      const available = channelsIn(availableBitmap);
      selectedBitmap = bitmapFor(
        channelsIn(selectedBitmap)
          .filter((channel) => available.includes(channel)));
      channelHost.replaceChildren();
      channelControls = [];
      groupControls = [];
      const sampleRate =
        Number(getSourceSampleRate()) || 0;
      sourceSummary.textContent = available.length
        ? `${sourceKind === "fft" ? "FFT" : "Raw-audio"} binding exposes ` +
          `${available.length} channel${available.length === 1 ? "" : "s"}` +
          (sampleRate > 0
            ? ` at ${sampleRate.toLocaleString()} Hz.`
            : ".")
        : `Choose a compatible ${
          sourceKind === "fft" ? "FFT" : "raw-audio"} binding above.`;
      if (!available.length) {
        channelHost.append(createElement("p", {
          className: "section-help",
          text: "No channels are available from the current binding."
        }));
      }
      else {
        const table = createElement("table", {
          className: "ishmael-settings-channel-table"
        });
        const head = createElement("thead");
        const heading = createElement("tr");
        heading.append(
          createElement("th", {
            text: sourceKind === "fft"
              ? "Channel / sequence"
              : "Channel"
          }),
          createElement("th", { text: "Group" }));
        head.append(heading);
        const body = createElement("tbody");
        for (const channel of available) {
          const row = createElement("tr");
          const selected = pointerControl(
            "input",
            `/channelBitmap/${channel}`,
            { type: "checkbox" });
          selected.checked =
            channelsIn(selectedBitmap).includes(channel);
          const selectedCell = createElement("td");
          selectedCell.append(checkboxField(
            sourceKind === "fft"
              ? `Channel / sequence ${channel}`
              : `Channel ${channel}`,
            selected));
          const groupCell = createElement("td");
          const group = pointerControl(
            "select",
            `/channelGroups/${channel}`);
          for (let value = 0; value < CHANNEL_COUNT; value++) {
            const option = createElement("option", {
              text: value,
              attributes: { value }
            });
            option.selected = assignments[channel] === value;
            group.append(option);
          }
          group.value = String(assignments[channel]);
          groupCell.append(group);
          row.append(selectedCell, groupCell);
          body.append(row);
          channelControls.push({ channel, control: selected });
          groupControls.push({ channel, control: group });
          selected.addEventListener(
            "change",
            updateGroupAvailability);
        }
        table.append(head, body);
        channelHost.append(table);
      }
      all.disabled = available.length === 0;
      none.disabled = available.length === 0;
      updateGroupAvailability();
    };
    groupingControls.forEach((control, type) => {
      control.addEventListener("change", () => {
        if (control.checked) selectGrouping(type);
      });
    });
    all.addEventListener("click", () => {
      channelControls.forEach((item) => {
        item.control.checked = true;
      });
      updateGroupAvailability();
    });
    none.addEventListener("click", () => {
      channelControls.forEach((item) => {
        item.control.checked = false;
      });
      updateGroupAvailability();
    });
    const sourceChanged = () => render();
    sourceSelect?.addEventListener("change", sourceChanged);
    render();

    return {
      collect() {
        sync();
        const groupingType = currentGrouping();
        let channelGroups = [];
        if (groupingType === "user") {
          const selected = channelsIn(selectedBitmap);
          const last = selected.length ? Math.max(...selected) : -1;
          channelGroups = assignments.slice(0, last + 1);
        }
        const result = {
          channelBitmap: selectedBitmap,
          groupingType,
          channelGroups
        };
        validateUserGrouping(result);
        return result;
      },
      cleanup() {
        sourceSelect?.removeEventListener?.(
          "change",
          sourceChanged);
      },
      refresh: render
    };
  }

  function mountPeakPicker(container, settings) {
    const section = createElement("fieldset", {
      className: "ishmael-settings-section ishmael-settings-peak",
      attributes: {
        "data-ishmael-section": "peak"
      }
    });
    section.append(createElement("legend", {
      text: "Peak Detection"
    }));
    const threshold = numberControl(
      settings.threshold,
      "/threshold",
      { min: 0 });
    const minTime = numberControl(
      settings.minTimeSeconds,
      "/minTimeSeconds",
      { min: 0 });
    const maxTime = numberControl(
      settings.maxTimeSeconds,
      "/maxTimeSeconds",
      { min: 0 });
    const refractory = numberControl(
      settings.refractoryTimeSeconds,
      "/refractoryTimeSeconds",
      { min: 0 });
    section.append(
      field("Threshold", threshold),
      field("Min time over threshold", minTime, { unit: "s" }),
      field(
        "Max time over threshold",
        maxTime,
        {
          unit: "s",
          help: "Set to zero to disable the maximum."
        }),
      field("Min IDI", refractory, { unit: "s" }));
    container.append(section);

    const collect = () => {
      const result = {
        threshold: finiteNumber(
          threshold.value,
          "Threshold",
          { min: 0 }),
        minTimeSeconds: finiteNumber(
          minTime.value,
          "Minimum time over threshold",
          { min: 0 }),
        maxTimeSeconds: finiteNumber(
          maxTime.value,
          "Maximum time over threshold",
          { min: 0 }),
        refractoryTimeSeconds: finiteNumber(
          refractory.value,
          "Minimum IDI",
          { min: 0 })
      };
      if (result.maxTimeSeconds !== 0 &&
          result.maxTimeSeconds < result.minTimeSeconds) {
        throw new Error(
          "Maximum time must be zero (disabled) or at least the minimum time");
      }
      return result;
    };
    return { collect };
  }

  function mountEnergyPane(container, settings) {
    const section = createElement("fieldset", {
      className: "ishmael-settings-section ishmael-settings-energy",
      attributes: {
        "data-ishmael-section": "detector",
        "data-ishmael-detector": "energy-sum"
      }
    });
    section.append(createElement("legend", {
      text: "Energy Sum"
    }));
    const f0 = numberControl(
      settings.f0Hz,
      "/f0Hz",
      { min: 0 });
    const f1 = numberControl(
      settings.f1Hz,
      "/f1Hz",
      { min: 0 });
    const useRatio = checkControl(
      settings.useRatio,
      "/useRatio");
    const ratioF0 = numberControl(
      settings.ratioF0Hz,
      "/ratioF0Hz",
      { min: 0 });
    const ratioF1 = numberControl(
      settings.ratioF1Hz,
      "/ratioF1Hz",
      { min: 0 });
    const adaptive = checkControl(
      settings.adaptiveThreshold,
      "/adaptiveThreshold");
    const longFilter = numberControl(
      settings.longFilter,
      "/longFilter",
      { min: 0 });
    const spikeDecay = numberControl(
      settings.spikeDecay,
      "/spikeDecay",
      { min: 1 });
    const smoothing = checkControl(
      settings.outputSmoothing,
      "/outputSmoothing");
    const shortFilter = numberControl(
      settings.shortFilter,
      "/shortFilter",
      { min: 0 });
    const useLog = checkControl(
      settings.useLog,
      "/useLog");
    section.append(
      field("Lower Frequency Bound", f0, { unit: "Hz" }),
      field("Upper Frequency Bound", f1, { unit: "Hz" }),
      checkboxField("Use Energy Ratio", useRatio),
      field("Lower Ratio Bound", ratioF0, { unit: "Hz" }),
      field("Upper Ratio Bound", ratioF1, { unit: "Hz" }),
      checkboxField("Use Adaptive Threshold", adaptive),
      field("Long filter", longFilter),
      field("Spike Threshold", spikeDecay),
      checkboxField("Use Detector Smoothing", smoothing),
      field("Short filter", shortFilter),
      checkboxField("Use log scale", useLog));
    container.append(section);

    const update = (changed = null) => {
      if (changed === useRatio && useRatio.checked) {
        adaptive.checked = false;
      }
      if (changed === adaptive && adaptive.checked) {
        useRatio.checked = false;
      }
      ratioF0.disabled = !useRatio.checked;
      ratioF1.disabled = !useRatio.checked;
      longFilter.disabled = !adaptive.checked;
      spikeDecay.disabled = !adaptive.checked;
      shortFilter.disabled = !smoothing.checked;
    };
    useRatio.addEventListener("change", () => update(useRatio));
    adaptive.addEventListener("change", () => update(adaptive));
    smoothing.addEventListener("change", () => update(smoothing));
    update();

    return {
      collect() {
        return {
          f0Hz: finiteNumber(
            f0.value,
            "Lower frequency bound",
            { min: 0 }),
          f1Hz: finiteNumber(
            f1.value,
            "Upper frequency bound",
            { min: 0 }),
          ratioF0Hz: finiteNumber(
            ratioF0.value,
            "Lower ratio bound",
            { min: 0 }),
          ratioF1Hz: finiteNumber(
            ratioF1.value,
            "Upper ratio bound",
            { min: 0 }),
          useRatio: useRatio.checked,
          adaptiveThreshold: adaptive.checked,
          longFilter: finiteNumber(
            longFilter.value,
            "Long filter",
            { min: 0 }),
          useLog: useLog.checked,
          spikeDecay: finiteNumber(
            spikeDecay.value,
            "Spike threshold",
            { min: 1 }),
          outputSmoothing: smoothing.checked,
          shortFilter: finiteNumber(
            shortFilter.value,
            "Short filter",
            { min: 0 })
        };
      }
    };
  }

  function mountSgramPane(container, settings, reportError) {
    const section = createElement("fieldset", {
      className: "ishmael-settings-section ishmael-settings-sgram",
      attributes: {
        "data-ishmael-section": "detector",
        "data-ishmael-detector": "spectrogram-correlation"
      }
    });
    section.append(createElement("legend", {
      text: "Spectrogram Correlation"
    }));
    section.append(createElement("p", {
      className: "section-help",
      text: "Segments (t0, f0, t1, f1)"
    }));
    const table = createElement("table", {
      className: "ishmael-settings-segment-table"
    });
    const head = createElement("thead");
    const heading = createElement("tr");
    for (const label of [
      "t0 (s)", "f0 (Hz)", "t1 (s)", "f1 (Hz)", ""
    ]) {
      heading.append(createElement("th", { text: label }));
    }
    head.append(heading);
    const body = createElement("tbody", {
      attributes: {
        "data-ishmael-segment-rows": ""
      }
    });
    table.append(head, body);
    const actions = createElement("div", {
      className: "ishmael-settings-actions"
    });
    const add = actionButton("Add Row", "add-segment");
    actions.append(add);
    const spread = numberControl(
      settings.spreadHz,
      "/spreadHz",
      { min: Number.MIN_VALUE });
    const useLog = checkControl(
      settings.useLog,
      "/useLog");
    const preview = createElement("div", {
      className: "ishmael-settings-contour",
      attributes: {
        "data-ishmael-contour-preview": "",
        role: "img",
        "aria-label": "Time-Frequency Contour"
      }
    });
    section.append(
      table,
      actions,
      field("Kernel Width", spread, { unit: "Hz" }),
      checkboxField("Use log-scaled spectrogram", useLog),
      createElement("h4", { text: "Time-Frequency Contour" }),
      preview);
    container.append(section);

    let segments = clone(settings.segments);
    let rows = [];
    const syncRows = () => {
      segments = rows.map((row, index) => [
        finiteNumber(
          row.controls[0].value,
          `Segment ${index + 1} t0`,
          { min: 0 }),
        finiteNumber(
          row.controls[1].value,
          `Segment ${index + 1} f0`,
          { min: 0 }),
        finiteNumber(
          row.controls[2].value,
          `Segment ${index + 1} t1`,
          { min: 0 }),
        finiteNumber(
          row.controls[3].value,
          `Segment ${index + 1} f1`,
          { min: 0 })
      ]);
    };
    const updatePreview = () => {
      preview.replaceChildren();
      if (!rows.length) {
        preview.append(createElement("span", {
          className: "section-help",
          text: "Add a segment to define the contour."
        }));
        return;
      }
      let decoded;
      try {
        syncRows();
        decoded = canonicalSettings(TYPE_SGRAM, {
          ...defaultSettings(TYPE_SGRAM),
          segments
        }).segments;
      }
      catch (error) {
        preview.append(createElement("span", {
          className: "section-help",
          text: error.message
        }));
        return;
      }
      const maxTime = Math.max(
        1,
        ...decoded.flatMap((segment) =>
          [segment[0], segment[2]]));
      const maxFrequency = Math.max(
        1,
        ...decoded.flatMap((segment) =>
          [segment[1], segment[3]]));
      const plot = createSvgElement("svg", {
        viewBox: "0 0 100 100",
        preserveAspectRatio: "none",
        "aria-hidden": "true"
      });
      decoded.forEach((segment, index) => {
        const x0 = 100 * segment[0] / maxTime;
        const x1 = 100 * segment[2] / maxTime;
        const y0 =
          100 - 100 * segment[1] / maxFrequency;
        const y1 =
          100 - 100 * segment[3] / maxFrequency;
        plot.append(
          createSvgElement("line", {
            class: "ishmael-settings-contour-line",
            "data-segment-index": index,
            x1: x0,
            y1: y0,
            x2: x1,
            y2: y1
          }),
          createSvgElement("circle", {
            class: "ishmael-settings-contour-point",
            cx: x0,
            cy: y0,
            r: 1.6
          }),
          createSvgElement("circle", {
            class: "ishmael-settings-contour-point",
            cx: x1,
            cy: y1,
            r: 1.6
          }));
      });
      preview.append(plot);
    };
    const renderRows = () => {
      body.replaceChildren();
      rows = [];
      segments.forEach((segment, index) => {
        const row = createElement("tr", {
          attributes: {
            "data-ishmael-segment-row": index
          }
        });
        const controls = segment.map((value, fieldIndex) =>
          numberControl(
            value,
            `/segments/${index}/${fieldIndex}`,
            { min: 0 }));
        controls.forEach((control) => {
          const cell = createElement("td");
          cell.append(control);
          row.append(cell);
          control.addEventListener("change", updatePreview);
        });
        const removeCell = createElement("td");
        const remove = actionButton(
          "Remove",
          `remove-segment-${index}`);
        remove.addEventListener("click", () => {
          try {
            syncRows();
            segments.splice(index, 1);
            renderRows();
          }
          catch (error) {
            reportError(error);
          }
        });
        removeCell.append(remove);
        row.append(removeCell);
        body.append(row);
        rows.push({ controls });
      });
      updatePreview();
    };
    add.addEventListener("click", () => {
      try {
        syncRows();
        segments.push([0, 0, 0, 0]);
        renderRows();
      }
      catch (error) {
        reportError(error);
      }
    });
    renderRows();

    return {
      collect() {
        syncRows();
        return {
          segments: clone(segments),
          spreadHz: finiteNumber(
            spread.value,
            "Kernel width",
            { exclusiveMin: 0 }),
          useLog: useLog.checked
        };
      }
    };
  }

  function readAscii(view, offset, length) {
    let result = "";
    for (let index = 0; index < length; index++) {
      result += String.fromCharCode(
        view.getUint8(offset + index));
    }
    return result;
  }

  function decodeWavKernel(view) {
    if (view.byteLength < 12 ||
        readAscii(view, 0, 4) !== "RIFF" ||
        readAscii(view, 8, 4) !== "WAVE") {
      return null;
    }
    let format = null;
    let dataOffset = -1;
    let dataLength = 0;
    for (let offset = 12; offset + 8 <= view.byteLength;) {
      const id = readAscii(view, offset, 4);
      const length = view.getUint32(offset + 4, true);
      const payload = offset + 8;
      if (payload + length > view.byteLength) {
        throw new Error("Kernel WAV contains a truncated chunk");
      }
      if (id === "fmt ") {
        if (length < 16) {
          throw new Error("Kernel WAV format chunk is too short");
        }
        let code = view.getUint16(payload, true);
        if (code === 0xfffe && length >= 40) {
          code = view.getUint16(payload + 24, true);
        }
        format = {
          code,
          channels: view.getUint16(payload + 2, true),
          blockAlign: view.getUint16(payload + 12, true),
          bits: view.getUint16(payload + 14, true)
        };
      }
      else if (id === "data") {
        dataOffset = payload;
        dataLength = length;
      }
      offset = payload + length + (length % 2);
    }
    if (!format || dataOffset < 0) {
      throw new Error("Kernel WAV requires format and data chunks");
    }
    if (format.channels < 1 ||
        format.blockAlign < 1 ||
        format.bits < 8) {
      throw new Error("Kernel WAV has invalid channel geometry");
    }
    const bytesPerSample = Math.ceil(format.bits / 8);
    if (format.blockAlign < bytesPerSample * format.channels) {
      throw new Error("Kernel WAV block alignment is invalid");
    }
    const frameCount = Math.floor(
      dataLength / format.blockAlign);
    const samples = new Array(frameCount);
    for (let frame = 0; frame < frameCount; frame++) {
      const offset = dataOffset + frame * format.blockAlign;
      if (format.code === 1 && format.bits === 8) {
        samples[frame] =
          (view.getUint8(offset) - 128) / 128;
      }
      else if (format.code === 1 && format.bits === 16) {
        samples[frame] =
          view.getInt16(offset, true) / 32768;
      }
      else if (format.code === 1 && format.bits === 24) {
        let value =
          view.getUint8(offset) |
          (view.getUint8(offset + 1) << 8) |
          (view.getUint8(offset + 2) << 16);
        if (value & 0x800000) value -= 0x1000000;
        samples[frame] = value / 8388608;
      }
      else if (format.code === 1 && format.bits === 32) {
        samples[frame] =
          view.getInt32(offset, true) / 2147483648;
      }
      else if (format.code === 3 && format.bits === 32) {
        samples[frame] = view.getFloat32(offset, true);
      }
      else if (format.code === 3 && format.bits === 64) {
        samples[frame] = view.getFloat64(offset, true);
      }
      else {
        throw new Error(
          `Kernel WAV encoding ${format.code}/${format.bits}-bit is unsupported`);
      }
      if (!Number.isFinite(samples[frame])) {
        throw new Error(
          `Kernel WAV sample ${frame + 1} is not finite`);
      }
    }
    return samples;
  }

  function decodeAiffKernel(view) {
    if (view.byteLength < 12 ||
        readAscii(view, 0, 4) !== "FORM") {
      return null;
    }
    const formType = readAscii(view, 8, 4);
    if (formType !== "AIFF" && formType !== "AIFC") {
      return null;
    }
    let channels = 0;
    let frameCount = 0;
    let bits = 0;
    let encoding = "NONE";
    let dataOffset = -1;
    let dataLength = 0;
    for (let offset = 12; offset + 8 <= view.byteLength;) {
      const id = readAscii(view, offset, 4);
      const length = view.getUint32(offset + 4, false);
      const payload = offset + 8;
      if (payload + length > view.byteLength) {
        throw new Error("Kernel AIFF contains a truncated chunk");
      }
      if (id === "COMM") {
        if (length < 18) {
          throw new Error("Kernel AIFF common chunk is too short");
        }
        channels = view.getUint16(payload, false);
        frameCount = view.getUint32(payload + 2, false);
        bits = view.getUint16(payload + 6, false);
        if (formType === "AIFC" && length >= 22) {
          encoding = readAscii(view, payload + 18, 4);
        }
      }
      else if (id === "SSND") {
        if (length < 8) {
          throw new Error("Kernel AIFF sound chunk is too short");
        }
        const soundOffset = view.getUint32(payload, false);
        dataOffset = payload + 8 + soundOffset;
        dataLength = length - 8 - soundOffset;
      }
      offset = payload + length + (length % 2);
    }
    if (channels < 1 || frameCount < 1 ||
        bits < 8 || dataOffset < 0 ||
        dataOffset > view.byteLength) {
      throw new Error(
        "Kernel AIFF requires valid common and sound chunks");
    }
    const bytesPerSample = Math.ceil(bits / 8);
    const blockAlign = bytesPerSample * channels;
    const availableFrames = Math.min(
      frameCount,
      Math.floor(dataLength / blockAlign));
    const littleEndian = encoding === "sowt";
    const floating = encoding === "fl32" ||
      encoding === "FL32";
    if (!["NONE", "twos", "sowt", "fl32", "FL32"]
      .includes(encoding)) {
      throw new Error(
        `Kernel AIFF encoding ${encoding} is unsupported`);
    }
    const samples = new Array(availableFrames);
    for (let frame = 0; frame < availableFrames; frame++) {
      const offset = dataOffset + frame * blockAlign;
      if (floating && bits === 32) {
        samples[frame] =
          view.getFloat32(offset, littleEndian);
      }
      else if (!floating && bits === 8) {
        samples[frame] = view.getInt8(offset) / 128;
      }
      else if (!floating && bits === 16) {
        samples[frame] =
          view.getInt16(offset, littleEndian) / 32768;
      }
      else if (!floating && bits === 24) {
        let value;
        if (littleEndian) {
          value =
            view.getUint8(offset) |
            (view.getUint8(offset + 1) << 8) |
            (view.getUint8(offset + 2) << 16);
        }
        else {
          value =
            (view.getUint8(offset) << 16) |
            (view.getUint8(offset + 1) << 8) |
            view.getUint8(offset + 2);
        }
        if (value & 0x800000) value -= 0x1000000;
        samples[frame] = value / 8388608;
      }
      else if (!floating && bits === 32) {
        samples[frame] =
          view.getInt32(offset, littleEndian) / 2147483648;
      }
      else {
        throw new Error(
          `Kernel AIFF ${bits}-bit encoding is unsupported`);
      }
      if (!Number.isFinite(samples[frame])) {
        throw new Error(
          `Kernel AIFF sample ${frame + 1} is not finite`);
      }
    }
    return samples;
  }

  function decodeKernelAudio(arrayBuffer) {
    if (!(arrayBuffer instanceof ArrayBuffer)) {
      throw new Error("Kernel audio must be an ArrayBuffer");
    }
    const view = new DataView(arrayBuffer);
    const samples =
      decodeWavKernel(view) ?? decodeAiffKernel(view);
    if (!samples) {
      throw new Error(
        "Kernel sound file must be WAV, AIFF, or uncompressed AIFC audio");
    }
    if (samples.length === 0) {
      throw new Error(
        "Kernel sound file contains no first-channel samples");
    }
    return samples;
  }

  function portableFileName(name) {
    const parts = String(name || "").split(/[\\/]/);
    const basename = parts[parts.length - 1];
    if (!basename ||
        basename === "." ||
        basename === ".." ||
        /[\\/:]/.test(basename)) {
      throw new Error(
        "Kernel sound file needs a portable filename");
    }
    return basename;
  }

  function mountKernelPane(
    container,
    settings,
    reportError) {
    const section = createElement("fieldset", {
      className: "ishmael-settings-section ishmael-settings-kernel",
      attributes: {
        "data-ishmael-section": "detector",
        "data-ishmael-detector": "matched-filter"
      }
    });
    section.append(createElement("legend", {
      text: "Matched filter"
    }));
    const fileLabel = createElement("label", {
      className: "ishmael-settings-kernel-picker"
    });
    fileLabel.append(createElement("span", {
      text: "Kernel sound file"
    }));
    const file = createElement("input", {
      type: "file",
      attributes: {
        accept:
          ".wav,.wave,.aif,.aiff,audio/wav,audio/x-wav,audio/aiff",
        "data-ishmael-kernel-file": ""
      }
    });
    fileLabel.append(file);
    const status = createElement("p", {
      className: "section-help",
      attributes: {
        "data-ishmael-kernel-status": ""
      }
    });
    const history = createElement("div", {
      className: "ishmael-settings-kernel-history",
      attributes: {
        "data-ishmael-kernel-history": ""
      }
    });
    const waveform = createElement("div", {
      className: "ishmael-settings-waveform",
      attributes: {
        "data-ishmael-kernel-waveform": "",
        role: "img",
        "aria-label": "Kernel first-channel waveform"
      }
    });
    section.append(
      fileLabel,
      status,
      createElement("h4", { text: "Kernel file history" }),
      createElement("p", {
        className: "section-help",
        text:
          "Recent basenames mirror PAMGuard's history. Reselect a file " +
          "to activate it because portable projects embed samples only " +
          "for the active kernel."
      }),
      history,
      createElement("h4", { text: "First-channel waveform" }),
      waveform);
    container.append(section);

    let names = clone(settings.kernelFilenameList);
    let samples = clone(settings.kernelSamples);
    let loading = false;
    const renderWaveform = () => {
      waveform.replaceChildren();
      if (!samples.length) {
        waveform.append(createElement("span", {
          className: "section-help",
          text: "Select a kernel file to embed its first channel."
        }));
        return;
      }
      const count = Math.min(96, samples.length);
      for (let index = 0; index < count; index++) {
        const sourceIndex = Math.min(
          samples.length - 1,
          Math.floor(index * samples.length / count));
        const value = Math.max(
          -1,
          Math.min(1, samples[sourceIndex]));
        waveform.append(createElement("i", {
          attributes: {
            style:
              `--kernel-magnitude:${Math.abs(value)};` +
              `--kernel-offset:${value}`
          }
        }));
      }
    };
    const render = () => {
      history.replaceChildren();
      if (!names.length) {
        history.append(createElement("p", {
          className: "section-help",
          text: "No kernel files selected."
        }));
      }
      names.forEach((name, index) => {
        const row = createElement("div", {
          className: "ishmael-settings-kernel-row",
          attributes: {
            "data-ishmael-kernel-history-row": index
          }
        });
        const storedName = pointerControl(
          "output",
          `/kernelFilenameList/${index}`,
          {
            attributes: {
              value: name
            }
          });
        storedName.value = name;
        storedName.textContent = name;
        const badge = createElement("span", {
          className: "ishmael-settings-kernel-badge",
          text: index === 0 ? "Active" : "Recent"
        });
        const remove = actionButton(
          "Remove",
          `remove-kernel-${index}`);
        remove.addEventListener("click", () => {
          if (index === 0) {
            // The portable schema stores samples only for element zero.
            // Without its samples no later history entry can become active.
            names = [];
            samples = [];
          }
          else {
            names.splice(index, 1);
          }
          render();
        });
        row.append(badge, storedName, remove);
        history.append(row);
      });
      status.textContent = names.length && samples.length
        ? `${names[0]} · ${samples.length.toLocaleString()} first-channel samples embedded.`
        : "No active kernel. Select a WAV or AIFF sound file.";
      renderWaveform();
    };
    const loadFile = async () => {
      const selected = file.files?.[0];
      if (!selected) return;
      try {
        loading = true;
        status.textContent = `Reading ${selected.name}...`;
        const decoded = decodeKernelAudio(
          await selected.arrayBuffer());
        const basename = portableFileName(selected.name);
        names = [
          basename,
          ...names.filter((name) => name !== basename)
        ].slice(0, MAX_KERNEL_HISTORY);
        samples = decoded;
        render();
      }
      catch (error) {
        reportError(error);
        status.textContent =
          `Kernel import failed: ${error.message}`;
      }
      finally {
        loading = false;
        file.value = "";
      }
    };
    file.addEventListener("change", loadFile);
    render();

    return {
      collect() {
        if (loading) {
          throw new Error(
            "Wait for the kernel sound file to finish loading");
        }
        return {
          kernelFilenameList: clone(names),
          kernelSamples: clone(samples)
        };
      },
      cleanup() {
        file.removeEventListener?.("change", loadFile);
      }
    };
  }

  function mountEditor(options) {
    ensureStylesheet();
    const {
      container,
      typeId,
      settings = {},
      sourceSelect = null,
      getAvailableChannelBitmap = () => 0,
      getSourceSampleRate = () => 0,
      reportError = () => {}
    } = options;
    requireType(typeId);
    const draft = canonicalSettings(typeId, settings);
    const root = createElement("div", {
      className: "ishmael-settings-editor",
      attributes: {
        "data-pamguard-ishmael-settings-editor": typeId
      }
    });
    container.append(root);

    // Pinned Java order: grouped source, detector-specific pane, peak picker.
    const source = mountGroupedSource({
      container: root,
      typeId,
      settings: draft,
      sourceSelect,
      getAvailableChannelBitmap,
      getSourceSampleRate
    });
    let detector;
    if (typeId === TYPE_ENERGY) {
      detector = mountEnergyPane(root, draft);
    }
    else if (typeId === TYPE_SGRAM) {
      detector = mountSgramPane(root, draft, reportError);
    }
    else {
      detector = mountKernelPane(root, draft, reportError);
    }
    const peak = mountPeakPicker(root, draft);

    return {
      collect() {
        return validateReady(typeId, canonicalSettings(typeId, {
          ...source.collect(),
          ...peak.collect(),
          ...detector.collect()
        }));
      },
      cleanup() {
        source.cleanup();
        detector.cleanup?.();
      },
      refreshSource: source.refresh
    };
  }

  globalThis.PamguardProjectIshmaelSettings = Object.freeze({
    mountEditor,
    defaultSettings,
    canonicalSettings,
    validateReady,
    decodeKernelAudio
  });
})();
