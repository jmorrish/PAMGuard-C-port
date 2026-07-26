(() => {
  "use strict";

  const UINT32_MAX = 0xffffffff;
  const INT32_MAX = 2147483647;
  const MAX_SAFE_INTEGER = Number.MAX_SAFE_INTEGER;
  const OPERATION_MODES = Object.freeze([
    ["idle", "Remain idle"],
    ["continuous", "Start recording"],
    ["cycle", "Start recording cycle"],
    [
      "restore-last",
      "Automatically return to last state at PAMGuard Stop"
    ]
  ]);
  const BIT_DEPTHS = Object.freeze([8, 16, 24, 32]);
  const TRIGGER_FIELDS = Object.freeze([
    "triggerName",
    "enabled",
    "secondsBeforeTrigger",
    "secondsAfterTrigger",
    "minDetectionCount",
    "countSeconds",
    "minGapBetweenTriggersSeconds",
    "maxTotalTriggerLengthSeconds",
    "dayBudgetMegaBytes",
    "lastTriggerStartUnixMs",
    "lastTriggerEndUnixMs",
    "usedDayBudgetBytes"
  ]);
  const SETTINGS_FIELDS = Object.freeze([
    "operationMode",
    "channelBitmap",
    "bitDepth",
    "enableBuffer",
    "bufferLengthSeconds",
    "fileInitials",
    "fileType",
    "autoIntervalSeconds",
    "autoDurationSeconds",
    "limitLengthSeconds",
    "maxLengthSeconds",
    "roundFileStarts",
    "limitLengthMegaBytes",
    "maxLengthMegaBytes",
    "datedSubFolders",
    "triggerPolicies"
  ]);
  const capturedScriptSource =
    typeof document !== "undefined" && document.currentScript
      ? document.currentScript.src
      : "";
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

  function ensureStylesheet() {
    if (document.querySelector(
      "link[data-pamguard-project-sound-recorder-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-sound-recorder-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL(
          "project-sound-recorder-settings.css",
          capturedScriptSource).href
      : "/assets/project-sound-recorder-settings.css";
    document.head.append(link);
  }

  function clone(value) {
    if (Array.isArray(value)) return value.map(clone);
    if (value && typeof value === "object") {
      return Object.fromEntries(
        Object.entries(value).map(
          ([name, child]) => [name, clone(child)]));
    }
    return value;
  }

  function defaultSettings() {
    return {
      operationMode: "idle",
      channelBitmap: 3,
      bitDepth: 16,
      enableBuffer: false,
      bufferLengthSeconds: 30,
      fileInitials: "PAM",
      fileType: "WAVE",
      autoIntervalSeconds: 300,
      autoDurationSeconds: 10,
      limitLengthSeconds: true,
      maxLengthSeconds: 3600,
      roundFileStarts: true,
      limitLengthMegaBytes: true,
      maxLengthMegaBytes: 640,
      datedSubFolders: true,
      triggerPolicies: []
    };
  }

  function exactObject(value, fields, label) {
    if (!value || typeof value !== "object" ||
        Array.isArray(value)) {
      throw new Error(`${label} must be an object`);
    }
    const actual = Object.keys(value);
    const missing = fields.filter(
      (name) =>
        !Object.prototype.hasOwnProperty.call(value, name));
    const extra = actual.filter(
      (name) => !fields.includes(name));
    if (missing.length || extra.length) {
      throw new Error(
        `${label} must contain exactly: ${fields.join(", ")}`);
    }
    return value;
  }

  function booleanValue(value, label) {
    if (typeof value !== "boolean") {
      throw new Error(`${label} must be true or false`);
    }
    return value;
  }

  function finiteNumber(value, label, options = {}) {
    const parsed = Number(value);
    if (!Number.isFinite(parsed) ||
        (options.integer && !Number.isInteger(parsed)) ||
        (options.min !== undefined && parsed < options.min) ||
        (options.max !== undefined && parsed > options.max)) {
      throw new Error(`${label} has an invalid value`);
    }
    return parsed;
  }

  function integer(value, label, min = 0, max = INT32_MAX) {
    return finiteNumber(value, label, {
      integer: true,
      min,
      max
    });
  }

  function boundedString(
    value,
    label,
    { allowEmpty = false } = {}) {
    if (typeof value !== "string" ||
        (!allowEmpty && value.length === 0) ||
        value.length > 256) {
      throw new Error(
        `${label} must contain ${allowEmpty ? "0" : "1"} to 256 characters`);
    }
    return value;
  }

  function canonicalTriggerPolicy(value, index) {
    const label = `Trigger policy ${index + 1}`;
    exactObject(value, TRIGGER_FIELDS, label);
    return {
      triggerName: boundedString(
        value.triggerName,
        `${label} name`),
      enabled: booleanValue(
        value.enabled,
        `${label} enabled`),
      secondsBeforeTrigger: finiteNumber(
        value.secondsBeforeTrigger,
        `${label} seconds before trigger`,
        { min: 0 }),
      secondsAfterTrigger: finiteNumber(
        value.secondsAfterTrigger,
        `${label} seconds after trigger`,
        { min: 0 }),
      minDetectionCount: integer(
        value.minDetectionCount,
        `${label} minimum detections`,
        1),
      countSeconds: integer(
        value.countSeconds,
        `${label} count integration time`),
      minGapBetweenTriggersSeconds: integer(
        value.minGapBetweenTriggersSeconds,
        `${label} minimum recording interval`),
      maxTotalTriggerLengthSeconds: integer(
        value.maxTotalTriggerLengthSeconds,
        `${label} maximum recording length`),
      dayBudgetMegaBytes: integer(
        value.dayBudgetMegaBytes,
        `${label} daily data budget`),
      lastTriggerStartUnixMs: integer(
        value.lastTriggerStartUnixMs,
        `${label} last trigger start`,
        0,
        MAX_SAFE_INTEGER),
      lastTriggerEndUnixMs: integer(
        value.lastTriggerEndUnixMs,
        `${label} last trigger end`,
        0,
        MAX_SAFE_INTEGER),
      usedDayBudgetBytes: integer(
        value.usedDayBudgetBytes,
        `${label} used daily budget`,
        0,
        MAX_SAFE_INTEGER)
    };
  }

  function canonicalSettings(value = defaultSettings()) {
    exactObject(value, SETTINGS_FIELDS, "Sound Recorder settings");
    if (!OPERATION_MODES.some(
      ([mode]) => mode === value.operationMode)) {
      throw new Error(
        "Sound Recorder operation mode must be idle, continuous, " +
          "cycle, or restore-last");
    }
    const bitDepth = integer(
      value.bitDepth,
      "Sound Recorder bit depth",
      8,
      32);
    if (!BIT_DEPTHS.includes(bitDepth)) {
      throw new Error(
        "Sound Recorder bit depth must be 8, 16, 24, or 32");
    }
    if (!Array.isArray(value.triggerPolicies) ||
        value.triggerPolicies.length > 1024) {
      throw new Error(
        "Sound Recorder trigger policies must contain at most 1024 entries");
    }
    const triggerPolicies = value.triggerPolicies.map(
      canonicalTriggerPolicy);
    const triggerNames = new Set();
    for (const policy of triggerPolicies) {
      if (triggerNames.has(policy.triggerName)) {
        throw new Error(
          "Sound Recorder trigger policy names must be unique");
      }
      triggerNames.add(policy.triggerName);
    }

    const autoIntervalSeconds = integer(
      value.autoIntervalSeconds,
      "Sound Recorder total cycle time",
      1);
    const autoDurationSeconds = integer(
      value.autoDurationSeconds,
      "Sound Recorder cycle recording length",
      1);
    if (autoIntervalSeconds <= autoDurationSeconds) {
      throw new Error(
        "Sound Recorder total cycle time must be greater than " +
          "the recording length");
    }

    return {
      operationMode: value.operationMode,
      channelBitmap: integer(
        value.channelBitmap,
        "Sound Recorder channel bitmap",
        1,
        UINT32_MAX),
      bitDepth,
      enableBuffer: booleanValue(
        value.enableBuffer,
        "Sound Recorder buffer enabled"),
      bufferLengthSeconds: integer(
        value.bufferLengthSeconds,
        "Sound Recorder buffer length"),
      fileInitials: boundedString(
        value.fileInitials,
        "Sound Recorder file name prefix",
        { allowEmpty: true }),
      fileType: boundedString(
        value.fileType,
        "Sound Recorder file type"),
      autoIntervalSeconds,
      autoDurationSeconds,
      limitLengthSeconds: booleanValue(
        value.limitLengthSeconds,
        "Sound Recorder file time limit enabled"),
      maxLengthSeconds: integer(
        value.maxLengthSeconds,
        "Sound Recorder maximum file length",
        1),
      roundFileStarts: booleanValue(
        value.roundFileStarts,
        "Sound Recorder rounded file starts"),
      limitLengthMegaBytes: booleanValue(
        value.limitLengthMegaBytes,
        "Sound Recorder file size limit enabled"),
      maxLengthMegaBytes: integer(
        value.maxLengthMegaBytes,
        "Sound Recorder maximum file size",
        1,
        MAX_SAFE_INTEGER),
      datedSubFolders: booleanValue(
        value.datedSubFolders,
        "Sound Recorder dated subfolders"),
      triggerPolicies
    };
  }

  function numberControl(value, pointer, options = {}) {
    const control = createElement("input", {
      type: "number",
      attributes: {
        min: options.min,
        max: options.max,
        step: options.step ?? "1",
        "data-setting-pointer": pointer
      }
    });
    control.value = String(value);
    return control;
  }

  function textControl(value, pointer, options = {}) {
    const control = createElement("input", {
      type: "text",
      attributes: {
        maxlength: "256",
        "data-setting-pointer": pointer
      }
    });
    control.value = value;
    control.readOnly = options.readOnly === true;
    return control;
  }

  function checkboxControl(value, pointer, attributes = {}) {
    const control = createElement("input", {
      type: "checkbox",
      attributes: {
        "data-setting-pointer": pointer,
        ...attributes
      }
    });
    control.checked = value;
    return control;
  }

  function selectControl(value, pointer, choices) {
    const control = createElement("select", {
      attributes: {
        "data-setting-pointer": pointer
      }
    });
    for (const [choice, label, availability] of choices) {
      const option = createElement("option", {
        text: label,
        attributes: {
          value: choice,
          "data-sound-recorder-runtime-supported":
            availability === false ? "false" : "true"
        }
      });
      control.append(option);
    }
    control.value = String(value);
    return control;
  }

  function field(label, control, options = {}) {
    const row = createElement("label", {
      className: "sound-recorder-settings-field"
    });
    row.append(
      createElement("span", {
        className: "sound-recorder-settings-label",
        text: label
      }),
      control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "sound-recorder-settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", {
        className: "sound-recorder-settings-help",
        text: options.help
      }));
    }
    return row;
  }

  function checkLabel(label, control, help = "") {
    const row = createElement("label", {
      className: "sound-recorder-settings-check"
    });
    row.append(control, createElement("span", { text: label }));
    if (help) {
      row.append(createElement("small", {
        className: "sound-recorder-settings-help",
        text: help
      }));
    }
    return row;
  }

  function section(title, help = "") {
    const result = createElement("section", {
      className: "sound-recorder-settings-section"
    });
    result.append(createElement("h4", { text: title }));
    if (help) {
      result.append(createElement("p", {
        className: "sound-recorder-settings-section-help",
        text: help
      }));
    }
    return result;
  }

  function boundary(label, value, kind) {
    const row = createElement("div", {
      className: "sound-recorder-settings-boundary",
      attributes: {
        "data-sound-recorder-boundary": kind
      }
    });
    row.append(
      createElement("strong", { text: label }),
      createElement("span", { text: value }));
    return row;
  }

  function hasChannel(bitmap, channel) {
    return Math.floor(bitmap / (2 ** channel)) % 2 === 1;
  }

  function mountEditor(options) {
    ensureStylesheet();
    const {
      container,
      settings = defaultSettings(),
      sourceLabel =
        "Connected raw-audio output (configured in the module graph)",
      outputFolderLabel =
        "Assigned by the host when this project runs",
      availableChannelBitmap = 0,
      onRuntimeAction = null,
      reportError = () => {}
    } = options;
    const draft = canonicalSettings(settings);
    const availableChannels = integer(
      availableChannelBitmap,
      "Available recorder channel bitmap",
      0,
      UINT32_MAX);
    const instance = ++editorSequence;
    let disposed = false;

    const root = createElement("div", {
      className: "sound-recorder-settings-editor",
      attributes: {
        "data-pamguard-sound-recorder-settings-editor": "true"
      }
    });
    const tabList = createElement("div", {
      className: "sound-recorder-settings-tabs",
      attributes: {
        role: "tablist",
        "aria-label": "Sound Recording Settings"
      }
    });
    const panels = new Map();
    const tabs = [];
    const tabDefinitions = [
      ["control", "Control"],
      ["files", "Files and Folders"],
      ["triggers", "Triggered Recordings"]
    ];

    function selectTab(selectedName) {
      tabs.forEach(({ name, tab }) => {
        const selected = name === selectedName;
        tab.setAttribute("aria-selected", String(selected));
        panels.get(name).hidden = !selected;
      });
    }

    for (const [name, label] of tabDefinitions) {
      const tabId =
        `sound-recorder-settings-${instance}-tab-${name}`;
      const panelId =
        `sound-recorder-settings-${instance}-panel-${name}`;
      const tab = createElement("button", {
        type: "button",
        text: label,
        attributes: {
          id: tabId,
          role: "tab",
          "aria-controls": panelId,
          "aria-selected": name === "control" ? "true" : "false",
          "data-sound-recorder-tab": name
        }
      });
      const panel = createElement("div", {
        className: "sound-recorder-settings-panel",
        attributes: {
          id: panelId,
          role: "tabpanel",
          "aria-labelledby": tabId,
          "data-sound-recorder-panel": name
        }
      });
      panel.hidden = name !== "control";
      tab.addEventListener("click", () => selectTab(name));
      tabList.append(tab);
      tabs.push({ name, tab });
      panels.set(name, panel);
    }
    root.append(tabList, ...panels.values());

    const controlPanel = panels.get("control");
    const sourceSection = section(
      "Raw data source",
      "The source connection belongs to the module graph. This dialog " +
        "only selects which connected channels are recorded.");
    sourceSection.append(boundary(
      "Source",
      String(sourceLabel),
      "graph-source"));

    const channelGrid = createElement("div", {
      className: "sound-recorder-settings-channels",
      attributes: {
        role: "group",
        "aria-label": "Recorder channels"
      }
    });
    const visibleChannelBitmap =
      availableChannels === 0
        ? draft.channelBitmap
        : Math.min(
            UINT32_MAX,
            availableChannels + draft.channelBitmap -
              (() => {
                let overlap = 0;
                for (let channel = 0; channel < 32; channel++) {
                  if (hasChannel(availableChannels, channel) &&
                      hasChannel(draft.channelBitmap, channel)) {
                    overlap += 2 ** channel;
                  }
                }
                return overlap;
              })());
    const channelControls = [];
    for (let channel = 0; channel < 32; channel++) {
      if (!hasChannel(visibleChannelBitmap, channel)) continue;
      const control = checkboxControl(
        hasChannel(draft.channelBitmap, channel),
        "/channelBitmap",
        {
          "data-sound-recorder-channel": channel,
          "data-sound-recorder-channel-available":
            availableChannels === 0 ||
            hasChannel(availableChannels, channel)
        });
      const label = createElement("label");
      label.append(
        control,
        createElement("span", { text: `Channel ${channel}` }));
      channelGrid.append(label);
      channelControls.push({ channel, control });
    }
    sourceSection.append(channelGrid);
    controlPanel.append(sourceSection);

    const transportSection = section(
      "Recorder controls",
      "Off and Continuous are live operator commands, not saved settings. " +
        "Starting the processing graph leaves the recorder safely Off.");
    const transportActions = createElement("div", {
      className: "sound-recorder-settings-transport"
    });
    const offAction = createElement("button", {
      type: "button",
      text: "Off",
      attributes: {
        "data-sound-recorder-action": "off"
      }
    });
    const continuousAction = createElement("button", {
      type: "button",
      text: "Continuous",
      attributes: {
        "data-sound-recorder-action": "continuous"
      }
    });
    const actionStatus = createElement("output", {
      className: "sound-recorder-settings-runtime-status",
      text: typeof onRuntimeAction === "function"
        ? "Recorder transport is ready for an operator command."
        : "Runtime command service is not connected in this view.",
      attributes: {
        "data-sound-recorder-runtime-status": "idle",
        "aria-live": "polite"
      }
    });
    offAction.disabled = typeof onRuntimeAction !== "function";
    continuousAction.disabled =
      typeof onRuntimeAction !== "function";

    function setRuntimeStatus(status) {
      const state = status && typeof status === "object"
        ? status.state
        : "";
      const message = status && typeof status === "object"
        ? status.message
        : status;
      actionStatus.setAttribute(
        "data-sound-recorder-runtime-status",
        state || "idle");
      actionStatus.textContent = message
        ? String(message)
        : "Recorder transport status is unavailable.";
    }

    function invokeRuntimeAction(action) {
      if (disposed || typeof onRuntimeAction !== "function") return;
      setRuntimeStatus({
        state: "pending",
        message:
          action === "continuous"
            ? "Requesting Continuous recording..."
            : "Requesting recorder Off..."
      });
      let result;
      try {
        result = onRuntimeAction(action);
      }
      catch (error) {
        setRuntimeStatus({
          state: "error",
          message: error.message || String(error)
        });
        reportError(error);
        return;
      }
      Promise.resolve(result)
        .then((status) => {
          if (!disposed) {
            setRuntimeStatus(status || {
              state: action,
              message:
                action === "continuous"
                  ? "Recorder command applied: Continuous."
                  : "Recorder command applied: Off."
            });
          }
        })
        .catch((error) => {
          if (!disposed) {
            setRuntimeStatus({
              state: "error",
              message: error.message || String(error)
            });
            reportError(error);
          }
        });
    }
    offAction.addEventListener(
      "click",
      () => invokeRuntimeAction("off"));
    continuousAction.addEventListener(
      "click",
      () => invokeRuntimeAction("continuous"));
    transportActions.append(offAction, continuousAction);
    transportSection.append(
      transportActions,
      actionStatus,
      createElement("p", {
        className: "sound-recorder-settings-boundary-note",
        text:
          "Automatic Cycle, Continuous + Buffer, and trigger-controlled " +
          "transport are not implemented in the current runtime."
      }));
    controlPanel.append(transportSection);

    const startupSection = section(
      "PAMGuard Startup Options",
      "These Java-compatible choices are saved portably. The current web " +
        "runtime still starts Off; only manual Off and Continuous are live.");
    const operationControls = new Map();
    const operationGroup = createElement("div", {
      className: "sound-recorder-settings-operation",
      attributes: {
        role: "radiogroup",
        "aria-label": "PAMGuard startup option"
      }
    });
    for (const [mode, label] of OPERATION_MODES) {
      const control = createElement("input", {
        type: "radio",
        attributes: {
          name: `sound-recorder-operation-${instance}`,
          value: mode,
          "data-setting-pointer": "/operationMode"
        }
      });
      control.value = mode;
      control.checked = draft.operationMode === mode;
      const choice = createElement("label");
      choice.append(control, createElement("span", { text: label }));
      operationGroup.append(choice);
      operationControls.set(mode, control);
    }
    startupSection.append(operationGroup);
    controlPanel.append(startupSection);

    const bufferSection = section(
      "Audio buffer",
      "Buffer settings are stored for PAMGuard parity. Buffered recording " +
        "and trigger pre-roll are not implemented by the current runtime.");
    const enableBuffer = checkboxControl(
      draft.enableBuffer,
      "/enableBuffer");
    const bufferLength = numberControl(
      draft.bufferLengthSeconds,
      "/bufferLengthSeconds",
      { min: 0 });
    const updateBuffer = () => {
      bufferLength.disabled = !enableBuffer.checked;
    };
    enableBuffer.addEventListener("change", updateBuffer);
    updateBuffer();
    bufferSection.append(
      checkLabel("Enable Buffer", enableBuffer),
      field("Buffer length", bufferLength, { unit: "s" }));
    controlPanel.append(bufferSection);

    const cycleSection = section(
      "Automatic recordings duty cycle settings",
      "The total cycle time is measured from the start of one recording " +
        "to the start of the next.");
    const autoDuration = numberControl(
      draft.autoDurationSeconds,
      "/autoDurationSeconds",
      { min: 1 });
    const autoInterval = numberControl(
      draft.autoIntervalSeconds,
      "/autoIntervalSeconds",
      { min: 1 });
    cycleSection.append(
      field("Recording length", autoDuration, { unit: "s" }),
      field("Total Cycle Time", autoInterval, { unit: "s" }));
    controlPanel.append(cycleSection);

    const filesPanel = panels.get("files");
    const formatSection = section(
      "Output file location, names and format",
      "File names automatically contain a UTC date and time. The output " +
        "folder is deployment state and is not stored in the portable project.");
    formatSection.append(boundary(
      "Output Folder",
      String(outputFolderLabel),
      "host-output-folder"));
    const fileInitials = textControl(
      draft.fileInitials,
      "/fileInitials");
    const fileChoices = [
      ["WAVE", "WAVE", true],
      ["AIFF", "AIFF (Java portable; runtime unavailable)", false],
      ["AU", "AU (Java portable; runtime unavailable)", false]
    ];
    if (!fileChoices.some(
      ([value]) => value === draft.fileType)) {
      fileChoices.unshift([
        draft.fileType,
        `${draft.fileType} (portable; runtime availability unknown)`,
        false
      ]);
    }
    const fileType = selectControl(
      draft.fileType,
      "/fileType",
      fileChoices);
    const bitDepth = selectControl(
      draft.bitDepth,
      "/bitDepth",
      BIT_DEPTHS.map(
        (depth) => [depth, `${depth} bit`, true]));
    const datedSubFolders = checkboxControl(
      draft.datedSubFolders,
      "/datedSubFolders");
    formatSection.append(
      field("File name prefix", fileInitials),
      field("File type", fileType, {
        help:
          "The current C++ recorder writes WAVE. Other Java host formats " +
          "remain visible as an explicit portable boundary."
      }),
      field("Bit depth", bitDepth),
      checkLabel(
        "Store in sub folders by date",
        datedSubFolders));
    filesPanel.append(formatSection);

    const lengthSection = section("Maximum file lengths");
    const limitSeconds = checkboxControl(
      draft.limitLengthSeconds,
      "/limitLengthSeconds");
    const maxSeconds = numberControl(
      draft.maxLengthSeconds,
      "/maxLengthSeconds",
      { min: 1 });
    const roundStarts = checkboxControl(
      draft.roundFileStarts,
      "/roundFileStarts");
    const limitMegaBytes = checkboxControl(
      draft.limitLengthMegaBytes,
      "/limitLengthMegaBytes");
    const maxMegaBytes = numberControl(
      draft.maxLengthMegaBytes,
      "/maxLengthMegaBytes",
      { min: 1 });
    const updateLengthControls = () => {
      maxSeconds.disabled = !limitSeconds.checked;
      roundStarts.disabled = !limitSeconds.checked;
      maxMegaBytes.disabled = !limitMegaBytes.checked;
    };
    limitSeconds.addEventListener(
      "change",
      updateLengthControls);
    limitMegaBytes.addEventListener(
      "change",
      updateLengthControls);
    updateLengthControls();
    lengthSection.append(
      checkLabel("Limit file length to", limitSeconds),
      field("Maximum length", maxSeconds, { unit: "Seconds" }),
      checkLabel(
        "Round file start times",
        roundStarts,
        "File start times are rounded to rigidly fixed times."),
      checkLabel("Limit file size to", limitMegaBytes),
      field("Maximum size", maxMegaBytes, { unit: "Mega Bytes" }));
    filesPanel.append(lengthSection);

    const triggerPanel = panels.get("triggers");
    const triggerIntro = section(
      "Triggered recordings",
      "Trigger policies belong to eligible upstream detector modules. " +
        "They are shown here when the graph supplies them; they cannot be " +
        "invented or renamed in the recorder dialog.");
    triggerIntro.append(createElement("p", {
      className: "sound-recorder-settings-boundary-note",
      text:
        "Trigger-controlled recording is not implemented in the current " +
        "runtime. PAMGuard Click Detector clicks are not a default clip or " +
        "recorder trigger source."
    }));
    triggerPanel.append(triggerIntro);

    const triggerViews = [];
    if (draft.triggerPolicies.length === 0) {
      triggerPanel.append(createElement("p", {
        className: "sound-recorder-settings-empty",
        text: "No eligible trigger policies are connected."
      }));
    }
    draft.triggerPolicies.forEach((policy, index) => {
      const prefix = `/triggerPolicies/${index}`;
      const policySection = section(policy.triggerName);
      policySection.setAttribute(
        "data-sound-recorder-trigger-policy",
        String(index));
      const triggerName = textControl(
        policy.triggerName,
        `${prefix}/triggerName`,
        { readOnly: true });
      const enabled = checkboxControl(
        policy.enabled,
        `${prefix}/enabled`);
      const secondsBefore = numberControl(
        policy.secondsBeforeTrigger,
        `${prefix}/secondsBeforeTrigger`,
        { min: 0, step: "any" });
      const secondsAfter = numberControl(
        policy.secondsAfterTrigger,
        `${prefix}/secondsAfterTrigger`,
        { min: 0, step: "any" });
      const minDetections = numberControl(
        policy.minDetectionCount,
        `${prefix}/minDetectionCount`,
        { min: 1 });
      const countSeconds = numberControl(
        policy.countSeconds,
        `${prefix}/countSeconds`,
        { min: 0 });
      const minGap = numberControl(
        policy.minGapBetweenTriggersSeconds,
        `${prefix}/minGapBetweenTriggersSeconds`,
        { min: 0 });
      const maxLength = numberControl(
        policy.maxTotalTriggerLengthSeconds,
        `${prefix}/maxTotalTriggerLengthSeconds`,
        { min: 0 });
      const dayBudget = numberControl(
        policy.dayBudgetMegaBytes,
        `${prefix}/dayBudgetMegaBytes`,
        { min: 0 });
      const lastStart = numberControl(
        policy.lastTriggerStartUnixMs,
        `${prefix}/lastTriggerStartUnixMs`,
        { min: 0 });
      const lastEnd = numberControl(
        policy.lastTriggerEndUnixMs,
        `${prefix}/lastTriggerEndUnixMs`,
        { min: 0 });
      const usedBudget = numberControl(
        policy.usedDayBudgetBytes,
        `${prefix}/usedDayBudgetBytes`,
        { min: 0 });
      triggerName.readOnly = true;
      lastStart.readOnly = true;
      lastEnd.readOnly = true;
      usedBudget.readOnly = true;
      const remainingBudget = createElement("output", {
        className: "sound-recorder-settings-budget",
        attributes: {
          "data-sound-recorder-trigger-budget": String(index)
        }
      });
      const updateBudget = () => {
        const budgetBytes =
          Number(dayBudget.value) * 1024 * 1024;
        const remainingBytes =
          Math.max(0, budgetBytes - Number(usedBudget.value));
        remainingBudget.textContent =
          Number(dayBudget.value) === 0
            ? "Unlimited"
            : `${(remainingBytes / 1024 / 1024).toFixed(2)} MB remaining`;
      };
      dayBudget.addEventListener("change", updateBudget);
      const resetBudget = createElement("button", {
        type: "button",
        text: "Reset used budget",
        attributes: {
          "data-sound-recorder-action": "reset-trigger-budget",
          "data-sound-recorder-trigger-index": index
        }
      });
      resetBudget.addEventListener("click", () => {
        usedBudget.value = "0";
        updateBudget();
      });
      updateBudget();

      const conditions = createElement("div", {
        className: "sound-recorder-settings-trigger-grid"
      });
      conditions.append(
        field("Trigger source", triggerName, {
          help: "Owned by the connected detector capability."
        }),
        checkLabel("Enable this trigger", enabled),
        field("Minimum number of detections", minDetections),
        field("Count integration time", countSeconds, { unit: "s" }),
        field("Seconds to record before trigger", secondsBefore, {
          unit: "s"
        }),
        field("Seconds to record after trigger", secondsAfter, {
          unit: "s"
        }),
        field("Daily data budget", dayBudget, {
          unit: "MBytes (0 = no limit)"
        }),
        field("Remaining data budget", remainingBudget),
        resetBudget,
        field("Max single triggered recording length", maxLength, {
          unit: "s (0 = no limit)"
        }),
        field("Min interval between recordings", minGap, {
          unit: "s"
        }));

      const stateDetails = createElement("details", {
        className: "sound-recorder-settings-trigger-state"
      });
      stateDetails.append(
        createElement("summary", {
          text: "Persisted trigger bookkeeping"
        }),
        field("Last trigger start (Unix ms)", lastStart, {
          help: "Read-only Java persisted decision state."
        }),
        field("Last trigger end (Unix ms)", lastEnd, {
          help: "Read-only Java persisted decision state."
        }),
        field("Used daily budget (bytes)", usedBudget, {
          help: "Use Reset used budget to clear this value."
        }));
      policySection.append(conditions, stateDetails);
      triggerPanel.append(policySection);
      triggerViews.push({
        triggerName,
        enabled,
        secondsBefore,
        secondsAfter,
        minDetections,
        countSeconds,
        minGap,
        maxLength,
        dayBudget,
        lastStart,
        lastEnd,
        usedBudget
      });
    });

    container.append(root);

    function selectedOperationMode() {
      for (const [mode, control] of operationControls) {
        if (control.checked) return mode;
      }
      throw new Error(
        "Select one Sound Recorder startup option");
    }

    function selectedChannelBitmap() {
      let bitmap = 0;
      for (const { channel, control } of channelControls) {
        if (control.checked) bitmap += 2 ** channel;
      }
      return bitmap;
    }

    return {
      collect() {
        const result = {
          operationMode: selectedOperationMode(),
          channelBitmap: selectedChannelBitmap(),
          bitDepth: Number(bitDepth.value),
          enableBuffer: enableBuffer.checked,
          bufferLengthSeconds: Number(bufferLength.value),
          fileInitials: fileInitials.value,
          fileType: fileType.value,
          autoIntervalSeconds: Number(autoInterval.value),
          autoDurationSeconds: Number(autoDuration.value),
          limitLengthSeconds: limitSeconds.checked,
          maxLengthSeconds: Number(maxSeconds.value),
          roundFileStarts: roundStarts.checked,
          limitLengthMegaBytes: limitMegaBytes.checked,
          maxLengthMegaBytes: Number(maxMegaBytes.value),
          datedSubFolders: datedSubFolders.checked,
          triggerPolicies: triggerViews.map((view) => ({
            triggerName: view.triggerName.value,
            enabled: view.enabled.checked,
            secondsBeforeTrigger: Number(view.secondsBefore.value),
            secondsAfterTrigger: Number(view.secondsAfter.value),
            minDetectionCount: Number(view.minDetections.value),
            countSeconds: Number(view.countSeconds.value),
            minGapBetweenTriggersSeconds: Number(view.minGap.value),
            maxTotalTriggerLengthSeconds: Number(view.maxLength.value),
            dayBudgetMegaBytes: Number(view.dayBudget.value),
            lastTriggerStartUnixMs: Number(view.lastStart.value),
            lastTriggerEndUnixMs: Number(view.lastEnd.value),
            usedDayBudgetBytes: Number(view.usedBudget.value)
          }))
        };
        return clone(canonicalSettings(result));
      },
      setRuntimeStatus,
      focus() {
        channelControls[0]?.control?.focus?.();
      },
      cleanup() {
        disposed = true;
      }
    };
  }

  globalThis.PamguardProjectSoundRecorderSettings =
    Object.freeze({
      mountEditor,
      canonicalSettings,
      defaultSettings
    });
})();
