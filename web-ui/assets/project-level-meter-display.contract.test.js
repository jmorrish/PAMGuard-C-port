"use strict";

const assert = require("node:assert/strict");

class FakeElement {
  constructor(tagName) {
    this.tagName = String(tagName).toUpperCase();
    this.children = [];
    this.className = "";
    this.textContent = "";
    this.attributes = new Map();
    this.style = {};
  }

  append(...children) {
    this.children.push(...children);
  }

  replaceChildren(...children) {
    this.children = [];
    this.append(...children);
  }

  setAttribute(name, value) {
    this.attributes.set(String(name), String(value));
  }
}

globalThis.document = {
  createElement(tagName) {
    return new FakeElement(tagName);
  }
};
globalThis.window = { devicePixelRatio: 1 };
globalThis.requestAnimationFrame = (callback) => {
  callback();
  return 1;
};

require("./project-displays.js");

const displays = globalThis.PamguardProjectDisplays;
assert.equal(typeof displays.mountLevelMeter, "function");

const container = new FakeElement("div");
const status = { textContent: "" };
const meter = displays.mountLevelMeter({
  container,
  status,
  settings: {
    minLevel: -80,
    scaleReference: 0,
    scaleType: 0
  },
  sourceBlockId: "block:levels",
  running: false,
  api: (path) => path
});
assert.match(status.textContent, /Start processing/);

meter.blockMetadata = {
  calibrationDbOffsetByChannel: [100, 110],
  voltsPeakToPeak: 4
};
meter.accept({
  payload: {
    measuredFrames: 12000,
    rmsDbfs: [-6.020599913279624, -3.010299956639812],
    peakDbfs: [-1, 0]
  }
});
assert.equal(container.children.length, 3);
assert.equal(container.children[0].children[1].textContent, "dB re. FS peak");
assert.equal(
  container.children[1].children[2].textContent,
  "-1.0 dB");
assert.equal(
  container.children[2].children[2].textContent,
  "0.0 dB");
assert.match(status.textContent, /2 channels.*12000 measured frames/);

meter.settings.scaleReference = 2;
meter.settings.scaleType = 1;
meter.render();
assert.equal(
  container.children[0].children[1].textContent,
  "dB re. 1\u00b5Pa RMS");
assert.equal(
  container.children[1].children[2].textContent,
  "94.0 dB");
assert.equal(
  container.children[2].children[2].textContent,
  "107.0 dB");

meter.settings.scaleReference = 1;
meter.settings.scaleType = 0;
meter.render();
assert.equal(
  container.children[0].children[1].textContent,
  "dB re. 1V peak");
assert.equal(
  container.children[2].children[2].textContent,
  "6.0 dB");

meter.settings.scaleReference = 2;
meter.blockMetadata.calibrationDbOffsetByChannel = [];
meter.render();
assert.match(
  container.children[0].textContent,
  /Micropascal reference unavailable/);

meter.dispose();
assert.equal(meter.disposed, true);

console.log(
  "Level Meter display reference, peak/RMS, and range contract passed");
