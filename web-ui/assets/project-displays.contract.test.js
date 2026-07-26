"use strict";

const assert = require("node:assert/strict");

globalThis.window = { devicePixelRatio: 1 };
globalThis.requestAnimationFrame = (callback) => {
  callback();
  return 1;
};

class FakeOffscreenCanvas {
  constructor(width, height) {
    this.width = width;
    this.height = height;
    this.log = [];
    this.context = {
      clearRect: (...args) =>
        this.log.push({ type: "clearRect", args }),
      createImageData: (imageWidth, imageHeight) => ({
        width: imageWidth,
        height: imageHeight,
        data: new Uint8ClampedArray(
          imageWidth * imageHeight * 4)
      }),
      putImageData: (...args) =>
        this.log.push({ type: "putImageData", args })
    };
  }

  getContext(type) {
    assert.equal(type, "2d");
    return this.context;
  }
}
globalThis.OffscreenCanvas = FakeOffscreenCanvas;

function fakeContext(log) {
  return {
    fillStyle: "",
    fillRect(...args) {
      log.push({ type: "fillRect", fillStyle: this.fillStyle, args });
    },
    fillText(text, ...args) {
      log.push({ type: "fillText", text, args });
    },
    drawImage(...args) {
      log.push({ type: "drawImage", args });
    },
    beginPath() {},
    moveTo() {},
    lineTo() {},
    stroke() {},
    save() {},
    restore() {},
    translate() {},
    rotate() {}
  };
}

function fakeCanvas() {
  const listeners = new Map();
  const log = [];
  const context = fakeContext(log);
  return {
    width: 0,
    height: 0,
    log,
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
    getContext(type) {
      assert.equal(type, "2d");
      return context;
    },
    dispatch(type, event) {
      listeners.get(type)?.(event);
    }
  };
}

require("./project-displays.js");

const displays = globalThis.PamguardProjectDisplays;
assert.ok(displays, "project display API was not registered");
assert.equal(typeof displays.mountClickDisplay, "function");
assert.equal(typeof displays.mountSpectrogram, "function");

function fftUnit(
  channel,
  timeMs,
  values = [1e-12, 1e-9, 1e-6],
  startSample = timeMs * 24) {
  return {
    timeMs,
    startSample,
    payload: {
      channel,
      firstBin: 0,
      magnitudeSquared: values
    }
  };
}

const firstSpectrogramCanvas = fakeCanvas();
const firstSpectrogramStatus = { textContent: "" };
const firstSpectrogram = displays.mountSpectrogram({
  canvas: firstSpectrogramCanvas,
  status: firstSpectrogramStatus,
  settings: {
    nPanels: 2,
    channelList: [0, 1],
    frequencyLimits: [0, 12000],
    amplitudeLimits: [50, 120],
    colourMap: "FIRE",
    wrapDisplay: false,
    timeScaleFixed: false,
    displayLength: 20,
    pixelsPerSlics: 3,
    showScale: true
  },
  sourceBlockId: "block:first-fft",
  sampleRateHz: 48000,
  fftLength: 1024,
  running: false,
  api: (path) => path
});
assert.deepEqual(firstSpectrogram.panelChannels, [0, 1]);
assert.match(firstSpectrogramStatus.textContent, /Start processing/);
firstSpectrogram.accept(fftUnit(0, 1000));
firstSpectrogram.accept(fftUnit(1, 1000));
firstSpectrogram.accept(fftUnit(2, 1000));
assert.equal(
  firstSpectrogram.columns.length,
  2,
  "only explicitly selected panel channels may enter history");
assert.match(firstSpectrogramStatus.textContent, /Live · 2 panels · ch 0, 1/);
assert.ok(
  firstSpectrogramCanvas.log.some(
    (entry) =>
      entry.type === "fillText" &&
      entry.text === "3 px / FFT slice"),
  "pixel-per-slice time axis was not rendered");

const secondSpectrogramCanvas = fakeCanvas();
const secondSpectrogramStatus = { textContent: "" };
const secondSpectrogram = displays.mountSpectrogram({
  canvas: secondSpectrogramCanvas,
  status: secondSpectrogramStatus,
  settings: {
    nPanels: 1,
    channelList: [2],
    frequencyLimits: [2000, 8000],
    amplitudeLimits: [65, 95],
    colourMap: "GREY",
    wrapDisplay: true,
    timeScaleFixed: true,
    displayLength: 12.5,
    pixelsPerSlics: 1,
    showScale: true
  },
  sourceBlockId: "block:second-fft",
  sampleRateHz: 24000,
  fftLength: 512,
  fftHop: 256,
  calibrationDbOffsetByChannel: [170, 170, 170],
  running: false,
  api: (path) => path
});
assert.equal(
  secondSpectrogram.streamCadenceMs,
  0,
  "the live Spectrogram must consume every FFT slice");
assert.equal(
  secondSpectrogram.streamHistoryFrames,
  16,
  "a new Spectrogram lost its bounded recent-frame seed");
secondSpectrogram.accept(fftUnit(0, 1000));
secondSpectrogramCanvas.log.length = 0;
for (let index = 0; index < 6; index++) {
  secondSpectrogram.accept(
    fftUnit(
      2,
      1000 + index * 5000,
      [1e-4, 1e-2, 1],
      500000 + index * 16384));
}
assert.equal(secondSpectrogram.columns.length, 6);
assert.equal(
  firstSpectrogram.columns.length,
  2,
  "independent Spectrogram instances shared frame history");
assert.match(
  secondSpectrogramStatus.textContent,
  /Live · 1 panel · ch 2 · 2\.0-8\.0 kHz/,
  "second display did not apply its independent channel/frequency range");
assert.ok(
  secondSpectrogramCanvas.log.some(
    (entry) =>
      entry.type === "fillText" &&
      entry.text === "wrap 12.5 s"),
  "fixed wrap time axis was not rendered");
assert.ok(
  Math.abs(
    secondSpectrogram.calibratedFftDb(1, 2) -
      (170 + 20 * Math.log10(Math.SQRT2 / 512))) <
    1e-9,
  "FFT display did not use PAMGuard's normalisation and calibration");
assert.ok(
  secondSpectrogramCanvas.log.some(
    (entry) =>
      entry.type === "fillText" &&
      entry.text === "95") &&
  secondSpectrogramCanvas.log.some(
    (entry) =>
      entry.type === "fillText" &&
      entry.text === "65"),
  "positive PAMGuard acoustic amplitude limits were inverted");
const fixedTimeColumns = secondSpectrogramCanvas.log.filter(
  (entry) =>
    entry.type === "fillRect" &&
    String(entry.fillStyle).startsWith("rgb(") &&
    entry.args[0] < secondSpectrogramCanvas.width - 44);
assert.equal(
  fixedTimeColumns.length,
  0,
  "fixed-time rendering recalculated historical cells on the main canvas");
const fixedRaster = secondSpectrogram.rastersByChannel.get(2);
assert.ok(fixedRaster, "fixed-time rendering did not create a ring raster");
const rasterWrites = fixedRaster.canvas.log.filter(
  (entry) => entry.type === "putImageData");
assert.deepEqual(
  rasterWrites.map((entry) => entry.args[1]),
  [0, 1, 2, 3, 4, 5],
  "consecutive live FFT slices did not advance contiguous ring columns");
assert.ok(
  secondSpectrogramCanvas.log.some(
    (entry) => entry.type === "drawImage"),
  "fixed-time rendering did not composite its cached raster");

const bufferedCanvas = fakeCanvas();
const bufferedStatus = { textContent: "" };
const bufferedSpectrogram = displays.mountSpectrogram({
  canvas: bufferedCanvas,
  status: bufferedStatus,
  settings: {
    nPanels: 1,
    channelList: [0],
    frequencyLimits: [0, 24000],
    amplitudeLimits: [50, 120],
    colourMap: "HOT",
    wrapDisplay: false,
    timeScaleFixed: true,
    displayLength: 20,
    pixelsPerSlics: 1,
    showScale: true
  },
  sourceBlockId: "",
  sampleRateHz: 48000,
  fftLength: 1024,
  fftHop: 512,
  running: true,
  presentationDelayMs: 1000,
  api: (path) => path
});
bufferedSpectrogram.accept(fftUnit(0, 1000));
bufferedSpectrogram.accept(fftUnit(0, 1011));
bufferedSpectrogram.accept(fftUnit(0, 1022));
assert.equal(
  bufferedSpectrogram.columns.length,
  0,
  "live FFT frames bypassed the presentation buffer");
assert.equal(bufferedSpectrogram.pendingColumns.length, 3);
assert.match(bufferedStatus.textContent, /Buffering live FFT/);
const presentationStart =
  bufferedSpectrogram.presentationWallOriginMs;
bufferedSpectrogram.drainPresentation(presentationStart - 1);
assert.equal(
  bufferedSpectrogram.columns.length,
  0,
  "presentation began before the configured delay");
bufferedSpectrogram.drainPresentation(presentationStart);
assert.equal(bufferedSpectrogram.columns.length, 1);
bufferedSpectrogram.drainPresentation(presentationStart + 11);
assert.equal(bufferedSpectrogram.columns.length, 2);
bufferedSpectrogram.drainPresentation(presentationStart + 22);
assert.equal(bufferedSpectrogram.columns.length, 3);
assert.deepEqual(
  bufferedSpectrogram.columns.map((column) => column.sequence),
  [0, 1, 2],
  "buffered FFT frames did not retain contiguous display sequencing");
const bufferedMetrics =
  bufferedCanvas.__pamguardSpectrogramMetrics();
assert.equal(bufferedMetrics.presentationDelayMs, 1000);
assert.equal(bufferedMetrics.queuedFrames, 0);
assert.equal(bufferedMetrics.presentedFrames, 3);
bufferedSpectrogram.dispose();

const immediateAnimationFrame =
  globalThis.requestAnimationFrame;
const priorCancelAnimationFrame =
  globalThis.cancelAnimationFrame;
const queuedAnimationFrames = [];
globalThis.requestAnimationFrame = (callback) => {
  queuedAnimationFrames.push(callback);
  return queuedAnimationFrames.length;
};
globalThis.cancelAnimationFrame = () => {};
const smoothCanvas = fakeCanvas();
const smoothSpectrogram = displays.mountSpectrogram({
  canvas: smoothCanvas,
  status: { textContent: "" },
  settings: {
    nPanels: 1,
    channelList: [0],
    frequencyLimits: [0, 24000],
    amplitudeLimits: [50, 120],
    colourMap: "HOT",
    wrapDisplay: false,
    timeScaleFixed: true,
    displayLength: 20,
    pixelsPerSlics: 1,
    showScale: true
  },
  sourceBlockId: "",
  sampleRateHz: 48000,
  fftLength: 1024,
  fftHop: 512,
  running: true,
  presentationDelayMs: 1000,
  api: (path) => path
});
// An empty source avoids an asynchronous stream connection in this isolated
// test; enable the production scrolling path before feeding it frames.
smoothSpectrogram.smoothScroll = true;
for (let index = 0; index < 200; index++) {
  smoothSpectrogram.accept(
    fftUnit(
      0,
      1000 + index * 512 / 48,
      [1e-4, 1e-2, 1],
      index * 512));
}
const smoothStart =
  smoothSpectrogram.presentationWallOriginMs;
smoothSpectrogram.drainPresentation(smoothStart, false);
const initialCursor =
  smoothSpectrogram.displayCursorByChannel.get(0);
assert.ok(
  Math.abs(initialCursor - 105.25) < 1e-9,
  "smooth presentation did not start one second behind the live edge");
assert.ok(
  smoothSpectrogram.rastersByChannel.get(0).slots >
    smoothSpectrogram.rastersByChannel.get(0).displaySlots,
  "smooth raster omitted its future-frame reserve");
smoothCanvas.log.length = 0;
smoothSpectrogram.render();
assert.ok(
  smoothCanvas.log.some(
    (entry) =>
      entry.type === "drawImage" &&
      entry.args.some(
        (value) =>
          Number.isFinite(value) &&
          Math.abs(value - Math.round(value)) > 1e-6)),
  "smooth rendering snapped its source window to integer FFT columns");

smoothSpectrogram.latestSequenceByChannel.set(0, 220);
smoothSpectrogram.advanceSmoothCursors(smoothStart + 100);
assert.ok(
  smoothSpectrogram.displayCursorByChannel.get(0) -
    initialCursor >
    48000 / 512 * 0.1,
  "delay lock did not accelerate toward an over-full buffer");
smoothSpectrogram.displayCursorByChannel.set(0, 0);
smoothSpectrogram.latestSequenceByChannel.set(0, 500);
smoothSpectrogram.advanceSmoothCursors(smoothStart + 200);
assert.ok(
  Math.abs(
    smoothSpectrogram.displayCursorByChannel.get(0) -
    (500 - 48000 / 512)) < 1e-9,
  "gross backlog recovery did not re-anchor at the requested delay");
assert.equal(
  smoothCanvas.__pamguardSpectrogramMetrics().smoothScroll,
  true);
smoothSpectrogram.dispose();
globalThis.requestAnimationFrame =
  immediateAnimationFrame;
if (priorCancelAnimationFrame === undefined) {
  delete globalThis.cancelAnimationFrame;
}
else {
  globalThis.cancelAnimationFrame =
    priorCancelAnimationFrame;
}

firstSpectrogram.dispose();
secondSpectrogram.dispose();

const canvas = fakeCanvas();
const status = { textContent: "" };
const clickDetail = { hidden: true };
const clickDetailStatus = { textContent: "" };
const clickWaveformCanvas = fakeCanvas();
const clickSpectrumCanvas = fakeCanvas();
const clickSpectrumStatus = { textContent: "" };
const clickWignerCanvas = fakeCanvas();
const clickWignerStatus = { textContent: "" };
const clickDisplay = displays.mountClickDisplay({
  canvas,
  status,
  detailRoot: clickDetail,
  detailStatus: clickDetailStatus,
  waveformCanvas: clickWaveformCanvas,
  spectrumCanvas: clickSpectrumCanvas,
  spectrumStatus: clickSpectrumStatus,
  wignerCanvas: clickWignerCanvas,
  wignerStatus: clickWignerStatus,
  settings: {
    channelBitmap: 3,
    timeWindowSeconds: 20,
    bearingLimitsDegrees: [0, 180],
    amplitudeLimitsDb: [0, 30],
    iciLimitsSeconds: [0.001, 3],
    showEchoes: false
  },
  sourceBlockId: "block:clicks",
  bearingBlockId: "block:bearings",
  sampleRateHz: 48000,
  running: false,
  api: (path) => path
});

assert.match(status.textContent, /Start processing/);

function clickUnit(startSample, timeMs, channelBitmap = 1, extra = {}) {
  return {
    uid: startSample + 1,
    timeMs,
    channelBitmap,
    payload: {
      startSample,
      timeMs,
      channelBitmap,
      signalExcessDb: 12,
      durationSamples: 64,
      waveform: [[0, 0.5, -0.5, 0]],
      ...extra
    }
  };
}

clickDisplay.acceptClick(clickUnit(0, 1000));
clickDisplay.acceptClick(clickUnit(48000, 2000));
assert.equal(clickDisplay.clicks.length, 2);
assert.equal(clickDisplay.clicks[1].iciSeconds, 1);
assert.match(status.textContent, /2 clicks/);

clickDisplay.acceptClick(clickUnit(48000, 2000));
clickDisplay.acceptClick(clickUnit(72000, 2500, 4));
clickDisplay.acceptClick(clickUnit(
  96000,
  3000,
  1,
  { echo: true }));
assert.equal(
  clickDisplay.clicks.length,
  2,
  "duplicates, unselected channels, and hidden echoes must not enter history");

clickDisplay.acceptBearing({
  payload: {
    clickStartSample: 48000,
    valid: true,
    azimuthDegrees: 37.5
  }
});
assert.equal(clickDisplay.clicks[1].bearingDegrees, 37.5);
assert.match(status.textContent, /1 bearings/);

for (let index = 2; index < 20; index++) {
  clickDisplay.acceptClick(
    clickUnit(index * 48000, 1000 + index * 1000));
}
assert.equal(clickDisplay.clicks.length, 20);
assert.equal(clickDisplay.clicks.at(-1).iciSeconds, 1);
assert.match(
  status.textContent,
  /clicks \/ 20 s/,
  "continuous history status must remain visible while clicks stream");

assert.ok(
  clickDisplay.hitPoints.length > 0,
  "rendered click history did not expose selectable points");
const point = clickDisplay.hitPoints[0];
canvas.dispatch("click", {
  clientX: point.x,
  clientY: point.y
});
assert.ok(clickDisplay.selected, "click selection did not persist");
assert.match(status.textContent, /selected sample/);
assert.equal(
  clickDetail.hidden,
  false,
  "selected-click detail remained hidden");
assert.match(clickDetailStatus.textContent, /sample \d+/);
assert.match(clickSpectrumStatus.textContent, /FFT 4/);
assert.match(clickSpectrumStatus.textContent, /peak/);
assert.ok(
  clickSpectrumCanvas.log.some(
    (entry) => entry.type === "fillText"),
  "selected click did not render its power spectrum axes");
assert.match(clickWignerStatus.textContent, /128 samples/);
assert.ok(
  clickWignerCanvas.log.some(
    (entry) => entry.type === "drawImage"),
  "selected click did not render its Wigner–Ville distribution");
clickDisplay.selected.waveform = [];
clickDisplay.renderSelectedClick();
assert.match(
  clickSpectrumStatus.textContent,
  /unavailable without waveform/,
  "missing click waveform did not produce an explicit spectrum state");
assert.match(
  clickWignerStatus.textContent,
  /unavailable without waveform/,
  "missing click waveform did not produce an explicit detail state");

const tone = Array.from(
  { length: 128 },
  (_, index) => Math.sin(2 * Math.PI * 6000 * index / 48000));
clickDisplay.selected = clickDisplay.clicks[0];
clickDisplay.selected.waveform = [tone, tone];
clickDisplay.renderSelectedClick();
assert.match(
  clickSpectrumStatus.textContent,
  /peak 6\.00 kHz/,
  "known 6 kHz click did not report the correct overall peak");
assert.match(
  clickSpectrumStatus.textContent,
  /ch 0 6\.00 kHz · ch 1 6\.00 kHz/,
  "known 6 kHz click did not report per-channel peaks");

clickDisplay.dispose();
assert.equal(clickDisplay.disposed, true);

const clickAnimationFrame =
  globalThis.requestAnimationFrame;
const clickCancelAnimationFrame =
  globalThis.cancelAnimationFrame;
const queuedClickFrames = [];
globalThis.requestAnimationFrame = (callback) => {
  queuedClickFrames.push(callback);
  return queuedClickFrames.length;
};
globalThis.cancelAnimationFrame = () => {};
const smoothClickCanvas = fakeCanvas();
const smoothClickDisplay = displays.mountClickDisplay({
  canvas: smoothClickCanvas,
  status: { textContent: "" },
  settings: {
    channelBitmap: 1,
    timeWindowSeconds: 20,
    bearingLimitsDegrees: [0, 180],
    amplitudeLimitsDb: [0, 30],
    iciLimitsSeconds: [0.001, 3],
    showEchoes: false
  },
  sourceBlockId: "",
  bearingBlockId: "",
  sampleRateHz: 48000,
  running: true,
  presentationDelayMs: 1000,
  api: (path) => path
});
// Keep network setup out of this renderer-only test while exercising the
// production continuous timeline.
smoothClickDisplay.continuousTimeline = true;
smoothClickDisplay.acceptClick(clickUnit(384000, 8000));
smoothClickDisplay.acceptClick(clickUnit(480000, 10000));
const clickClockStart =
  smoothClickDisplay.latestArrivalWallMs;
smoothClickDisplay.advanceTimeline(clickClockStart);
assert.equal(
  smoothClickDisplay.displayEndMs,
  9000,
  "Click display did not begin one second behind its source clock");
smoothClickDisplay.render();
const firstClickX = smoothClickDisplay.hitPoints.find(
  (pointEntry) => pointEntry.click.timeMs === 8000)?.x;
smoothClickDisplay.advanceTimeline(clickClockStart + 100);
smoothClickDisplay.render();
const secondClickX = smoothClickDisplay.hitPoints.find(
  (pointEntry) => pointEntry.click.timeMs === 8000)?.x;
assert.ok(
  Number.isFinite(firstClickX) &&
  Number.isFinite(secondClickX) &&
  secondClickX < firstClickX,
  "Click positions did not move continuously between detections");
assert.ok(
  smoothClickDisplay.displayEndMs - 9000 > 100,
  "Click delay lock did not accelerate toward an over-full buffer");
smoothClickDisplay.displayEndMs = 0;
smoothClickDisplay.advanceTimeline(clickClockStart + 200);
assert.equal(
  smoothClickDisplay.displayEndMs,
  9200,
  "Click timeline did not recover from a gross interruption");
const smoothClickMetrics =
  smoothClickCanvas.__pamguardClickMetrics();
assert.equal(smoothClickMetrics.continuousTimeline, true);
assert.equal(smoothClickMetrics.presentationDelayMs, 1000);
smoothClickDisplay.dispose();
globalThis.requestAnimationFrame = clickAnimationFrame;
if (clickCancelAnimationFrame === undefined) {
  delete globalThis.cancelAnimationFrame;
}
else {
  globalThis.cancelAnimationFrame =
    clickCancelAnimationFrame;
}

console.log("project display contracts passed");
