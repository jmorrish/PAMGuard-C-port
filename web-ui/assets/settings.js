    // ================= controlled-unit settings ==========================

    let graphSettingsModuleId = "";
    let graphSettingsDraft = null;
    let graphSettingsSectionId = "";
    let graphSettingsAdvancedMode = false;

    const GRAPH_SETTING_SECTIONS = {
      "pamguard.acquisition": [
        { id: "source", name: "Source", description: "Identity and timing of the acquired audio.", keys: ["sourceId", "sampleRateHz", "channelCount"] },
        { id: "calibration", name: "Calibration", description: "Optional channel calibration offsets. Leave empty for uncalibrated audio.", keys: ["calibrationDbOffsetByChannel"] }
      ],
      "pamguard.fft": [
        { id: "transform", name: "FFT", description: "PAMGuard transform length, hop, window, and selected source channels.", keys: ["fftLength", "fftHop", "windowType", "channels"] },
        { id: "click-removal", name: "Click removal", description: "Optional Java-equivalent click suppression applied before windowing.", keys: ["clickRemoval", "clickThreshold", "clickPower"] }
      ],
      "pamguard.decimator": [
        { id: "output", name: "Output", description: "Output sampling and channel selection.", keys: ["outputSampleRateHz", "channelBitmap"] },
        { id: "filter", name: "Anti-alias filter", description: "Full PAMGuard anti-alias filter design and interpolation settings.", keys: ["filter", "interpolation"] }
      ],
      "pamguard.filter": [
        { id: "filter", name: "Filter", description: "PAMGuard IIR filter design and channel selection.", keys: ["type", "band", "order", "highPassFreqHz", "lowPassFreqHz", "channelBitmap"] }
      ],
      "pamguard.spectrogram-noise": [
        { id: "methods", name: "Methods", description: "Enable and order PAMGuard spectrogram noise-reduction stages.", keys: ["medianFilter", "medianFilterLength", "averageSubtraction", "updateConstant", "kernelSmoothing"] },
        { id: "threshold", name: "Threshold", description: "Threshold output and select the final stage.", keys: ["threshold", "thresholdDb", "finalOutput"] }
      ],
      "pamguard.click-detector": [
        { id: "channels", name: "Source & channels", description: "Select detection and trigger channels.", keys: ["channelBitmap", "triggerBitmap", "minTriggerChannels"] },
        { id: "trigger", name: "Trigger", description: "PAMGuard exponential trigger and event-separation controls.", keys: ["thresholdDb", "longFilter", "longFilter2", "shortFilter", "minSep", "maxLength"] },
        { id: "waveform", name: "Waveform", description: "Samples retained before and after each trigger.", keys: ["preSample", "postSample"] },
        { id: "filters", name: "Filters", description: "Pre-filter and trigger-filter designs.", keys: ["preFilter", "triggerFilter"] },
        { id: "noise", name: "Noise & background", description: "Noise sampling and trigger-background publication intervals.", keys: ["sampleNoise", "noiseSampleIntervalSeconds", "storeBackground", "backgroundIntervalMilliseconds"] },
        { id: "output", name: "Output", description: "Optional high-rate diagnostic output.", keys: ["publishTriggerFunction"] }
      ]
    };

    const GRAPH_SETTING_LABELS = {
      sourceId: "Source identity",
      sampleRateHz: "Sample rate (Hz)",
      channelCount: "Number of channels",
      calibrationDbOffsetByChannel: "Calibration offsets by channel (dB)",
      fftLength: "FFT length (samples)",
      fftHop: "FFT hop (samples)",
      windowType: "Window function",
      channels: "FFT channels",
      clickRemoval: "Enable click removal",
      clickThreshold: "Click threshold (standard deviations)",
      clickPower: "Click suppression power",
      outputSampleRateHz: "Output sample rate (Hz)",
      channelBitmap: "Channels",
      triggerBitmap: "Trigger channels",
      filter: "Anti-alias filter",
      interpolation: "Interpolation mode",
      type: "Filter type",
      band: "Filter band",
      order: "Filter order",
      highPassFreqHz: "High-pass frequency (Hz)",
      lowPassFreqHz: "Low-pass frequency (Hz)",
      passBandRippleDb: "Pass-band ripple (dB)",
      medianFilter: "Median filter",
      medianFilterLength: "Median filter length",
      averageSubtraction: "Average subtraction",
      updateConstant: "Background update constant",
      kernelSmoothing: "Gaussian kernel smoothing",
      threshold: "Enable threshold",
      thresholdDb: "Threshold (dB)",
      finalOutput: "Final output stage",
      minTriggerChannels: "Minimum trigger channels",
      longFilter: "Long filter constant",
      longFilter2: "Second long filter constant",
      shortFilter: "Short filter constant",
      minSep: "Minimum click separation (samples)",
      maxLength: "Maximum click length (samples)",
      preSample: "Pre-trigger samples",
      postSample: "Post-trigger samples",
      preFilter: "Pre-filter",
      triggerFilter: "Trigger filter",
      sampleNoise: "Sample click noise",
      noiseSampleIntervalSeconds: "Noise sample interval (seconds)",
      storeBackground: "Store trigger background",
      backgroundIntervalMilliseconds: "Background interval (ms)",
      publishTriggerFunction: "Publish trigger function"
    };

    function graphResolveSchema(schema, rootSchema) {
      if (!schema?.$ref) return schema || {};
      const prefix = "#/$defs/";
      if (schema.$ref.startsWith(prefix)) {
        return rootSchema?.$defs?.[schema.$ref.slice(prefix.length)] || {};
      }
      return schema;
    }

    function graphInferSchema(value) {
      if (Array.isArray(value)) {
        return {
          type: "array",
          items: value.length
            ? graphInferSchema(value[0])
            : { type: "number" }
        };
      }
      if (value !== null && typeof value === "object") {
        return {
          type: "object",
          properties: Object.fromEntries(Object.entries(value).map(
            ([key, item]) => [key, graphInferSchema(item)]))
        };
      }
      return { type: typeof value };
    }

    function graphGetPath(root, path) {
      return path.reduce(
        (value, part) => value === undefined ? undefined : value[part],
        root);
    }

    function graphSetPath(root, path, value) {
      let target = root;
      path.slice(0, -1).forEach((part) => {
        if (!target[part] || typeof target[part] !== "object") {
          target[part] = {};
        }
        target = target[part];
      });
      target[path[path.length - 1]] = value;
    }

    function graphHumanizeSetting(key) {
      if (GRAPH_SETTING_LABELS[key]) return GRAPH_SETTING_LABELS[key];
      return key
        .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
        .replace(/^./, (character) => character.toUpperCase());
    }

    function graphSettingHelp(key, schema) {
      const constraints = [];
      if (schema.minimum !== undefined) {
        constraints.push(`minimum ${schema.minimum}`);
      }
      if (schema.exclusiveMinimum !== undefined) {
        constraints.push(`greater than ${schema.exclusiveMinimum}`);
      }
      if (schema.maximum !== undefined) {
        constraints.push(`maximum ${schema.maximum}`);
      }
      if (key === "channelBitmap" || key === "triggerBitmap") {
        constraints.push("channels are numbered from 0");
      }
      if (key === "channels") {
        constraints.push("select one or more source channels");
      }
      return constraints.join(" · ");
    }

    function graphSettingControl(key, path, schema, value, rootSchema) {
      schema = graphResolveSchema(
        schema && Object.keys(schema).length
          ? schema
          : graphInferSchema(value),
        rootSchema);
      if (schema.type === "object" ||
          (!schema.type && schema.properties)) {
        const fieldset = document.createElement("fieldset");
        fieldset.className = "graph-setting-object";
        const legend = document.createElement("legend");
        legend.textContent = graphHumanizeSetting(key);
        const grid = document.createElement("div");
        grid.className = "graph-settings-grid";
        for (const [childKey, childSchema] of Object.entries(
          schema.properties || {})) {
          grid.append(graphSettingControl(
            childKey,
            [...path, childKey],
            childSchema,
            value?.[childKey],
            rootSchema));
        }
        fieldset.append(legend, grid);
        return fieldset;
      }

      const field = document.createElement("div");
      field.className = "graph-setting-field";
      const labelText = graphHumanizeSetting(key);
      const pathValue = JSON.stringify(path);

      if (key === "channelBitmap" || key === "triggerBitmap" ||
          key === "channels") {
        field.classList.add("full");
        const label = document.createElement("div");
        label.className = "field-label";
        label.textContent = labelText;
        const picker = document.createElement("div");
        picker.className = "graph-channel-grid";
        picker.dataset.settingPath = pathValue;
        picker.dataset.settingKind =
          key === "channels" ? "channels" : "bitmap";
        const bitmap = Number(value ?? 0) >>> 0;
        const selected = new Set(
          Array.isArray(value) ? value.map(Number) : []);
        for (let channel = 0; channel < 32; channel++) {
          const item = document.createElement("label");
          const check = document.createElement("input");
          check.type = "checkbox";
          check.dataset.channel = String(channel);
          check.checked = key === "channels"
            ? selected.has(channel)
            : Boolean(bitmap & (2 ** channel));
          item.append(check, String(channel));
          picker.append(item);
        }
        field.append(label, picker);
      }
      else if (schema.type === "boolean") {
        const label = document.createElement("label");
        label.className = "graph-setting-check";
        const input = document.createElement("input");
        input.type = "checkbox";
        input.checked = Boolean(value);
        input.dataset.settingPath = pathValue;
        input.dataset.settingKind = "boolean";
        label.append(input, labelText);
        field.append(label);
      }
      else {
        const label = document.createElement("label");
        label.textContent = labelText;
        let input;
        if (Array.isArray(schema.enum)) {
          input = document.createElement("select");
          schema.enum.forEach((option) =>
            input.add(new Option(String(option), String(option))));
          input.value = String(value ?? schema.enum[0] ?? "");
          input.dataset.settingKind = "string";
        }
        else if (key === "type" &&
                 ["none", "butterworth", "chebyshev"].includes(
                   String(value))) {
          input = document.createElement("select");
          [
            ["None", "none"],
            ["Butterworth", "butterworth"],
            ["Chebyshev", "chebyshev"]
          ].forEach(([name, option]) =>
            input.add(new Option(name, option)));
          input.value = String(value ?? "none");
          input.dataset.settingKind = "string";
        }
        else if (key === "band") {
          input = document.createElement("select");
          [
            ["High pass", "highPass"],
            ["Low pass", "lowPass"],
            ["Band pass", "bandPass"],
            ["Band stop", "bandStop"]
          ].forEach(([name, option]) =>
            input.add(new Option(name, option)));
          input.value = String(value ?? "highPass");
          input.dataset.settingKind = "string";
        }
        else if (key === "interpolation") {
          input = document.createElement("select");
          [
            ["None", "0"],
            ["Linear", "1"],
            ["Quadratic", "2"]
          ].forEach(([name, option]) =>
            input.add(new Option(name, option)));
          input.value = String(value ?? 0);
          input.dataset.settingKind = "integer";
        }
        else if (key === "finalOutput") {
          input = document.createElement("select");
          [
            ["Binary output (0 and 1)", "0"],
            ["Output of preceding stage", "1"],
            ["Raw FFT input", "2"]
          ].forEach(([name, option]) =>
            input.add(new Option(name, option)));
          input.value = String(value ?? 2);
          input.dataset.settingKind = "integer";
        }
        else if (schema.type === "number" ||
                 schema.type === "integer") {
          input = document.createElement("input");
          input.type = "number";
          input.step = schema.type === "integer" ? "1" : "any";
          if (schema.minimum !== undefined) input.min = schema.minimum;
          if (schema.maximum !== undefined) input.max = schema.maximum;
          input.value = value ?? 0;
          input.dataset.settingKind = schema.type;
        }
        else if (schema.type === "array" &&
                 ["number", "integer", "string"].includes(
                   schema.items?.type)) {
          input = document.createElement("input");
          input.value = Array.isArray(value) ? value.join(", ") : "";
          input.placeholder = "Comma-separated values";
          input.dataset.settingKind = "array";
          input.dataset.itemKind = schema.items.type;
        }
        else if (schema.type === "object" ||
                 schema.type === "array") {
          input = document.createElement("textarea");
          input.value = JSON.stringify(value ?? (
            schema.type === "array" ? [] : {}), null, 2);
          input.dataset.settingKind = "json";
          field.classList.add("full");
        }
        else {
          input = document.createElement("input");
          input.value = value ?? "";
          input.dataset.settingKind = "string";
        }
        input.dataset.settingPath = pathValue;
        label.append(input);
        field.append(label);
      }
      const helpText = graphSettingHelp(key, schema);
      if (helpText) {
        const help = document.createElement("p");
        help.className = "hint";
        help.textContent = helpText;
        field.append(help);
      }
      return field;
    }

    function graphSettingsSections(descriptor, settings) {
      const configured = GRAPH_SETTING_SECTIONS[descriptor.id];
      const propertyKeys = Object.keys(
        descriptor.settingsSchema?.properties || settings || {});
      if (!configured) {
        return [{
          id: "settings",
          name: "Settings",
          description: descriptor.description,
          keys: propertyKeys
        }];
      }
      const used = new Set(configured.flatMap(
        (section) => section.keys));
      const remaining = propertyKeys.filter((key) => !used.has(key));
      return remaining.length
        ? [...configured, {
          id: "other",
          name: "Other settings",
          description: "Additional settings exposed by this module.",
          keys: remaining
        }]
        : configured;
    }

    function renderGraphSettingsDialog() {
      const module = graphDraft.modules.find(
        (candidate) => candidate.id === graphSettingsModuleId);
      const descriptor = module ? graphType(module.typeId) : null;
      if (!module || !descriptor) return;
      $("graphSettingsTitle").textContent = module.name;
      $("graphSettingsSubtitle").textContent =
        `${descriptor.name} · ${descriptor.category} · draft configuration`;
      const nav = $("graphSettingsNav");
      const content = $("graphSettingsContent");
      nav.replaceChildren();
      content.replaceChildren();
      $("graphSettingsAdvanced").textContent =
        graphSettingsAdvancedMode ? "Guided settings" : "Advanced JSON";
      if (graphSettingsAdvancedMode) {
        const advancedButton = document.createElement("button");
        advancedButton.className = "active";
        advancedButton.textContent = "Advanced JSON";
        nav.append(advancedButton);
        const section = document.createElement("section");
        section.className = "graph-settings-section";
        const heading = document.createElement("h4");
        heading.textContent = "Advanced JSON";
        const description = document.createElement("p");
        description.textContent =
          "The complete module settings object. Server validation still runs before Apply.";
        const textarea = document.createElement("textarea");
        textarea.id = "graphAdvancedJson";
        textarea.className = "graph-advanced-json";
        textarea.spellcheck = false;
        textarea.value = JSON.stringify(graphSettingsDraft, null, 2);
        section.append(heading, description, textarea);
        content.append(section);
        return;
      }
      const sections = graphSettingsSections(
        descriptor,
        graphSettingsDraft);
      if (!sections.some(
          (section) => section.id === graphSettingsSectionId)) {
        graphSettingsSectionId = sections[0]?.id || "settings";
      }
      for (const section of sections) {
        const button = document.createElement("button");
        button.textContent = section.name;
        button.classList.toggle(
          "active",
          section.id === graphSettingsSectionId);
        button.addEventListener("click", () => {
          try {
            graphSettingsDraft = graphReadSettingsForm();
          }
          catch (error) {
            graphSetValidation(`Settings error: ${error.message}`, false);
            return;
          }
          graphSettingsSectionId = section.id;
          renderGraphSettingsDialog();
        });
        nav.append(button);
      }
      const active = sections.find(
        (section) => section.id === graphSettingsSectionId) || sections[0];
      const panel = document.createElement("section");
      panel.className = "graph-settings-section";
      const heading = document.createElement("h4");
      heading.textContent = active?.name || "Settings";
      const description = document.createElement("p");
      description.textContent = active?.description || descriptor.description;
      const grid = document.createElement("div");
      grid.className = "graph-settings-grid";
      for (const key of active?.keys || []) {
        const schema = descriptor.settingsSchema?.properties?.[key] ||
          graphInferSchema(graphSettingsDraft[key]);
        grid.append(graphSettingControl(
          key,
          [key],
          schema,
          graphSettingsDraft[key],
          descriptor.settingsSchema || {}));
      }
      panel.append(heading, description, grid);
      content.append(panel);
    }

    function graphReadSettingsForm() {
      if (graphSettingsAdvancedMode) {
        const parsed = JSON.parse($("graphAdvancedJson").value);
        if (!parsed || Array.isArray(parsed) ||
            typeof parsed !== "object") {
          throw new Error("Advanced settings must be a JSON object");
        }
        return parsed;
      }
      const settings = graphClone(graphSettingsDraft);
      for (const control of $("graphSettingsContent").querySelectorAll(
        "[data-setting-path]")) {
        const path = JSON.parse(control.dataset.settingPath);
        const kind = control.dataset.settingKind;
        let value;
        if (kind === "bitmap" || kind === "channels") {
          const channels = Array.from(control.querySelectorAll(
            "input:checked")).map((input) => Number(input.dataset.channel));
          if (kind === "channels") {
            value = channels;
          }
          else {
            value = channels.reduce(
              (bitmap, channel) => (bitmap + 2 ** channel) >>> 0,
              0);
          }
        }
        else if (kind === "boolean") {
          value = control.checked;
        }
        else if (kind === "integer") {
          value = Number(control.value);
          if (!Number.isInteger(value)) {
            throw new Error(
              `${graphHumanizeSetting(path.at(-1))} must be an integer`);
          }
        }
        else if (kind === "number") {
          value = Number(control.value);
          if (!Number.isFinite(value)) {
            throw new Error(
              `${graphHumanizeSetting(path.at(-1))} must be a number`);
          }
        }
        else if (kind === "array") {
          const values = control.value.trim()
            ? control.value.split(",").map((item) => item.trim())
            : [];
          value = values.map((item) => {
            if (control.dataset.itemKind === "integer") {
              const parsed = Number(item);
              if (!Number.isInteger(parsed)) {
                throw new Error(
                  `${graphHumanizeSetting(path.at(-1))} contains a non-integer`);
              }
              return parsed;
            }
            if (control.dataset.itemKind === "number") {
              const parsed = Number(item);
              if (!Number.isFinite(parsed)) {
                throw new Error(
                  `${graphHumanizeSetting(path.at(-1))} contains a non-number`);
              }
              return parsed;
            }
            return item;
          });
        }
        else if (kind === "json") {
          value = JSON.parse(control.value);
        }
        else {
          value = control.value;
        }
        graphSetPath(settings, path, value);
      }
      return settings;
    }

    function openGraphSettings(moduleId) {
      const module = graphDraft?.modules.find(
        (candidate) => candidate.id === moduleId);
      const descriptor = module ? graphType(module.typeId) : null;
      if (!module || !descriptor) return;
      hideGraphContextMenu();
      graphSettingsModuleId = moduleId;
      graphSettingsDraft = graphClone(module.settings || {});
      graphSettingsAdvancedMode = false;
      graphSettingsSectionId =
        graphSettingsSections(descriptor, graphSettingsDraft)[0]?.id ||
        "settings";
      renderGraphSettingsDialog();
      $("graphSettingsDialog").showModal();
    }

    function closeGraphSettings() {
      if ($("graphSettingsDialog").open) {
        $("graphSettingsDialog").close();
      }
      graphSettingsModuleId = "";
      graphSettingsDraft = null;
    }

    function saveGraphSettings() {
      const module = graphDraft?.modules.find(
        (candidate) => candidate.id === graphSettingsModuleId);
      if (!module) return;
      try {
        graphSettingsDraft = graphReadSettingsForm();
        graphRecordUndo();
        module.settings = graphClone(graphSettingsDraft);
        graphMarkDirty();
        closeGraphSettings();
        renderGraphEditor();
        graphSetValidation(
          `${module.name} settings saved to the draft. Validate and Apply to make them authoritative.`,
          null);
      }
      catch (error) {
        graphSetValidation(`Settings error: ${error.message}`, false);
      }
    }

    function createSettingsController() {
      let mounted = false;
      return Object.freeze({
        mount() {
          if (mounted) {
            throw new Error(
              "Settings controller is already mounted");
          }
          mounted = true;
        },
        dispose() {
          if (!mounted) return;
          mounted = false;
          closeGraphSettings();
          $("graphSettingsNav").replaceChildren();
          $("graphSettingsContent").replaceChildren();
          $("graphInspectorContent").replaceChildren();
        }
      });
    }

    globalThis.PamguardModules = Object.freeze({
      ...(globalThis.PamguardModules || {}),
      settings: Object.freeze({
        create: createSettingsController
      })
    });
