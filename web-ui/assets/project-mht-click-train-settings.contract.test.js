"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

class FakeEvent {
  constructor(type) {
    this.type = type;
  }
}
globalThis.Event = FakeEvent;

function matches(element, selector) {
  const match = selector.match(
    /^([a-zA-Z0-9-]+)?\[([a-zA-Z0-9-]+)(?:=['"]?([^'"]*)['"]?)?\]$/);
  if (match) {
    const [, tag, attribute, value] = match;
    if (tag && element.tagName !== tag.toUpperCase()) return false;
    const actual = element.getAttribute(attribute);
    return actual !== null &&
      (value === undefined || actual === value);
  }
  return element.tagName === selector.toUpperCase();
}

class FakeElement {
  constructor(tagName, ownerDocument) {
    this.tagName = String(tagName).toUpperCase();
    this.ownerDocument = ownerDocument;
    this.parentNode = null;
    this.children = [];
    this.attributes = new Map();
    this.listeners = new Map();
    this.className = "";
    this.textContent = "";
    this.type = "";
    this.value = "";
    this.checked = false;
    this.selected = false;
    this.disabled = false;
    this.hidden = false;
  }

  append(...children) {
    for (const child of children) {
      if (child.parentNode) child.remove();
      child.parentNode = this;
      this.children.push(child);
    }
  }

  replaceChildren(...children) {
    this.children.forEach((child) => {
      child.parentNode = null;
    });
    this.children = [];
    this.append(...children);
  }

  remove() {
    if (!this.parentNode) return;
    const index = this.parentNode.children.indexOf(this);
    if (index >= 0) this.parentNode.children.splice(index, 1);
    this.parentNode = null;
  }

  setAttribute(name, value) {
    this.attributes.set(String(name), String(value));
  }

  getAttribute(name) {
    return this.attributes.has(String(name))
      ? this.attributes.get(String(name))
      : null;
  }

  addEventListener(type, callback) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(callback);
  }

  removeEventListener(type, callback) {
    this.listeners.set(
      type,
      (this.listeners.get(type) || [])
        .filter((listener) => listener !== callback));
  }

  dispatchEvent(event) {
    for (const callback of this.listeners.get(event.type) || []) {
      callback.call(this, event);
    }
  }

  querySelectorAll(selector) {
    const result = [];
    const visit = (element) => {
      for (const child of element.children) {
        if (matches(child, selector)) result.push(child);
        visit(child);
      }
    };
    visit(this);
    return result;
  }

  querySelector(selector) {
    return this.querySelectorAll(selector)[0] || null;
  }
}

class FakeDocument {
  constructor() {
    this.currentScript = null;
    this.head = new FakeElement("head", this);
    this.body = new FakeElement("body", this);
  }

  createElement(tag) {
    return new FakeElement(tag, this);
  }

  querySelector(selector) {
    return this.head.querySelector(selector) ||
      this.body.querySelector(selector);
  }
}

globalThis.document = new FakeDocument();
require("./project-mht-click-train-settings.js");

const moduleApi =
  globalThis.PamguardProjectMhtClickTrainSettings;
assert.ok(moduleApi?.mountEditor);
assert.ok(moduleApi?.canonicalSettings);
assert.ok(moduleApi?.defaultSettings);

const defaults = moduleApi.defaultSettings();
const fixture = JSON.parse(fs.readFileSync(
  path.resolve(
    __dirname,
    "../../cpp-engine/tests/fixtures/click-train/settings-defaults.json"),
  "utf8")).portableSettingsDefaults;
assert.deepEqual(
  defaults,
  fixture,
  "browser defaults must be the pinned Java 2.02.18e fixture");
assert.deepEqual(
  moduleApi.canonicalSettings(defaults),
  defaults,
  "Java fixture defaults must canonicalize without changing a value");
assert.deepEqual(defaults.channelGroups, [1]);
assert.deepEqual(defaults.kernel, {
  nHold: 20,
  nPruneback: 4,
  nPrunebackStart: 5,
  maxCoast: 3
});
assert.equal(defaults.chi2.maximumIciSeconds, 0.4);
assert.equal(defaults.classifier.preClassifier.chi2Threshold, 1500);
assert.equal(
  defaults.classifier.spectrumTemplate.spectrum.length,
  30);
assert.equal(defaults.localisation.enabled, false);

assert.throws(
  () => moduleApi.canonicalSettings({
    ...defaults,
    rawJson: {}
  }),
  /must contain exactly/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...defaults,
    channelGroups: [3, 2]
  }),
  /cannot overlap/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...defaults,
    dataSelector: {
      ...defaults.dataSelector,
      includedClickTypes: [101, 101]
    }
  }),
  /must not contain duplicates/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...defaults,
    classifier: {
      ...defaults.classifier,
      preClassifier: {
        ...defaults.classifier.preClassifier,
        minimumSelectedPercentage: 0.1
      }
    }
  }),
  /must remain zero/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...defaults,
    classifier: {
      ...defaults.classifier,
      idi: {
        ...defaults.classifier.idi,
        minimumMedianIdi: 3,
        maximumMedianIdi: 2
      }
    }
  }),
  /median IDI limits must be ordered/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...defaults,
    localisation: {
      ...defaults.localisation,
      enabled: true
    }
  }),
  /not implemented/);

function allElements(root) {
  const result = [];
  const visit = (element) => {
    result.push(element);
    element.children.forEach(visit);
  };
  visit(root);
  return result;
}

function atAttribute(root, attribute, value) {
  return allElements(root).find(
    (element) => element.getAttribute(attribute) === value);
}

function allAtPointer(root, pointer) {
  return allElements(root).filter(
    (element) =>
      element.getAttribute("data-setting-pointer") === pointer);
}

function atPointer(root, pointer) {
  return allAtPointer(root, pointer)[0];
}

function atAction(root, action) {
  return atAttribute(
    root,
    "data-mht-click-train-action",
    action);
}

const settings = moduleApi.defaultSettings();
settings.channelGroups = [3, 12];
settings.dataSelector.enabled = true;
settings.dataSelector.useEchoes = false;
settings.dataSelector.includedClickTypes = [0, 101];
settings.kernel = {
  nHold: 31,
  nPruneback: 6,
  nPrunebackStart: 8,
  maxCoast: 4
};
settings.chi2.maximumIciSeconds = 0.75;
settings.chi2.coastPenalty = 11;
settings.chi2.newTrackPenalty = 51;
settings.chi2.newTrackClicks = 4;
settings.chi2.longTrackExponent = 0.2;
settings.chi2.lowIciExponent = 0.3;
settings.chi2.electricalNoiseFilter = {
  enabled: true,
  minimumChi2: 0.00002,
  dataUnits: 44
};
settings.chi2.variables.idi.minimumIdiSeconds = 0.001;
settings.chi2.variables.amplitude.jumpEnabled = true;
settings.chi2.variables.amplitude.maximumJumpDb = 12;
settings.chi2.variables.bearing.jumpEnabled = true;
settings.chi2.variables.bearing.jumpDirection = "both";
settings.chi2.variables.correlation.enabled = false;
settings.classifier.runClassifier = true;
settings.classifier.preClassifier = {
  chi2Threshold: 900,
  minimumClicks: 7,
  minimumSelectedPercentage: 0,
  minimumTimeSeconds: 0.25,
  speciesFlag: 4
};
settings.classifier.idi = {
  enabled: true,
  useMedianIdi: true,
  minimumMedianIdi: 0.01,
  maximumMedianIdi: 1.2,
  useMeanIdi: true,
  minimumMeanIdi: 0.02,
  maximumMeanIdi: 1.3,
  useStdIdi: true,
  minimumStdIdi: 0.03,
  maximumStdIdi: 0.8,
  speciesFlag: 5
};
settings.classifier.bearing = {
  enabled: true,
  minimumBearingRadians: -0.25,
  maximumBearingRadians: 1.25,
  useMean: true,
  minimumMeanDerivative: -0.002,
  maximumMeanDerivative: 0.003,
  useMedian: true,
  minimumMedianDerivative: -0.004,
  maximumMedianDerivative: 0.005,
  useStd: false,
  minimumStdDerivative: 0,
  maximumStdDerivative: 0.04,
  speciesFlag: 6
};
settings.classifier.spectrumTemplate = {
  enabled: true,
  name: "Operator spectrum",
  sampleRateHz: 96000,
  spectrum: [0.125, 0.5, 1, 0.25],
  correlationThreshold: 0.62,
  speciesFlag: 7
};
settings.localisation.minimumDataUnits = 25;
settings.localisation.minimumAngleRangeRadians = 0.75;
moduleApi.canonicalSettings(settings);

let availableGroups = [3, 12];
const sourceSelect = document.createElement("select");
const container = document.createElement("div");
document.body.append(container);
const reportedErrors = [];
const editor = moduleApi.mountEditor({
  container,
  settings,
  sourceSelect,
  getAvailableChannelGroups: () => availableGroups,
  reportError: (error) => reportedErrors.push(error)
});

for (const pointer of [
  "/channelGroups/3",
  "/channelGroups/12",
  "/dataSelector/enabled",
  "/dataSelector/useEchoes",
  "/dataSelector/minimumAmplitudeDb",
  "/dataSelector/includedClickTypes/0",
  "/dataSelector/includedClickTypes/1",
  "/algorithm",
  "/kernel/nHold",
  "/kernel/nPruneback",
  "/kernel/nPrunebackStart",
  "/kernel/maxCoast",
  "/chi2/maximumIciSeconds",
  "/chi2/coastPenalty",
  "/chi2/newTrackPenalty",
  "/chi2/newTrackClicks",
  "/chi2/longTrackExponent",
  "/chi2/lowIciExponent",
  "/chi2/electricalNoiseFilter/enabled",
  "/chi2/electricalNoiseFilter/minimumChi2",
  "/chi2/electricalNoiseFilter/dataUnits",
  "/chi2/variables/idi/enabled",
  "/chi2/variables/idi/error",
  "/chi2/variables/idi/minimumError",
  "/chi2/variables/idi/minimumIdiSeconds",
  "/chi2/variables/amplitude/jumpEnabled",
  "/chi2/variables/amplitude/maximumJumpDb",
  "/chi2/variables/bearing/errorRadians",
  "/chi2/variables/bearing/minimumErrorRadians",
  "/chi2/variables/bearing/jumpEnabled",
  "/chi2/variables/bearing/maximumJumpRadians",
  "/chi2/variables/bearing/jumpDirection",
  "/chi2/variables/correlation/enabled",
  "/chi2/variables/timeDelay/error",
  "/chi2/variables/length/minimumError",
  "/chi2/variables/peakFrequency/enabled",
  "/classifier/runClassifier",
  "/classifier/preClassifier/chi2Threshold",
  "/classifier/preClassifier/minimumClicks",
  "/classifier/preClassifier/minimumSelectedPercentage",
  "/classifier/preClassifier/minimumTimeSeconds",
  "/classifier/preClassifier/speciesFlag",
  "/classifier/idi/enabled",
  "/classifier/idi/useMedianIdi",
  "/classifier/idi/minimumMedianIdi",
  "/classifier/idi/maximumMedianIdi",
  "/classifier/idi/useMeanIdi",
  "/classifier/idi/useStdIdi",
  "/classifier/idi/speciesFlag",
  "/classifier/bearing/enabled",
  "/classifier/bearing/minimumBearingRadians",
  "/classifier/bearing/maximumBearingRadians",
  "/classifier/bearing/useMean",
  "/classifier/bearing/useMedian",
  "/classifier/bearing/useStd",
  "/classifier/bearing/speciesFlag",
  "/classifier/spectrumTemplate/enabled",
  "/classifier/spectrumTemplate/name",
  "/classifier/spectrumTemplate/sampleRateHz",
  "/classifier/spectrumTemplate/spectrum/0",
  "/classifier/spectrumTemplate/spectrum/3",
  "/classifier/spectrumTemplate/correlationThreshold",
  "/classifier/spectrumTemplate/speciesFlag",
  "/localisation/enabled",
  "/localisation/minimumDataUnits",
  "/localisation/minimumAngleRangeRadians"
]) {
  assert.ok(atPointer(container, pointer), `Missing ${pointer}`);
}

assert.equal(
  allElements(container).some(
    (element) => element.tagName === "TEXTAREA"),
  false,
  "MHT settings must never use textarea/raw JSON");
const text = allElements(container)
  .map((element) => element.textContent)
  .join(" ");
assert.match(text, /Detector/);
assert.match(text, /Pre Classifier/);
assert.match(text, /Species Classifiers/);
assert.match(text, /MHT Kernel Settings/);
assert.match(text, /χ² Calculation Settings/);
assert.match(text, /Detection Selector/);
assert.match(text, /Click Train Localisation/);
assert.doesNotMatch(text, /settings JSON|raw JSON/i);

const detectorPanel = atAttribute(
  container,
  "data-mht-click-train-panel",
  "detector");
const prePanel = atAttribute(
  container,
  "data-mht-click-train-panel",
  "pre-classifier");
const speciesPanel = atAttribute(
  container,
  "data-mht-click-train-panel",
  "species-classifiers");
assert.equal(detectorPanel.hidden, false);
assert.equal(prePanel.hidden, true);
assert.equal(speciesPanel.hidden, true);
atAttribute(
  container,
  "data-mht-click-train-tab",
  "pre-classifier").dispatchEvent(new Event("click"));
assert.equal(detectorPanel.hidden, true);
assert.equal(prePanel.hidden, false);
atAttribute(
  container,
  "data-mht-click-train-tab",
  "species-classifiers").dispatchEvent(new Event("click"));
assert.equal(speciesPanel.hidden, false);

assert.equal(
  atAttribute(
    container,
    "data-mht-click-train-conditional",
    "data-selector").hidden,
  false);
const selectorEnabled = atPointer(
  container,
  "/dataSelector/enabled");
selectorEnabled.checked = false;
selectorEnabled.dispatchEvent(new Event("change"));
assert.equal(
  atAttribute(
    container,
    "data-mht-click-train-conditional",
    "data-selector").hidden,
  true);
selectorEnabled.checked = true;
selectorEnabled.dispatchEvent(new Event("change"));

const classifierEnabled = atPointer(
  container,
  "/classifier/runClassifier");
classifierEnabled.checked = false;
classifierEnabled.dispatchEvent(new Event("change"));
assert.equal(
  atAttribute(
    container,
    "data-mht-click-train-conditional",
    "species-classifiers").hidden,
  true);
classifierEnabled.checked = true;
classifierEnabled.dispatchEvent(new Event("change"));

const meanIdiToggle = atPointer(
  container,
  "/classifier/idi/useMeanIdi");
meanIdiToggle.checked = false;
meanIdiToggle.dispatchEvent(new Event("change"));
assert.equal(
  atPointer(
    container,
    "/classifier/idi/minimumMeanIdi").parentNode.parentNode.hidden,
  true);
meanIdiToggle.checked = true;
meanIdiToggle.dispatchEvent(new Event("change"));

assert.deepEqual(
  editor.collect(),
  settings,
  "Every canonical value, including Java radians, must round-trip");
assert.deepEqual(
  settings.kernel,
  {
    nHold: 31,
    nPruneback: 6,
    nPrunebackStart: 8,
    maxCoast: 4
  },
  "mount and collect must not mutate the persisted settings object");

const nHold = atPointer(container, "/kernel/nHold");
nHold.value = "42";
assert.equal(
  settings.kernel.nHold,
  31,
  "edits are draft-only before the outer dialog accepts OK");
assert.equal(editor.collect().kernel.nHold, 42);
nHold.value = "31";

const addSpectrum = atAction(container, "add-spectrum-bin");
const removeSpectrum = atAction(container, "remove-spectrum-bin");
addSpectrum.dispatchEvent(new Event("click"));
assert.equal(
  editor.collect().classifier.spectrumTemplate.spectrum.length,
  5);
assert.equal(
  settings.classifier.spectrumTemplate.spectrum.length,
  4,
  "spectrum edit must remain draft-only");
removeSpectrum.dispatchEvent(new Event("click"));
assert.deepEqual(editor.collect(), settings);

const maxIci = atPointer(container, "/chi2/maximumIciSeconds");
maxIci.value = "0";
assert.throws(() => editor.collect(), /Maximum ICI/);
maxIci.value = "0.75";
const minimumMedian = atPointer(
  container,
  "/classifier/idi/minimumMedianIdi");
minimumMedian.value = "2";
assert.throws(
  () => editor.collect(),
  /median IDI limits must be ordered/);
minimumMedian.value = "0.01";
const firstClickType = atPointer(
  container,
  "/dataSelector/includedClickTypes/0");
firstClickType.value = "101";
assert.throws(
  () => editor.collect(),
  /must not contain duplicates/);
firstClickType.value = "0";
assert.deepEqual(editor.collect(), settings);

availableGroups = [3, 48];
sourceSelect.dispatchEvent(new Event("change"));
assert.ok(atPointer(container, "/channelGroups/48"));
assert.ok(
  atPointer(container, "/channelGroups/12"),
  "saved group remains visible when source metadata changes");
assert.match(
  allElements(container)
    .map((element) => element.textContent)
    .join(" "),
  /saved; not currently advertised/);
assert.deepEqual(editor.collect(), settings);

editor.cleanup();
sourceSelect.dispatchEvent(new Event("change"));
assert.equal(reportedErrors.length, 0);

console.log(
  "MHT Click Train dedicated settings browser contract passed");
