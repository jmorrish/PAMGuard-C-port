"use strict";

const assert = require("node:assert/strict");

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
require("./project-whistle-moan-settings.js");

const moduleApi = globalThis.PamguardProjectWhistleMoanSettings;
assert.ok(moduleApi?.mountEditor);
assert.deepEqual(moduleApi.canonicalSettings({}), {
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
});
assert.throws(
  () => moduleApi.validateReady(
    moduleApi.canonicalSettings({})),
  /Select at least one detection channel/);
assert.throws(
  () => moduleApi.canonicalSettings({ connectType: 6 }),
  /Connection type must be 4 or 8/);

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

function groupingControl(root, value) {
  return allAtPointer(root, "/groupingType").find(
    (control) => control.value === value);
}

const settings = {
  channelBitmap: 7,
  groupingType: "user",
  channelGroups: [0, 0, 1],
  minFrequencyHz: 2000,
  maxFrequencyHz: 0,
  connectType: 8,
  minLength: 12,
  minPixels: 25,
  keepShapeStubs: true,
  fragmentationMethod: 3,
  maxCrossLength: 6,
  noiseReduction: {
    medianFilter: true,
    medianFilterLength: 63,
    averageSubtraction: true,
    updateConstant: 0.1,
    kernelSmoothing: true,
    threshold: true,
    thresholdDb: 9.5,
    finalOutput: 1
  }
};
let availableBitmap = 7;
let sampleRate = 48000;
const sourceSelect = document.createElement("select");
const container = document.createElement("div");
document.body.append(container);
const editor = moduleApi.mountEditor({
  container,
  settings,
  sourceSelect,
  getAvailableChannelBitmap: () => availableBitmap,
  getSourceSampleRate: () => sampleRate
});

for (const pointer of [
  "/channelBitmap/0",
  "/channelBitmap/1",
  "/channelBitmap/2",
  "/groupingType",
  "/channelGroups/0",
  "/channelGroups/1",
  "/channelGroups/2",
  "/minFrequencyHz",
  "/maxFrequencyHz",
  "/connectType",
  "/minLength",
  "/minPixels",
  "/keepShapeStubs",
  "/fragmentationMethod",
  "/maxCrossLength",
  "/noiseReduction/medianFilter",
  "/noiseReduction/medianFilterLength",
  "/noiseReduction/averageSubtraction",
  "/noiseReduction/updateConstant",
  "/noiseReduction/kernelSmoothing",
  "/noiseReduction/threshold",
  "/noiseReduction/thresholdDb",
  "/noiseReduction/finalOutput"
]) {
  assert.ok(atPointer(container, pointer), `Missing ${pointer}`);
}
assert.equal(
  allElements(container).some(
    (element) => element.tagName === "TEXTAREA"),
  false,
  "Whistle settings must never fall back to generic JSON");

const text = allElements(container)
  .map((element) => element.textContent)
  .join(" ");
assert.match(text, /Detection/);
assert.match(text, /Noise and Thresholding/);
assert.match(text, /Channel\/Sequence list and grouping/);
assert.match(text, /Connections/);
assert.doesNotMatch(
  text,
  /peak detector|colour|recorder|storage|alarm|background interval/i,
  "legacy detector/display/storage preferences leaked into the editor");

const detectionPanel = atAttribute(
  container,
  "data-whistle-panel",
  "detection");
const noisePanel = atAttribute(
  container,
  "data-whistle-panel",
  "noise");
assert.equal(detectionPanel.hidden, false);
assert.equal(noisePanel.hidden, true);
atAttribute(container, "data-whistle-tab", "noise")
  .dispatchEvent(new Event("click"));
assert.equal(detectionPanel.hidden, true);
assert.equal(noisePanel.hidden, false);
atAttribute(container, "data-whistle-tab", "detection")
  .dispatchEvent(new Event("click"));

assert.deepEqual(editor.collect(), settings);
assert.equal(
  atPointer(container, "/maxFrequencyHz").value,
  "0",
  "portable source-Nyquist sentinel must not be materialized");
assert.equal(
  atPointer(container, "/keepShapeStubs").checked,
  false,
  "Java's Remove small stubs checkbox must invert keepShapeStubs");

const allGrouping = groupingControl(container, "all");
const userGrouping = groupingControl(container, "user");
allGrouping.checked = true;
userGrouping.checked = false;
allGrouping.dispatchEvent(new Event("change"));
assert.equal(
  atPointer(container, "/channelGroups/2").disabled,
  true);
let collected = editor.collect();
assert.equal(collected.groupingType, "all");
assert.deepEqual(collected.channelGroups, []);

allGrouping.checked = false;
userGrouping.checked = true;
userGrouping.dispatchEvent(new Event("change"));
atPointer(container, "/channelGroups/2").value = "1";
collected = editor.collect();
assert.equal(collected.groupingType, "user");
assert.deepEqual(collected.channelGroups, [0, 0, 1]);

const fragmentation = atPointer(
  container,
  "/fragmentationMethod");
fragmentation.value = "2";
fragmentation.dispatchEvent(new Event("change"));
assert.equal(
  atPointer(container, "/maxCrossLength").disabled,
  true);
fragmentation.value = "3";
fragmentation.dispatchEvent(new Event("change"));
assert.equal(
  atPointer(container, "/maxCrossLength").disabled,
  false);

const medianToggle = atPointer(
  container,
  "/noiseReduction/medianFilter");
const medianLength = atPointer(
  container,
  "/noiseReduction/medianFilterLength");
medianToggle.checked = false;
medianToggle.dispatchEvent(new Event("change"));
assert.equal(medianLength.disabled, true);
assert.equal(
  atAttribute(
    container,
    "data-whistle-noise-readiness",
    "").getAttribute("data-state"),
  "needs-configuration");
assert.throws(
  () => editor.collect(),
  /requires Median Filter/);
medianToggle.checked = true;
medianToggle.dispatchEvent(new Event("change"));
assert.equal(medianLength.disabled, false);

medianLength.value = "60";
assert.throws(
  () => editor.collect(),
  /Median filter length must be odd/);
medianLength.value = "63";
const updateConstant = atPointer(
  container,
  "/noiseReduction/updateConstant");
updateConstant.value = "0.6";
assert.throws(
  () => editor.collect(),
  /update constant has an invalid value/);
updateConstant.value = "0.1";

const output = atPointer(
  container,
  "/noiseReduction/finalOutput");
assert.deepEqual(
  output.children.map(
    (option) => option.getAttribute("value")),
  ["0", "1", "2"]);
assert.deepEqual(
  output.children.map((option) => option.textContent),
  [
    "Binary output (0's and 1's)",
    "Use the output of the preceeding step",
    "Use the input from the raw FFT data"
  ]);

const removeStubs = atPointer(
  container,
  "/keepShapeStubs");
removeStubs.checked = true;
collected = editor.collect();
assert.equal(collected.keepShapeStubs, false);
removeStubs.checked = false;
assert.deepEqual(editor.collect(), settings);

for (const pointer of [
  "/channelBitmap/0",
  "/channelBitmap/1",
  "/channelBitmap/2"
]) {
  atPointer(container, pointer).checked = false;
}
assert.throws(
  () => editor.collect(),
  /Select at least one detection channel/);

availableBitmap = 3;
sampleRate = 96000;
sourceSelect.dispatchEvent(new Event("change"));
assert.equal(atPointer(container, "/channelBitmap/2"), undefined);
assert.match(
  atAttribute(
    container,
    "data-whistle-source-summary",
    "").textContent,
  /96,000 Hz/);

editor.cleanup();
console.log(
  "Whistle and Moan Detector dedicated settings browser contract passed");
