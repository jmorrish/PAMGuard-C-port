(() => {
  "use strict";

  const global = globalThis;
  const FRAME_HEADER_BYTES = 40;
  const MAX_PACKET_FRAMES = 10_000_000;

  function finiteNumber(value, name, options = {}) {
    const number = Number(value);
    if (!Number.isFinite(number) ||
        (options.minimum !== undefined && number < options.minimum) ||
        (options.maximum !== undefined && number > options.maximum)) {
      throw new Error(`${name} is outside its supported range`);
    }
    return number;
  }

  function selectedChannels(bitmap, availableBitmap) {
    const selected = [];
    const requested = Number(bitmap);
    const available = Number(availableBitmap);
    if (!Number.isInteger(requested) || requested <= 0 ||
        requested > 0xffffffff ||
        !Number.isInteger(available) || available <= 0 ||
        available > 0xffffffff) {
      throw new Error("Sound Output requires at least one available channel");
    }
    const requestedBits = BigInt(requested);
    const availableBits = BigInt(available);
    if ((requestedBits & availableBits) !== requestedBits) {
      throw new Error(
        "Sound Output selects a channel which its source does not provide");
    }
    for (let channel = 0; channel < 32; channel++) {
      if ((requestedBits & (1n << BigInt(channel))) !== 0n) {
        selected.push(channel);
      }
    }
    return selected;
  }

  function normalizeConfig(config) {
    if (!config || typeof config !== "object") {
      throw new Error("Sound Output configuration is required");
    }
    const sourceBlockId = String(config.sourceBlockId || "");
    if (!sourceBlockId) {
      throw new Error("Sound Output requires a playable raw-audio source");
    }
    const sampleRateHz = finiteNumber(
      config.sampleRateHz,
      "Source sample rate",
      { minimum: 1 });
    const channels = selectedChannels(
      config.settings?.channelBitmap,
      config.channelBitmap);
    const local = config.local || {};
    const mix = String(local.mix || "direct");
    if (!["direct", "mono", "stereo"].includes(mix)) {
      throw new Error("Sound Output mix must be direct, mono, or stereo");
    }
    const outputChannels = mix === "mono"
      ? 1
      : mix === "stereo" ? 2 : channels.length;
    const defaultSampleRate =
      config.settings?.defaultSampleRate !== false;
    const outputRateHz = finiteNumber(
      config.settings?.playbackRateHz ?? 48000,
      "Output sample rate",
      { minimum: 1 });
    const playbackSpeed = finiteNumber(
      config.settings?.playbackSpeed ?? 1,
      "Playback speed",
      { minimum: 0.03125, maximum: 32 });
    const playbackGainDb = finiteNumber(
      config.settings?.playbackGainDb ?? 0,
      "Playback gain",
      { minimum: -80, maximum: 80 });
    const hpFilter = finiteNumber(
      config.settings?.hpFilter ?? 0,
      "High-pass filter",
      { minimum: 0, maximum: 0.5 });
    const latencyMs = finiteNumber(
      local.latencyMs ?? 100,
      "Output latency",
      { minimum: 20, maximum: 2000 });
    return {
      sourceBlockId,
      sampleRateHz,
      channels,
      mix,
      outputChannels,
      defaultSampleRate,
      outputRateHz,
      playbackSpeed,
      playbackGainDb,
      hpFilter,
      highPassHz: hpFilter * sampleRateHz,
      latencyMs,
      muted: Boolean(local.muted),
      deviceId: String(config.deviceId || "")
    };
  }

  class Pga1Decoder {
    constructor(expectedChannels) {
      this.expectedChannels = expectedChannels;
      this.carry = new Uint8Array(0);
    }

    push(value) {
      const incoming = value instanceof Uint8Array
        ? value
        : new Uint8Array(value);
      const bytes = new Uint8Array(this.carry.length + incoming.length);
      bytes.set(this.carry);
      bytes.set(incoming, this.carry.length);
      const packets = [];
      let cursor = 0;
      while (bytes.length - cursor >= FRAME_HEADER_BYTES) {
        if (bytes[cursor] !== 0x50 ||
            bytes[cursor + 1] !== 0x47 ||
            bytes[cursor + 2] !== 0x41 ||
            bytes[cursor + 3] !== 0x31) {
          throw new Error("invalid PGA1 audio frame magic");
        }
        const header = new DataView(
          bytes.buffer,
          bytes.byteOffset + cursor,
          FRAME_HEADER_BYTES);
        const headerBytes = header.getUint32(4, true);
        const channelCount = header.getUint32(8, true);
        const frameCount = header.getUint32(12, true);
        if (headerBytes !== FRAME_HEADER_BYTES ||
            channelCount !== this.expectedChannels ||
            frameCount > MAX_PACKET_FRAMES) {
          throw new Error("invalid PGA1 audio frame header");
        }
        const packetBytes =
          headerBytes + frameCount * channelCount * Float32Array.BYTES_PER_ELEMENT;
        if (bytes.length - cursor < packetBytes) break;
        const samples = new Float32Array(frameCount * channelCount);
        const payload = new DataView(
          bytes.buffer,
          bytes.byteOffset + cursor + headerBytes,
          samples.byteLength);
        for (let index = 0; index < samples.length; index++) {
          samples[index] = payload.getFloat32(
            index * Float32Array.BYTES_PER_ELEMENT,
            true);
        }
        packets.push({
          channelCount,
          frameCount,
          timeUnixMs: Number(header.getBigInt64(16, true)),
          startSample: Number(header.getBigUint64(24, true)),
          droppedFrames: Number(header.getBigUint64(32, true)),
          samples
        });
        cursor += packetBytes;
      }
      this.carry = bytes.slice(cursor);
      return packets;
    }
  }

  function mixPacket(packet, mix) {
    if (mix === "direct") return packet.samples;
    const outputChannels = mix === "mono" ? 1 : 2;
    const result = new Float32Array(packet.frameCount * outputChannels);
    for (let frame = 0; frame < packet.frameCount; frame++) {
      const sourceOffset = frame * packet.channelCount;
      const targetOffset = frame * outputChannels;
      if (mix === "mono") {
        let sum = 0;
        for (let channel = 0; channel < packet.channelCount; channel++) {
          sum += packet.samples[sourceOffset + channel];
        }
        result[targetOffset] = sum / packet.channelCount;
      }
      else {
        result[targetOffset] = packet.samples[sourceOffset] || 0;
        result[targetOffset + 1] = packet.channelCount > 1
          ? packet.samples[sourceOffset + 1]
          : packet.samples[sourceOffset] || 0;
      }
    }
    return result;
  }

  class LinearResampler {
    constructor(channelCount, sourceRateHz, outputRateHz, playbackSpeed) {
      this.channelCount = channelCount;
      this.sourcePerOutput =
        sourceRateHz * playbackSpeed / outputRateHz;
      this.buffer = [];
      this.position = 0;
    }

    push(samples) {
      for (const value of samples) this.buffer.push(value);
      const bufferedFrames =
        Math.floor(this.buffer.length / this.channelCount);
      const output = [];
      while (this.position + 1 < bufferedFrames) {
        const before = Math.floor(this.position);
        const fraction = this.position - before;
        for (let channel = 0; channel < this.channelCount; channel++) {
          const first =
            this.buffer[before * this.channelCount + channel];
          const second =
            this.buffer[(before + 1) * this.channelCount + channel];
          output.push(first + (second - first) * fraction);
        }
        this.position += this.sourcePerOutput;
      }
      const consumed = Math.floor(this.position);
      if (consumed > 0) {
        this.buffer.splice(0, consumed * this.channelCount);
        this.position -= consumed;
      }
      return new Float32Array(output);
    }
  }

  const processorSource = `
    class PamguardProjectMonitorProcessor extends AudioWorkletProcessor {
      constructor() {
        super();
        this.queue = [];
        this.current = null;
        this.offset = 0;
        this.bufferedFrames = 0;
        this.gain = 1;
        this.latencyFrames = sampleRate * 0.1;
        this.highPassHz = 0;
        this.previousInput = [];
        this.previousOutput = [];
        this.underrunFrames = 0;
        this.droppedFrames = 0;
        this.reportCountdown = 0;
        this.port.onmessage = (event) => {
          const item = event.data;
          if (item.type === "controls") {
            this.gain = item.gain;
            this.highPassHz = item.highPassHz;
            this.latencyFrames = Math.max(128, item.latencyFrames);
            return;
          }
          const frames = item.samples.length / item.channels;
          this.queue.push(item);
          this.bufferedFrames += frames;
          while (this.bufferedFrames > this.latencyFrames * 2 &&
                 this.queue.length > 1) {
            const dropped = this.queue.shift();
            const droppedFrames =
              dropped.samples.length / dropped.channels;
            this.bufferedFrames -= droppedFrames;
            this.droppedFrames += droppedFrames;
          }
        };
      }
      process(inputs, outputs) {
        const output = outputs[0];
        const frames = output[0]?.length || 0;
        for (let frame = 0; frame < frames; frame++) {
          while (!this.current ||
                 this.offset >=
                   this.current.samples.length / this.current.channels) {
            this.current = this.queue.shift() || null;
            this.offset = 0;
            if (!this.current) break;
          }
          if (!this.current) {
            this.underrunFrames++;
            continue;
          }
          for (let channel = 0; channel < output.length; channel++) {
            const sourceChannel = Math.min(
              channel,
              this.current.channels - 1);
            const input = this.current.samples[
              this.offset * this.current.channels + sourceChannel];
            let value = input;
            if (this.highPassHz > 0) {
              const rc = 1 / (2 * Math.PI * this.highPassHz);
              const alpha = rc / (rc + 1 / sampleRate);
              value = alpha * (
                (this.previousOutput[channel] || 0) +
                input -
                (this.previousInput[channel] || 0));
              this.previousInput[channel] = input;
              this.previousOutput[channel] = value;
            }
            output[channel][frame] = value * this.gain;
          }
          this.offset++;
          this.bufferedFrames = Math.max(0, this.bufferedFrames - 1);
        }
        this.reportCountdown -= frames;
        if (this.reportCountdown <= 0) {
          this.reportCountdown = sampleRate / 4;
          this.port.postMessage({
            type: "health",
            bufferedFrames: this.bufferedFrames,
            underrunFrames: this.underrunFrames,
            droppedFrames: this.droppedFrames
          });
        }
        return true;
      }
    }
    registerProcessor(
      "pamguard-project-monitor",
      PamguardProjectMonitorProcessor);`;

  class ProjectAudioMonitor {
    constructor(options = {}) {
      this.getBaseUrl = options.getBaseUrl ||
        (() => global.location?.origin || "");
      this.getHeaders = options.getHeaders || (() => ({}));
      this.fetch = options.fetch || global.fetch?.bind(global);
      this.onStatus = options.onStatus || (() => {});
      this.contextFactory = options.contextFactory || null;
      this.context = null;
      this.node = null;
      this.abortController = null;
      this.pumpPromise = null;
      this.config = null;
      this.sourceLatencyMs = 0;
      this.transportDroppedFrames = 0;
      this.receivedFrames = 0;
      this.disposed = false;
    }

    status(phase, detail = {}) {
      this.onStatus({
        phase,
        sourceBlockId: this.config?.sourceBlockId || "",
        ...detail
      });
    }

    async enumerateOutputDevices() {
      if (!global.navigator?.mediaDevices?.enumerateDevices) return [];
      const devices = await global.navigator.mediaDevices.enumerateDevices();
      return devices
        .filter((device) => device.kind === "audiooutput")
        .map((device) => ({
          id: device.deviceId,
          label: device.label || "Audio output"
        }));
    }

    createContext(config) {
      if (this.contextFactory) return this.contextFactory(config);
      const AudioContextClass =
        global.AudioContext || global.webkitAudioContext;
      if (!AudioContextClass || !global.AudioWorkletNode) {
        throw new Error(
          "This browser does not support low-latency AudioWorklet output");
      }
      return new AudioContextClass({
        latencyHint: "interactive",
        ...(config.defaultSampleRate
          ? {}
          : { sampleRate: config.outputRateHz })
      });
    }

    sendControls() {
      if (!this.node || !this.context || !this.config) return;
      const linearGain = this.config.muted
        ? 0
        : 10 ** (this.config.playbackGainDb / 20);
      this.node.port.postMessage({
        type: "controls",
        gain: linearGain,
        highPassHz: this.config.highPassHz,
        latencyFrames:
          this.context.sampleRate * this.config.latencyMs / 1000
      });
    }

    async start(providedConfig) {
      if (this.disposed) throw new Error("Sound Output has been disposed");
      await this.stop(false);
      if (!this.fetch) throw new Error("Fetch is unavailable");
      const config = normalizeConfig(providedConfig);
      this.config = config;
      this.status("starting", {
        message: "Opening browser audio output…"
      });

      const context = this.createContext(config);
      this.context = context;
      if (config.deviceId && typeof context.setSinkId === "function") {
        await context.setSinkId(config.deviceId);
      }
      const workletUrl = URL.createObjectURL(new Blob(
        [processorSource],
        { type: "application/javascript" }));
      try {
        await context.audioWorklet.addModule(workletUrl);
      }
      finally {
        URL.revokeObjectURL(workletUrl);
      }
      const node = new global.AudioWorkletNode(
        context,
        "pamguard-project-monitor",
        {
          numberOfInputs: 0,
          numberOfOutputs: 1,
          outputChannelCount: [config.outputChannels]
        });
      this.node = node;
      node.port.onmessage = (event) => {
        if (event.data?.type !== "health" || !this.context) return;
        const bufferedMs =
          event.data.bufferedFrames / this.context.sampleRate * 1000;
        const deviceLatencyMs = (
          Number(this.context.baseLatency || 0) +
          Number(this.context.outputLatency || 0)) * 1000;
        this.status("live", {
          sampleRateHz: this.context.sampleRate,
          channels: config.channels,
          bufferedMs,
          estimatedLatencyMs:
            this.sourceLatencyMs + bufferedMs + deviceLatencyMs,
          underrunFrames: event.data.underrunFrames,
          transportDroppedFrames: this.transportDroppedFrames,
          outputDroppedFrames: event.data.droppedFrames,
          receivedFrames: this.receivedFrames
        });
      };
      node.connect(context.destination);
      this.sendControls();
      await context.resume();

      const controller = new AbortController();
      this.abortController = controller;
      try {
        const baseUrl = String(this.getBaseUrl() || "").replace(/\/$/, "");
        const query = new URLSearchParams({
          channels: config.channels.join(","),
          format: "framed"
        });
        const response = await this.fetch(
          `${baseUrl}/data-blocks/` +
            `${encodeURIComponent(config.sourceBlockId)}/audio-f32le?${query}`,
          {
            headers: this.getHeaders(),
            signal: controller.signal
          });
        if (!response.ok || !response.body) {
          throw new Error(
            `audio stream unavailable (${response.status})`);
        }
        if (Number(response.headers.get(
              "X-PAMGuard-Channel-Count")) !== config.channels.length ||
            response.headers.get(
              "X-PAMGuard-Audio-Framing") !== "pga1") {
          throw new Error("audio stream contract changed");
        }
        this.status("live", {
          sampleRateHz: context.sampleRate,
          channels: config.channels,
          message: "Audio stream connected"
        });
        this.pumpPromise = this.pump(
          response.body.getReader(),
          controller.signal);
        this.pumpPromise.catch((error) => {
          if (controller.signal.aborted) return;
          this.status("error", { message: String(error.message || error) });
          void this.stop(false);
        });
      }
      catch (error) {
        await this.stop(false);
        throw error;
      }
    }

    async pump(reader, signal) {
      const config = this.config;
      const decoder = new Pga1Decoder(config.channels.length);
      const resampler = new LinearResampler(
        config.outputChannels,
        config.sampleRateHz,
        this.context.sampleRate,
        config.playbackSpeed);
      while (!signal.aborted) {
        const { done, value } = await reader.read();
        if (done) {
          if (!signal.aborted) {
            throw new Error("audio data-block stream ended");
          }
          return;
        }
        for (const packet of decoder.push(value)) {
          this.receivedFrames += packet.frameCount;
          this.sourceLatencyMs =
            packet.timeUnixMs > 1_000_000_000_000
              ? Math.max(0, Date.now() - packet.timeUnixMs)
              : 0;
          this.transportDroppedFrames = packet.droppedFrames;
          const mixed = mixPacket(packet, config.mix);
          const output = resampler.push(mixed);
          if (!output.length || signal.aborted || !this.node) continue;
          this.node.port.postMessage(
            {
              samples: output,
              channels: config.outputChannels
            },
            [output.buffer]);
        }
      }
    }

    async stop(announce = true) {
      if (this.abortController) {
        this.abortController.abort();
        this.abortController = null;
      }
      const pump = this.pumpPromise;
      this.pumpPromise = null;
      if (pump) await pump.catch(() => {});
      if (this.node) {
        this.node.disconnect();
        this.node = null;
      }
      if (this.context) {
        await this.context.close();
        this.context = null;
      }
      this.config = null;
      this.sourceLatencyMs = 0;
      this.transportDroppedFrames = 0;
      this.receivedFrames = 0;
      if (announce) this.status("stopped", { message: "Stopped" });
    }

    async dispose() {
      this.disposed = true;
      await this.stop(false);
    }
  }

  global.PamguardProjectAudio = Object.freeze({
    createMonitor: (options) => new ProjectAudioMonitor(options),
    normalizeConfig,
    Pga1Decoder,
    LinearResampler,
    mixPacket
  });
})();
