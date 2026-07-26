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
require("./project-noise-ltsa-settings.js");

const moduleApi = globalThis.PamguardProjectNoiseLtsaSettings;
assert.ok(moduleApi?.mountEditor);
assert.deepEqual(moduleApi.canonicalFftNoiseSettings({}), {
  channelBitmap: 1,
  measurementIntervalSeconds: 60,
  nMeasures: 100,
  useAll: true,
  bands: []
});
assert.deepEqual(moduleApi.canonicalNoiseBandSettings({}), {
  channelBitmap: 1,
  bandType: "thirdOctave",
  filterType: "butterworth",
  iirOrder: 6,
  firOrder: 7,
  firGamma: 2.5,
  outputIntervalSeconds: 10,
  minimumFrequencyHz: 1.7925856629456591,
  maximumFrequencyHz: 1133.6866687924667,
  referenceFrequencyHz: 1000
});
assert.deepEqual(moduleApi.canonicalLtsaSettings({}), {
  channelBitmap: 0,
  intervalSeconds: 60,
  longerFactor: 10
});

const generated = {
  thirdOctave: moduleApi.createStandardFftBands(
    "thirdOctave", 48000, 1024),
  decidecade: moduleApi.createStandardFftBands(
    "decidecade", 48000, 1024),
  octave: moduleApi.createStandardFftBands(
    "octave", 48000, 1024),
  decade: moduleApi.createStandardFftBands(
    "decade", 48000, 1024)
};
assert.deepEqual(
  Object.fromEntries(
    Object.entries(generated).map(([key, value]) =>
      [key, value.length])),
  {
    thirdOctave: 17,
    decidecade: 17,
    octave: 7,
    decade: 2
  },
  "standard controls must translate NoiseControl.createBands exactly");
assert.equal(
  generated.thirdOctave[0].lowFrequencyHz,
  445.4493590701696);
assert.equal(
  generated.decade.at(-1).highFrequencyHz,
  3162.277660168379);

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

function atPointer(root, pointer) {
  return atAttribute(root, "data-setting-pointer", pointer);
}

function action(root, value) {
  return atAttribute(root, "data-noise-ltsa-action", value);
}

const source = document.createElement("select");
let channelBitmap = 3;
let sampleRate = 48000;
let fftLength = 1024;
let fftHop = 512;
const reportedErrors = [];
const commonOptions = {
  sourceSelect: source,
  getAvailableChannelBitmap: () => channelBitmap,
  getSourceSampleRate: () => sampleRate,
  getSourceFftLength: () => fftLength,
  getSourceFftHop: () => fftHop,
  reportError: (error) => reportedErrors.push(error)
};

const fftContainer = document.createElement("div");
document.body.append(fftContainer);
const fftEditor = moduleApi.mountEditor({
  ...commonOptions,
  container: fftContainer,
  typeId: "pamguard.fft-noise-monitor",
  settings: {
    channelBitmap: 3,
    measurementIntervalSeconds: 5,
    nMeasures: 10,
    useAll: false,
    bands: [{
      name: "Operator band",
      lowFrequencyHz: 1000,
      highFrequencyHz: 2000,
      bandType: null
    }]
  }
});
for (const pointer of [
  "/channelBitmap/0",
  "/channelBitmap/1",
  "/measurementIntervalSeconds",
  "/nMeasures",
  "/useAll",
  "/bands/draft/name",
  "/bands/draft/lowFrequencyHz",
  "/bands/draft/highFrequencyHz"
]) {
  assert.ok(atPointer(fftContainer, pointer), `Missing ${pointer}`);
}
assert.equal(
  allElements(fftContainer).some(
    (element) => element.tagName === "TEXTAREA"),
  false,
  "Noise Monitor must never fall back to raw JSON");
assert.deepEqual(fftEditor.collect(), {
  channelBitmap: 3,
  measurementIntervalSeconds: 5,
  nMeasures: 10,
  useAll: false,
  bands: [{
    name: "Operator band",
    lowFrequencyHz: 1000,
    highFrequencyHz: 2000,
    bandType: null
  }]
});

const thirdOctave = atAttribute(
  fftContainer,
  "data-noise-standard-band",
  "thirdOctave");
thirdOctave.checked = true;
thirdOctave.dispatchEvent(new Event("change"));
assert.equal(
  fftEditor.collect().bands.length,
  18,
  "standard bands should replace other standard families and retain custom rows");
const octave = atAttribute(
  fftContainer,
  "data-noise-standard-band",
  "octave");
octave.checked = true;
octave.dispatchEvent(new Event("change"));
assert.equal(thirdOctave.checked, false);
assert.equal(fftEditor.collect().bands.length, 8);
octave.checked = false;
octave.dispatchEvent(new Event("change"));
assert.equal(fftEditor.collect().bands.length, 1);

action(fftContainer, "edit-custom-band-0")
  .dispatchEvent(new Event("click"));
atPointer(fftContainer, "/bands/draft/name").value = "Edited";
atPointer(
  fftContainer,
  "/bands/draft/lowFrequencyHz").value = "1200";
atPointer(
  fftContainer,
  "/bands/draft/highFrequencyHz").value = "2400";
action(fftContainer, "save-custom-band")
  .dispatchEvent(new Event("click"));
assert.deepEqual(fftEditor.collect().bands[0], {
  name: "Edited",
  lowFrequencyHz: 1200,
  highFrequencyHz: 2400,
  bandType: null
});

action(fftContainer, "add-custom-band")
  .dispatchEvent(new Event("click"));
atPointer(fftContainer, "/bands/draft/name").value = "Second";
atPointer(
  fftContainer,
  "/bands/draft/lowFrequencyHz").value = "3000";
atPointer(
  fftContainer,
  "/bands/draft/highFrequencyHz").value = "4000";
action(fftContainer, "save-custom-band")
  .dispatchEvent(new Event("click"));
assert.equal(fftEditor.collect().bands.length, 2);
action(fftContainer, "remove-custom-band-1")
  .dispatchEvent(new Event("click"));
assert.equal(fftEditor.collect().bands.length, 1);

atPointer(fftContainer, "/useAll").checked = true;
atPointer(fftContainer, "/useAll")
  .dispatchEvent(new Event("change"));
assert.equal(
  fftEditor.collect().nMeasures,
  468,
  "Use all FFT data must reproduce Java's automatic measure count");
assert.equal(reportedErrors.length, 0);

const noiseBandContainer = document.createElement("div");
document.body.append(noiseBandContainer);
const noiseBandEditor = moduleApi.mountEditor({
  ...commonOptions,
  container: noiseBandContainer,
  typeId: "pamguard.noise-band-monitor",
  settings: moduleApi.canonicalNoiseBandSettings({})
});
for (const pointer of [
  "/channelBitmap/0",
  "/channelBitmap/1",
  "/outputIntervalSeconds",
  "/bandType",
  "/referenceFrequencyHz",
  "/maximumFrequencyHz",
  "/minimumFrequencyHz",
  "/filterType",
  "/filterOrder",
  "/iirOrder",
  "/firOrder",
  "/firGamma"
]) {
  assert.ok(atPointer(noiseBandContainer, pointer), `Missing ${pointer}`);
}
assert.equal(
  allElements(noiseBandContainer).some(
    (element) => element.tagName === "TEXTAREA"),
  false,
  "Noise Band Monitor must omit generic JSON and display preferences");
const noiseBandText = allElements(noiseBandContainer)
  .map((element) => element.textContent)
  .join(" ");
assert.doesNotMatch(
  noiseBandText,
  /Log Scale|Show Grid|Show Decimators|Show ANSI standards/,
  "Java plot preferences must not leak into portable science settings");
assert.deepEqual(
  atPointer(noiseBandContainer, "/bandType").children.map(
    (option) => option.getAttribute("value")),
  [
    "octave",
    "thirdOctave",
    "decidecade",
    "decade",
    "tenthOctave",
    "twelfthOctave"
  ],
  "NoiseBandDialog band order changed");
const filterType = atPointer(noiseBandContainer, "/filterType");
const filterOrder = atPointer(noiseBandContainer, "/filterOrder");
filterOrder.value = "8";
filterOrder.dispatchEvent(new Event("change"));
filterType.value = "firWindow";
filterType.dispatchEvent(new Event("change"));
assert.equal(filterOrder.value, "7");
assert.equal(atPointer(noiseBandContainer, "/firGamma").disabled, false);
filterOrder.value = "9";
filterOrder.dispatchEvent(new Event("change"));
filterType.value = "butterworth";
filterType.dispatchEvent(new Event("change"));
assert.equal(filterOrder.value, "8");
action(noiseBandContainer, "default-reference-frequency")
  .dispatchEvent(new Event("click"));
action(noiseBandContainer, "maximum-source-frequency")
  .dispatchEvent(new Event("click"));
assert.deepEqual(noiseBandEditor.collect(), {
  channelBitmap: 1,
  bandType: "thirdOctave",
  filterType: "butterworth",
  iirOrder: 8,
  firOrder: 9,
  firGamma: 2.5,
  outputIntervalSeconds: 10,
  minimumFrequencyHz: 1.7925856629456591,
  maximumFrequencyHz: 24000,
  referenceFrequencyHz: 1000
});
filterOrder.value = "7";
assert.throws(
  () => noiseBandEditor.collect(),
  /IIR filter order must be even/);
filterOrder.value = "8";

const ltsaContainer = document.createElement("div");
document.body.append(ltsaContainer);
const ltsaEditor = moduleApi.mountEditor({
  ...commonOptions,
  container: ltsaContainer,
  typeId: "pamguard.ltsa",
  settings: {
    channelBitmap: 3,
    intervalSeconds: 120,
    longerFactor: 20
  }
});
for (const pointer of [
  "/channelBitmap/0",
  "/channelBitmap/1",
  "/intervalSeconds",
  "/longerFactor"
]) {
  assert.ok(atPointer(ltsaContainer, pointer), `Missing ${pointer}`);
}
assert.deepEqual(ltsaEditor.collect(), {
  channelBitmap: 3,
  intervalSeconds: 120,
  longerFactor: 20
});
const advancedText = allElements(ltsaContainer)
  .map((element) => element.textContent)
  .join(" ");
assert.match(advancedText, /persisted, dormant/i);
assert.match(advancedText, /no runtime effect/i);

fftEditor.cleanup();
noiseBandEditor.cleanup();
ltsaEditor.cleanup();
console.log(
  "Noise Monitor, Noise Band Monitor, and LTSA dedicated settings " +
    "browser contracts passed");
