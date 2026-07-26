"use strict";

const assert = require("node:assert/strict");

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
  constructor(tagName) {
    this.tagName = String(tagName).toUpperCase();
    this.parentNode = null;
    this.children = [];
    this.attributes = new Map();
    this.className = "";
    this.textContent = "";
    this.type = "";
    this.value = "";
    this.checked = false;
  }

  append(...children) {
    for (const child of children) {
      child.parentNode = this;
      this.children.push(child);
    }
  }

  setAttribute(name, value) {
    this.attributes.set(String(name), String(value));
  }

  getAttribute(name) {
    return this.attributes.has(String(name))
      ? this.attributes.get(String(name))
      : null;
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
    this.head = new FakeElement("head");
    this.body = new FakeElement("body");
  }

  createElement(tag) {
    return new FakeElement(tag);
  }

  querySelector(selector) {
    return this.head.querySelector(selector) ||
      this.body.querySelector(selector);
  }
}

globalThis.document = new FakeDocument();
require("./project-level-meter-settings.js");

const moduleApi = globalThis.PamguardProjectLevelMeterSettings;
assert.ok(moduleApi?.mountEditor);
assert.deepEqual(moduleApi.canonicalSettings({}), {
  minLevel: -80,
  scaleReference: 0,
  scaleType: 0
});

const container = document.createElement("div");
document.body.append(container);
const editor = moduleApi.mountEditor({
  container,
  settings: {
    minLevel: -65,
    scaleReference: 2,
    scaleType: 1
  }
});

function atPointer(pointer) {
  return container.querySelector(
    `[data-setting-pointer='${pointer}']`);
}

assert.ok(container.querySelector(
  "[data-pamguard-level-meter-settings-editor]"));
assert.equal(container.querySelectorAll("textarea").length, 0);
assert.deepEqual(editor.collect(), {
  minLevel: -65,
  scaleReference: 2,
  scaleType: 1
});

const range = atPointer("/minLevel");
const reference = atPointer("/scaleReference");
const radios = container.querySelectorAll(
  "[data-setting-pointer='/scaleType']");
range.value = "12.9";
reference.value = "1";
radios[0].checked = true;
radios[1].checked = false;
assert.deepEqual(editor.collect(), {
  minLevel: -12,
  scaleReference: 1,
  scaleType: 0
});

range.value = "0.5";
assert.throws(
  () => editor.collect(),
  /greater than zero/);

console.log(
  "Level Meter dedicated settings browser contract passed");
