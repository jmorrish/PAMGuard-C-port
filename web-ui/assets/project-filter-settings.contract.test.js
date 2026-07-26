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
    this.dataset = {};
    this.className = "";
    this.textContent = "";
    this.type = "";
    this.value = "";
    this.checked = false;
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
require("./project-filter-settings.js");

const moduleApi = globalThis.PamguardProjectFilterSettings;
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

function atPointer(root, pointer) {
  return allElements(root).find(
    (element) =>
      element.getAttribute("data-setting-pointer") === pointer);
}

function action(root, name) {
  return allElements(root).find(
    (element) => element.getAttribute("data-filter-action") === name);
}

const filterSettings = {
  channelBitmap: 3,
  ...moduleApi.canonicalFilterParams({})
};
const filterContainer = document.createElement("div");
const source = document.createElement("select");
document.body.append(filterContainer);
const filterEditor = moduleApi.mountEditor({
  container: filterContainer,
  typeId: "pamguard.filter",
  settings: filterSettings,
  sourceSelect: source,
  getAvailableChannelBitmap: () => 3,
  getSourceSampleRate: () => 48000
});
for (const pointer of [
  "/channelBitmap/0",
  "/channelBitmap/1",
  "/type",
  "/band",
  "/order",
  "/lowPassFreqHz",
  "/highPassFreqHz",
  "/passBandRippleDb",
  "/stopBandRippleDb",
  "/chebyGamma"
]) {
  assert.ok(atPointer(filterContainer, pointer), `Missing ${pointer}`);
}
assert.equal(
  allElements(filterContainer).some(
    (element) => element.tagName === "TEXTAREA"),
  false);
atPointer(filterContainer, "/type").value = "chebyshev";
atPointer(filterContainer, "/type").dispatchEvent(new Event("change"));
atPointer(filterContainer, "/order").value = "3";
assert.throws(
  () => filterEditor.collect(),
  /order must be 1 or an even number/);
atPointer(filterContainer, "/order").value = "4";
atPointer(filterContainer, "/passBandRippleDb").value = "1.5";
assert.equal(filterEditor.collect().passBandRippleDb, 1.5);

atPointer(filterContainer, "/type").value = "firArbitrary";
atPointer(filterContainer, "/type").dispatchEvent(new Event("change"));
action(filterContainer, "add-point").dispatchEvent(new Event("click"));
action(filterContainer, "add-point").dispatchEvent(new Event("click"));
atPointer(
  filterContainer,
  "/arbitraryFrequenciesHz/0").value = "1000";
atPointer(
  filterContainer,
  "/arbitraryGainsDb/0").value = "-40";
atPointer(
  filterContainer,
  "/arbitraryFrequenciesHz/1").value = "12000";
atPointer(
  filterContainer,
  "/arbitraryGainsDb/1").value = "0";
const arbitrary = filterEditor.collect();
assert.deepEqual(
  arbitrary.arbitraryFrequenciesHz,
  [1000, 12000]);
assert.deepEqual(arbitrary.arbitraryGainsDb, [-40, 0]);

let decimatorSourceRate = 48000;
const decimatorSettings = {
  outputSampleRateHz: 2000,
  channelBitmap: 3,
  interpolation: 0,
  filter: {
    ...moduleApi.canonicalFilterParams({}),
    band: "lowPass",
    order: 6,
    lowPassFreqHz: 1000
  }
};
const decimatorContainer = document.createElement("div");
const decimatorSource = document.createElement("select");
document.body.append(decimatorContainer);
const decimatorEditor = moduleApi.mountEditor({
  container: decimatorContainer,
  typeId: "pamguard.decimator",
  settings: decimatorSettings,
  sourceSelect: decimatorSource,
  getAvailableChannelBitmap: () => 3,
  getSourceSampleRate: () => decimatorSourceRate
});
for (const pointer of [
  "/outputSampleRateHz",
  "/interpolation",
  "/filter/type",
  "/filter/band",
  "/filter/order",
  "/filter/lowPassFreqHz"
]) {
  assert.ok(atPointer(decimatorContainer, pointer), `Missing ${pointer}`);
}
atPointer(decimatorContainer, "/outputSampleRateHz").value = "4000";
action(
  decimatorContainer,
  "decimator-default-filter").dispatchEvent(new Event("click"));
assert.equal(atPointer(decimatorContainer, "/filter/type").value,
  "butterworth");
assert.equal(atPointer(decimatorContainer, "/filter/band").value,
  "lowPass");
assert.equal(atPointer(decimatorContainer, "/filter/order").value, "6");
assert.equal(
  atPointer(decimatorContainer, "/filter/lowPassFreqHz").value,
  "2000");
atPointer(decimatorContainer, "/interpolation").value = "1";
assert.equal(
  decimatorEditor.collect().interpolation,
  0,
  "Java resets unnecessary interpolation for integer rate ratios");
decimatorSourceRate = 44100;
decimatorSource.dispatchEvent(new Event("change"));
assert.equal(decimatorEditor.collect().interpolation, 1);
atPointer(decimatorContainer, "/outputSampleRateHz").value = "88200";
action(
  decimatorContainer,
  "decimator-default-filter").dispatchEvent(new Event("click"));
assert.equal(
  atPointer(decimatorContainer, "/filter/lowPassFreqHz").value,
  "22050",
  "Java default filter uses half the lower input/output rate");
assert.equal(
  decimatorEditor.collect().interpolation,
  0,
  "integer upsampling also makes interpolation unnecessary in Java");

filterEditor.cleanup();
decimatorEditor.cleanup();
console.log("project Filter/Decimator settings contracts passed");
