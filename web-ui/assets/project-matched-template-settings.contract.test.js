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

  click() {
    this.dispatchEvent(new FakeEvent("click"));
  }

  focus() {}

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

  createElementNS(_namespace, tag) {
    return new FakeElement(tag, this);
  }

  querySelector(selector) {
    return this.head.querySelector(selector) ||
      this.body.querySelector(selector);
  }
}

globalThis.document = new FakeDocument();
require("./project-matched-template-settings.js");

const moduleApi =
  globalThis.PamguardProjectMatchedTemplateSettings;
assert.ok(moduleApi?.mountEditor);

function allElements(root) {
  const result = [];
  const visit = (element) => {
    result.push(element);
    element.children.forEach(visit);
  };
  visit(root);
  return result;
}

function allAtAttribute(root, attribute, value) {
  return allElements(root).filter(
    (element) =>
      element.getAttribute(attribute) === value);
}

function atAttribute(root, attribute, value) {
  return allAtAttribute(root, attribute, value)[0];
}

function atPointer(root, pointer) {
  return atAttribute(root, "data-setting-pointer", pointer);
}

const presetLibrary = JSON.parse(fs.readFileSync(
  path.join(
    __dirname,
    "matched-template-default-templates.json"),
  "utf8"));
const presetTemplates =
  moduleApi.canonicalPresetLibrary(presetLibrary);
assert.deepEqual(
  presetTemplates.map((template) => template.name),
  [
    "Beaked Whale Click",
    "Dolphin Click",
    "Harbour Porpoise",
    "Sperm Whale (P0 P1 P2)",
    "None"
  ]);

const exactMatchWaveform = [
  0,
  -0,
  0.125,
  -0.25,
  1.25e-12,
  -9.75
];
const exactRejectWaveform = [
  -1,
  -0.5,
  0,
  0.5,
  1,
  0.25
];
const settings = {
  clickType: 101,
  normalisationType: 2,
  peakSearch: true,
  peakSmoothing: 5,
  lengthDb: 6,
  restrictedBins: 2048,
  channelClassification: 0,
  classifiers: [
    {
      thresholdToAccept: 0.01,
      normalisation: 0,
      matchTemplate: {
        name: "Exact match",
        sampleRateHz: 192000,
        waveform: exactMatchWaveform
      },
      rejectTemplate: {
        name: "Exact reject",
        sampleRateHz: 192000,
        waveform: exactRejectWaveform
      }
    }
  ]
};

assert.throws(
  () => moduleApi.canonicalSettings({
    ...settings,
    peakSmoothing: 4
  }),
  /must be an odd value/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...settings,
    restrictedBins: 1000
  }),
  /power of two/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...settings,
    clickType: 99
  }),
  /Click type must be 100 to 255/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...settings,
    classifiers: []
  }),
  /requires 1 to 64/);
const decimalRateSettings = moduleApi.canonicalSettings({
  ...settings,
  classifiers: [{
    ...settings.classifiers[0],
    matchTemplate: {
      ...settings.classifiers[0].matchTemplate,
      sampleRateHz: 48000.123456789
    }
  }]
});
assert.equal(
  decimalRateSettings.classifiers[0].matchTemplate.sampleRateHz,
  Math.fround(48000.123456789),
  "Template sample rates must use Java MatchTemplate float precision");

const imported = moduleApi.parseTemplateCsv(
  "\uFEFF0,-0,0.5,-0.25,1e-6\r\n192000\r\n",
  "portable.csv");
assert.equal(imported.name, "portable.csv");
assert.equal(imported.sampleRateHz, 192000);
assert.deepEqual(
  imported.waveform,
  [0, -0, 0.5, -0.25, 1e-6]);
assert.equal(Object.is(imported.waveform[1], -0), true);
assert.throws(
  () => moduleApi.parseTemplateCsv(
    "0,1,2,3\n192000\nunexpected"),
  /exactly two non-empty rows/);
assert.throws(
  () => moduleApi.parseTemplateCsv("0,1,2,3\n192000"),
  /must contain 5 to/);
assert.throws(
  () => moduleApi.parseTemplateCsv("0,1,2,3,4\n0"),
  /sample rate has an invalid value/i);

(async () => {
  let sourceSampleRate = 192000;
  const errors = [];
  const sourceSelect = document.createElement("select");
  const container = document.createElement("div");
  document.body.append(container);
  const editor = moduleApi.mountEditor({
    container,
    settings,
    sourceSelect,
    getSourceSampleRate: () => sourceSampleRate,
    loadPresetLibrary: () => Promise.resolve(presetLibrary),
    reportError: (error) => errors.push(error)
  });
  await editor.ready;
  assert.deepEqual(errors, []);

  for (const pointer of [
    "/channelClassification",
    "/clickType",
    "/peakSearch",
    "/restrictedBins",
    "/lengthDb",
    "/peakSmoothing",
    "/normalisationType",
    "/classifiers/0/thresholdToAccept",
    "/classifiers/0/matchTemplate/name",
    "/classifiers/0/matchTemplate/sampleRateHz",
    "/classifiers/0/rejectTemplate/name",
    "/classifiers/0/rejectTemplate/sampleRateHz"
  ]) {
    assert.ok(atPointer(container, pointer), `Missing ${pointer}`);
  }
  assert.equal(
    allElements(container).some(
      (element) => element.tagName === "TEXTAREA"),
    false,
    "Matched-template settings must never use generic JSON");

  const text = allElements(container)
    .map((element) => element.textContent)
    .join(" ");
  assert.match(text, /General Classifier Settings/);
  assert.match(text, /Channel Options/);
  assert.match(text, /Click Type/);
  assert.match(text, /Click Waveform/);
  assert.match(text, /Amplitude Normalisation/);
  assert.match(text, /Click Template Settings/);
  assert.match(text, /Match threshold/);
  assert.match(text, /Match Template/);
  assert.match(text, /Reject Template/);
  assert.match(text, /MAT unavailable/);
  assert.doesNotMatch(
    text,
    /symbol|fill colour|alarm|RainbowClick|legacy file/i,
    "Java display/storage preferences leaked into the portable editor");

  assert.equal(
    atAttribute(
      container,
      "data-matched-template-preset-status",
      "ready").textContent,
    "5 PAMGuard template presets available");
  assert.match(
    atAttribute(
      container,
      "data-matched-template-restricted-duration",
      "").textContent,
    /10\.67 ms/);
  sourceSampleRate = 96000;
  sourceSelect.dispatchEvent(new Event("change"));
  assert.match(
    atAttribute(
      container,
      "data-matched-template-restricted-duration",
      "").textContent,
    /21\.33 ms/);

  const peakSearch = atPointer(container, "/peakSearch");
  peakSearch.checked = false;
  peakSearch.dispatchEvent(new Event("change"));
  assert.equal(
    atPointer(container, "/restrictedBins").disabled,
    true);
  assert.equal(atPointer(container, "/lengthDb").disabled, true);
  assert.equal(
    atPointer(container, "/peakSmoothing").disabled,
    true);
  peakSearch.checked = true;
  peakSearch.dispatchEvent(new Event("change"));

  let collected = editor.collect();
  assert.equal(
    collected.classifiers[0].normalisation,
    2,
    "OK must synchronise classifier normalisation to global");
  assert.deepEqual(
    collected.classifiers[0].matchTemplate.waveform,
    exactMatchWaveform);
  assert.equal(
    Object.is(
      collected.classifiers[0].matchTemplate.waveform[1],
      -0),
    true,
    "Waveform values must not be normalised, resampled, or rounded");

  const presetControls = allAtAttribute(
    container,
    "data-matched-template-action",
    "choose-preset");
  assert.equal(presetControls.length, 2);
  assert.deepEqual(
    presetControls[0].children
      .slice(1)
      .map((option) => option.textContent),
    [
      "Beaked Whale Click",
      "Dolphin Click",
      "Harbour Porpoise",
      "Sperm Whale (P0 P1 P2)"
    ],
    "Java excludes None from the match-template preset picker");
  assert.equal(
    presetControls[1].children.at(-1).textContent,
    "None",
    "Java includes None for reject templates");

  const porpoiseIndex = presetTemplates.findIndex(
    (template) => template.name === "Harbour Porpoise");
  presetControls[0].value = String(porpoiseIndex);
  presetControls[0].dispatchEvent(new Event("change"));
  collected = editor.collect();
  assert.deepEqual(
    collected.classifiers[0].matchTemplate,
    presetTemplates[porpoiseIndex],
    "Preset waveform arrays must be copied exactly");

  atPointer(
    container,
    "/classifiers/0/thresholdToAccept").value = "0.37";
  atAttribute(
    container,
    "data-matched-template-action",
    "add-classifier").click();
  assert.equal(
    allAtAttribute(
      container,
      "data-matched-template-panel",
      "1").length,
    1);
  collected = editor.collect();
  assert.equal(collected.classifiers.length, 2);
  assert.equal(collected.classifiers[0].thresholdToAccept, 0.37);
  assert.equal(collected.classifiers[1].thresholdToAccept, 0.01);
  assert.equal(
    collected.classifiers[1].matchTemplate.name,
    "Beaked Whale");
  assert.equal(
    collected.classifiers[1].rejectTemplate.name,
    "Dolphin");
  assert.equal(collected.classifiers[1].normalisation, 2);
  assert.deepEqual(
    collected.classifiers[1].matchTemplate.waveform,
    presetTemplates[0].waveform);
  assert.deepEqual(
    collected.classifiers[1].rejectTemplate.waveform,
    presetTemplates[1].waveform);

  const removeButtons = allAtAttribute(
    container,
    "data-matched-template-action",
    "remove-classifier");
  assert.equal(removeButtons.length, 2);
  removeButtons[1].click();
  assert.equal(editor.collect().classifiers.length, 1);
  assert.equal(
    atAttribute(
      container,
      "data-matched-template-action",
      "remove-classifier").disabled,
    true,
    "The last Java classifier tab cannot be removed");

  editor.importCsv(
    0,
    "rejectTemplate",
    "0,-0,0.125,-0.25,0.5\n48000",
    "operator-template.csv");
  collected = editor.collect();
  assert.deepEqual(
    collected.classifiers[0].rejectTemplate,
    {
      name: "operator-template.csv",
      sampleRateHz: 48000,
      waveform: [0, -0, 0.125, -0.25, 0.5]
    });
  assert.equal(
    Object.is(
      collected.classifiers[0].rejectTemplate.waveform[1],
      -0),
    true);

  atPointer(container, "/clickType").value = "256";
  assert.equal(
    editor.collect().clickType,
    0,
    "Java spinner value 256 must persist through its byte-wrapped alias");
  atPointer(container, "/clickType").value = "101";

  editor.cleanup();
  console.log(
    "Matched Template dedicated settings browser contract passed");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
