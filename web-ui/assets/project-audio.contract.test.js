"use strict";

const assert = require("node:assert/strict");
require("./project-audio.js");

const audio = globalThis.PamguardProjectAudio;
assert.ok(audio, "project audio API was not registered");

const normalized = audio.normalizeConfig({
  sourceBlockId: "block:acquisition:raw",
  sampleRateHz: 48000,
  channelBitmap: 3,
  settings: {
    channelBitmap: 3,
    defaultSampleRate: true,
    playbackRateHz: 44100,
    playbackSpeed: 1,
    playbackGainDb: 6,
    hpFilter: 0.125
  },
  local: {
    mix: "stereo",
    latencyMs: 100,
    muted: false
  }
});
assert.deepEqual(normalized.channels, [0, 1]);
assert.equal(normalized.outputChannels, 2);
assert.equal(normalized.highPassHz, 6000);
assert.equal(
  normalized.outputRateHz,
  44100,
  "portable playbackRateHz must drive the explicit browser output rate");
assert.throws(
  () => audio.normalizeConfig({
    sourceBlockId: "block:acquisition:raw",
    sampleRateHz: 48000,
    channelBitmap: 1,
    settings: {
      channelBitmap: 2,
      defaultSampleRate: true,
      playbackRateHz: 48000,
      playbackSpeed: 1,
      playbackGainDb: 0,
      hpFilter: 0
    }
  }),
  /does not provide/);

const highChannel = audio.normalizeConfig({
  sourceBlockId: "block:acquisition:raw",
  sampleRateHz: 48000,
  channelBitmap: 0x80000000,
  settings: {
    channelBitmap: 0x80000000,
    defaultSampleRate: true,
    playbackRateHz: 48000,
    playbackSpeed: 1,
    playbackGainDb: 0,
    hpFilter: 0
  }
});
assert.deepEqual(
  highChannel.channels,
  [31],
  "channel 31 must survive Java's unsigned 32-bit bitmap");

function packetBytes(samples, channelCount, metadata = {}) {
  const frameCount = samples.length / channelCount;
  const buffer = new ArrayBuffer(40 + samples.length * 4);
  const bytes = new Uint8Array(buffer);
  bytes.set([0x50, 0x47, 0x41, 0x31]);
  const view = new DataView(buffer);
  view.setUint32(4, 40, true);
  view.setUint32(8, channelCount, true);
  view.setUint32(12, frameCount, true);
  view.setBigInt64(16, BigInt(metadata.timeUnixMs || 0), true);
  view.setBigUint64(24, BigInt(metadata.startSample || 0), true);
  view.setBigUint64(32, BigInt(metadata.droppedFrames || 0), true);
  samples.forEach((sample, index) =>
    view.setFloat32(40 + index * 4, sample, true));
  return bytes;
}

const encoded = packetBytes(
  [1, -1, 0.5, -0.5],
  2,
  { timeUnixMs: 1234, startSample: 99, droppedFrames: 7 });
const decoder = new audio.Pga1Decoder(2);
assert.equal(decoder.push(encoded.slice(0, 17)).length, 0);
const decoded = decoder.push(encoded.slice(17));
assert.equal(decoded.length, 1);
assert.equal(decoded[0].frameCount, 2);
assert.equal(decoded[0].startSample, 99);
assert.equal(decoded[0].droppedFrames, 7);
assert.deepEqual(Array.from(decoded[0].samples), [1, -1, 0.5, -0.5]);

const mono = audio.mixPacket(decoded[0], "mono");
assert.deepEqual(Array.from(mono), [0, 0]);
const stereo = audio.mixPacket({
  frameCount: 2,
  channelCount: 1,
  samples: new Float32Array([0.25, -0.75])
}, "stereo");
assert.deepEqual(Array.from(stereo), [0.25, 0.25, -0.75, -0.75]);

const resampler = new audio.LinearResampler(1, 4, 2, 1);
assert.deepEqual(
  Array.from(resampler.push(new Float32Array([0, 1, 2, 3]))),
  [0, 2]);

assert.throws(
  () => new audio.Pga1Decoder(1).push(encoded),
  /invalid PGA1 audio frame header/);

async function monitorLifecycleContract() {
  const originalNode = globalThis.AudioWorkletNode;
  const originalCreateObjectUrl = URL.createObjectURL;
  const originalRevokeObjectUrl = URL.revokeObjectURL;
  const events = [];
  const contexts = [];
  const nodes = [];
  let request = null;

  class FakeAudioWorkletNode {
    constructor() {
      this.port = {
        onmessage: null,
        postMessage() {}
      };
      this.disconnected = false;
      nodes.push(this);
    }

    connect() {}

    disconnect() {
      this.disconnected = true;
    }
  }

  globalThis.AudioWorkletNode = FakeAudioWorkletNode;
  URL.createObjectURL = () => "blob:pamguard-project-audio-contract";
  URL.revokeObjectURL = () => {};
  try {
    const monitor = audio.createMonitor({
      getBaseUrl: () => "http://engine.test",
      getHeaders: () => ({ "X-API-Key": "contract-key" }),
      onStatus: (status) => events.push(status),
      contextFactory: () => {
        const context = {
          sampleRate: 48000,
          baseLatency: 0.01,
          outputLatency: 0.02,
          destination: {},
          audioWorklet: {
            async addModule() {}
          },
          async setSinkId(value) {
            context.sinkId = value;
          },
          async resume() {
            context.resumed = true;
          },
          async close() {
            context.closed = true;
          }
        };
        contexts.push(context);
        return context;
      },
      fetch: async (url, options) => {
        request = { url, options };
        return {
          ok: true,
          status: 200,
          headers: {
            get(name) {
              if (name === "X-PAMGuard-Channel-Count") return "2";
              if (name === "X-PAMGuard-Audio-Framing") return "pga1";
              return null;
            }
          },
          body: {
            getReader() {
              let delivered = false;
              return {
                read() {
                  if (!delivered) {
                    delivered = true;
                    return Promise.resolve({
                      done: false,
                      value: encoded
                    });
                  }
                  return new Promise((resolve) => {
                    if (options.signal.aborted) {
                      resolve({ done: true });
                      return;
                    }
                    options.signal.addEventListener(
                      "abort",
                      () => resolve({ done: true }),
                      { once: true });
                  });
                }
              };
            }
          }
        };
      }
    });

    await monitor.start({
      sourceBlockId: "block:acquisition:raw",
      sampleRateHz: 48000,
      channelBitmap: 3,
      deviceId: "browser-device",
      settings: {
        channelBitmap: 3,
        defaultSampleRate: true,
        playbackRateHz: 48000,
        playbackSpeed: 1,
        playbackGainDb: 0,
        hpFilter: 0
      }
    });
    assert.match(
      request.url,
      /data-blocks\/block%3Aacquisition%3Araw\/audio-f32le\?/);
    assert.match(request.url, /channels=0%2C1/);
    assert.equal(request.options.headers["X-API-Key"], "contract-key");
    assert.equal(contexts[0].sinkId, "browser-device");
    assert.equal(contexts[0].resumed, true);
    assert.deepEqual(
      events.slice(0, 2).map((event) => event.phase),
      ["starting", "live"]);
    await new Promise((resolve) => setImmediate(resolve));
    nodes[0].port.onmessage({
      data: {
        type: "health",
        bufferedFrames: 2,
        underrunFrames: 0,
        droppedFrames: 0
      }
    });
    assert.equal(
      events.at(-1).receivedFrames,
      2,
      "Sound Output status must report decoded source frames");

    await monitor.stop();
    assert.equal(request.options.signal.aborted, true);
    assert.equal(contexts[0].closed, true);
    assert.equal(events.at(-1).phase, "stopped");

    await monitor.dispose();
    await assert.rejects(
      () => monitor.start({
        sourceBlockId: "block:acquisition:raw",
        sampleRateHz: 48000,
        channelBitmap: 1,
        settings: {
          channelBitmap: 1,
          defaultSampleRate: true,
          playbackRateHz: 48000,
          playbackSpeed: 1,
          playbackGainDb: 0,
          hpFilter: 0
        }
      }),
      /disposed/);
  }
  finally {
    globalThis.AudioWorkletNode = originalNode;
    URL.createObjectURL = originalCreateObjectUrl;
    URL.revokeObjectURL = originalRevokeObjectUrl;
  }
}

monitorLifecycleContract()
  .then(() => console.log("project audio contracts passed"))
  .catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
