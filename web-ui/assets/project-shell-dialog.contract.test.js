"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const shellSource = fs.readFileSync(
  path.join(__dirname, "project-shell.js"),
  "utf8");
const functionStart = shellSource.indexOf(
  "  async function showFormDialog(");
const functionEnd = shellSource.indexOf(
  "\n\n  async function pollStatus(",
  functionStart);
assert(functionStart >= 0, "project shell omitted showFormDialog");
assert(functionEnd > functionStart, "could not isolate showFormDialog");
const showFormDialogSource = shellSource.slice(
  functionStart,
  functionEnd);

function createClassList() {
  const values = new Set();
  return {
    contains: (value) => values.has(value),
    remove: (value) => values.delete(value),
    toggle(value, force) {
      const enabled = force === undefined ? !values.has(value) : force;
      if (enabled) values.add(value);
      else values.delete(value);
      return enabled;
    }
  };
}

function createDialogHarness() {
  const closeListeners = new Set();
  let failNextShow = true;
  const cancel = { hidden: false };
  const accept = {
    textContent: "",
    classList: createClassList()
  };
  const dialog = {
    open: false,
    returnValue: "",
    addEventListener(type, listener) {
      assert.equal(type, "close");
      closeListeners.add(listener);
    },
    removeEventListener(type, listener) {
      assert.equal(type, "close");
      closeListeners.delete(listener);
    },
    querySelector() {
      return null;
    },
    showModal() {
      if (failNextShow) {
        failNextShow = false;
        throw new Error("forced showModal failure");
      }
      this.open = true;
    },
    close(returnValue = "") {
      this.returnValue = returnValue;
      this.open = false;
      const listeners = Array.from(closeListeners);
      closeListeners.clear();
      for (const listener of listeners) listener();
    }
  };
  const body = {
    children: [],
    replaceChildren(...children) {
      this.children = children;
    }
  };
  const form = {
    querySelector() {
      return cancel;
    }
  };
  const elements = {
    formDialog: dialog,
    dialogForm: form,
    dialogEyebrow: { textContent: "" },
    dialogTitle: { textContent: "" },
    dialogBody: body,
    dialogNote: { textContent: "" },
    dialogAccept: accept
  };
  const createShowFormDialog = new Function(
    "elements",
    "showToast",
    "requestAnimationFrame",
    `
      let formDialogCloseBarrier = Promise.resolve();
      const $ = (id) => elements[id];
      ${showFormDialogSource}
      return showFormDialog;
    `);
  return {
    accept,
    cancel,
    dialog,
    listenerCount: () => closeListeners.size,
    showFormDialog: createShowFormDialog(
      elements,
      () => {},
      (callback) => callback())
  };
}

async function dialogFailureRecoveryContract() {
  const harness = createDialogHarness();
  await assert.rejects(
    harness.showFormDialog({
      eyebrow: "Contract",
      title: "Expected failure",
      body: {},
      dangerous: true,
      cancelHidden: true
    }),
    /forced showModal failure/);
  assert.equal(
    harness.listenerCount(),
    0,
    "failed showModal left a stale close listener");
  assert.equal(
    harness.accept.classList.contains("danger"),
    false,
    "failed showModal left the destructive action style mounted");
  assert.equal(
    harness.cancel.hidden,
    false,
    "failed showModal left the cancel action hidden");

  const recovered = harness.showFormDialog({
    eyebrow: "Contract",
    title: "Recovered dialog",
    body: {}
  });
  await Promise.resolve();
  assert.equal(
    harness.dialog.open,
    true,
    "a rejected close barrier blocked the next dialog");
  assert.equal(
    harness.listenerCount(),
    1,
    "recovered dialog did not own exactly one close listener");
  harness.dialog.close("default");
  assert.equal(
    await recovered,
    true,
    "recovered dialog did not preserve normal acceptance semantics");
  assert.equal(harness.listenerCount(), 0);
}

dialogFailureRecoveryContract().catch((error) => {
  process.nextTick(() => {
    throw error;
  });
});
