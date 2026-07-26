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

  querySelector(selector) {
    return this.head.querySelector(selector) ||
      this.body.querySelector(selector);
  }
}

globalThis.document = new FakeDocument();
require("./project-clip-generator-settings.js");

const moduleApi =
  globalThis.PamguardProjectClipGeneratorSettings;
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

function allAtPointer(root, pointer) {
  return allAtAttribute(root, "data-setting-pointer", pointer);
}

function atPointer(root, pointer) {
  return allAtPointer(root, pointer)[0];
}

const fixture = JSON.parse(fs.readFileSync(
  path.join(
    __dirname,
    "..",
    "..",
    "cpp-engine",
    "tests",
    "fixtures",
    "clip-generator",
    "settings-defaults.json"),
  "utf8"));

assert.deepEqual(moduleApi.JAVA_AUTHORITY, {
  version: fixture.authority.version,
  commit: fixture.authority.commit,
  settingsClass: fixture.authority.settingsClass,
  triggerPolicyClass: fixture.authority.triggerPolicyClass,
  dialogClass: "clipgenerator.ClipDialog",
  triggerDialogClass: "clipgenerator.ClipGenSettingDialog"
});
assert.deepEqual(
  moduleApi.canonicalSettings(),
  fixture.portableSettingsDefaults,
  "Browser defaults must equal the pinned Java fixture exactly");
assert.deepEqual(
  moduleApi.POLICY_FIELDS,
  Object.keys(fixture.triggerPolicyFieldDefaults),
  "The browser policy keys must equal the canonical portable schema");
assert.deepEqual(
  moduleApi.defaultTriggerPolicy(
    fixture.triggerPolicyFieldDefaults.triggerSource),
  fixture.triggerPolicyFieldDefaults,
  "Eligible-source policies must reproduce ClipGenSetting defaults");
assert.equal(
  moduleApi.defaultTriggerPolicy(
    fixture.triggerPolicyFieldDefaults.triggerSource,
    { spectrogramMark: true }).useDataBudget,
  fixture.sourceEligibilityBoundary.spectrogramMarkUsesDataBudget);

assert.throws(
  () => moduleApi.canonicalSettings({
    ...fixture.portableSettingsDefaults,
    extra: true
  }),
  /contain exactly/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...fixture.portableSettingsDefaults,
    storageMode: "annotation"
  }),
  /wav-files, binary, or both/);
assert.throws(
  () => moduleApi.canonicalTriggerPolicy({
    ...fixture.triggerPolicyFieldDefaults,
    secondsBeforeTrigger: -0.01
  }),
  /invalid value/);
assert.throws(
  () => moduleApi.canonicalTriggerPolicy({
    ...fixture.triggerPolicyFieldDefaults,
    secondsBeforeTrigger: "0"
  }),
  /must be a number/);
assert.throws(
  () => moduleApi.canonicalTriggerPolicy({
    ...fixture.triggerPolicyFieldDefaults,
    channelSelection: "first-channel"
  }),
  /channelSelection/);
assert.throws(
  () => moduleApi.canonicalTriggerPolicy({
    ...fixture.triggerPolicyFieldDefaults,
    clipPrefix: "é".repeat(129)
  }),
  /256 UTF-8 bytes/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...fixture.portableSettingsDefaults,
    triggerPolicies: [
      fixture.triggerPolicyFieldDefaults,
      fixture.triggerPolicyFieldDefaults
    ]
  }),
  /must be unique/);

const whistleSource = {
  unitId: "whistle-detector-1",
  outputRole: "whistles"
};
const spectrogramMarkSource = {
  unitId: "clip-generator-1",
  outputRole: "spectrogramMarks"
};
const clickSource = {
  unitId: "click-detector-1",
  outputRole: "clicks"
};
const unboundIshmaelSource = {
  unitId: "ishmael-detector-1",
  outputRole: "detections"
};
const triggerSources = [
  {
    ...clickSource,
    name: "Click Detector 1 · Clicks",
    typeId: "pamguard.click-detector",
    // The Java type boundary wins even if stale metadata says otherwise.
    capabilities: ["detections", "clip-trigger"]
  },
  {
    ...whistleSource,
    name: "Whistle and Moan Detector 1 · Whistles",
    typeId: "pamguard.whistles-moans",
    capabilities: ["detections", "clip-trigger"]
  },
  {
    ...spectrogramMarkSource,
    name: "Clip Generator 1 · Spectrogram Marks",
    typeId: "pamguard.clip-generator",
    sourceKind: "spectrogram-mark",
    capabilities: ["clip-trigger"]
  },
  {
    ...unboundIshmaelSource,
    name: "Ishmael Energy Sum 1 · Detections",
    typeId: "pamguard.ishmael-energy-sum",
    capabilities: ["detections", "clip-trigger"]
  },
  {
    unitId: "fft-1",
    outputRole: "fft",
    name: "FFT Engine 1 · FFT data",
    typeId: "pamguard.fft",
    capabilities: ["frequency-domain"]
  }
];
const settings = {
  storageMode: "binary",
  datedSubFolders: true,
  triggerPolicies: [
    {
      triggerSource: whistleSource,
      enabled: true,
      secondsBeforeTrigger: 1.25,
      secondsAfterTrigger: 2.5,
      channelSelection: "all-channels",
      clipPrefix: "WH_",
      useDataBudget: true,
      dataBudgetKilobytes: 4096,
      budgetPeriodHours: 12
    },
    {
      triggerSource: spectrogramMarkSource,
      enabled: true,
      secondsBeforeTrigger: 0,
      secondsAfterTrigger: 0,
      channelSelection: "detection-channels-only",
      clipPrefix: null,
      useDataBudget: false,
      dataBudgetKilobytes: 10240,
      budgetPeriodHours: 24
    }
  ]
};
const settingsSnapshot = JSON.parse(JSON.stringify(settings));
let boundSources = [
  whistleSource,
  spectrogramMarkSource
];
const errors = [];
const rawAudioSourceSelect = document.createElement("select");
const triggerSourceControl = document.createElement("div");
const container = document.createElement("div");
document.body.append(container);
const editor = moduleApi.mountEditor({
  container,
  settings,
  rawAudioSourceSelect,
  getRawAudioSourceName: () =>
    "Sound Acquisition 1 · Raw audio",
  getAvailableTriggerSources: () => triggerSources,
  getBoundTriggerSources: () => boundSources,
  triggerSourceControl,
  reportError: (error) => errors.push(error)
});
assert.deepEqual(errors, []);

assert.equal(
  allElements(container).some(
    (element) => element.tagName === "TEXTAREA"),
  false,
  "Clip Generator settings must never expose generic JSON");
for (const pointer of [
  "/storageMode",
  "/datedSubFolders",
  "/triggerPolicies/0/enabled",
  "/triggerPolicies/0/channelSelection",
  "/triggerPolicies/0/secondsBeforeTrigger",
  "/triggerPolicies/0/secondsAfterTrigger",
  "/triggerPolicies/0/clipPrefix",
  "/triggerPolicies/0/useDataBudget",
  "/triggerPolicies/0/dataBudgetKilobytes",
  "/triggerPolicies/0/budgetPeriodHours",
  "/triggerPolicies/1/enabled",
  "/triggerPolicies/1/channelSelection",
  "/triggerPolicies/1/secondsBeforeTrigger",
  "/triggerPolicies/1/secondsAfterTrigger",
  "/triggerPolicies/1/clipPrefix",
  "/triggerPolicies/1/useDataBudget",
  "/triggerPolicies/1/dataBudgetKilobytes",
  "/triggerPolicies/1/budgetPeriodHours"
]) {
  assert.ok(atPointer(container, pointer), `Missing ${pointer}`);
}

const text = allElements(container)
  .map((element) => element.textContent)
  .join(" ");
assert.match(text, /Audio Data Source/);
assert.match(text, /Storage options/);
assert.match(text, /Data Triggers/);
assert.match(text, /Channel selection/);
assert.match(text, /Time before trigger/);
assert.match(text, /Time after trigger/);
assert.match(text, /File initials/);
assert.match(text, /Data Budget/);
assert.match(text, /Record everything/);
assert.match(text, /Budget data/);
assert.match(text, /Annotation storage is unavailable/);
assert.match(text, /PAMGuard explicitly disables Click Detector/);
assert.match(text, /Spectrogram marks are eligible/);
assert.match(text, /graph binding/);
assert.match(text, /receiver-owned policy|Clip Generator owns/);

const clickRow = atAttribute(
  container,
  "data-clip-generator-source",
  moduleApi.sourceKey(clickSource));
assert.equal(
  clickRow.getAttribute("data-clip-generator-eligibility"),
  "click-detector-ineligible");
const clickBinding = atAttribute(
  clickRow,
  "data-clip-generator-binding-source",
  moduleApi.sourceKey(clickSource));
assert.equal(clickBinding.disabled, true);
assert.equal(clickBinding.checked, false);
assert.equal(
  allElements(clickRow).some(
    (element) =>
      element.getAttribute("data-setting-pointer")?.includes(
        "/triggerPolicies/")),
  false,
  "Click Detector must not acquire a receiver policy");

const markRow = atAttribute(
  container,
  "data-clip-generator-source",
  moduleApi.sourceKey(spectrogramMarkSource));
assert.equal(
  markRow.getAttribute("data-clip-generator-source-kind"),
  "spectrogram-mark");
assert.equal(
  markRow.getAttribute("data-clip-generator-eligibility"),
  "eligible");
assert.equal(
  atAttribute(
    markRow,
    "data-clip-generator-binding-source",
    moduleApi.sourceKey(spectrogramMarkSource)).checked,
  true);
const markBudget = allAtPointer(
  container,
  "/triggerPolicies/1/useDataBudget");
assert.equal(
  markBudget.find(
    (control) =>
      control.getAttribute("data-clip-generator-budget-mode") ===
        "off").checked,
  true);
assert.equal(
  atPointer(
    container,
    "/triggerPolicies/1/dataBudgetKilobytes").disabled,
  true,
  "Spectrogram mark budget fields must be disabled by default");

assert.deepEqual(
  editor.collect(),
  settings,
  "OK with untouched controls must round-trip canonical settings");
assert.deepEqual(
  editor.collectTriggerSources(),
  boundSources,
  "Receiver policy sources must equal the multi-source binding");
assert.deepEqual(
  settings,
  settingsSnapshot,
  "Mount and OK must not mutate the caller's project snapshot");

const wav = atAttribute(
  container,
  "data-clip-generator-storage-option",
  "wav-files");
wav.checked = true;
wav.dispatchEvent(new Event("change"));
atPointer(container, "/datedSubFolders").checked = false;
atPointer(
  container,
  "/triggerPolicies/0/secondsBeforeTrigger").value = "0.75";
atPointer(
  container,
  "/triggerPolicies/0/secondsAfterTrigger").value = "3.5";
atPointer(
  container,
  "/triggerPolicies/0/channelSelection").value =
    "first-detection-channel-only";
atPointer(
  container,
  "/triggerPolicies/0/clipPrefix").value = "OPS_";
atPointer(
  container,
  "/triggerPolicies/0/dataBudgetKilobytes").value = "5.5";
atPointer(
  container,
  "/triggerPolicies/0/budgetPeriodHours").value = "6";

let collected = editor.collect();
assert.equal(collected.storageMode, "both");
assert.equal(collected.datedSubFolders, false);
assert.deepEqual(collected.triggerPolicies[0], {
  triggerSource: whistleSource,
  enabled: true,
  secondsBeforeTrigger: 0.75,
  secondsAfterTrigger: 3.5,
  channelSelection: "first-detection-channel-only",
  clipPrefix: "OPS_",
  useDataBudget: true,
  dataBudgetKilobytes: 5632,
  budgetPeriodHours: 6
});
assert.equal(
  collected.triggerPolicies[1].useDataBudget,
  false,
  "Spectrogram mark record-everything policy must survive OK");

const ishmaelBinding = atAttribute(
  container,
  "data-clip-generator-binding-source",
  moduleApi.sourceKey(unboundIshmaelSource));
ishmaelBinding.checked = true;
ishmaelBinding.dispatchEvent(new Event("change"));
collected = editor.collect();
assert.equal(collected.triggerPolicies.length, 3);
assert.deepEqual(
  collected.triggerPolicies[2],
  moduleApi.defaultTriggerPolicy(unboundIshmaelSource),
  "Selecting an eligible source must synthesize exact Java defaults");
assert.deepEqual(
  editor.collectTriggerSources(),
  [
    whistleSource,
    spectrogramMarkSource,
    unboundIshmaelSource
  ]);

const markBinding = atAttribute(
  container,
  "data-clip-generator-binding-source",
  moduleApi.sourceKey(spectrogramMarkSource));
markBinding.checked = false;
markBinding.dispatchEvent(new Event("change"));
collected = editor.collect();
assert.deepEqual(
  collected.triggerPolicies.map(
    (policy) => policy.triggerSource),
  [whistleSource, unboundIshmaelSource],
  "Removing a trigger graph source must remove its receiver policy");

const cancelled = editor.cancel();
assert.deepEqual(cancelled, settingsSnapshot);
assert.deepEqual(
  settings,
  settingsSnapshot,
  "Cancel must leave the supplied project snapshot untouched");

const freshContainer = document.createElement("div");
const freshEditor = moduleApi.mountEditor({
  container: freshContainer,
  settings: fixture.portableSettingsDefaults,
  getAvailableTriggerSources: () => [triggerSources[2]],
  boundTriggerSources: [spectrogramMarkSource]
});
const freshCollected = freshEditor.collect();
assert.deepEqual(
  freshCollected.triggerPolicies,
  [{
    ...fixture.triggerPolicyFieldDefaults,
    triggerSource: spectrogramMarkSource,
    useDataBudget: false
  }],
  "A newly bound spectrogram mark must use Java's no-budget default");
freshEditor.cleanup();

const emptyContainer = document.createElement("div");
const emptyEditor = moduleApi.mountEditor({
  container: emptyContainer,
  settings: fixture.portableSettingsDefaults,
  boundTriggerSources: []
});
assert.deepEqual(
  emptyEditor.collect(),
  fixture.portableSettingsDefaults,
  "A fresh ClipSettings with no trigger blocks must stay exactly empty");
assert.ok(atAttribute(
  emptyContainer,
  "data-clip-generator-trigger-state",
  "empty"));
emptyEditor.cleanup();

console.log(
  "Clip Generator dedicated settings browser contract passed");
