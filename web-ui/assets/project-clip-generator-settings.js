(() => {
  "use strict";

  const MAX_TRIGGER_POLICIES = 1024;
  const MAX_PREFIX_BYTES = 256;
  const MAX_JAVA_INT = 2147483647;
  const UNIT_ID_PATTERN =
    /^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$/;
  const OUTPUT_ROLE_PATTERN =
    /^[a-z][A-Za-z0-9]{0,63}$/;
  const STORAGE_OPTIONS = Object.freeze([
    "wav-files",
    "binary",
    "both"
  ]);
  const CHANNEL_SELECTIONS = Object.freeze([
    [
      "detection-channels-only",
      "Detection channels only"
    ],
    [
      "first-detection-channel-only",
      "First detection channel only"
    ],
    [
      "all-channels",
      "All channels"
    ]
  ]);
  const JAVA_AUTHORITY = Object.freeze({
    version: "2.02.18e",
    commit: "dca55c81ef6f1498a8a3b926c69e7182afb915ee",
    settingsClass: "clipgenerator.ClipSettings",
    triggerPolicyClass: "clipgenerator.ClipGenSetting",
    dialogClass: "clipgenerator.ClipDialog",
    triggerDialogClass: "clipgenerator.ClipGenSettingDialog"
  });
  const SETTINGS_FIELDS = Object.freeze([
    "storageMode",
    "datedSubFolders",
    "triggerPolicies"
  ]);
  const POLICY_FIELDS = Object.freeze([
    "triggerSource",
    "enabled",
    "secondsBeforeTrigger",
    "secondsAfterTrigger",
    "channelSelection",
    "clipPrefix",
    "useDataBudget",
    "dataBudgetKilobytes",
    "budgetPeriodHours"
  ]);
  const SOURCE_FIELDS = Object.freeze([
    "unitId",
    "outputRole"
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
      for (const [name, value] of Object.entries(
          options.attributes)) {
        if (value !== null && value !== undefined) {
          element.setAttribute(name, String(value));
        }
      }
    }
    return element;
  }

  function ensureStylesheet() {
    if (document.querySelector(
      "link[data-pamguard-project-clip-generator-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-clip-generator-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL(
          "project-clip-generator-settings.css",
          capturedScriptSource).href
      : "/assets/project-clip-generator-settings.css";
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

  function utf8Length(value) {
    if (typeof TextEncoder !== "undefined") {
      return new TextEncoder().encode(value).length;
    }
    return unescape(encodeURIComponent(value)).length;
  }

  function requireExactFields(value, expected, label) {
    if (!value || typeof value !== "object" ||
        Array.isArray(value)) {
      throw new Error(`${label} must be an object`);
    }
    const actual = Object.keys(value);
    if (actual.length !== expected.length ||
        actual.some((name) => !expected.includes(name))) {
      throw new Error(
        `${label} must contain exactly: ${expected.join(", ")}`);
    }
  }

  function booleanValue(value, label) {
    if (typeof value !== "boolean") {
      throw new Error(`${label} must be a boolean`);
    }
    return value;
  }

  function finiteNumber(value, label, options = {}) {
    if (options.strict && typeof value !== "number") {
      throw new Error(`${label} must be a number`);
    }
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

  function defaultSettings() {
    return {
      storageMode: "binary",
      datedSubFolders: true,
      triggerPolicies: []
    };
  }

  function canonicalTriggerSource(value, label = "Trigger source") {
    requireExactFields(value, SOURCE_FIELDS, label);
    if (typeof value.unitId !== "string" ||
        !UNIT_ID_PATTERN.test(value.unitId)) {
      throw new Error(
        `${label} unitId is not a stable project entity id`);
    }
    if (typeof value.outputRole !== "string" ||
        !OUTPUT_ROLE_PATTERN.test(value.outputRole)) {
      throw new Error(
        `${label} outputRole is not a lower-camel project role id`);
    }
    return {
      unitId: value.unitId,
      outputRole: value.outputRole
    };
  }

  function sourceKey(value) {
    const source = value?.triggerSource || value;
    return `${source?.unitId || ""}|${source?.outputRole || ""}`;
  }

  function defaultTriggerPolicy(triggerSource, options = {}) {
    return {
      triggerSource: canonicalTriggerSource(
        triggerSource,
        "Default trigger source"),
      enabled: true,
      secondsBeforeTrigger: 0,
      secondsAfterTrigger: 0,
      channelSelection: "detection-channels-only",
      clipPrefix: null,
      useDataBudget: !options.spectrogramMark,
      dataBudgetKilobytes: 10 * 1024,
      budgetPeriodHours: 24
    };
  }

  function canonicalTriggerPolicy(value, index = 0) {
    const label = `Trigger policy ${index}`;
    requireExactFields(value, POLICY_FIELDS, label);
    const channelSelection = value.channelSelection;
    if (!CHANNEL_SELECTIONS.some(
        ([stored]) => stored === channelSelection)) {
      throw new Error(
        `${label} channelSelection has an invalid value`);
    }
    if (value.clipPrefix !== null &&
        typeof value.clipPrefix !== "string") {
      throw new Error(
        `${label} clipPrefix must be a string or null`);
    }
    if (typeof value.clipPrefix === "string" &&
        utf8Length(value.clipPrefix) > MAX_PREFIX_BYTES) {
      throw new Error(
        `${label} clipPrefix must contain at most ` +
          `${MAX_PREFIX_BYTES} UTF-8 bytes`);
    }
    return {
      triggerSource: canonicalTriggerSource(
        value.triggerSource,
        `${label} triggerSource`),
      enabled: booleanValue(value.enabled, `${label} enabled`),
      secondsBeforeTrigger: finiteNumber(
        value.secondsBeforeTrigger,
        `${label} secondsBeforeTrigger`,
        { min: 0, strict: true }),
      secondsAfterTrigger: finiteNumber(
        value.secondsAfterTrigger,
        `${label} secondsAfterTrigger`,
        { min: 0, strict: true }),
      channelSelection,
      clipPrefix: value.clipPrefix,
      useDataBudget: booleanValue(
        value.useDataBudget,
        `${label} useDataBudget`),
      dataBudgetKilobytes: finiteNumber(
        value.dataBudgetKilobytes,
        `${label} dataBudgetKilobytes`,
        {
          integer: true,
          min: 0,
          max: MAX_JAVA_INT,
          strict: true
        }),
      budgetPeriodHours: finiteNumber(
        value.budgetPeriodHours,
        `${label} budgetPeriodHours`,
        { exclusiveMin: 0, strict: true })
    };
  }

  function canonicalSettings(value) {
    if (value === undefined) return defaultSettings();
    requireExactFields(
      value,
      SETTINGS_FIELDS,
      "Clip Generator settings");
    if (!STORAGE_OPTIONS.includes(value.storageMode)) {
      throw new Error(
        "Clip Generator storageMode must be wav-files, binary, or both");
    }
    if (!Array.isArray(value.triggerPolicies) ||
        value.triggerPolicies.length > MAX_TRIGGER_POLICIES) {
      throw new Error(
        `Clip Generator triggerPolicies must contain at most ` +
          `${MAX_TRIGGER_POLICIES} entries`);
    }
    const triggerPolicies = value.triggerPolicies.map(
      canonicalTriggerPolicy);
    const keys = triggerPolicies.map(
      (policy) => sourceKey(policy.triggerSource));
    if (new Set(keys).size !== keys.length) {
      throw new Error(
        "Clip Generator trigger policy sources must be unique");
    }
    return {
      storageMode: value.storageMode,
      datedSubFolders: booleanValue(
        value.datedSubFolders,
        "Clip Generator datedSubFolders"),
      triggerPolicies
    };
  }

  function descriptorReference(value, index) {
    const source = value?.triggerSource || {
      unitId: value?.unitId,
      outputRole: value?.outputRole
    };
    return canonicalTriggerSource(
      source,
      `Available trigger source ${index}`);
  }

  function normalizeSourceDescriptor(value, index) {
    const triggerSource = descriptorReference(value, index);
    const capabilities = Array.isArray(value?.capabilities)
      ? value.capabilities.map(String)
      : [];
    const typeId = typeof value?.typeId === "string"
      ? value.typeId
      : "";
    const sourceKind =
      typeof value?.sourceKind === "string"
        ? value.sourceKind
        : typeof value?.kind === "string"
        ? value.kind
        : "";
    const clickDetector =
      value?.clickDetector === true ||
      typeId === "pamguard.click-detector";
    const spectrogramMark =
      value?.spectrogramMark === true ||
      sourceKind === "spectrogram-mark" ||
      outputLooksLikeSpectrogramMark(triggerSource.outputRole);
    const advertisedEligible =
      typeof value?.eligible === "boolean"
        ? value.eligible
        : capabilities.includes("clip-trigger");
    const eligible = clickDetector
      ? false
      : spectrogramMark
      ? true
      : advertisedEligible;
    const reason = clickDetector
      ? "PAMGuard explicitly disables Click Detector clip triggering " +
        "because click rates can create excessive clips."
      : spectrogramMark
      ? "Spectrogram marks are eligible; new policies record everything " +
        "without a data budget, matching ClipControl."
      : eligible
      ? "This output advertises the Java-equivalent clip-trigger capability."
      : typeof value?.eligibilityReason === "string"
      ? value.eligibilityReason
      : "This output does not advertise the clip-trigger capability.";
    return {
      triggerSource,
      key: sourceKey(triggerSource),
      name:
        typeof value?.name === "string" && value.name
          ? value.name
          : typeof value?.label === "string" && value.label
          ? value.label
          : `${triggerSource.unitId} · ${triggerSource.outputRole}`,
      typeId,
      capabilities,
      clickDetector,
      spectrogramMark,
      eligible,
      reason
    };
  }

  function outputLooksLikeSpectrogramMark(outputRole) {
    return outputRole === "spectrogramMarks" ||
      outputRole === "spectrogramMark";
  }

  function sourceList(value) {
    const result = typeof value === "function" ? value() : value;
    if (result === undefined || result === null) return [];
    if (!Array.isArray(result)) {
      throw new Error("Clip Generator source provider must return an array");
    }
    return result;
  }

  function section(title, help) {
    const result = createElement("section", {
      className: "clip-generator-settings-section"
    });
    result.append(
      createElement("h3", { text: title }),
      createElement("p", {
        className: "section-help",
        text: help
      }));
    return result;
  }

  function checkbox(text, pointer, checked) {
    const label = createElement("label", {
      className: "clip-generator-settings-check"
    });
    const control = createElement("input", {
      type: "checkbox",
      attributes: {
        "data-setting-pointer": pointer
      }
    });
    control.checked = checked;
    label.append(control, createElement("span", { text }));
    return { label, control };
  }

  function numberControl(value, pointer, attributes = {}) {
    const control = createElement("input", {
      type: "number",
      attributes: {
        "data-setting-pointer": pointer,
        step: "any",
        ...attributes
      }
    });
    control.value = String(value);
    return control;
  }

  function selectControl(value, pointer, choices) {
    const control = createElement("select", {
      attributes: {
        "data-setting-pointer": pointer
      }
    });
    for (const [stored, label] of choices) {
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
      className: "clip-generator-settings-field"
    });
    row.append(
      createElement("span", {
        className: "clip-generator-settings-label",
        text: label
      }),
      control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "clip-generator-settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", {
        className: "clip-generator-settings-help",
        text: options.help
      }));
    }
    return row;
  }

  function mountEditor(options) {
    ensureStylesheet();
    const {
      container,
      settings,
      rawAudioSourceSelect = null,
      getRawAudioSourceName = () => "",
      getAvailableTriggerSources = () => [],
      getBoundTriggerSources,
      boundTriggerSources,
      triggerSourceControl = null,
      reportError = () => {}
    } = options;
    if (!container) {
      throw new Error("Clip Generator settings require a container");
    }

    const initialSettings = canonicalSettings(settings);
    const draft = clone(initialSettings);
    const policyByKey = new Map(
      draft.triggerPolicies.map(
        (policy) => [
          sourceKey(policy.triggerSource),
          clone(policy)
        ]));
    const hasExplicitBindings =
      typeof getBoundTriggerSources === "function" ||
      Object.prototype.hasOwnProperty.call(
        options,
        "boundTriggerSources");
    let candidates = [];
    let candidateByKey = new Map();
    let selectedKeys = new Set(
      draft.triggerPolicies.map(
        (policy) => sourceKey(policy.triggerSource)));
    let activePolicyKey =
      draft.triggerPolicies.length
        ? sourceKey(draft.triggerPolicies[0].triggerSource)
        : null;
    let policyViews = new Map();
    let disposed = false;

    const editor = createElement("div", {
      className: "clip-generator-settings-editor",
      attributes: {
        "data-clip-generator-settings-editor": "",
        "data-java-authority":
          `${JAVA_AUTHORITY.version}@${JAVA_AUTHORITY.commit}`
      }
    });

    const sourceSection = section(
      "Audio Data Source",
      "The raw-audio source is the controlled unit's graph binding. " +
        "It is deliberately not duplicated in portable Clip Generator " +
        "settings.");
    const rawSourceStatus = createElement("div", {
      className: "clip-generator-settings-boundary",
      attributes: {
        role: "status",
        "data-clip-generator-raw-audio-boundary": ""
      }
    });
    sourceSection.append(rawSourceStatus);

    const storageSection = section(
      "Storage options",
      "These executable options reproduce ClipDialog. Storage " +
        "destinations are configured by the host and are not portable " +
        "project settings.");
    const storageChoices = createElement("div", {
      className: "clip-generator-settings-storage"
    });
    const wav = checkbox(
      "Store in WAV files",
      "/storageMode",
      draft.storageMode === "wav-files" ||
        draft.storageMode === "both");
    wav.control.setAttribute(
      "data-clip-generator-storage-option",
      "wav-files");
    const binary = checkbox(
      "Store in binary files",
      "/storageMode",
      draft.storageMode === "binary" ||
        draft.storageMode === "both");
    binary.control.setAttribute(
      "data-clip-generator-storage-option",
      "binary");
    const dated = checkbox(
      "Store data in sub folders by date",
      "/datedSubFolders",
      draft.datedSubFolders);
    storageChoices.append(wav.label, binary.label, dated.label);
    const storageBoundary = createElement("div", {
      className: "clip-generator-settings-boundary",
      attributes: {
        "data-clip-generator-unsupported":
          "annotation-storage"
      }
    });
    storageBoundary.append(
      createElement("strong", {
        text: "Annotation storage is unavailable."
      }),
      createElement("span", {
        text:
          " PAMGuard's dialog path is commented out and ClipProcess " +
          "cannot execute that mode."
      }));
    storageSection.append(storageChoices, storageBoundary);

    const triggerSection = section(
      "Data Triggers",
      "Clip Generator owns these per-source policies. The source " +
        "selection is the same multi-source triggers binding shown by " +
        "the Data Model connection lines.");
    const triggerBoundary = createElement("div", {
      className: "clip-generator-settings-boundary",
      attributes: {
        "data-clip-generator-trigger-ownership": "receiver"
      },
      text:
        "Only outputs advertising the clip-trigger capability may be " +
        "connected. Policies are stored on this Clip Generator, never " +
        "on a detector."
    });
    const triggerHost = createElement("div", {
      className: "clip-generator-settings-trigger-host",
      attributes: {
        "data-clip-generator-trigger-host": ""
      }
    });
    triggerSection.append(triggerBoundary, triggerHost);
    editor.append(sourceSection, storageSection, triggerSection);
    container.append(editor);

    const refreshRawSource = () => {
      const name = String(getRawAudioSourceName() || "").trim();
      rawSourceStatus.textContent = name
        ? `Graph-bound raw audio: ${name}.`
        : "No raw-audio graph source is currently selected.";
      rawSourceStatus.setAttribute(
        "data-state",
        name ? "bound" : "unbound");
    };

    const updateStorageControls = () => {
      dated.control.disabled = !wav.control.checked;
      dated.label.setAttribute(
        "data-disabled",
        dated.control.disabled ? "true" : "false");
    };
    wav.control.addEventListener("change", updateStorageControls);
    binary.control.addEventListener(
      "change",
      updateStorageControls);
    updateStorageControls();

    function sourceProviderValue() {
      return typeof getBoundTriggerSources === "function"
        ? getBoundTriggerSources
        : boundTriggerSources;
    }

    function syncPolicyViews() {
      for (const [key, view] of policyViews) {
        const existing = policyByKey.get(key);
        if (!existing) continue;
        const useDataBudget = view.budgetOn.checked;
        let dataBudgetKilobytes =
          existing.dataBudgetKilobytes;
        let budgetPeriodHours =
          existing.budgetPeriodHours;
        if (useDataBudget) {
          const megabytes = finiteNumber(
            view.dataBudgetMegabytes.value,
            `${view.sourceName} data budget`,
            { min: 0 });
          dataBudgetKilobytes = Math.trunc(megabytes * 1024);
          if (dataBudgetKilobytes > MAX_JAVA_INT) {
            throw new Error(
              `${view.sourceName} data budget exceeds Java's range`);
          }
          budgetPeriodHours = finiteNumber(
            view.budgetPeriodHours.value,
            `${view.sourceName} budget period`,
            { exclusiveMin: 0 });
        }
        policyByKey.set(key, canonicalTriggerPolicy({
          triggerSource: existing.triggerSource,
          enabled: view.enabled.checked,
          secondsBeforeTrigger: finiteNumber(
            view.secondsBeforeTrigger.value,
            `${view.sourceName} time before trigger`,
            { min: 0 }),
          secondsAfterTrigger: finiteNumber(
            view.secondsAfterTrigger.value,
            `${view.sourceName} time after trigger`,
            { min: 0 }),
          channelSelection: view.channelSelection.value,
          clipPrefix:
            view.clipPrefix.value === ""
              ? null
              : view.clipPrefix.value,
          useDataBudget,
          dataBudgetKilobytes,
          budgetPeriodHours
        }, view.index));
      }
    }

    function sourceOrder() {
      return candidates.filter(
        (candidate) => selectedKeys.has(candidate.key));
    }

    function refreshCandidates(options = {}) {
      if (options.sync !== false) syncPolicyViews();
      const available = sourceList(
        getAvailableTriggerSources);
      const next = [];
      const nextByKey = new Map();
      const add = (descriptor) => {
        if (nextByKey.has(descriptor.key)) return;
        nextByKey.set(descriptor.key, descriptor);
        next.push(descriptor);
      };
      available.forEach((source, index) =>
        add(normalizeSourceDescriptor(source, index)));

      const explicitBindings = hasExplicitBindings
        ? sourceList(sourceProviderValue())
        : null;
      if (explicitBindings) {
        selectedKeys = new Set(
          explicitBindings.map((source, index) =>
            sourceKey(descriptorReference(
              source,
              index))));
        explicitBindings.forEach((source, index) => {
          const reference = descriptorReference(source, index);
          const key = sourceKey(reference);
          if (!nextByKey.has(key)) {
            add(normalizeSourceDescriptor({
              ...source,
              triggerSource: reference,
              eligible: source?.eligible !== false,
              eligibilityReason:
                source?.eligibilityReason ||
                "This source is present on the current triggers binding."
            }, next.length));
          }
        });
      }

      for (const policy of policyByKey.values()) {
        const key = sourceKey(policy.triggerSource);
        if (!nextByKey.has(key)) {
          add(normalizeSourceDescriptor({
            triggerSource: policy.triggerSource,
            name:
              `${policy.triggerSource.unitId} · ` +
              policy.triggerSource.outputRole,
            eligible: true,
            eligibilityReason:
              "This saved receiver policy is retained until its graph " +
              "binding is changed."
          }, next.length));
        }
      }

      candidates = next;
      candidateByKey = nextByKey;
      for (const key of selectedKeys) {
        const candidate = candidateByKey.get(key);
        if (candidate?.eligible && !policyByKey.has(key)) {
          policyByKey.set(
            key,
            defaultTriggerPolicy(
              candidate.triggerSource,
              {
                spectrogramMark:
                  candidate.spectrogramMark
              }));
        }
      }
      if (!activePolicyKey ||
          !selectedKeys.has(activePolicyKey) ||
          !candidateByKey.get(activePolicyKey)?.eligible) {
        activePolicyKey = sourceOrder().find(
          (candidate) => candidate.eligible)?.key || null;
      }
      renderTriggers();
    }

    function renderPolicyPanel(candidate, policy, index) {
      const panelId = `clip-generator-policy-${index}`;
      const panel = createElement("section", {
        className: "clip-generator-settings-policy",
        attributes: {
          id: panelId,
          "data-clip-generator-policy": candidate.key,
          "aria-label": `${candidate.name} clip generation settings`
        }
      });
      panel.hidden = activePolicyKey !== candidate.key;
      if (panel.hidden) panel.setAttribute("hidden", "");
      panel.append(createElement("h4", {
        text: `${candidate.name} · Clip Generation`
      }));

      const base = `/triggerPolicies/${index}`;
      const channelSelection = selectControl(
        policy.channelSelection,
        `${base}/channelSelection`,
        CHANNEL_SELECTIONS);
      const secondsBeforeTrigger = numberControl(
        policy.secondsBeforeTrigger,
        `${base}/secondsBeforeTrigger`,
        { min: 0 });
      const secondsAfterTrigger = numberControl(
        policy.secondsAfterTrigger,
        `${base}/secondsAfterTrigger`,
        { min: 0 });
      const clipPrefix = createElement("input", {
        type: "text",
        attributes: {
          "data-setting-pointer": `${base}/clipPrefix`,
          maxlength: MAX_PREFIX_BYTES,
          placeholder: "Default"
        }
      });
      clipPrefix.value = policy.clipPrefix ?? "";
      panel.append(
        field("Channel selection", channelSelection),
        field(
          "Time before trigger",
          secondsBeforeTrigger,
          { unit: "seconds" }),
        field(
          "Time after trigger",
          secondsAfterTrigger,
          { unit: "seconds" }),
        field(
          "File initials",
          clipPrefix,
          {
            help:
              "Used for WAV clips. Empty keeps the default prefix."
          }));

      const budget = createElement("fieldset", {
        className: "clip-generator-settings-budget"
      });
      budget.append(createElement("legend", {
        text: "Data Budget"
      }));
      const budgetModes = createElement("div", {
        className: "clip-generator-settings-budget-modes"
      });
      const budgetOffLabel = createElement("label");
      const budgetOff = createElement("input", {
        type: "radio",
        attributes: {
          name: `clip-budget-${index}`,
          "data-setting-pointer": `${base}/useDataBudget`,
          "data-clip-generator-budget-mode": "off"
        }
      });
      budgetOff.checked = !policy.useDataBudget;
      budgetOffLabel.append(
        budgetOff,
        createElement("span", { text: "Record everything" }));
      const budgetOnLabel = createElement("label");
      const budgetOn = createElement("input", {
        type: "radio",
        attributes: {
          name: `clip-budget-${index}`,
          "data-setting-pointer": `${base}/useDataBudget`,
          "data-clip-generator-budget-mode": "on"
        }
      });
      budgetOn.checked = policy.useDataBudget;
      budgetOnLabel.append(
        budgetOn,
        createElement("span", { text: "Budget data" }));
      budgetModes.append(budgetOffLabel, budgetOnLabel);

      const dataBudgetMegabytes = numberControl(
        policy.dataBudgetKilobytes / 1024,
        `${base}/dataBudgetKilobytes`,
        {
          min: 0,
          max: MAX_JAVA_INT / 1024
        });
      const budgetPeriodHours = numberControl(
        policy.budgetPeriodHours,
        `${base}/budgetPeriodHours`,
        { min: Number.MIN_VALUE });
      const budgetFields = createElement("div", {
        className: "clip-generator-settings-budget-fields"
      });
      budgetFields.append(
        field(
          "Data budget",
          dataBudgetMegabytes,
          {
            unit: "Megabytes",
            help:
              "PAMGuard stores this value as integer kilobytes."
          }),
        field(
          "Budget period",
          budgetPeriodHours,
          { unit: "Hours" }));
      budget.append(budgetModes, budgetFields);
      panel.append(budget);

      const updateBudget = () => {
        if (budgetOn.checked) budgetOff.checked = false;
        if (budgetOff.checked) budgetOn.checked = false;
        if (!budgetOn.checked && !budgetOff.checked) {
          budgetOff.checked = true;
        }
        dataBudgetMegabytes.disabled = !budgetOn.checked;
        budgetPeriodHours.disabled = !budgetOn.checked;
        budgetFields.setAttribute(
          "data-disabled",
          budgetOn.checked ? "false" : "true");
      };
      budgetOn.addEventListener("change", updateBudget);
      budgetOff.addEventListener("change", updateBudget);
      updateBudget();

      return {
        panel,
        view: {
          panelId,
          index,
          sourceName: candidate.name,
          enabled: null,
          channelSelection,
          secondsBeforeTrigger,
          secondsAfterTrigger,
          clipPrefix,
          budgetOn,
          budgetOff,
          dataBudgetMegabytes,
          budgetPeriodHours
        }
      };
    }

    function renderTriggers() {
      policyViews = new Map();
      triggerHost.replaceChildren();
      if (!candidates.length) {
        triggerHost.append(createElement("div", {
          className: "clip-generator-settings-empty",
          attributes: {
            "data-clip-generator-trigger-state": "empty"
          },
          text:
            "No possible trigger data blocks exist in the active " +
            "project. Add a clip-trigger-capable detector or use a " +
            "Spectrogram mark source."
        }));
        return;
      }

      const tableHost = createElement("div", {
        className: "clip-generator-settings-table-host"
      });
      const table = createElement("table", {
        className: "clip-generator-settings-table"
      });
      const head = createElement("thead");
      const heading = createElement("tr");
      for (const label of [
        "Data Name",
        "Trigger source",
        "Enabled",
        "Settings"
      ]) {
        heading.append(createElement("th", {
          text: label,
          attributes: { scope: "col" }
        }));
      }
      head.append(heading);
      const body = createElement("tbody");
      const policyPanels = createElement("div", {
        className: "clip-generator-settings-policy-host"
      });
      let policyIndex = 0;

      for (const candidate of candidates) {
        const row = createElement("tr", {
          attributes: {
            "data-clip-generator-source": candidate.key,
            "data-clip-generator-eligibility":
              candidate.clickDetector
                ? "click-detector-ineligible"
                : candidate.eligible
                ? "eligible"
                : "ineligible",
            "data-clip-generator-source-kind":
              candidate.spectrogramMark
                ? "spectrogram-mark"
                : "data-block"
          }
        });
        const nameCell = createElement("th", {
          attributes: { scope: "row" }
        });
        nameCell.append(
          createElement("strong", { text: candidate.name }),
          createElement("small", {
            text:
              `${candidate.triggerSource.unitId} · ` +
              candidate.triggerSource.outputRole
          }),
          createElement("span", {
            className:
              "clip-generator-settings-eligibility " +
              (candidate.eligible
                ? "is-eligible"
                : "is-ineligible"),
            text: candidate.eligible
              ? "Clip trigger eligible"
              : "Not eligible",
            attributes: {
              title: candidate.reason
            }
          }),
          createElement("small", {
            className: "clip-generator-settings-reason",
            text: candidate.reason
          }));

        const bindingCell = createElement("td");
        const binding = createElement("input", {
          type: "checkbox",
          attributes: {
            "data-clip-generator-binding-source":
              candidate.key,
            "aria-label":
              `Connect ${candidate.name} to Clip Generator triggers`
          }
        });
        binding.checked = selectedKeys.has(candidate.key);
        // A stale invalid binding can only be switched off. An unbound
        // ineligible source can never be switched on.
        binding.disabled =
          !candidate.eligible && !binding.checked;
        binding.addEventListener("change", () => {
          try {
            syncPolicyViews();
            if (binding.checked) {
              selectedKeys.add(candidate.key);
              if (!policyByKey.has(candidate.key)) {
                policyByKey.set(
                  candidate.key,
                  defaultTriggerPolicy(
                    candidate.triggerSource,
                    {
                      spectrogramMark:
                        candidate.spectrogramMark
                    }));
              }
              activePolicyKey = candidate.key;
            }
            else {
              selectedKeys.delete(candidate.key);
              if (activePolicyKey === candidate.key) {
                activePolicyKey = null;
              }
            }
            renderTriggers();
          }
          catch (error) {
            reportError(error);
          }
        });
        bindingCell.append(binding);

        const enabledCell = createElement("td");
        const settingsCell = createElement("td");
        if (candidate.eligible &&
            selectedKeys.has(candidate.key)) {
          const policy =
            policyByKey.get(candidate.key) ||
            defaultTriggerPolicy(
              candidate.triggerSource,
              {
                spectrogramMark:
                  candidate.spectrogramMark
              });
          policyByKey.set(candidate.key, policy);
          const enabled = createElement("input", {
            type: "checkbox",
            attributes: {
              "data-setting-pointer":
                `/triggerPolicies/${policyIndex}/enabled`,
              "aria-label":
                `Enable clips from ${candidate.name}`
            }
          });
          enabled.checked = policy.enabled;
          const settingsButton = createElement("button", {
            type: "button",
            className: "secondary",
            text: "Settings",
            attributes: {
              "data-clip-generator-action":
                "toggle-policy",
              "data-clip-generator-policy-source":
                candidate.key,
              "aria-controls":
                `clip-generator-policy-${policyIndex}`,
              "aria-expanded":
                activePolicyKey === candidate.key
                  ? "true"
                  : "false"
            }
          });
          settingsButton.disabled = !policy.enabled;
          enabled.addEventListener("change", () => {
            try {
              syncPolicyViews();
              settingsButton.disabled = !enabled.checked;
              if (!enabled.checked &&
                  activePolicyKey === candidate.key) {
                activePolicyKey = null;
                renderTriggers();
              }
            }
            catch (error) {
              reportError(error);
            }
          });
          settingsButton.addEventListener("click", () => {
            try {
              syncPolicyViews();
              activePolicyKey =
                activePolicyKey === candidate.key
                  ? null
                  : candidate.key;
              renderTriggers();
            }
            catch (error) {
              reportError(error);
            }
          });
          enabledCell.append(enabled);
          settingsCell.append(settingsButton);
          const policyPanel = renderPolicyPanel(
            candidate,
            policy,
            policyIndex);
          policyPanel.view.enabled = enabled;
          policyViews.set(candidate.key, policyPanel.view);
          policyPanels.append(policyPanel.panel);
          policyIndex++;
        }
        else {
          enabledCell.append(createElement("span", {
            text: "—",
            attributes: { "aria-hidden": "true" }
          }));
          settingsCell.append(createElement("span", {
            text: candidate.eligible
              ? "Connect source first"
              : "Unsupported",
            className: "clip-generator-settings-unavailable"
          }));
        }
        row.append(
          nameCell,
          bindingCell,
          enabledCell,
          settingsCell);
        body.append(row);
      }
      table.append(head, body);
      tableHost.append(table);
      triggerHost.append(tableHost, policyPanels);
    }

    function storageMode() {
      if (wav.control.checked && binary.control.checked) {
        return "both";
      }
      if (wav.control.checked) return "wav-files";
      if (binary.control.checked) return "binary";
      throw new Error("No Clip Generator storage option is selected");
    }

    function collectTriggerSources() {
      syncPolicyViews();
      const result = [];
      for (const candidate of sourceOrder()) {
        if (!candidate.eligible) {
          throw new Error(
            `${candidate.name} cannot trigger Clip Generator: ` +
              candidate.reason);
        }
        result.push(clone(candidate.triggerSource));
      }
      return result;
    }

    function collect() {
      const triggerSources = collectTriggerSources();
      const triggerPolicies = triggerSources.map(
        (source) => {
          const policy = policyByKey.get(sourceKey(source));
          if (!policy) {
            throw new Error(
              "Each Clip Generator trigger binding requires a " +
                "receiver-owned policy");
          }
          return clone(policy);
        });
      return canonicalSettings({
        storageMode: storageMode(),
        datedSubFolders: dated.control.checked,
        triggerPolicies
      });
    }

    const sourceChanged = () => {
      try {
        refreshCandidates();
      }
      catch (error) {
        reportError(error);
      }
    };
    const rawSourceChanged = () => refreshRawSource();
    triggerSourceControl?.addEventListener?.(
      "change",
      sourceChanged);
    rawAudioSourceSelect?.addEventListener?.(
      "change",
      rawSourceChanged);
    refreshRawSource();
    refreshCandidates({ sync: false });

    return {
      collect,
      collectTriggerSources,
      collectConfiguration() {
        return {
          settings: collect(),
          triggerSources: collectTriggerSources()
        };
      },
      refreshSources: sourceChanged,
      focus() {
        wav.control.focus?.();
      },
      cancel() {
        if (!disposed) {
          triggerSourceControl?.removeEventListener?.(
            "change",
            sourceChanged);
          rawAudioSourceSelect?.removeEventListener?.(
            "change",
            rawSourceChanged);
          disposed = true;
        }
        return clone(initialSettings);
      },
      cleanup() {
        if (disposed) return;
        triggerSourceControl?.removeEventListener?.(
          "change",
          sourceChanged);
        rawAudioSourceSelect?.removeEventListener?.(
          "change",
          rawSourceChanged);
        disposed = true;
      }
    };
  }

  globalThis.PamguardProjectClipGeneratorSettings =
    Object.freeze({
      mountEditor,
      canonicalSettings,
      canonicalTriggerPolicy,
      canonicalTriggerSource,
      defaultSettings,
      defaultTriggerPolicy,
      sourceKey,
      JAVA_AUTHORITY,
      POLICY_FIELDS
    });
})();
