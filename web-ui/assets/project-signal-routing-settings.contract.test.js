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
    return actual !== null && (value === undefined || actual === value);
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
    this.disabled = false;
    this.hidden = false;
    this.step = "";
  }

  append(...children) {
    for (const child of children) {
      if (child.parentNode) child.remove();
      child.parentNode = this;
      this.children.push(child);
    }
  }

  replaceChildren(...children) {
    for (const child of this.children) child.parentNode = null;
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
    const listeners = this.listeners.get(type) || [];
    this.listeners.set(
      type,
      listeners.filter((listener) => listener !== callback));
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
require("./project-signal-routing-settings.js");

const routing = globalThis.PamguardProjectSignalRoutingSettings;
assert.ok(routing?.mountEditor);

const amplifierDefaults = routing.canonicalAmplifierSettings({});
assert.equal(amplifierDefaults.channelSettings.length, 32);
assert.ok(amplifierDefaults.channelSettings.every(
  (row) => row.gainDb === 0 && row.invert === false));

const patchDefaults = routing.canonicalPatchSettings({});
assert.equal(patchDefaults.routingMatrix.length, 32);
assert.ok(patchDefaults.routingMatrix.every(
  (row, input) =>
    row.length === 32 &&
    row.every((selected, output) => selected === (input === output))));
assert.equal(patchDefaults.advancedGainMatrix, null);

function allElements(root) {
  const result = [];
  const visit = (element) => {
    result.push(element);
    element.children.forEach(visit);
  };
  visit(root);
  return result;
}

function atPointer(root, pointer) {
  return allElements(root).find(
    (element) =>
      element.getAttribute("data-setting-pointer") === pointer);
}

let amplifierBitmap = (2 ** 0) + (2 ** 3);
const amplifierSource = document.createElement("select");
const amplifierContainer = document.createElement("div");
document.body.append(amplifierContainer);
const amplifier = routing.mountEditor({
  container: amplifierContainer,
  typeId: "pamguard.amplifier",
  settings: amplifierDefaults,
  sourceSelect: amplifierSource,
  getAvailableChannelBitmap: () => amplifierBitmap
});
assert.ok(atPointer(
  amplifierContainer,
  "/channelSettings/0/gainDb"));
assert.ok(atPointer(
  amplifierContainer,
  "/channelSettings/3/invert"));
assert.equal(
  atPointer(amplifierContainer, "/channelSettings/1/gainDb"),
  undefined,
  "Amplifier must show only source-published absolute channels");
atPointer(
  amplifierContainer,
  "/channelSettings/3/gainDb").value = "6.25";
atPointer(
  amplifierContainer,
  "/channelSettings/3/invert").checked = true;
let collectedAmplifier = amplifier.collect();
assert.deepEqual(
  collectedAmplifier.channelSettings[3],
  { gainDb: 6.25, invert: true });
assert.equal(collectedAmplifier.channelSettings.length, 32);

amplifierBitmap = 2 ** 1;
amplifierSource.dispatchEvent(new Event("change"));
assert.ok(atPointer(
  amplifierContainer,
  "/channelSettings/1/gainDb"));
assert.equal(
  atPointer(amplifierContainer, "/channelSettings/3/gainDb"),
  undefined);
collectedAmplifier = amplifier.collect();
assert.deepEqual(
  collectedAmplifier.channelSettings[3],
  { gainDb: 6.25, invert: true },
  "hidden Amplifier channel settings must remain portable");
allElements(amplifierContainer).find(
  (element) =>
    element.getAttribute("data-signal-routing-action") ===
      "reset-amplifier").dispatchEvent(new Event("click"));
assert.ok(amplifier.collect().channelSettings.every(
  (row) => row.gainDb === 0 && row.invert === false));

let patchBitmap = (2 ** 0) + (2 ** 2);
const patchSource = document.createElement("select");
const patchContainer = document.createElement("div");
document.body.append(patchContainer);
const patch = routing.mountEditor({
  container: patchContainer,
  typeId: "pamguard.patch-panel",
  settings: patchDefaults,
  sourceSelect: patchSource,
  getAvailableChannelBitmap: () => patchBitmap
});
assert.ok(atPointer(patchContainer, "/routingMatrix/0/31"));
assert.ok(atPointer(patchContainer, "/routingMatrix/2/31"));
assert.equal(
  atPointer(patchContainer, "/routingMatrix/1/1"),
  undefined,
  "Patch rows must follow the selected source channel map");
atPointer(patchContainer, "/routingMatrix/0/0").checked = false;
atPointer(patchContainer, "/routingMatrix/0/7").checked = true;
let collectedPatch = patch.collect();
assert.equal(collectedPatch.routingMatrix[0][0], false);
assert.equal(collectedPatch.routingMatrix[0][7], true);
assert.ok(
  collectedPatch.routingMatrix[1].every((selected) => !selected),
  "Java Patch OK clears routes belonging to hidden input rows");

const advancedToggle = atPointer(
  patchContainer,
  "/advancedGainMatrix/enabled");
advancedToggle.checked = true;
advancedToggle.dispatchEvent(new Event("change"));
assert.ok(atPointer(
  patchContainer,
  "/advancedGainMatrix/2/9"));
atPointer(
  patchContainer,
  "/advancedGainMatrix/2/9").value = "-0.375";
collectedPatch = patch.collect();
assert.equal(collectedPatch.advancedGainMatrix[2][9], -0.375);
assert.equal(collectedPatch.advancedGainMatrix.length, 32);
allElements(patchContainer).find(
  (element) =>
    element.getAttribute("data-signal-routing-action") ===
      "clear").dispatchEvent(new Event("click"));
assert.ok(patch.collect().routingMatrix.every(
  (row) => row.every((selected) => !selected)));
allElements(patchContainer).find(
  (element) =>
    element.getAttribute("data-signal-routing-action") ===
      "identity").dispatchEvent(new Event("click"));
collectedPatch = patch.collect();
assert.equal(collectedPatch.routingMatrix[0][0], true);
assert.equal(collectedPatch.routingMatrix[2][2], true);
assert.ok(collectedPatch.routingMatrix[1].every(
  (selected) => !selected));

assert.equal(
  allElements(amplifierContainer)
    .concat(allElements(patchContainer))
    .some((element) => element.tagName === "TEXTAREA"),
  false,
  "normal signal-routing settings must never expose raw JSON");

assert.throws(
  () => routing.canonicalPatchSettings({
    routingMatrix: [[true]],
    advancedGainMatrix: null
  }),
  /32 input rows/);

amplifier.cleanup();
patch.cleanup();
console.log("project signal-routing settings contracts passed");
