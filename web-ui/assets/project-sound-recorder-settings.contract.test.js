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
    this.disabled = false;
    this.hidden = false;
    this.readOnly = false;
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
    if (!this.disabled) {
      this.dispatchEvent(new FakeEvent("click"));
    }
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
require("./project-sound-recorder-settings.js");

const moduleApi =
  globalThis.PamguardProjectSoundRecorderSettings;
assert.ok(moduleApi?.mountEditor);
assert.ok(moduleApi?.canonicalSettings);
assert.ok(moduleApi?.defaultSettings);

const fixture = JSON.parse(fs.readFileSync(
  path.join(
    __dirname,
    "..",
    "..",
    "cpp-engine",
    "tests",
    "fixtures",
    "sound-recorder",
    "settings-defaults.json"),
  "utf8"));
assert.equal(fixture.authority.version, "2.02.18e");
assert.equal(
  fixture.authority.commit,
  "dca55c81ef6f1498a8a3b926c69e7182afb915ee");
assert.deepEqual(
  moduleApi.defaultSettings(),
  fixture.portableSettingsDefaults,
  "Browser defaults must be the pinned Java-exported defaults");
assert.deepEqual(
  moduleApi.canonicalSettings(fixture.portableSettingsDefaults),
  fixture.portableSettingsDefaults);

assert.throws(
  () => moduleApi.canonicalSettings({}),
  /must contain exactly/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...fixture.portableSettingsDefaults,
    channelBitmap: 0
  }),
  /channel bitmap has an invalid value/i);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...fixture.portableSettingsDefaults,
    bitDepth: 12
  }),
  /8, 16, 24, or 32/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...fixture.portableSettingsDefaults,
    operationMode: "automatic"
  }),
  /idle, continuous, cycle, or restore-last/);
assert.throws(
  () => moduleApi.canonicalSettings({
    ...fixture.portableSettingsDefaults,
    autoIntervalSeconds: 10,
    autoDurationSeconds: 10
  }),
  /cycle time must be greater/i);

const triggerDefault = fixture.triggerPolicyFieldDefaults;
assert.throws(
  () => moduleApi.canonicalSettings({
    ...fixture.portableSettingsDefaults,
    triggerPolicies: [
      triggerDefault,
      { ...triggerDefault }
    ]
  }),
  /names must be unique/i);

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
      element.getAttribute(attribute) === String(value));
}

function atAttribute(root, attribute, value) {
  return allAtAttribute(root, attribute, value)[0] || null;
}

function atPointer(root, pointer) {
  return atAttribute(root, "data-setting-pointer", pointer);
}

function renderedText(root) {
  return allElements(root)
    .map((element) => element.textContent)
    .join(" ");
}

(async () => {
  const settings = {
    ...fixture.portableSettingsDefaults,
    operationMode: "restore-last",
    triggerPolicies: [{
      ...triggerDefault,
      triggerName: "Whistle and Moan Detector",
      usedDayBudgetBytes: 1048576,
      lastTriggerStartUnixMs: 1720000000000,
      lastTriggerEndUnixMs: 1720000009000
    }]
  };
  const original = structuredClone(settings);
  const runtimeActions = [];
  const errors = [];
  const container = document.createElement("div");
  document.body.append(container);
  const editor = moduleApi.mountEditor({
    container,
    settings,
    sourceLabel: "Acquisition 1 / Raw audio",
    outputFolderLabel: "Host recording directory",
    availableChannelBitmap: 7,
    onRuntimeAction(action) {
      runtimeActions.push(action);
      return {
        state: action,
        message: `Recorder is ${action}.`
      };
    },
    reportError(error) {
      errors.push(error);
    }
  });

  assert.ok(atAttribute(
    container,
    "data-pamguard-sound-recorder-settings-editor",
    "true"));
  const offRuntimeAction = atAttribute(
    container,
    "data-sound-recorder-action",
    "off");
  const continuousRuntimeAction = atAttribute(
    container,
    "data-sound-recorder-action",
    "continuous");
  assert.equal(
    offRuntimeAction.disabled,
    false,
    "Connected Off transport must remain operator-controllable");
  assert.equal(
    continuousRuntimeAction.disabled,
    false,
    "Connected Continuous transport must remain operator-controllable");
  editor.setRuntimeStatus({
    state: "continuous",
    message:
      "Continuous \u00b7 writing PAM_20260725_193000.wav \u00b7 128 frames"
  });
  const safeRuntimeStatus = atAttribute(
    container,
    "data-sound-recorder-runtime-status",
    "continuous");
  assert.match(
    safeRuntimeStatus.textContent,
    /writing PAM_20260725_193000\.wav/);
  assert.doesNotMatch(
    safeRuntimeStatus.textContent,
    /[A-Za-z]:[\\/]|runtime[-_ ]?(?:node|block|id)/i,
    "Recorder status must remain a safe filename/status surface");
  assert.equal(
    allElements(container).some(
      (element) => element.tagName === "TEXTAREA"),
    false,
    "Sound Recorder settings must never use generic JSON");

  for (const pointer of [
    "/operationMode",
    "/channelBitmap",
    "/bitDepth",
    "/enableBuffer",
    "/bufferLengthSeconds",
    "/fileInitials",
    "/fileType",
    "/autoIntervalSeconds",
    "/autoDurationSeconds",
    "/limitLengthSeconds",
    "/maxLengthSeconds",
    "/roundFileStarts",
    "/limitLengthMegaBytes",
    "/maxLengthMegaBytes",
    "/datedSubFolders"
  ]) {
    assert.ok(atPointer(container, pointer), `Missing ${pointer}`);
  }
  for (const fieldName of [
    "triggerName",
    "enabled",
    "secondsBeforeTrigger",
    "secondsAfterTrigger",
    "minDetectionCount",
    "countSeconds",
    "minGapBetweenTriggersSeconds",
    "maxTotalTriggerLengthSeconds",
    "dayBudgetMegaBytes",
    "lastTriggerStartUnixMs",
    "lastTriggerEndUnixMs",
    "usedDayBudgetBytes"
  ]) {
    assert.ok(
      atPointer(
        container,
        `/triggerPolicies/0/${fieldName}`),
      `Missing trigger control ${fieldName}`);
  }

  assert.equal(
    atPointer(
      container,
      "/triggerPolicies/0/triggerName").readOnly,
    true,
    "Trigger identities must remain graph/capability owned");
  assert.equal(
    atPointer(
      container,
      "/triggerPolicies/0/usedDayBudgetBytes").readOnly,
    true,
    "Trigger bookkeeping must not be free-form operator input");

  const text = renderedText(container);
  assert.match(text, /Control/);
  assert.match(text, /Files and Folders/);
  assert.match(text, /Triggered Recordings/);
  assert.match(text, /PAMGuard Startup Options/);
  assert.match(text, /Remain idle/);
  assert.match(text, /Start recording cycle/);
  assert.match(text, /Raw data source/);
  assert.match(text, /Acquisition 1 \/ Raw audio/);
  assert.match(text, /Host recording directory/);
  assert.match(text, /Starting the processing graph leaves the recorder safely Off/);
  assert.match(text, /Off and Continuous are live operator commands/);
  assert.match(text, /Automatic Cycle, Continuous \+ Buffer/);
  assert.match(text, /trigger-controlled transport are not implemented/);
  assert.match(text, /Click Detector clicks are not a default/);
  assert.doesNotMatch(
    text,
    /RainbowClick|sound alarm|Swing colours|legacy file/i);

  assert.ok(atAttribute(
    container,
    "data-sound-recorder-boundary",
    "graph-source"));
  assert.ok(atAttribute(
    container,
    "data-sound-recorder-boundary",
    "host-output-folder"));
  assert.equal(
    Object.prototype.hasOwnProperty.call(
      editor.collect(),
      "rawDataSource"),
    false);
  assert.equal(
    Object.prototype.hasOwnProperty.call(
      editor.collect(),
      "outputFolder"),
    false);

  const filesTab = atAttribute(
    container,
    "data-sound-recorder-tab",
    "files");
  const filesPanel = atAttribute(
    container,
    "data-sound-recorder-panel",
    "files");
  assert.equal(filesPanel.hidden, true);
  filesTab.click();
  assert.equal(filesPanel.hidden, false);
  assert.equal(filesTab.getAttribute("aria-selected"), "true");

  const operationControls = allAtAttribute(
    container,
    "data-setting-pointer",
    "/operationMode");
  operationControls.forEach((control) => {
    control.checked = control.value === "continuous";
  });
  const channelZero = atAttribute(
    container,
    "data-sound-recorder-channel",
    "0");
  const channelTwo = atAttribute(
    container,
    "data-sound-recorder-channel",
    "2");
  channelZero.checked = false;
  channelTwo.checked = true;

  atPointer(container, "/bitDepth").value = "24";
  const enableBuffer = atPointer(container, "/enableBuffer");
  enableBuffer.checked = true;
  enableBuffer.dispatchEvent(new Event("change"));
  assert.equal(
    atPointer(container, "/bufferLengthSeconds").disabled,
    false);
  atPointer(container, "/bufferLengthSeconds").value = "12";
  atPointer(container, "/autoDurationSeconds").value = "20";
  atPointer(container, "/autoIntervalSeconds").value = "90";
  atPointer(container, "/fileInitials").value = "OPS";
  atPointer(container, "/fileType").value = "WAVE";
  atPointer(container, "/datedSubFolders").checked = false;
  const limitSeconds = atPointer(
    container,
    "/limitLengthSeconds");
  limitSeconds.checked = false;
  limitSeconds.dispatchEvent(new Event("change"));
  assert.equal(
    atPointer(container, "/maxLengthSeconds").disabled,
    true);
  assert.equal(
    atPointer(container, "/roundFileStarts").disabled,
    true);
  atPointer(container, "/maxLengthSeconds").value = "1800";
  atPointer(container, "/roundFileStarts").checked = false;
  atPointer(container, "/maxLengthMegaBytes").value = "512";

  atPointer(
    container,
    "/triggerPolicies/0/enabled").checked = true;
  atPointer(
    container,
    "/triggerPolicies/0/secondsBeforeTrigger").value = "1.5";
  atPointer(
    container,
    "/triggerPolicies/0/secondsAfterTrigger").value = "7.25";
  atPointer(
    container,
    "/triggerPolicies/0/minDetectionCount").value = "2";
  atPointer(
    container,
    "/triggerPolicies/0/countSeconds").value = "3";
  atPointer(
    container,
    "/triggerPolicies/0/minGapBetweenTriggersSeconds").value = "4";
  atPointer(
    container,
    "/triggerPolicies/0/maxTotalTriggerLengthSeconds").value = "5";
  atPointer(
    container,
    "/triggerPolicies/0/dayBudgetMegaBytes").value = "6";
  atAttribute(
    container,
    "data-sound-recorder-action",
    "reset-trigger-budget").click();
  assert.equal(
    atPointer(
      container,
      "/triggerPolicies/0/usedDayBudgetBytes").value,
    "0");

  atAttribute(
    container,
    "data-sound-recorder-action",
    "continuous").click();
  atAttribute(
    container,
    "data-sound-recorder-action",
    "off").click();
  await Promise.resolve();
  assert.deepEqual(runtimeActions, ["continuous", "off"]);
  assert.equal(
    atAttribute(
      container,
      "data-sound-recorder-runtime-status",
      "off").textContent,
    "Recorder is off.");
  assert.deepEqual(errors, []);

  const collected = editor.collect();
  assert.deepEqual(collected, {
    operationMode: "continuous",
    channelBitmap: 6,
    bitDepth: 24,
    enableBuffer: true,
    bufferLengthSeconds: 12,
    fileInitials: "OPS",
    fileType: "WAVE",
    autoIntervalSeconds: 90,
    autoDurationSeconds: 20,
    limitLengthSeconds: false,
    maxLengthSeconds: 1800,
    roundFileStarts: false,
    limitLengthMegaBytes: true,
    maxLengthMegaBytes: 512,
    datedSubFolders: false,
    triggerPolicies: [{
      triggerName: "Whistle and Moan Detector",
      enabled: true,
      secondsBeforeTrigger: 1.5,
      secondsAfterTrigger: 7.25,
      minDetectionCount: 2,
      countSeconds: 3,
      minGapBetweenTriggersSeconds: 4,
      maxTotalTriggerLengthSeconds: 5,
      dayBudgetMegaBytes: 6,
      lastTriggerStartUnixMs: 1720000000000,
      lastTriggerEndUnixMs: 1720000009000,
      usedDayBudgetBytes: 0
    }]
  });
  assert.deepEqual(
    moduleApi.canonicalSettings(collected),
    collected,
    "OK must emit canonical Sound Recorder settings");
  assert.deepEqual(
    settings,
    original,
    "Editing and Cancel must leave the supplied settings untouched");

  const roundtripContainer = document.createElement("div");
  const roundtripEditor = moduleApi.mountEditor({
    container: roundtripContainer,
    settings: collected,
    availableChannelBitmap: 7
  });
  assert.deepEqual(
    roundtripEditor.collect(),
    collected,
    "Saved settings must survive a canonical editor roundtrip");

  const defaultContainer = document.createElement("div");
  const defaultEditor = moduleApi.mountEditor({
    container: defaultContainer,
    settings: moduleApi.defaultSettings()
  });
  assert.deepEqual(
    defaultEditor.collect(),
    fixture.portableSettingsDefaults);
  assert.match(
    renderedText(defaultContainer),
    /No eligible trigger policies are connected/);
  assert.equal(
    atAttribute(
      defaultContainer,
      "data-sound-recorder-action",
      "off").disabled,
    true,
    "Runtime controls remain visible but disabled until the service hook exists");

  editor.cleanup();
  roundtripEditor.cleanup();
  defaultEditor.cleanup();
  console.log(
    "Sound Recorder dedicated settings browser contract passed");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
