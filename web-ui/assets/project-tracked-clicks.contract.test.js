"use strict";

const assert = require("node:assert/strict");

globalThis.window = { devicePixelRatio: 1 };
globalThis.requestAnimationFrame = (callback) => {
  callback();
  return 1;
};

function fakeCanvas() {
  const listeners = new Map();
  const context = {
    fillRect() {},
    fillText() {},
    beginPath() {},
    moveTo() {},
    lineTo() {},
    stroke() {},
    save() {},
    restore() {},
    translate() {},
    rotate() {}
  };
  return {
    width: 0,
    height: 0,
    addEventListener(type, callback) {
      listeners.set(type, callback);
    },
    getBoundingClientRect() {
      return {
        left: 0,
        top: 0,
        width: 900,
        height: 450
      };
    },
    getContext() {
      return context;
    }
  };
}

const pendingResponses = [];
const requests = [];
globalThis.fetch = async (path, options = {}) => {
  requests.push({
    path,
    method: options.method || "GET",
    body: options.body ? JSON.parse(options.body) : null,
    headers: options.headers || {}
  });
  const next = pendingResponses.shift();
  assert.ok(next, `Unexpected tracked-event request ${path}`);
  return {
    ok: next.status >= 200 && next.status < 300,
    status: next.status,
    async json() {
      return next.body;
    }
  };
};

function respond(status, body) {
  pendingResponses.push({ status, body });
}

function clickUnit(uid, startSample, timeMs) {
  return {
    uid,
    startSample,
    timeMs,
    channelBitmap: 3,
    payload: {
      startSample,
      timeMs,
      channelBitmap: 3,
      signalExcessDb: 12,
      durationSamples: 64,
      waveform: [[0, 0.5, -0.5, 0]]
    }
  };
}

function event(eventId, clicks, status = "missingBearing") {
  return {
    eventId,
    comment: "Manual Click Train Detection",
    clickCount: clicks.length,
    clicks: clicks.map((click) => ({
      uid: click.uid,
      startSample: click.startSample,
      timeMs: click.timeMs,
      channelBitmap: click.channelBitmap,
      bearingRadians: null
    })),
    localisation: {
      available: false,
      status,
      message: "Scientific prerequisite unavailable"
    }
  };
}

require("./project-displays.js");

(async () => {
  const displays = globalThis.PamguardProjectDisplays;
  const canvas = fakeCanvas();
  const status = { textContent: "" };
  const display = displays.mountClickDisplay({
    canvas,
    status,
    settings: {
      channelBitmap: 3,
      timeWindowSeconds: 20,
      bearingLimitsDegrees: [0, 180],
      amplitudeLimitsDb: [0, 30],
      iciLimitsSeconds: [0.001, 3],
      showEchoes: true
    },
    sourceBlockId: "block:clicks",
    sampleRateHz: 48000,
    clickDetectorUnitId:
      "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
    running: false,
    api: (path) => path,
    headers: () => ({ "X-API-Key": "test-key" })
  });
  display.acceptClick(clickUnit(11, 480, 1000));
  display.acceptClick(clickUnit(12, 960, 1010));
  display.markedKeys.add(display.clicks[0].key);
  display.markedKeys.add(display.clicks[1].key);

  const firstEvent = event(1, display.clicks);
  respond(201, firstEvent);
  respond(200, { events: [firstEvent] });
  await display.assignMarked(null);
  assert.deepEqual(requests[0], {
    path:
      "/v1/projects/active/click-detectors/" +
      "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa/" +
      "tracked-events:assign",
    method: "POST",
    body: {
      clicks: [
        { uid: 11, startSample: 480, channelBitmap: 3 },
        { uid: 12, startSample: 960, channelBitmap: 3 }
      ],
      eventId: null
    },
    headers: {
      "X-API-Key": "test-key",
      "Content-Type": "application/json"
    }
  });
  assert.equal(display.eventByUid.get(11), 1);
  assert.equal(display.eventByUid.get(12), 1);

  display.markedKeys.clear();
  display.markedKeys.add(display.clicks[0].key);
  respond(200, { removed: true, clickUid: 11 });
  const eventAfterRemoval = event(1, [display.clicks[1]]);
  respond(200, { events: [eventAfterRemoval] });
  await display.removeMarked();
  assert.equal(
    requests.at(-2).path,
    "/v1/projects/active/click-detectors/" +
      "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa/" +
      "tracked-clicks/11");
  assert.equal(requests.at(-2).method, "DELETE");
  assert.equal(display.eventByUid.has(11), false);

  const secondEvent = event(2, [display.clicks[0]]);
  display.trackedEvents = [eventAfterRemoval, secondEvent];
  display.trackedEventSelect = { value: "1" };
  display.trackedReassignSelect = { value: "2" };
  const combined = event(2, display.clicks);
  respond(200, combined);
  respond(200, { events: [combined] });
  await display.reassignSelectedEvent();
  assert.deepEqual(requests.at(-2).body, { targetEventId: 2 });
  assert.match(requests.at(-2).path, /\/1:reassign$/);
  assert.equal(display.trackedEvents.length, 1);

  display.trackedEventSelect.value = "2";
  respond(409, {
    available: false,
    status: "movingArrayOriginUnavailable",
    code: "moving_array_origin_unavailable",
    message:
      "Target-motion range requires the origin and heading at every click"
  });
  await display.localiseSelectedEvent();
  assert.match(requests.at(-1).path, /\/2:localise$/);
  assert.equal(requests.at(-1).method, "POST");
  assert.equal(pendingResponses.length, 0);

  display.dispose();
  console.log("project tracked-click operator contracts passed");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
