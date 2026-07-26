(() => {
  "use strict";

  const MAX_CLASSIFIERS = 64;
  const MAX_TEMPLATE_SAMPLES = 1 << 20;
  const MIN_CSV_SAMPLES = 5;
  const NORMALISATIONS = Object.freeze([
    ["0", "peak to peak"],
    ["1", "norm"],
    ["2", "none"]
  ]);
  const CHANNEL_OPTIONS = Object.freeze([
    [
      "0",
      "Require positive identification on all channels individually"
    ],
    [
      "1",
      "Require positive identification on only one channel"
    ]
  ]);
  const RESTRICTED_BIN_OPTIONS = Object.freeze(
    Array.from({ length: 16 }, (_, index) => 2 ** (index + 2)));
  const SMOOTHING_OPTIONS = Object.freeze(
    Array.from({ length: 512 }, (_, index) => index * 2 + 3));
  const PRESET_URL =
    "/assets/matched-template-default-templates.json";
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
    const element = document.createElementNS
      ? document.createElementNS("http://www.w3.org/2000/svg", tag)
      : document.createElement(tag);
    for (const [name, value] of Object.entries(attributes)) {
      element.setAttribute(name, String(value));
    }
    return element;
  }

  function ensureStylesheet() {
    if (document.querySelector(
      "link[data-pamguard-project-matched-template-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-matched-template-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL(
          "project-matched-template-settings.css",
          capturedScriptSource).href
      : "/assets/project-matched-template-settings.css";
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

  function utf8Length(value) {
    if (typeof TextEncoder !== "undefined") {
      return new TextEncoder().encode(value).length;
    }
    return unescape(encodeURIComponent(value)).length;
  }

  function canonicalTemplate(value, label) {
    if (!value || typeof value !== "object" ||
        Array.isArray(value)) {
      throw new Error(`${label} is missing`);
    }
    const name = typeof value.name === "string"
      ? value.name
      : "";
    if (!name || utf8Length(name) > 256) {
      throw new Error(
        `${label} name must contain 1 to 256 UTF-8 bytes`);
    }
    const waveform = value.waveform;
    if (!Array.isArray(waveform) ||
        waveform.length < 1 ||
        waveform.length > MAX_TEMPLATE_SAMPLES) {
      throw new Error(
        `${label} waveform must contain 1 to ` +
          `${MAX_TEMPLATE_SAMPLES} samples`);
    }
    const sampleRateHz = Math.fround(finiteNumber(
      value.sampleRateHz,
      `${label} sample rate`,
      { exclusiveMin: 0 }));
    if (!Number.isFinite(sampleRateHz) || sampleRateHz <= 0) {
      throw new Error(
        `${label} sample rate must fit a finite positive Java float`);
    }
    return {
      name,
      // MatchTemplate.sR is a Java float. Math.fround keeps imported decimal
      // rates on the same branch/cutoff/output-length path as PAMGuard.
      sampleRateHz,
      waveform: waveform.map((sample, index) =>
        finiteNumber(
          sample,
          `${label} waveform sample ${index}`))
    };
  }

  function canonicalClassifier(value, index) {
    if (!value || typeof value !== "object" ||
        Array.isArray(value)) {
      throw new Error(`Template ${index} classifier is missing`);
    }
    return {
      thresholdToAccept: finiteNumber(
        value.thresholdToAccept,
        `Template ${index} match threshold`,
        { min: -5000, max: 5000 }),
      normalisation: finiteNumber(
        value.normalisation,
        `Template ${index} normalisation`,
        { integer: true, min: 0, max: 2 }),
      matchTemplate: canonicalTemplate(
        value.matchTemplate,
        `Template ${index} match template`),
      rejectTemplate: canonicalTemplate(
        value.rejectTemplate,
        `Template ${index} reject template`)
    };
  }

  function canonicalSettings(value = {}) {
    if (!Array.isArray(value.classifiers) ||
        value.classifiers.length < 1 ||
        value.classifiers.length > MAX_CLASSIFIERS) {
      throw new Error(
        `Matched Template requires 1 to ${MAX_CLASSIFIERS} ` +
          "classifier tabs");
    }
    const peakSmoothing = finiteNumber(
      value.peakSmoothing ?? 5,
      "Peak smoothing",
      { integer: true, min: 3, max: 1025 });
    if (peakSmoothing % 2 === 0) {
      throw new Error(
        "Peak smoothing must be an odd value from 3 to 1025");
    }
    const restrictedBins = finiteNumber(
      value.restrictedBins ?? 2048,
      "Restricted samples",
      { integer: true, min: 4, max: 131072 });
    if (!RESTRICTED_BIN_OPTIONS.includes(restrictedBins)) {
      throw new Error(
        "Restricted samples must be a power of two from 4 to 131072");
    }
    const clickType = finiteNumber(
        value.clickType ?? 101,
        "Click type",
        { integer: true, min: 0, max: 255 });
    if (clickType !== 0 &&
        (clickType < 100 || clickType > 255)) {
      throw new Error(
        "Click type must be 100 to 255, or 0 for Java's 256 alias");
    }
    return {
      clickType,
      normalisationType: finiteNumber(
        value.normalisationType ?? 1,
        "Amplitude normalisation",
        { integer: true, min: 0, max: 2 }),
      peakSearch:
        typeof value.peakSearch === "boolean"
          ? value.peakSearch
          : true,
      peakSmoothing,
      lengthDb: finiteNumber(
        value.lengthDb ?? 6,
        "Peak threshold",
        { min: 0.1, max: 300 }),
      restrictedBins,
      channelClassification: finiteNumber(
        value.channelClassification ?? 0,
        "Channel classification",
        { integer: true, min: 0, max: 1 }),
      classifiers: value.classifiers.map(canonicalClassifier)
    };
  }

  function canonicalPresetLibrary(value) {
    const templates = Array.isArray(value)
      ? value
      : value?.templates;
    if (!Array.isArray(templates) || !templates.length) {
      throw new Error(
        "The PAMGuard matched-template preset library is empty");
    }
    return templates.map((template, index) =>
      canonicalTemplate(template, `Preset ${index}`));
  }

  function parseNumericRow(line, label) {
    const values = line.split(",").map((cell, index) => {
      const trimmed = cell.trim();
      if (!trimmed) {
        throw new Error(`${label} value ${index} is empty`);
      }
      return finiteNumber(trimmed, `${label} value ${index}`);
    });
    return values;
  }

  /**
   * PAMGuard portable CSV format:
   *   row 1: comma-separated waveform samples
   *   row 2: template sample rate in Hz
   */
  function parseTemplateCsv(
      text,
      fileName = "Imported template.csv") {
    const lines = String(text)
      .replace(/^\uFEFF/, "")
      .split(/\r?\n/)
      .filter((line) => line.trim().length > 0);
    if (lines.length !== 2) {
      throw new Error(
        "Template CSV must contain exactly two non-empty rows: " +
          "waveform, then sample rate");
    }
    const waveform = parseNumericRow(
      lines[0],
      "Template waveform");
    if (waveform.length < MIN_CSV_SAMPLES ||
        waveform.length > MAX_TEMPLATE_SAMPLES) {
      throw new Error(
        `Template CSV waveform must contain ${MIN_CSV_SAMPLES} to ` +
          `${MAX_TEMPLATE_SAMPLES} samples`);
    }
    const sampleRateRow = parseNumericRow(
      lines[1],
      "Template sample rate");
    const result = {
      name: String(fileName || "Imported template.csv"),
      sampleRateHz: finiteNumber(
        sampleRateRow[0],
        "Template sample rate",
        { exclusiveMin: 0 }),
      waveform
    };
    return canonicalTemplate(result, "Imported template");
  }

  async function loadDefaultPresetLibrary() {
    if (typeof fetch !== "function") {
      throw new Error(
        "This browser cannot load the matched-template preset library");
    }
    const response = await fetch(PRESET_URL);
    if (!response.ok) {
      throw new Error(
        `Preset library request failed (${response.status})`);
    }
    return response.json();
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
      className: "matched-template-settings-field"
    });
    row.append(
      createElement("span", {
        className: "matched-template-settings-label",
        text: label
      }),
      control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "matched-template-settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", {
        className: "matched-template-settings-help",
        text: options.help
      }));
    }
    return row;
  }

  function checkboxLabel(label, control, help = "") {
    const wrapper = createElement("label", {
      className: "matched-template-settings-check"
    });
    wrapper.append(control, createElement("span", { text: label }));
    if (help) {
      wrapper.append(createElement("small", {
        className: "matched-template-settings-help",
        text: help
      }));
    }
    return wrapper;
  }

  function actionButton(label, action, options = {}) {
    return createElement("button", {
      type: "button",
      className: options.className || "secondary",
      text: label,
      attributes: {
        "data-matched-template-action": action,
        title: options.title
      }
    });
  }

  function waveformPoints(waveform) {
    const width = 280;
    const height = 72;
    const count = Math.min(waveform.length, 140);
    if (!count) return "";
    const samples = Array.from({ length: count }, (_, index) => {
      const sourceIndex = count === 1
        ? 0
        : Math.round(
            index * (waveform.length - 1) / (count - 1));
      return waveform[sourceIndex];
    });
    const maximum = Math.max(
      ...samples.map((sample) => Math.abs(sample)),
      Number.EPSILON);
    return samples.map((sample, index) => {
      const x = count === 1
        ? width / 2
        : index * width / (count - 1);
      const y = height / 2 -
        sample / maximum * (height / 2 - 4);
      return `${x.toFixed(2)},${y.toFixed(2)}`;
    }).join(" ");
  }

  function waveformPreview(template) {
    const figure = createElement("figure", {
      className: "matched-template-waveform"
    });
    const svg = createSvgElement("svg", {
      viewBox: "0 0 280 72",
      role: "img",
      "aria-label": `${template.name} waveform preview`
    });
    svg.append(
      createSvgElement("line", {
        x1: 0,
        y1: 36,
        x2: 280,
        y2: 36,
        class: "matched-template-waveform-zero"
      }),
      createSvgElement("polyline", {
        points: waveformPoints(template.waveform),
        class: "matched-template-waveform-line"
      }));
    const durationMs =
      template.waveform.length * 1000 / template.sampleRateHz;
    figure.append(
      svg,
      createElement("figcaption", {
        text:
          `${template.waveform.length.toLocaleString()} samples · ` +
          `${durationMs.toFixed(3)} ms`
      }));
    return figure;
  }

  function newJavaClassifier(presets) {
    const beaked = presets.find(
      (template) => template.name === "Beaked Whale Click");
    const dolphin = presets.find(
      (template) => template.name === "Dolphin Click");
    if (!beaked || !dolphin) {
      throw new Error(
        "The PAMGuard preset library does not contain the Java " +
          "default Beaked Whale and Dolphin templates");
    }
    const matchTemplate = clone(beaked);
    const rejectTemplate = clone(dolphin);
    matchTemplate.name = "Beaked Whale";
    rejectTemplate.name = "Dolphin";
    return {
      thresholdToAccept: 0.01,
      normalisation: 0,
      matchTemplate,
      rejectTemplate
    };
  }

  function mountEditor(options) {
    ensureStylesheet();
    const {
      container,
      settings,
      sourceSelect = null,
      getSourceSampleRate = () => 0,
      loadPresetLibrary = loadDefaultPresetLibrary,
      readFileText = (file) => file.text(),
      reportError = () => {}
    } = options;
    const draft = canonicalSettings(settings);
    let presets = [];
    let activeClassifier = 0;
    let classifierViews = [];
    let presetSelects = [];
    let disposed = false;

    const root = createElement("div", {
      className: "matched-template-settings-editor",
      attributes: {
        "data-pamguard-matched-template-settings-editor": "true"
      }
    });
    const general = createElement("section", {
      className: "matched-template-settings-section"
    });
    general.append(createElement("h4", {
      text: "General Classifier Settings"
    }));
    const channelClassification = selectControl(
      draft.channelClassification,
      "/channelClassification",
      CHANNEL_OPTIONS);
    const clickType = numberControl(
      draft.clickType === 0 ? 256 : draft.clickType,
      "/clickType",
      { min: 100, max: 256, step: 1 });
    general.append(
      field("Channel Options", channelClassification),
      field("Click Type", clickType, {
        help:
          "Portable unsigned view of PAMGuard's Java byte click type."
      }));
    root.append(general);

    const waveformSection = createElement("section", {
      className: "matched-template-settings-section"
    });
    waveformSection.append(createElement("h4", {
      text: "Click Waveform"
    }));
    const peakSearch = pointerControl(
      "input",
      "/peakSearch",
      { type: "checkbox" });
    peakSearch.checked = draft.peakSearch;
    const restrictedBins = selectControl(
      draft.restrictedBins,
      "/restrictedBins",
      RESTRICTED_BIN_OPTIONS.map(
        (value) => [String(value), String(value)]));
    const duration = createElement("span", {
      className: "matched-template-settings-derived",
      attributes: {
        "data-matched-template-restricted-duration": ""
      }
    });
    const restrictionRow = createElement("div", {
      className: "matched-template-settings-restriction"
    });
    restrictionRow.append(
      checkboxLabel(
        "Restrict parameter extraction to",
        peakSearch),
      restrictedBins,
      createElement("span", { text: "samples" }),
      duration);
    const lengthDb = numberControl(
      draft.lengthDb,
      "/lengthDb",
      // Java's editable spinner has min 0.1, initial value 6, and a 0.5
      // increment. HTML anchors step validity at min, which would
      // incorrectly make Java's own default 6 invalid (5.6/6.1 are the
      // adjacent anchored values). Keep the exact range and validate the
      // editable value in collect(), without imposing that HTML-only grid.
      { min: 0.1, max: 300, step: "any" });
    const peakSmoothing = selectControl(
      draft.peakSmoothing,
      "/peakSmoothing",
      SMOOTHING_OPTIONS.map(
        (value) => [String(value), String(value)]));
    const peakRow = createElement("div", {
      className: "matched-template-settings-peak-row"
    });
    peakRow.append(
      field("Peak threshold", lengthDb, { unit: "dB" }),
      field("Smoothing", peakSmoothing, { unit: "bins" }));
    waveformSection.append(restrictionRow, peakRow);
    root.append(waveformSection);

    const normalisationSection = createElement("section", {
      className:
        "matched-template-settings-section " +
        "matched-template-settings-normalisation"
    });
    const normalisationType = selectControl(
      draft.normalisationType,
      "/normalisationType",
      NORMALISATIONS);
    normalisationSection.append(
      field("Amplitude Normalisation", normalisationType, {
        help:
          "Applied to the click and synchronised to every template " +
          "classifier when OK is accepted."
      }));
    root.append(normalisationSection);

    const classifierSection = createElement("section", {
      className:
        "matched-template-settings-section " +
        "matched-template-settings-classifiers"
    });
    const classifierHeading = createElement("div", {
      className: "matched-template-settings-classifier-heading"
    });
    classifierHeading.append(createElement("h4", {
      text: "Click Template Settings"
    }));
    const addClassifier = actionButton(
      "Add template",
      "add-classifier");
    addClassifier.disabled = true;
    classifierHeading.append(addClassifier);
    const presetStatus = createElement("p", {
      className: "section-help",
      text: "Loading PAMGuard template presets…",
      attributes: {
        "data-matched-template-preset-status": "loading"
      }
    });
    const tabs = createElement("div", {
      className: "matched-template-settings-tabs",
      attributes: {
        role: "tablist",
        "aria-label": "Matched template classifiers"
      }
    });
    const panels = createElement("div", {
      className: "matched-template-settings-panels"
    });
    classifierSection.append(
      classifierHeading,
      presetStatus,
      tabs,
      panels);
    root.append(classifierSection);
    container.append(root);

    const updateRestriction = () => {
      const enabled = peakSearch.checked;
      restrictedBins.disabled = !enabled;
      lengthDb.disabled = !enabled;
      peakSmoothing.disabled = !enabled;
      const sampleRate = Number(getSourceSampleRate());
      duration.textContent =
        Number.isFinite(sampleRate) && sampleRate > 0
          ? `(${(
              Number(restrictedBins.value) *
              1000 / sampleRate).toFixed(2)} ms) around click center`
          : "(source sample rate unavailable)";
    };
    peakSearch.addEventListener("change", updateRestriction);
    restrictedBins.addEventListener("change", updateRestriction);
    const sourceChanged = () => updateRestriction();
    sourceSelect?.addEventListener("change", sourceChanged);
    updateRestriction();

    const populatePresetSelect = (select, includeNone) => {
      select.replaceChildren(createElement("option", {
        text: presets.length
          ? "Choose a PAMGuard preset…"
          : "PAMGuard presets unavailable",
        attributes: { value: "" }
      }));
      presets.forEach((template, index) => {
        if (!includeNone && template.name === "None") return;
        select.append(createElement("option", {
          text: template.name,
          attributes: { value: String(index) }
        }));
      });
      select.value = "";
      select.disabled = presets.length === 0;
    };

    const populateAllPresetSelects = () => {
      for (const item of presetSelects) {
        populatePresetSelect(item.select, item.role === "rejectTemplate");
      }
    };

    const syncClassifiers = () => {
      for (const view of classifierViews) {
        const classifier = draft.classifiers[view.index];
        classifier.thresholdToAccept = finiteNumber(
          view.threshold.value,
          `Template ${view.index} match threshold`,
          { min: -5000, max: 5000 });
        for (const item of view.templates) {
          const template = classifier[item.role];
          const name = item.name.value;
          if (!name || utf8Length(name) > 256) {
            throw new Error(
              `Template ${view.index} ${item.label} name must ` +
                "contain 1 to 256 UTF-8 bytes");
          }
          template.name = name;
          template.sampleRateHz = finiteNumber(
            item.sampleRate.value,
            `Template ${view.index} ${item.label} sample rate`,
            { exclusiveMin: 0 });
        }
      }
    };

    const activateClassifier = (index) => {
      activeClassifier = Math.max(
        0,
        Math.min(index, draft.classifiers.length - 1));
      Array.from(tabs.children).forEach((tab, tabIndex) => {
        tab.setAttribute(
          "aria-selected",
          tabIndex === activeClassifier ? "true" : "false");
      });
      Array.from(panels.children).forEach((panel, panelIndex) => {
        panel.hidden = panelIndex !== activeClassifier;
      });
    };

    const importCsv = (
        classifierIndex,
        role,
        text,
        fileName) => {
      syncClassifiers();
      if (!draft.classifiers[classifierIndex] ||
          !["matchTemplate", "rejectTemplate"].includes(role)) {
        throw new Error("Template import target is no longer available");
      }
      draft.classifiers[classifierIndex][role] =
        parseTemplateCsv(text, fileName);
      activeClassifier = classifierIndex;
      renderClassifiers();
    };

    const createTemplatePane = (
        classifierIndex,
        role,
        label) => {
      const template =
        draft.classifiers[classifierIndex][role];
      const pane = createElement("article", {
        className: "matched-template-settings-template",
        attributes: {
          "data-matched-template-role": role
        }
      });
      pane.append(createElement("h5", { text: label }));
      const name = pointerControl(
        "input",
        `/classifiers/${classifierIndex}/${role}/name`,
        {
          type: "text",
          attributes: { maxlength: 256 }
        });
      name.value = template.name;
      const sampleRate = numberControl(
        template.sampleRateHz,
        `/classifiers/${classifierIndex}/${role}/sampleRateHz`,
        { min: Number.MIN_VALUE, step: "any" });
      pane.append(
        field("Name", name),
        field("Sample rate", sampleRate, { unit: "Hz" }),
        waveformPreview(template));

      const importRow = createElement("div", {
        className: "matched-template-settings-import"
      });
      const preset = createElement("select", {
        attributes: {
          "data-matched-template-action": "choose-preset",
          "aria-label": `${label} PAMGuard preset`
        }
      });
      populatePresetSelect(
        preset,
        role === "rejectTemplate");
      presetSelects.push({ select: preset, role });
      preset.addEventListener("change", () => {
        const index = Number(preset.value);
        if (!Number.isInteger(index) || !presets[index]) return;
        try {
          syncClassifiers();
          draft.classifiers[classifierIndex][role] =
            clone(presets[index]);
          activeClassifier = classifierIndex;
          renderClassifiers();
        }
        catch (error) {
          reportError(error);
        }
      });

      const csvInput = createElement("input", {
        type: "file",
        attributes: {
          accept: ".csv,text/csv",
          hidden: "",
          "data-matched-template-action": "csv-file"
        }
      });
      const csvButton = actionButton(
        "Import CSV…",
        "import-csv");
      csvButton.addEventListener("click", () => csvInput.click());
      csvInput.addEventListener("change", async () => {
        const file = csvInput.files?.[0];
        if (!file) return;
        try {
          importCsv(
            classifierIndex,
            role,
            await readFileText(file),
            file.name);
        }
        catch (error) {
          reportError(error);
        }
        finally {
          csvInput.value = "";
        }
      });
      const matButton = actionButton(
        "MAT unavailable",
        "mat-unavailable",
        {
          title:
            "MAT template import is host-only. Use portable CSV " +
            "in the browser."
        });
      matButton.disabled = true;
      importRow.append(preset, csvButton, csvInput, matButton);
      pane.append(
        importRow,
        createElement("small", {
          className: "matched-template-settings-help",
          text:
            "Portable CSV: comma-separated waveform on row 1; " +
            "sample rate in Hz on row 2."
        }));
      return {
        pane,
        view: {
          role,
          label: label.toLowerCase(),
          name,
          sampleRate
        }
      };
    };

    function renderClassifiers() {
      tabs.replaceChildren();
      panels.replaceChildren();
      classifierViews = [];
      presetSelects = [];
      draft.classifiers.forEach((classifier, index) => {
        const tab = createElement("button", {
          type: "button",
          text: `Template ${index}`,
          attributes: {
            role: "tab",
            "data-matched-template-tab": index
          }
        });
        tab.addEventListener(
          "click",
          () => activateClassifier(index));
        tabs.append(tab);

        const panel = createElement("section", {
          className: "matched-template-settings-panel",
          attributes: {
            role: "tabpanel",
            "data-matched-template-panel": index
          }
        });
        const panelHeading = createElement("div", {
          className: "matched-template-settings-panel-heading"
        });
        panelHeading.append(createElement("h5", {
          text: "Click Template Settings"
        }));
        const remove = actionButton(
          "Remove template",
          "remove-classifier");
        remove.disabled = draft.classifiers.length === 1;
        remove.addEventListener("click", () => {
          if (draft.classifiers.length === 1) return;
          try {
            syncClassifiers();
            draft.classifiers.splice(index, 1);
            activeClassifier = Math.min(
              index,
              draft.classifiers.length - 1);
            renderClassifiers();
          }
          catch (error) {
            reportError(error);
          }
        });
        panelHeading.append(remove);
        const threshold = numberControl(
          classifier.thresholdToAccept,
          `/classifiers/${index}/thresholdToAccept`,
          { min: -5000, max: 5000, step: 0.01 });
        panel.append(
          panelHeading,
          field("Match threshold", threshold),
          createElement("h5", {
            className: "matched-template-settings-subheading",
            text: "Click Templates"
          }));
        const templateGrid = createElement("div", {
          className: "matched-template-settings-template-grid"
        });
        const match = createTemplatePane(
          index,
          "matchTemplate",
          "Match Template");
        const reject = createTemplatePane(
          index,
          "rejectTemplate",
          "Reject Template");
        templateGrid.append(match.pane, reject.pane);
        panel.append(templateGrid);
        panels.append(panel);
        classifierViews.push({
          index,
          threshold,
          templates: [match.view, reject.view]
        });
      });
      addClassifier.disabled =
        presets.length === 0 ||
        draft.classifiers.length >= MAX_CLASSIFIERS;
      activateClassifier(activeClassifier);
    }

    addClassifier.addEventListener("click", () => {
      if (draft.classifiers.length >= MAX_CLASSIFIERS ||
          presets.length === 0) {
        return;
      }
      try {
        syncClassifiers();
        draft.classifiers.push(newJavaClassifier(presets));
        activeClassifier = draft.classifiers.length - 1;
        renderClassifiers();
      }
      catch (error) {
        reportError(error);
      }
    });
    renderClassifiers();

    const ready = Promise.resolve()
      .then(() => loadPresetLibrary())
      .then((library) => {
        presets = canonicalPresetLibrary(library);
        if (disposed) return presets;
        presetStatus.textContent =
          `${presets.length} PAMGuard template presets available`;
        presetStatus.setAttribute(
          "data-matched-template-preset-status",
          "ready");
        populateAllPresetSelects();
        addClassifier.disabled =
          draft.classifiers.length >= MAX_CLASSIFIERS;
        return presets;
      })
      .catch((error) => {
        if (!disposed) {
          presetStatus.textContent =
            "PAMGuard template presets could not be loaded. " +
            "Existing templates and CSV import remain available.";
          presetStatus.setAttribute(
            "data-matched-template-preset-status",
            "error");
          reportError(error);
        }
        return [];
      });

    return {
      ready,
      collect() {
        syncClassifiers();
        const normalisation = finiteNumber(
          normalisationType.value,
          "Amplitude normalisation",
          { integer: true, min: 0, max: 2 });
        const result = canonicalSettings({
          clickType: (() => {
            const displayed = finiteNumber(
            clickType.value,
            "Click type",
              { integer: true, min: 100, max: 256 });
            return displayed === 256 ? 0 : displayed;
          })(),
          normalisationType: normalisation,
          peakSearch: peakSearch.checked,
          peakSmoothing: finiteNumber(
            peakSmoothing.value,
            "Peak smoothing",
            { integer: true, min: 3, max: 1025 }),
          lengthDb: finiteNumber(
            lengthDb.value,
            "Peak threshold",
            { min: 0.1, max: 300 }),
          restrictedBins: finiteNumber(
            restrictedBins.value,
            "Restricted samples",
            { integer: true, min: 4, max: 131072 }),
          channelClassification: finiteNumber(
            channelClassification.value,
            "Channel classification",
            { integer: true, min: 0, max: 1 }),
          classifiers: draft.classifiers
        });
        result.classifiers.forEach((classifier) => {
          classifier.normalisation = normalisation;
        });
        return result;
      },
      importCsv,
      focus() {
        channelClassification.focus?.();
      },
      cleanup() {
        disposed = true;
        sourceSelect?.removeEventListener?.(
          "change",
          sourceChanged);
      }
    };
  }

  globalThis.PamguardProjectMatchedTemplateSettings =
    Object.freeze({
      mountEditor,
      canonicalSettings,
      canonicalPresetLibrary,
      parseTemplateCsv
    });
})();
