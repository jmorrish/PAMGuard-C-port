(() => {
  "use strict";

  let editorSequence = 0;
  const CHANNEL_COUNT = 32;
  const GROUPING_TYPES = Object.freeze([
    ["singles", "No grouping"],
    ["all", "One group"],
    ["user", "User groups"]
  ]);
  const CONNECTION_TYPES = Object.freeze([
    ["4", "Connect 4 (sides only)"],
    ["8", "Connect 8 (sides and diagonals)"]
  ]);
  const FRAGMENTATION_METHODS = Object.freeze([
    ["0", "Leave branched regions intact"],
    ["1", "Discard branched regions"],
    ["2", "Separate all branches"],
    ["3", "Re-link across joins"]
  ]);
  const THRESHOLD_OUTPUTS = Object.freeze([
    ["0", "Binary output (0's and 1's)"],
    ["1", "Use the output of the preceeding step"],
    ["2", "Use the input from the raw FFT data"]
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
      "link[data-pamguard-project-whistle-moan-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-whistle-moan-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL(
          "project-whistle-moan-settings.css",
          capturedScriptSource).href
      : "/assets/project-whistle-moan-settings.css";
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

  function defaultSettings() {
    return {
      channelBitmap: 0,
      groupingType: "all",
      channelGroups: [],
      minFrequencyHz: 0,
      maxFrequencyHz: 0,
      connectType: 8,
      minLength: 10,
      minPixels: 20,
      keepShapeStubs: false,
      fragmentationMethod: 3,
      maxCrossLength: 5,
      noiseReduction: {
        medianFilter: false,
        medianFilterLength: 61,
        averageSubtraction: false,
        updateConstant: 0.02,
        kernelSmoothing: false,
        threshold: false,
        thresholdDb: 8,
        finalOutput: 2
      }
    };
  }

  function canonicalSettings(value = {}) {
    const defaults = defaultSettings();
    const groups = Array.isArray(value.channelGroups)
      ? value.channelGroups.map((group, index) =>
          finiteNumber(
            group,
            `Channel ${index} group`,
            { integer: true, min: 0, max: 31 }))
      : [];
    if (groups.length > CHANNEL_COUNT) {
      throw new Error(
        "Channel groups cannot contain more than 32 assignments");
    }
    const groupingType = GROUPING_TYPES.some(
      ([candidate]) => candidate === value.groupingType)
      ? value.groupingType
      : defaults.groupingType;
    const minFrequencyHz = finiteNumber(
      value.minFrequencyHz ?? defaults.minFrequencyHz,
      "Minimum frequency",
      { min: 0 });
    const maxFrequencyHz = finiteNumber(
      value.maxFrequencyHz ?? defaults.maxFrequencyHz,
      "Maximum frequency",
      { min: 0 });
    if (maxFrequencyHz > 0 &&
        minFrequencyHz > maxFrequencyHz) {
      throw new Error(
        "Minimum frequency must not exceed maximum frequency");
    }
    const connectType = finiteNumber(
      value.connectType ?? defaults.connectType,
      "Connection type",
      { integer: true, min: 4, max: 8 });
    if (![4, 8].includes(connectType)) {
      throw new Error("Connection type must be 4 or 8");
    }
    const fragmentationMethod = finiteNumber(
      value.fragmentationMethod ??
        defaults.fragmentationMethod,
      "Crossing and joining method",
      { integer: true, min: 0, max: 3 });
    const noise = value.noiseReduction || {};
    const medianFilterLength = finiteNumber(
      noise.medianFilterLength ??
        defaults.noiseReduction.medianFilterLength,
      "Median filter length",
      { integer: true, min: 3, max: 2147483647 });
    if (medianFilterLength % 2 === 0) {
      throw new Error(
        "Median filter length must be odd and at least 3");
    }
    return {
      channelBitmap: portableBitmap(
        value.channelBitmap ?? defaults.channelBitmap),
      groupingType,
      channelGroups: groups,
      minFrequencyHz,
      maxFrequencyHz,
      connectType,
      minLength: finiteNumber(
        value.minLength ?? defaults.minLength,
        "Minimum length",
        { integer: true, min: 1, max: 0xffffffff }),
      minPixels: finiteNumber(
        value.minPixels ?? defaults.minPixels,
        "Minimum total size",
        { integer: true, min: 1, max: 0xffffffff }),
      keepShapeStubs:
        typeof value.keepShapeStubs === "boolean"
          ? value.keepShapeStubs
          : defaults.keepShapeStubs,
      fragmentationMethod,
      maxCrossLength: finiteNumber(
        value.maxCrossLength ?? defaults.maxCrossLength,
        "Maximum cross length",
        { integer: true, min: 1, max: 0xffffffff }),
      noiseReduction: {
        medianFilter:
          typeof noise.medianFilter === "boolean"
            ? noise.medianFilter
            : defaults.noiseReduction.medianFilter,
        medianFilterLength,
        averageSubtraction:
          typeof noise.averageSubtraction === "boolean"
            ? noise.averageSubtraction
            : defaults.noiseReduction.averageSubtraction,
        updateConstant: finiteNumber(
          noise.updateConstant ??
            defaults.noiseReduction.updateConstant,
          "Average subtraction update constant",
          { exclusiveMin: 0, max: 0.5 }),
        kernelSmoothing:
          typeof noise.kernelSmoothing === "boolean"
            ? noise.kernelSmoothing
            : defaults.noiseReduction.kernelSmoothing,
        threshold:
          typeof noise.threshold === "boolean"
            ? noise.threshold
            : defaults.noiseReduction.threshold,
        thresholdDb: finiteNumber(
          noise.thresholdDb ??
            defaults.noiseReduction.thresholdDb,
          "Threshold",
          { exclusiveMin: 0 }),
        finalOutput: finiteNumber(
          noise.finalOutput ??
            defaults.noiseReduction.finalOutput,
          "Threshold final output",
          { integer: true, min: 0, max: 2 })
      }
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

  function validateReady(settings) {
    if (settings.channelBitmap === 0) {
      throw new Error(
        "Select at least one detection channel or sequence");
    }
    validateUserGrouping(settings);
    const noise = settings.noiseReduction;
    if (!noise.medianFilter ||
        !noise.averageSubtraction ||
        !noise.threshold) {
      throw new Error(
        "The supported standard FFT path requires Median Filter, " +
          "Average Subtraction, and Thresholding");
    }
    return settings;
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
      attributes: {
        step: "any",
        ...attributes
      }
    });
    control.value = String(value);
    return control;
  }

  function selectControl(value, pointer, choices) {
    const control = pointerControl("select", pointer);
    for (const [stored, label] of choices) {
      const option = createElement("option", {
        text: label,
        attributes: { value: stored }
      });
      option.selected = stored === String(value);
      control.append(option);
    }
    control.value = String(value);
    return control;
  }

  function field(label, control, options = {}) {
    const row = createElement("label", {
      className: "whistle-moan-settings-field"
    });
    row.append(
      createElement("span", {
        className: "whistle-moan-settings-label",
        text: label
      }),
      control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "whistle-moan-settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", {
        className: "whistle-moan-settings-help",
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
      attributes: { "data-whistle-action": action }
    });
  }

  function checkboxLabel(label, control, help = "") {
    const wrapper = createElement("label", {
      className: "whistle-moan-settings-check"
    });
    wrapper.append(
      control,
      createElement("span", { text: label }));
    if (help) {
      wrapper.append(createElement("small", {
        className: "whistle-moan-settings-help",
        text: help
      }));
    }
    return wrapper;
  }

  function mountGroupedSource(options) {
    const {
      container,
      settings,
      sourceSelect,
      getAvailableChannelBitmap
    } = options;
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
    let channelControls = [];
    let groupControls = [];

    const root = createElement("fieldset", {
      className: "whistle-moan-settings-source"
    });
    root.append(createElement("legend", {
      text: "Channel/Sequence list and grouping"
    }));
    const grouping = createElement("div", {
      className: "whistle-moan-settings-grouping",
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
            name: "whistle-moan-grouping",
            value
          }
        });
      control.value = value;
      control.checked = settings.groupingType === value;
      grouping.append(checkboxLabel(label, control));
      groupingControls.set(value, control);
    }
    const actions = createElement("div", {
      className: "whistle-moan-settings-actions"
    });
    const all = actionButton("All available", "all-channels");
    const none = actionButton("None", "no-channels");
    actions.append(all, none);
    const channelHost = createElement("div", {
      className: "whistle-moan-settings-channel-host"
    });
    root.append(grouping, actions, channelHost);
    container.append(root);

    const currentGrouping = () =>
      GROUPING_TYPES.find(([value]) =>
        groupingControls.get(value).checked)?.[0] || "all";
    const sync = () => {
      if (channelControls.length) {
        selectedBitmap = channelControls.reduce(
          (bitmap, item) =>
            bitmap +
              (item.control.checked ? 2 ** item.channel : 0),
          0);
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
    const render = () => {
      sync();
      channelHost.replaceChildren();
      channelControls = [];
      groupControls = [];
      const available = channelsIn(
        getAvailableChannelBitmap());
      if (!available.length) {
        channelHost.append(createElement("p", {
          className: "section-help",
          text:
            "Choose a compatible FFT source to expose channels or sequences."
        }));
      }
      else {
        const table = createElement("table", {
          className: "whistle-moan-settings-channel-table"
        });
        const head = createElement("thead");
        const heading = createElement("tr");
        heading.append(
          createElement("th", { text: "Channel / sequence" }),
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
            Math.floor(selectedBitmap / (2 ** channel)) % 2 === 1;
          const selectedLabel = checkboxLabel(
            `Channel / sequence ${channel}`,
            selected);
          const group = selectControl(
            assignments[channel],
            `/channelGroups/${channel}`,
            Array.from(
              { length: CHANNEL_COUNT },
              (_, value) => [String(value), String(value)]));
          group.value = String(assignments[channel]);
          const channelCell = createElement("td");
          const groupCell = createElement("td");
          channelCell.append(selectedLabel);
          groupCell.append(group);
          row.append(channelCell, groupCell);
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
      }
    };
  }

  function mountNoiseMethod(options) {
    const {
      container,
      title,
      enabledPointer,
      enabled,
      description,
      children = []
    } = options;
    const root = createElement("fieldset", {
      className: "whistle-moan-settings-noise-method",
      attributes: { "data-whistle-noise-method": title }
    });
    root.append(createElement("legend", { text: title }));
    const toggle = pointerControl(
      "input",
      enabledPointer,
      { type: "checkbox" });
    toggle.checked = enabled;
    root.append(checkboxLabel(`Run ${title}`, toggle, description));
    children.forEach((child) => root.append(child.row));
    container.append(root);
    const update = () => {
      children.forEach((child) => {
        child.control.disabled = !toggle.checked;
      });
    };
    toggle.addEventListener("change", update);
    update();
    return { toggle, update };
  }

  function mountEditor(options) {
    ensureStylesheet();
    const {
      container,
      settings = {},
      sourceSelect = null,
      getAvailableChannelBitmap = () => 0,
      getSourceSampleRate = () => 0
    } = options;
    const draft = canonicalSettings(settings);
    const instanceId =
      `whistle-moan-settings-${++editorSequence}`;
    const detectionPanelId = `${instanceId}-detection-panel`;
    const noisePanelId = `${instanceId}-noise-panel`;
    const root = createElement("div", {
      className: "whistle-moan-settings-editor",
      attributes: {
        "data-pamguard-whistle-moan-settings-editor": "true"
      }
    });
    const tabList = createElement("div", {
      className: "whistle-moan-settings-tabs",
      attributes: {
        role: "tablist",
        "aria-label": "Whistle and Moan settings"
      }
    });
    const detectionTab = createElement("button", {
      type: "button",
      text: "Detection",
      attributes: {
        id: `${instanceId}-detection-tab`,
        role: "tab",
        "aria-controls": detectionPanelId,
        "data-whistle-tab": "detection"
      }
    });
    const noiseTab = createElement("button", {
      type: "button",
      text: "Noise and Thresholding",
      attributes: {
        id: `${instanceId}-noise-tab`,
        role: "tab",
        "aria-controls": noisePanelId,
        "data-whistle-tab": "noise"
      }
    });
    tabList.append(detectionTab, noiseTab);
    const detectionPanel = createElement("section", {
      className: "whistle-moan-settings-panel",
      attributes: {
        id: detectionPanelId,
        role: "tabpanel",
        "aria-labelledby": `${instanceId}-detection-tab`,
        "data-whistle-panel": "detection"
      }
    });
    const noisePanel = createElement("section", {
      className: "whistle-moan-settings-panel",
      attributes: {
        id: noisePanelId,
        role: "tabpanel",
        "aria-labelledby": `${instanceId}-noise-tab`,
        "data-whistle-panel": "noise"
      }
    });
    root.append(tabList, detectionPanel, noisePanel);
    container.append(root);

    const activateTab = (name) => {
      const detection = name === "detection";
      detectionPanel.hidden = !detection;
      noisePanel.hidden = detection;
      detectionTab.setAttribute(
        "aria-selected",
        detection ? "true" : "false");
      noiseTab.setAttribute(
        "aria-selected",
        detection ? "false" : "true");
    };
    detectionTab.addEventListener(
      "click",
      () => activateTab("detection"));
    noiseTab.addEventListener(
      "click",
      () => activateTab("noise"));
    activateTab("detection");

    const groupedSource = mountGroupedSource({
      container: detectionPanel,
      settings: draft,
      sourceSelect,
      getAvailableChannelBitmap
    });
    const sourceSummary = createElement("p", {
      className: "section-help",
      attributes: { "data-whistle-source-summary": "" }
    });
    detectionPanel.append(sourceSummary);

    const connections = createElement("fieldset", {
      className: "whistle-moan-settings-connections"
    });
    connections.append(createElement("legend", {
      text: "Connections"
    }));
    const minFrequency = numberControl(
      draft.minFrequencyHz,
      "/minFrequencyHz",
      { min: 0 });
    const maxFrequency = numberControl(
      draft.maxFrequencyHz,
      "/maxFrequencyHz",
      { min: 0 });
    const connectType = selectControl(
      draft.connectType,
      "/connectType",
      CONNECTION_TYPES);
    const minLength = numberControl(
      draft.minLength,
      "/minLength",
      { min: 1, max: 0xffffffff, step: 1 });
    const minPixels = numberControl(
      draft.minPixels,
      "/minPixels",
      { min: 1, max: 0xffffffff, step: 1 });
    const removeStubs = pointerControl(
      "input",
      "/keepShapeStubs",
      { type: "checkbox" });
    removeStubs.checked = !draft.keepShapeStubs;
    const fragmentation = selectControl(
      draft.fragmentationMethod,
      "/fragmentationMethod",
      FRAGMENTATION_METHODS);
    const maxCrossLength = numberControl(
      draft.maxCrossLength,
      "/maxCrossLength",
      { min: 1, max: 0xffffffff, step: 1 });
    const stubsRow = createElement("div", {
      className: "whistle-moan-settings-field"
    });
    stubsRow.append(
      createElement("span", {
        className: "whistle-moan-settings-label",
        text: "Shape 'stubs'"
      }),
      checkboxLabel("Remove small stubs", removeStubs));
    connections.append(
      field("Min Frequency", minFrequency, { unit: "Hz" }),
      field(
        "Max Frequency",
        maxFrequency,
        {
          unit: "Hz",
          help: "0 is the persisted source-Nyquist sentinel."
        }),
      field("Connection Type", connectType),
      field(
        "Minimum length",
        minLength,
        { unit: "time slices" }),
      field(
        "Minimum total size",
        minPixels,
        { unit: "pixels" }),
      stubsRow,
      field("Crossing and Joining", fragmentation),
      field(
        "Max Cross length",
        maxCrossLength,
        { unit: "time slices" }));
    detectionPanel.append(connections);

    const updateFragmentation = () => {
      maxCrossLength.disabled =
        Number(fragmentation.value) !== 3;
    };
    fragmentation.addEventListener(
      "change",
      updateFragmentation);
    updateFragmentation();

    const noiseIntro = createElement("div", {
      className: "whistle-moan-settings-readiness",
      attributes: {
        role: "status",
        "data-whistle-noise-readiness": ""
      }
    });
    noisePanel.append(noiseIntro);
    const medianLength = numberControl(
      draft.noiseReduction.medianFilterLength,
      "/noiseReduction/medianFilterLength",
      { min: 3, max: 2147483647, step: 2 });
    const median = mountNoiseMethod({
      container: noisePanel,
      title: "Median Filter",
      enabledPointer: "/noiseReduction/medianFilter",
      enabled: draft.noiseReduction.medianFilter,
      description:
        "Required for the supported standard FFT source path.",
      children: [{
        control: medianLength,
        row: field(
          "Filter length (should be odd)",
          medianLength)
      }]
    });
    const updateConstant = numberControl(
      draft.noiseReduction.updateConstant,
      "/noiseReduction/updateConstant",
      { min: Number.MIN_VALUE, max: 0.5 });
    const average = mountNoiseMethod({
      container: noisePanel,
      title: "Average Subtraction",
      enabledPointer: "/noiseReduction/averageSubtraction",
      enabled: draft.noiseReduction.averageSubtraction,
      description:
        "Required for the supported standard FFT source path.",
      children: [{
        control: updateConstant,
        row: field(
          "Update constant (e.g. .02)",
          updateConstant)
      }]
    });
    const kernel = mountNoiseMethod({
      container: noisePanel,
      title: "Gaussian Kernel Smoothing",
      enabledPointer: "/noiseReduction/kernelSmoothing",
      enabled: draft.noiseReduction.kernelSmoothing,
      description:
        "Optional fixed 3 x 3 Gaussian smoothing kernel."
    });
    const thresholdDb = numberControl(
      draft.noiseReduction.thresholdDb,
      "/noiseReduction/thresholdDb",
      { min: Number.MIN_VALUE });
    const finalOutput = selectControl(
      draft.noiseReduction.finalOutput,
      "/noiseReduction/finalOutput",
      THRESHOLD_OUTPUTS);
    const threshold = mountNoiseMethod({
      container: noisePanel,
      title: "Thresholding",
      enabledPointer: "/noiseReduction/threshold",
      enabled: draft.noiseReduction.threshold,
      description:
        "Required for the supported standard FFT source path.",
      children: [
        {
          control: thresholdDb,
          row: field("Threshold", thresholdDb, { unit: "dB" })
        },
        {
          control: finalOutput,
          row: field(
            "Above-threshold data",
            finalOutput,
            {
              help:
                "Below-threshold data are always set to zero."
            })
        }
      ]
    });
    const updateReadiness = () => {
      const ready =
        median.toggle.checked &&
        average.toggle.checked &&
        threshold.toggle.checked;
      noiseIntro.setAttribute(
        "data-state",
        ready ? "ready" : "needs-configuration");
      noiseIntro.textContent = ready
        ? "Required standard FFT noise chain ready."
        : "Enable Median Filter, Average Subtraction, and Thresholding " +
          "for the supported standard FFT source path.";
    };
    [
      median.toggle,
      average.toggle,
      kernel.toggle,
      threshold.toggle
    ].forEach((control) =>
      control.addEventListener("change", updateReadiness));
    updateReadiness();

    const refreshSource = () => {
      const sampleRate = Number(getSourceSampleRate()) || 0;
      sourceSummary.textContent = sampleRate > 0
        ? `Selected FFT source: ${sampleRate.toLocaleString()} Hz, ` +
          `Nyquist ${(sampleRate / 2).toLocaleString()} Hz.`
        : "Selected FFT source sample rate is unavailable.";
    };
    sourceSelect?.addEventListener("change", refreshSource);
    refreshSource();

    return {
      collect() {
        const source = groupedSource.collect();
        const result = canonicalSettings({
          ...source,
          minFrequencyHz: finiteNumber(
            minFrequency.value,
            "Minimum frequency",
            { min: 0 }),
          maxFrequencyHz: finiteNumber(
            maxFrequency.value,
            "Maximum frequency",
            { min: 0 }),
          connectType: finiteNumber(
            connectType.value,
            "Connection type",
            { integer: true, min: 4, max: 8 }),
          minLength: finiteNumber(
            minLength.value,
            "Minimum length",
            { integer: true, min: 1, max: 0xffffffff }),
          minPixels: finiteNumber(
            minPixels.value,
            "Minimum total size",
            { integer: true, min: 1, max: 0xffffffff }),
          keepShapeStubs: !removeStubs.checked,
          fragmentationMethod: finiteNumber(
            fragmentation.value,
            "Crossing and joining method",
            { integer: true, min: 0, max: 3 }),
          maxCrossLength: finiteNumber(
            maxCrossLength.value,
            "Maximum cross length",
            { integer: true, min: 1, max: 0xffffffff }),
          noiseReduction: {
            medianFilter: median.toggle.checked,
            medianFilterLength: finiteNumber(
              medianLength.value,
              "Median filter length",
              { integer: true, min: 3, max: 2147483647 }),
            averageSubtraction: average.toggle.checked,
            updateConstant: finiteNumber(
              updateConstant.value,
              "Average subtraction update constant",
              { exclusiveMin: 0, max: 0.5 }),
            kernelSmoothing: kernel.toggle.checked,
            threshold: threshold.toggle.checked,
            thresholdDb: finiteNumber(
              thresholdDb.value,
              "Threshold",
              { exclusiveMin: 0 }),
            finalOutput: finiteNumber(
              finalOutput.value,
              "Threshold final output",
              { integer: true, min: 0, max: 2 })
          }
        });
        if (![4, 8].includes(result.connectType)) {
          throw new Error("Connection type must be 4 or 8");
        }
        return clone(validateReady(result));
      },
      cleanup() {
        groupedSource.cleanup();
        sourceSelect?.removeEventListener?.(
          "change",
          refreshSource);
      }
    };
  }

  globalThis.PamguardProjectWhistleMoanSettings =
    Object.freeze({
      mountEditor,
      canonicalSettings,
      validateReady
    });
})();
