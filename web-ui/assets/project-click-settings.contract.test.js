"use strict";

const assert = require("node:assert/strict");

class FakeEvent {
  constructor(type, options = {}) {
    this.type = type;
    this.bubbles = Boolean(options.bubbles);
    this.defaultPrevented = false;
  }

  preventDefault() {
    this.defaultPrevented = true;
  }
}

globalThis.Event = FakeEvent;

class FakeClassList {
  constructor(element) {
    this.element = element;
  }

  values() {
    return new Set(
      String(this.element.className || "")
        .split(/\s+/)
        .filter(Boolean));
  }

  write(values) {
    this.element.className = [...values].join(" ");
  }

  toggle(name, force) {
    const values = this.values();
    const add = force === undefined ? !values.has(name) : Boolean(force);
    if (add) values.add(name);
    else values.delete(name);
    this.write(values);
    return add;
  }
}

function matchesSelector(element, selector) {
  const trimmed = selector.trim();
  const attribute = trimmed.match(
    /^([a-zA-Z0-9-]+)\[([a-zA-Z0-9-]+)\]$/);
  if (attribute) {
    return element.tagName === attribute[1].toUpperCase() &&
      element.getAttribute(attribute[2]) !== null;
  }
  return element.tagName === trimmed.toUpperCase();
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
    this.classList = new FakeClassList(this);
    this.textContent = "";
    this._value = "";
    this.checked = false;
    this.disabled = false;
    this.hidden = false;
    this.type = "";
    this.id = "";
    this.tabIndex = 0;
    this.customValidity = "";
  }

  get value() {
    return this._value;
  }

  set value(value) {
    this._value = String(value);
  }

  append(...children) {
    for (const child of children) {
      if (!(child instanceof FakeElement)) {
        throw new Error("Fake DOM only accepts element children");
      }
      if (child.parentNode) {
        const oldIndex = child.parentNode.children.indexOf(child);
        if (oldIndex >= 0) {
          child.parentNode.children.splice(oldIndex, 1);
        }
      }
      child.parentNode = this;
      this.children.push(child);
    }
  }

  insertBefore(child, reference) {
    if (child.parentNode) {
      const oldIndex = child.parentNode.children.indexOf(child);
      if (oldIndex >= 0) child.parentNode.children.splice(oldIndex, 1);
    }
    child.parentNode = this;
    if (reference === null) {
      this.children.push(child);
      return child;
    }
    const index = this.children.indexOf(reference);
    if (index < 0) throw new Error("Reference node is not a child");
    this.children.splice(index, 0, child);
    return child;
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

  removeAttribute(name) {
    this.attributes.delete(String(name));
  }

  addEventListener(type, callback) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(callback);
  }

  dispatchEvent(event) {
    for (const callback of this.listeners.get(event.type) || []) {
      callback.call(this, event);
    }
    return !event.defaultPrevented;
  }

  querySelectorAll(selector) {
    const selectors = selector.split(",").map((part) => part.trim());
    const result = [];
    const visit = (element) => {
      for (const child of element.children) {
        if (selectors.some((part) => matchesSelector(child, part))) {
          result.push(child);
        }
        visit(child);
      }
    };
    visit(this);
    return result;
  }

  querySelector(selector) {
    return this.querySelectorAll(selector)[0] || null;
  }

  setCustomValidity(message) {
    this.customValidity = String(message);
  }

  focus() {
    this.ownerDocument.activeElement = this;
  }
}

class FakeDocument {
  constructor() {
    this.currentScript = null;
    this.activeElement = null;
    this.documentElement = new FakeElement("html", this);
    this.head = new FakeElement("head", this);
    this.body = new FakeElement("body", this);
    this.documentElement.append(this.head, this.body);
  }

  createElement(tagName) {
    return new FakeElement(tagName, this);
  }

  querySelector(selector) {
    if (matchesSelector(this.head, selector)) return this.head;
    if (matchesSelector(this.body, selector)) return this.body;
    return this.documentElement.querySelector(selector);
  }
}

globalThis.document = new FakeDocument();

require("./project-click-settings.js");

const clickSettings = globalThis.PamguardProjectClickSettings;
assert.ok(clickSettings, "Click settings API was not registered");
assert.ok(Object.isFrozen(clickSettings), "Click settings API must be frozen");
assert.equal(typeof clickSettings.mountEditor, "function");

const basicType = {
  name: "Parity basic",
  speciesCode: 1,
  enabled: false,
  discard: true,
  whichSelections: 31,
  band1FreqHz: [6000, 12000],
  band2FreqHz: [12000, 18000],
  band1EnergyDb: [198, 201],
  band2EnergyDb: [190, 193],
  bandEnergyDifferenceDb: 5,
  peakFrequencySearchHz: [3000, 20000],
  peakFrequencyRangeHz: [8500, 9500],
  peakWidthHz: [2500, 3500],
  widthEnergyFraction: 125,
  meanSumRangeHz: [3000, 20000],
  meanSelectionRangeHz: [9500, 10000],
  clickLengthMs: [0.3, 0.38],
  lengthEnergyFraction: -5
};

const sweepType = {
  name: "Parity sweep",
  speciesCode: 2,
  discard: true,
  enabled: false,
  channelChoice: "useMeans",
  restrictLength: false,
  restrictedBins: 256,
  restrictedBinType: "clickStart",
  enableLength: true,
  lengthSmoothing: 5,
  lengthDb: -6,
  lengthMs: [0.1, 3],
  enableEnergyBands: true,
  testEnergyBandHz: [10000, 18000],
  controlEnergyBand0Hz: [1000, 6000],
  controlEnergyBand1Hz: [20000, 24000],
  energyThreshold0Db: 4,
  energyThreshold1Db: 7,
  testAmplitude: true,
  amplitudeRangeDb: [70, 170],
  enableFftFilter: true,
  fftFilter: {
    band: "bandPass",
    lowPassFreqHz: 2000,
    highPassFreqHz: 9000
  },
  enablePeak: true,
  enableWidth: true,
  enableMean: true,
  peakSearchRangeHz: [3000, 20000],
  peakRangeHz: [8500, 9500],
  peakWidthRangeHz: [2500, 3500],
  meanRangeHz: [9000, 11000],
  peakSmoothing: 7,
  peakWidthThresholdDb: 8,
  enableZeroCrossings: true,
  zeroCrossingCount: [2, 20],
  enableSweep: true,
  zeroCrossingSweepKhzPerMs: [-2, 3],
  enableMinCrossCorrelation: true,
  enablePeakCrossCorrelation: true,
  minCorrelation: 0.4,
  correlationFactor: 0,
  enableBearingLimits: true,
  excludeBearingLimits: true,
  bearingLimitsRadians: [-2, 2]
};

const suppliedClassification = {
  runOnline: true,
  mode: "sweep",
  discardUnclassified: true,
  checkAllClassifiers: true,
  amplitudeDbOffsetByChannel: [0, -3.5],
  basicTypes: [basicType],
  sweepTypes: [sweepType]
};

const container = document.createElement("div");
document.body.append(container);
const reportedErrors = [];
const editor = clickSettings.mountEditor({
  container,
  settings: { classification: suppliedClassification },
  reportError: (error) => reportedErrors.push(error)
});

function allElements(root) {
  const result = [];
  const visit = (element) => {
    result.push(element);
    element.children.forEach(visit);
  };
  visit(root);
  return result;
}

function controlsAt(pointer) {
  return allElements(container).filter(
    (element) => element.getAttribute("data-click-setting") === pointer);
}

function controlAt(pointer) {
  const controls = controlsAt(pointer);
  assert.ok(controls.length > 0, `Missing structured control ${pointer}`);
  return controls[0];
}

function assertScalarFields(base, fields) {
  for (const fieldName of fields) {
    controlAt(`${base}/${fieldName}`);
  }
}

function assertPairFields(base, fields) {
  for (const fieldName of fields) {
    controlAt(`${base}/${fieldName}/0`);
    controlAt(`${base}/${fieldName}/1`);
  }
}

assert.equal(
  allElements(container).filter(
    (element) => element.tagName === "TEXTAREA").length,
  0,
  "normal Click settings editor must not expose raw JSON textareas");

const basicBase = "/classification/basicTypes/*";
assertScalarFields(basicBase, [
  "name",
  "speciesCode",
  "enabled",
  "discard",
  "whichSelections",
  "bandEnergyDifferenceDb",
  "widthEnergyFraction",
  "lengthEnergyFraction"
]);
assertPairFields(basicBase, [
  "band1FreqHz",
  "band2FreqHz",
  "band1EnergyDb",
  "band2EnergyDb",
  "peakFrequencySearchHz",
  "peakFrequencyRangeHz",
  "peakWidthHz",
  "meanSumRangeHz",
  "meanSelectionRangeHz",
  "clickLengthMs"
]);

const sweepBase = "/classification/sweepTypes/*";
assertScalarFields(sweepBase, [
  "name",
  "speciesCode",
  "discard",
  "enabled",
  "channelChoice",
  "restrictLength",
  "restrictedBins",
  "restrictedBinType",
  "enableLength",
  "lengthSmoothing",
  "lengthDb",
  "enableEnergyBands",
  "energyThreshold0Db",
  "energyThreshold1Db",
  "testAmplitude",
  "enableFftFilter",
  "enablePeak",
  "enableWidth",
  "enableMean",
  "peakSmoothing",
  "peakWidthThresholdDb",
  "enableZeroCrossings",
  "enableSweep",
  "enableMinCrossCorrelation",
  "enablePeakCrossCorrelation",
  "minCorrelation",
  "correlationFactor",
  "enableBearingLimits",
  "excludeBearingLimits"
]);
assertScalarFields(`${sweepBase}/fftFilter`, [
  "band",
  "lowPassFreqHz",
  "highPassFreqHz"
]);
assertPairFields(sweepBase, [
  "lengthMs",
  "testEnergyBandHz",
  "controlEnergyBand0Hz",
  "controlEnergyBand1Hz",
  "amplitudeRangeDb",
  "peakSearchRangeHz",
  "peakRangeHz",
  "peakWidthRangeHz",
  "meanRangeHz",
  "zeroCrossingCount",
  "zeroCrossingSweepKhzPerMs",
  "bearingLimitsRadians"
]);

const text = allElements(container)
  .map((element) => element.textContent)
  .join(" ");
assert.match(
  text,
  /BasicClickIdentifier does not consult this inherited flag/,
  "Basic enabled Java quirk must be explicit in the editor");
assert.match(
  text,
  /Manual tracked events \/ target motion/,
  "TrackedClickLocaliser manual-event workflow must be explicit");
for (let index = 0; index < 4; index += 1) {
  controlAt(`/localisation/trackedTrain/isSelected/${index}`);
}
assertScalarFields("/localisation/trackedTrain", [
  "maxRangeM",
  "maxHeightM",
  "minHeightM",
  "maxTimeMilliseconds",
  "limitPoints",
  "maxPoints"
]);

const collected = editor.collect();
assert.deepEqual(
  collected.classification,
  suppliedClassification,
  "structured Basic/Sweep controls must round-trip every canonical field");
assert.deepEqual(
  collected.localisation.trackedTrain,
  {
    isSelected: [true, false, false, false],
    maxRangeM: 20000,
    maxHeightM: 5,
    minHeightM: -5000,
    maxTimeMilliseconds: 200,
    limitPoints: false,
    maxPoints: 30
  },
  "ClickLocParams controls must round-trip the Java defaults");
controlAt("/localisation/trackedTrain/isSelected/1").checked = true;
controlAt("/localisation/trackedTrain/maxRangeM").value = "12500";
assert.deepEqual(
  editor.collect().localisation.trackedTrain.isSelected,
  [true, true, false, false],
  "multiple Java group-localiser algorithms must be selectable");
assert.equal(
  editor.collect().localisation.trackedTrain.maxRangeM,
  12500,
  "editable ClickLocParams values must be collected");
assert.equal(reportedErrors.length, 0);

const checkAll = controlAt(
  "/classification/checkAllClassifiers");
const mode = controlAt("/classification/mode");
assert.equal(checkAll.disabled, false);
mode.value = "basic";
mode.dispatchEvent(new Event("change"));
assert.equal(
  checkAll.disabled,
  true,
  "check-all must be visibly Sweep-only");
mode.value = "sweep";
mode.dispatchEvent(new Event("change"));
assert.equal(checkAll.disabled, false);

const enablePeak = controlAt(`${sweepBase}/enablePeak`);
const enableWidth = controlAt(`${sweepBase}/enableWidth`);
const enableMean = controlAt(`${sweepBase}/enableMean`);
const peakSmoothing = controlAt(`${sweepBase}/peakSmoothing`);
const peakThreshold = controlAt(
  `${sweepBase}/peakWidthThresholdDb`);
peakSmoothing.value = "4";
assert.throws(
  () => editor.collect(),
  /Peak smoothing must be odd/,
  "active Sweep spectral tests must enforce Java's odd smoothing rule");
for (const toggle of [enablePeak, enableWidth, enableMean]) {
  toggle.checked = false;
  toggle.dispatchEvent(new Event("change"));
}
peakSmoothing.value = "2";
peakThreshold.value = "0";
const inactiveSpectral = editor.collect()
  .classification.sweepTypes[0];
assert.equal(inactiveSpectral.peakSmoothing, 2);
assert.equal(inactiveSpectral.peakWidthThresholdDb, 0);
assert.equal(
  inactiveSpectral.enablePeak ||
    inactiveSpectral.enableWidth ||
    inactiveSpectral.enableMean,
  false,
  "inactive Java Sweep fields must preserve stale values without validation");

for (const toggle of [enablePeak, enableWidth, enableMean]) {
  toggle.checked = true;
  toggle.dispatchEvent(new Event("change"));
}
peakSmoothing.value = "7";
peakThreshold.value = "8";

const addBasic = allElements(container).find(
  (element) =>
    element.tagName === "BUTTON" &&
    element.textContent === "Add basic type");
assert.ok(addBasic, "Basic classifier list has no add action");
addBasic.dispatchEvent(new Event("click"));
assert.equal(
  controlsAt(`${basicBase}/name`).length,
  2,
  "new Basic entries must render structured controls");
assert.equal(
  allElements(container).some(
    (element) => element.tagName === "TEXTAREA"),
  false);
assert.throws(
  () => editor.collect(),
  /Basic classifier species codes must be unique/,
  "portable classifier identities must be unique within their mode");

console.log("project Click settings contracts passed");
