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
    this.files = [];
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
require("./project-ishmael-settings.js");

const api = globalThis.PamguardProjectIshmaelSettings;
assert.ok(api?.mountEditor);
assert.ok(api?.canonicalSettings);
assert.ok(api?.decodeKernelAudio);

const ENERGY = "pamguard.ishmael-energy-sum";
const SGRAM = "pamguard.ishmael-sgram-corr";
const MATCH = "pamguard.ishmael-match-filter";
const commonDefaults = {
  channelBitmap: 0,
  groupingType: "all",
  channelGroups: [],
  threshold: 1,
  minTimeSeconds: 0,
  maxTimeSeconds: 99999,
  refractoryTimeSeconds: 0
};
assert.deepEqual(api.defaultSettings(ENERGY), {
  ...commonDefaults,
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
});
assert.deepEqual(api.defaultSettings(SGRAM), {
  ...commonDefaults,
  segments: [],
  spreadHz: 100,
  useLog: false
});
assert.deepEqual(api.defaultSettings(MATCH), {
  ...commonDefaults,
  kernelFilenameList: [],
  kernelSamples: []
});
assert.throws(
  () => api.defaultSettings("pamguard.not-ishmael"),
  /Unsupported Ishmael settings type/);
assert.throws(
  () => api.canonicalSettings(
    ENERGY,
    { useRatio: true, adaptiveThreshold: true }),
  /mutually exclusive/);
assert.throws(
  () => api.canonicalSettings(
    ENERGY,
    { f0Hz: 2000, f1Hz: 1000 }),
  /Lower frequency bound/);
assert.throws(
  () => api.canonicalSettings(
    SGRAM,
    { segments: [[1, 100, 0, 200]] }),
  /start time/);
assert.throws(
  () => api.canonicalSettings(
    MATCH,
    { kernelFilenameList: ["C:\\kernel.wav"] }),
  /portable basename/);
assert.throws(
  () => api.canonicalSettings(
    MATCH,
    { kernelFilenameList: ["a.wav", "a.wav"] }),
  /duplicates/);
assert.throws(
  () => api.validateReady(
    SGRAM,
    api.defaultSettings(SGRAM)),
  /Select at least one FFT/);

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

function allAtAttribute(root, attribute, value) {
  return allElements(root).filter(
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

function assertJavaSectionOrder(container, typeId) {
  const root = atAttribute(
    container,
    "data-pamguard-ishmael-settings-editor",
    typeId);
  assert.ok(root);
  assert.deepEqual(
    root.children.map((child) =>
      child.getAttribute("data-ishmael-section")),
    ["source", "detector", "peak"]);
}

const sourceSelect = document.createElement("select");
let availableBitmap = 3;
let sourceRate = 48000;
const energyContainer = document.createElement("div");
document.body.append(energyContainer);
const energySettings = {
  channelBitmap: 3,
  groupingType: "user",
  channelGroups: [6, 6],
  threshold: 1.25,
  minTimeSeconds: 0.2,
  maxTimeSeconds: 1.5,
  refractoryTimeSeconds: 0.4,
  f0Hz: 2000,
  f1Hz: 10000,
  ratioF0Hz: 12000,
  ratioF1Hz: 18000,
  useRatio: true,
  adaptiveThreshold: false,
  longFilter: 0.0002,
  useLog: true,
  spikeDecay: 80,
  outputSmoothing: true,
  shortFilter: 0.2
};
const energyEditor = api.mountEditor({
  container: energyContainer,
  typeId: ENERGY,
  settings: energySettings,
  sourceSelect,
  getAvailableChannelBitmap: () => availableBitmap,
  getSourceSampleRate: () => sourceRate
});
assertJavaSectionOrder(energyContainer, ENERGY);
assert.equal(
  atAttribute(
    energyContainer,
    "data-ishmael-source-kind",
    "fft") !== undefined,
  true);
for (const pointer of [
  "/channelBitmap/0",
  "/channelBitmap/1",
  "/groupingType",
  "/channelGroups/0",
  "/channelGroups/1",
  "/f0Hz",
  "/f1Hz",
  "/useRatio",
  "/ratioF0Hz",
  "/ratioF1Hz",
  "/adaptiveThreshold",
  "/longFilter",
  "/spikeDecay",
  "/outputSmoothing",
  "/shortFilter",
  "/useLog",
  "/threshold",
  "/minTimeSeconds",
  "/maxTimeSeconds",
  "/refractoryTimeSeconds"
]) {
  assert.ok(atPointer(energyContainer, pointer), `Missing ${pointer}`);
}
assert.equal(
  allElements(energyContainer).some(
    (element) => element.tagName === "TEXTAREA"),
  false,
  "Ishmael settings must never expose raw JSON");
assert.deepEqual(energyEditor.collect(), energySettings);
const energyText = allElements(energyContainer)
  .map((element) => element.textContent)
  .join(" ");
for (const label of [
  "FFT source channels / sequences",
  "Energy Sum",
  "Lower Frequency Bound",
  "Upper Frequency Bound",
  "Use Energy Ratio",
  "Use Adaptive Threshold",
  "Use Detector Smoothing",
  "Use log scale",
  "Peak Detection",
  "Threshold",
  "Min time over threshold",
  "Max time over threshold",
  "Min IDI"
]) {
  assert.match(energyText, new RegExp(label));
}
assert.doesNotMatch(
  energyText,
  /display scaling|alarm|storage|vertical scale/i);

const ratio = atPointer(energyContainer, "/useRatio");
const adaptive = atPointer(
  energyContainer,
  "/adaptiveThreshold");
assert.equal(
  atPointer(energyContainer, "/ratioF0Hz").disabled,
  false);
assert.equal(
  atPointer(energyContainer, "/longFilter").disabled,
  true);
adaptive.checked = true;
adaptive.dispatchEvent(new Event("change"));
assert.equal(ratio.checked, false);
assert.equal(
  atPointer(energyContainer, "/ratioF0Hz").disabled,
  true);
assert.equal(
  atPointer(energyContainer, "/longFilter").disabled,
  false);
ratio.checked = true;
ratio.dispatchEvent(new Event("change"));
assert.equal(adaptive.checked, false);

const maxTime = atPointer(
  energyContainer,
  "/maxTimeSeconds");
maxTime.value = "0.1";
assert.throws(
  () => energyEditor.collect(),
  /Maximum time/);
maxTime.value = "1.5";

availableBitmap = 1;
sourceRate = 96000;
sourceSelect.dispatchEvent(new Event("change"));
assert.equal(
  atPointer(energyContainer, "/channelBitmap/1"),
  undefined);
assert.match(
  atAttribute(
    energyContainer,
    "data-ishmael-source-summary",
    "").textContent,
  /96,?000 Hz/);
assert.equal(energyEditor.collect().channelBitmap, 1);
energyEditor.cleanup();

availableBitmap = 7;
sourceRate = 192000;
const sgramSource = document.createElement("select");
const sgramContainer = document.createElement("div");
document.body.append(sgramContainer);
const sgramSettings = {
  channelBitmap: 5,
  groupingType: "singles",
  channelGroups: [],
  threshold: 0.75,
  minTimeSeconds: 0.1,
  maxTimeSeconds: 0,
  refractoryTimeSeconds: 0.25,
  segments: [
    [0, 12000, 0.25, 18000],
    [0.25, 18000, 0.5, 14000]
  ],
  spreadHz: 250,
  useLog: true
};
const reported = [];
const sgramEditor = api.mountEditor({
  container: sgramContainer,
  typeId: SGRAM,
  settings: sgramSettings,
  sourceSelect: sgramSource,
  getAvailableChannelBitmap: () => availableBitmap,
  getSourceSampleRate: () => sourceRate,
  reportError: (error) => reported.push(error.message)
});
assertJavaSectionOrder(sgramContainer, SGRAM);
assert.deepEqual(sgramEditor.collect(), sgramSettings);
assert.equal(
  allAtAttribute(
    sgramContainer,
    "data-ishmael-segment-row",
    "0").length,
  1);
assert.ok(atPointer(sgramContainer, "/segments/0/0"));
assert.ok(atPointer(sgramContainer, "/segments/1/3"));
assert.ok(atPointer(sgramContainer, "/spreadHz"));
assert.ok(atPointer(sgramContainer, "/useLog"));
assert.equal(
  allElements(sgramContainer).some(
    (element) => element.tagName === "TEXTAREA"),
  false);
atAttribute(
  sgramContainer,
  "data-ishmael-action",
  "add-segment").dispatchEvent(new Event("click"));
assert.ok(atPointer(sgramContainer, "/segments/2/3"));
assert.equal(sgramEditor.collect().segments.length, 3);
atAttribute(
  sgramContainer,
  "data-ishmael-action",
  "remove-segment-2").dispatchEvent(new Event("click"));
assert.deepEqual(sgramEditor.collect(), sgramSettings);
atPointer(sgramContainer, "/segments/0/2").value = "-1";
assert.throws(
  () => sgramEditor.collect(),
  /Segment 1 t1/);
atPointer(sgramContainer, "/segments/0/2").value = "0.25";
assert.deepEqual(reported, []);
sgramEditor.cleanup();

function makeStereoPcm16Wav(frames) {
  const channels = 2;
  const bits = 16;
  const blockAlign = channels * bits / 8;
  const dataLength = frames.length * blockAlign;
  const buffer = new ArrayBuffer(44 + dataLength);
  const view = new DataView(buffer);
  const ascii = (offset, text) => {
    for (let index = 0; index < text.length; index++) {
      view.setUint8(offset + index, text.charCodeAt(index));
    }
  };
  ascii(0, "RIFF");
  view.setUint32(4, 36 + dataLength, true);
  ascii(8, "WAVE");
  ascii(12, "fmt ");
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, channels, true);
  view.setUint32(24, 48000, true);
  view.setUint32(28, 48000 * blockAlign, true);
  view.setUint16(32, blockAlign, true);
  view.setUint16(34, bits, true);
  ascii(36, "data");
  view.setUint32(40, dataLength, true);
  frames.forEach((frame, index) => {
    view.setInt16(44 + index * blockAlign, frame[0], true);
    view.setInt16(
      44 + index * blockAlign + 2,
      frame[1],
      true);
  });
  return buffer;
}

function makeMonoPcm16Aiff(samples) {
  const soundBytes = samples.length * 2;
  const soundChunkLength = 8 + soundBytes;
  const buffer = new ArrayBuffer(
    12 + 8 + 18 + 8 + soundChunkLength);
  const view = new DataView(buffer);
  const ascii = (offset, text) => {
    for (let index = 0; index < text.length; index++) {
      view.setUint8(offset + index, text.charCodeAt(index));
    }
  };
  ascii(0, "FORM");
  view.setUint32(4, buffer.byteLength - 8, false);
  ascii(8, "AIFF");
  ascii(12, "COMM");
  view.setUint32(16, 18, false);
  view.setUint16(20, 1, false);
  view.setUint32(22, samples.length, false);
  view.setUint16(26, 16, false);
  // 44.1 kHz as an 80-bit IEEE extended value. The decoder intentionally
  // ignores it, matching PAMGuard's matched-filter process.
  view.setUint16(28, 0x400e, false);
  view.setUint32(30, 0xac440000, false);
  view.setUint32(34, 0, false);
  ascii(38, "SSND");
  view.setUint32(42, soundChunkLength, false);
  view.setUint32(46, 0, false);
  view.setUint32(50, 0, false);
  samples.forEach((sample, index) => {
    view.setInt16(54 + index * 2, sample, false);
  });
  return buffer;
}

const decoded = api.decodeKernelAudio(
  makeStereoPcm16Wav([
    [-32768, 1234],
    [0, 2345],
    [32767, 3456]
  ]));
assert.deepEqual(decoded.slice(0, 2), [-1, 0]);
assert.ok(Math.abs(decoded[2] - 32767 / 32768) < 1e-15);
assert.deepEqual(
  api.decodeKernelAudio(
    makeMonoPcm16Aiff([-32768, 0, 16384])),
  [-1, 0, 0.5]);
assert.throws(
  () => api.decodeKernelAudio(new ArrayBuffer(20)),
  /must be WAV, AIFF/);

availableBitmap = 3;
sourceRate = 48000;
const matchSource = document.createElement("select");
const matchContainer = document.createElement("div");
document.body.append(matchContainer);
const matchSettings = {
  channelBitmap: 3,
  groupingType: "all",
  channelGroups: [],
  threshold: 2,
  minTimeSeconds: 0,
  maxTimeSeconds: 99999,
  refractoryTimeSeconds: 0.1,
  kernelFilenameList: ["active.wav", "older.aif"],
  kernelSamples: [-1, 0, 0.5, 0]
};
const matchEditor = api.mountEditor({
  container: matchContainer,
  typeId: MATCH,
  settings: matchSettings,
  sourceSelect: matchSource,
  getAvailableChannelBitmap: () => availableBitmap,
  getSourceSampleRate: () => sourceRate
});
assertJavaSectionOrder(matchContainer, MATCH);
assert.equal(
  atAttribute(
    matchContainer,
    "data-ishmael-source-kind",
    "rawAudio") !== undefined,
  true);
assert.deepEqual(matchEditor.collect(), matchSettings);
assert.ok(atAttribute(
  matchContainer,
  "data-ishmael-kernel-file",
  ""));
assert.ok(atPointer(
  matchContainer,
  "/kernelFilenameList/0"));
assert.equal(
  allElements(matchContainer).some(
    (element) => element.tagName === "TEXTAREA"),
  false);
const matchText = allElements(matchContainer)
  .map((element) => element.textContent)
  .join(" ");
assert.match(matchText, /Raw-audio source channels/);
assert.match(matchText, /Matched filter/);
assert.match(matchText, /Kernel sound file/);
assert.match(matchText, /First-channel waveform/);
assert.match(matchText, /Peak Detection/);

atAttribute(
  matchContainer,
  "data-ishmael-action",
  "remove-kernel-0").dispatchEvent(new Event("click"));
assert.throws(
  () => matchEditor.collect(),
  /Select a kernel sound file/);

async function finishKernelImportContract() {
  const input = atAttribute(
    matchContainer,
    "data-ishmael-kernel-file",
    "");
  input.files = [{
    name: "fresh-kernel.wav",
    arrayBuffer: async () => makeStereoPcm16Wav([
      [-32768, 100],
      [0, 200],
      [16384, 300]
    ])
  }];
  input.dispatchEvent(new Event("change"));
  await new Promise((resolve) => setImmediate(resolve));
  const imported = matchEditor.collect();
  assert.deepEqual(
    imported.kernelFilenameList,
    ["fresh-kernel.wav"]);
  assert.deepEqual(
    imported.kernelSamples,
    [-1, 0, 0.5]);
  assert.match(
    atAttribute(
      matchContainer,
      "data-ishmael-kernel-status",
      "").textContent,
    /3 first-channel samples/);
  matchEditor.cleanup();

  assert.equal(
    document.head.querySelectorAll(
      "link[data-pamguard-project-ishmael-settings]").length,
    1,
    "The dedicated stylesheet must be mounted once");

  console.log(
    "Ishmael dedicated settings browser contract passed");
}

finishKernelImportContract().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
