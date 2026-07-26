(() => {
  "use strict";

  function clamp(value, minimum, maximum) {
    return Math.max(minimum, Math.min(maximum, value));
  }

  function interpolatedColour(level, stops) {
    const value = clamp(level, 0, 1);
    const scaled = value * (stops.length - 1);
    const lower = Math.min(stops.length - 2, Math.floor(scaled));
    const fraction = scaled - lower;
    return stops[lower].map((component, index) =>
      Math.round(
        component +
        (stops[lower + 1][index] - component) * fraction));
  }

  const SPECTROGRAM_COLOUR_MAPS = Object.freeze({
    GREY: [[255, 255, 255], [0, 0, 0]],
    REVERSEGREY: [[0, 0, 0], [255, 255, 255]],
    BLUE: [[0, 0, 0], [0, 0, 255]],
    GREEN: [[0, 0, 0], [0, 255, 0]],
    RED: [[0, 0, 0], [255, 0, 0]],
    HOT: [
      [0, 0, 0],
      [0, 0, 255],
      [0, 255, 255],
      [0, 255, 0],
      [255, 200, 0],
      [255, 0, 0]
    ],
    HSV: [
      [255, 0, 0],
      [255, 255, 0],
      [0, 255, 0],
      [0, 255, 255],
      [0, 0, 255],
      [255, 0, 255],
      [255, 0, 0]
    ],
    FIRE: [
      [0, 0, 0],
      [255, 0, 0],
      [255, 200, 0],
      [255, 255, 0],
      [255, 255, 255]
    ],
    PATRIOTIC: [
      [255, 0, 0],
      [255, 255, 255],
      [0, 0, 255]
    ]
  });

  function spectrogramColour(level, colourMap) {
    return interpolatedColour(
      level,
      SPECTROGRAM_COLOUR_MAPS[colourMap] ||
        SPECTROGRAM_COLOUR_MAPS.GREY);
  }

  function spectrogramPanelChannels(settings) {
    const count = clamp(
      Math.trunc(Number(settings?.nPanels) || 1),
      1,
      32);
    const configured = Array.isArray(settings?.channelList)
      ? settings.channelList
      : [0];
    return Array.from({ length: count }, (_, index) =>
      clamp(
        Math.trunc(
          Number(configured[index] ?? configured.at(-1) ?? 0) || 0),
        0,
        31));
  }

  function amplitudeRange(settings) {
    const limits = Array.isArray(settings?.amplitudeLimits)
      ? settings.amplitudeLimits.map(Number)
      : [50, 120];
    if (limits.length < 2 ||
        limits.some((value) => !Number.isFinite(value))) {
      return [50, 120];
    }
    return [
      Math.min(limits[0], limits[1]),
      Math.max(limits[0], limits[1])
    ];
  }

  function resizeCanvas(canvas) {
    const bounds = canvas.getBoundingClientRect();
    const ratio = Math.max(1, window.devicePixelRatio || 1);
    const width = Math.max(1, Math.round(bounds.width * ratio));
    const height = Math.max(1, Math.round(bounds.height * ratio));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    return { width, height, ratio };
  }

  function createRasterCanvas(width, height) {
    if (typeof OffscreenCanvas === "function") {
      return new OffscreenCanvas(width, height);
    }
    if (typeof document !== "undefined" &&
        typeof document.createElement === "function") {
      const canvas = document.createElement("canvas");
      canvas.width = width;
      canvas.height = height;
      return canvas;
    }
    return null;
  }

  class ProjectSpectrogram {
    constructor(options) {
      this.canvas = options.canvas;
      this.status = options.status;
      this.settings = options.settings || {};
      this.sourceBlockId = options.sourceBlockId || "";
      this.sampleRateHz = Number(options.sampleRateHz) || 0;
      this.fftLength = Math.max(0, Number(options.fftLength) || 0);
      this.fftHop = Math.max(0, Number(options.fftHop) || 0);
      this.calibrationDbOffsetByChannel =
        Array.isArray(options.calibrationDbOffsetByChannel)
          ? options.calibrationDbOffsetByChannel.map(Number)
          : [];
      this.api = options.api;
      this.headers = options.headers || (() => ({}));
      this.onError = options.onError || (() => {});
      this.running = Boolean(options.running);
      // PAMGuard's live Spectrogram advances once for every FFTDataUnit.
      // Presentation throttling belongs in render scheduling, not in the data
      // stream: sampling the stream creates artificial black gaps between
      // otherwise adjacent FFT slices.
      this.streamCadenceMs = 0;
      // A newly opened display needs a small retained seed because its tab
      // may be mounted just after audio passed. Keep this deliberately short:
      // 16 slices are about 170 ms at 48 kHz / 512-hop, so the controlled
      // presentation delay remains close to one second rather than inheriting
      // several seconds of server history.
      this.streamHistoryFrames = 16;
      const requestedPresentationDelay =
        Number(options.presentationDelayMs);
      this.presentationDelayMs = this.running
        ? clamp(
            Number.isFinite(requestedPresentationDelay)
              ? requestedPresentationDelay
              : 1000,
            0,
            5000)
        : 0;
      this.presentationTickMs = 16;
      this.smoothScroll = Boolean(
        this.running &&
        this.sourceBlockId &&
        this.settings.timeScaleFixed &&
        !this.settings.wrapDisplay);
      this.panelChannels = spectrogramPanelChannels(this.settings);
      this.channels = [...new Set(this.panelChannels)];
      this.sequenceByChannel = new Map();
      this.firstTimeByChannel = new Map();
      this.latestSequenceByChannel = new Map();
      this.displayCursorByChannel = new Map();
      this.rastersByChannel = new Map();
      this.colourLut = new Uint8ClampedArray(256 * 3);
      for (let index = 0; index < 256; index++) {
        const colour = spectrogramColour(
          index / 255,
          this.settings.colourMap);
        this.colourLut[index * 3] = colour[0];
        this.colourLut[index * 3 + 1] = colour[1];
        this.colourLut[index * 3 + 2] = colour[2];
      }
      this.columns = [];
      this.pendingColumns = [];
      this.lastTimeMs = 0;
      this.lastReceivedTimeMs = 0;
      this.lastRenderWallMs = 0;
      this.acceptedFrameCount = 0;
      this.presentedFrameCount = 0;
      this.presentationMediaOriginMs = null;
      this.presentationWallOriginMs = 0;
      this.presentationStarted = false;
      this.presentationTimer = null;
      this.presentationAnimationFrame = null;
      this.lastAnimationWallMs = 0;
      this.renderDurationsMs = [];
      this.renderIntervalsMs = [];
      this.lastRenderStartMs = 0;
      this.renderTimer = null;
      this.renderPending = false;
      this.disposed = false;
      this.abortController = null;
      this.resizeObserver = typeof ResizeObserver === "function"
        ? new ResizeObserver(() => this.scheduleRender())
        : null;
      this.resizeObserver?.observe(this.canvas);
      this.canvas.__pamguardSpectrogramMetrics = () =>
        this.performanceMetrics();
      this.scheduleRender();
      if (this.running && this.sourceBlockId) {
        void this.connect();
      }
      else {
        this.setStatus(
          this.sourceBlockId
            ? "Start processing to receive FFT frames"
            : "Select an FFT source");
      }
    }

    setStatus(message) {
      if (!this.disposed && this.status) {
        this.status.textContent = message;
      }
    }

    async connect() {
      const controller = new AbortController();
      this.abortController = controller;
      this.setStatus("Connecting to FFT stream…");
      try {
        await this.loadBlockMetadata(controller.signal);
        await Promise.all(
          this.channels.map(
            (channel) =>
              this.consumeChannelStream(
                channel,
                controller.signal)));
        if (!controller.signal.aborted) {
          this.setStatus("FFT stream ended");
        }
      }
      catch (error) {
        if (controller.signal.aborted || this.disposed) return;
        const message = error?.message || String(error);
        this.setStatus(message);
        this.onError(error);
      }
    }

    async loadBlockMetadata(signal) {
      try {
        const response = await fetch(
          this.api("/data-blocks"),
          {
            headers: this.headers(),
            signal
          });
        if (!response.ok) return;
        const catalogue = await response.json();
        const block = catalogue?.dataBlocks?.find(
          (candidate) => candidate.id === this.sourceBlockId);
        if (!block) return;
        this.sampleRateHz =
          Number(block.sampleRateHz) > 0
            ? Number(block.sampleRateHz)
            : this.sampleRateHz;
        this.fftLength =
          Number(block.fftLength) > 0
            ? Number(block.fftLength)
            : this.fftLength;
        this.fftHop =
          Number(block.fftHop) > 0
            ? Number(block.fftHop)
            : this.fftHop;
        if (Array.isArray(block.calibrationDbOffsetByChannel)) {
          this.calibrationDbOffsetByChannel =
            block.calibrationDbOffsetByChannel.map(Number);
        }
      }
      catch (error) {
        if (signal.aborted) throw error;
        // Metadata improves calibration and timing, but an older service
        // without the catalogue must not prevent the live stream.
      }
    }

    async consumeChannelStream(channel, signal) {
      const query = new URLSearchParams({
        format: "ndjson",
        history: String(this.streamHistoryFrames),
        channels: String(channel),
        cadenceMs: String(this.streamCadenceMs)
      });
      const response = await fetch(
        this.api(
          `/data-blocks/${encodeURIComponent(this.sourceBlockId)}` +
          `/stream?${query}`),
        {
          headers: this.headers(),
          signal
        });
      if (!response.ok || !response.body) {
        throw new Error(`FFT stream unavailable (${response.status})`);
      }
      this.setStatus("Live");
      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffered = "";
      while (!signal.aborted) {
        const { done, value } = await reader.read();
        if (done) break;
        buffered += decoder.decode(value, { stream: true });
        let newline;
        while ((newline = buffered.indexOf("\n")) >= 0) {
          const line = buffered.slice(0, newline).trim();
          buffered = buffered.slice(newline + 1);
          if (!line) continue;
          this.accept(JSON.parse(line));
        }
      }
    }

    accept(unit) {
      const payload = unit?.payload;
      if (!Array.isArray(payload?.magnitudeSquared)) return;
      const channel = Math.max(0, Number(payload.channel) || 0);
      if (!this.channels.includes(channel)) return;
      const timeMs = Number(unit.timeMs);
      const safeTime = Number.isFinite(timeMs) ? timeMs : Date.now();
      const sequence = this.sequenceByChannel.get(channel) || 0;
      this.sequenceByChannel.set(channel, sequence + 1);
      if (!this.firstTimeByChannel.has(channel)) {
        this.firstTimeByChannel.set(channel, safeTime);
      }
      this.latestSequenceByChannel.set(channel, sequence);
      const column = {
        timeMs: safeTime,
        startSample: Number.isFinite(Number(unit.startSample))
          ? Number(unit.startSample)
          : null,
        channel,
        sequence,
        values: payload.magnitudeSquared.map(Number),
        firstBin: Math.max(0, Number(payload.firstBin) || 0),
        discontinuity: Boolean(unit.discontinuity)
      };
      this.acceptedFrameCount++;
      this.lastReceivedTimeMs = Math.max(
        this.lastReceivedTimeMs,
        safeTime);

      if (this.presentationDelayMs <= 0) {
        this.presentColumn(column);
        this.scheduleRender();
        return;
      }

      if (this.smoothScroll) {
        this.writeRasterColumn(column);
        column.rasterWritten = true;
      }
      this.pendingColumns.push(column);
      if (this.presentationMediaOriginMs === null ||
          (!this.presentationStarted &&
           safeTime < this.presentationMediaOriginMs)) {
        this.presentationMediaOriginMs = safeTime;
      }
      if (this.presentationWallOriginMs <= 0) {
        this.presentationWallOriginMs =
          Date.now() + this.presentationDelayMs;
      }
      this.updateBufferingStatus();
      this.schedulePresentation();
    }

    presentColumn(column) {
      this.lastTimeMs = Math.max(this.lastTimeMs, column.timeMs);
      this.presentedFrameCount++;
      this.columns.push(column);
      if (!column.rasterWritten) {
        this.writeRasterColumn(column);
        column.rasterWritten = true;
      }
      const windowSeconds = Math.max(
        0.5,
        Number(this.settings.displayLength) || 20);
      const cutoff =
        this.lastTimeMs - Math.max(180, windowSeconds * 3) * 1000;
      while (this.columns.length > 2 &&
             (this.columns[0].timeMs < cutoff ||
              this.columns.length > 8192)) {
        this.columns.shift();
      }
    }

    updateBufferingStatus(now = Date.now()) {
      if (this.presentationStarted ||
          this.presentationWallOriginMs <= 0) return;
      const remaining = Math.max(
        0,
        this.presentationWallOriginMs - now);
      const buffered =
        this.presentationDelayMs - remaining;
      this.setStatus(
        `Buffering live FFT · ` +
        `${(buffered / 1000).toFixed(1)} / ` +
        `${(this.presentationDelayMs / 1000).toFixed(1)} s`);
    }

    schedulePresentation() {
      if (this.disposed || this.presentationDelayMs <= 0) return;
      if (this.smoothScroll &&
          typeof requestAnimationFrame === "function") {
        if (this.presentationAnimationFrame !== null) return;
        this.presentationAnimationFrame =
          requestAnimationFrame(() => {
            this.presentationAnimationFrame = null;
            if (this.disposed) return;
            this.drainPresentation(Date.now(), false);
            if (this.presentationStarted) {
              this.render();
            }
            this.schedulePresentation();
          });
        return;
      }
      if (this.presentationTimer !== null) return;
      let delay = this.presentationTickMs;
      if (!this.presentationStarted &&
          this.presentationWallOriginMs > 0) {
        delay = Math.min(
          50,
          Math.max(
            0,
            this.presentationWallOriginMs - Date.now()));
      }
      this.presentationTimer = setTimeout(() => {
        this.presentationTimer = null;
        this.drainPresentation();
      }, delay);
    }

    advanceSmoothCursors(now) {
      const frameRate =
        this.sampleRateHz > 0 && this.fftHop > 0
          ? this.sampleRateHz / this.fftHop
          : 0;
      if (frameRate <= 0) return;
      const targetLag = Math.max(
        2,
        this.presentationDelayMs * frameRate / 1000);
      const elapsedSeconds =
        this.lastAnimationWallMs > 0
          ? Math.min(
              0.5,
              Math.max(
                0,
                (now - this.lastAnimationWallMs) / 1000))
          : 0;

      for (const channel of this.channels) {
        const latest =
          this.latestSequenceByChannel.get(channel);
        if (!Number.isFinite(latest)) continue;
        let cursor =
          this.displayCursorByChannel.get(channel);
        if (!Number.isFinite(cursor)) {
          cursor = Math.max(0, latest - targetLag);
        }
        else if (elapsedSeconds > 0) {
          const lag = latest - cursor;
          const skew = clamp(
            (lag - targetLag) / targetLag,
            -0.3,
            0.3);
          cursor = Math.min(
            latest,
            cursor +
              frameRate * (1 + skew) * elapsedSeconds);
          // A sleeping tab or long stream interruption should resume at the
          // requested lag, not animate a stale backlog for many seconds.
          if (lag > targetLag * 3 + frameRate) {
            cursor = Math.max(0, latest - targetLag);
          }
        }
        this.displayCursorByChannel.set(channel, cursor);
      }
      this.lastAnimationWallMs = now;
    }

    drainPresentation(now = Date.now(), reschedule = true) {
      if (this.disposed || this.presentationDelayMs <= 0 ||
          this.presentationMediaOriginMs === null) return;
      if (now < this.presentationWallOriginMs) {
        this.updateBufferingStatus(now);
        if (reschedule) this.schedulePresentation();
        return;
      }

      this.presentationStarted = true;
      if (this.smoothScroll) {
        this.advanceSmoothCursors(now);
      }
      const playheadTimeMs =
        this.presentationMediaOriginMs +
        (now - this.presentationWallOriginMs);
      this.pendingColumns.sort((left, right) =>
        left.timeMs - right.timeMs ||
        (left.startSample ?? 0) - (right.startSample ?? 0) ||
        left.channel - right.channel);
      const due = [];
      const waiting = [];
      for (const candidate of this.pendingColumns) {
        const isDue = this.smoothScroll
          ? candidate.sequence <= Math.floor(
              this.displayCursorByChannel.get(
                candidate.channel) ?? -1)
          : candidate.timeMs <= playheadTimeMs + 0.5;
        (isDue ? due : waiting).push(candidate);
      }
      if (due.length > 0) {
        this.pendingColumns = waiting;
        for (const column of due) {
          this.presentColumn(column);
        }
        if (reschedule) this.scheduleRender();
      }
      if (reschedule) this.schedulePresentation();
    }

    scheduleRender() {
      if (this.renderPending || this.disposed) return;
      this.renderPending = true;
      const render = () => {
        this.renderTimer = null;
        requestAnimationFrame(() => {
          this.renderPending = false;
          if (!this.disposed) this.render();
        });
      };
      // Java SpectrogramDisplay repaints at about 50 ms. The cached raster
      // makes a smoother 30 FPS presentation cheap while FFT ingestion stays
      // independent of paint timing.
      const minimumIntervalMs = this.running ? 33 : 0;
      const elapsed = Date.now() - this.lastRenderWallMs;
      const delay = Math.max(0, minimumIntervalMs - elapsed);
      if (delay > 0) {
        this.renderTimer = setTimeout(render, delay);
      }
      else {
        render();
      }
    }

    calibratedFftDb(magnitudeSquared, channel) {
      const magnitude = Math.max(
        Number(magnitudeSquared),
        Number.MIN_VALUE);
      const length = Math.max(1, this.fftLength);
      const offset =
        Number(this.calibrationDbOffsetByChannel[channel]);
      // Java AcquisitionProcess.fftAmplitude2dB(..., isSquared=true):
      // sqrt(power) / fftLength * sqrt(2), followed by the acquisition
      // channel calibration. Runtime FFT magnitudes are deliberately
      // unnormalised, so 10*log10(raw power) is not a display level.
      return 10 * Math.log10(magnitude) +
        20 * Math.log10(Math.SQRT2 / length) +
        (Number.isFinite(offset) ? offset : 0);
    }

    fixedFrameSlots() {
      const windowSeconds = Math.max(
        0.5,
        Number(this.settings.displayLength) || 20);
      return Math.max(
        1,
        this.sampleRateHz > 0 && this.fftHop > 0
          ? Math.round(
              windowSeconds * this.sampleRateHz / this.fftHop)
          : 1);
    }

    ensureRaster(channel, requiredBins) {
      if (!this.settings.timeScaleFixed ||
          this.sampleRateHz <= 0 ||
          this.fftHop <= 0) {
        return null;
      }
      const displaySlots = this.fixedFrameSlots();
      const reserveSlots = this.smoothScroll
        ? Math.max(
            2,
            Math.ceil(
              (this.presentationDelayMs + 500) *
              this.sampleRateHz /
              (1000 * this.fftHop)))
        : 0;
      const slots = displaySlots + reserveSlots;
      const bins = Math.max(
        1,
        Math.floor(this.fftLength / 2) + 1,
        requiredBins);
      const current = this.rastersByChannel.get(channel);
      if (current &&
          current.slots === slots &&
          current.displaySlots === displaySlots &&
          current.bins === bins) {
        return current;
      }
      const canvas = createRasterCanvas(slots, bins);
      const context = canvas?.getContext?.("2d", {
        alpha: true,
        willReadFrequently: false
      });
      if (!canvas || !context ||
          typeof context.createImageData !== "function" ||
          typeof context.putImageData !== "function") {
        return null;
      }
      context.clearRect(0, 0, slots, bins);
      const raster = {
        canvas,
        context,
        slots,
        displaySlots,
        bins,
        paintedCount: 0,
        latestPosition: -1,
        latestSequence: -1
      };
      this.rastersByChannel.set(channel, raster);
      return raster;
    }

    writeRasterColumn(column) {
      const requiredBins =
        column.firstBin + column.values.length;
      const raster = this.ensureRaster(
        column.channel,
        requiredBins);
      if (!raster) return;

      const image = raster.context.createImageData(
        1,
        raster.bins);
      const [minimumDb, maximumDb] =
        amplitudeRange(this.settings);
      const dbRange = Math.max(1, maximumDb - minimumDb);
      for (let offset = 0;
           offset < column.values.length;
           offset++) {
        const bin = column.firstBin + offset;
        if (bin < 0 || bin >= raster.bins) continue;
        const db = this.calibratedFftDb(
          column.values[offset],
          column.channel);
        const colourIndex = Math.round(
          clamp((db - minimumDb) / dbRange, 0, 1) * 255);
        const lutOffset = colourIndex * 3;
        const pixelOffset =
          (raster.bins - 1 - bin) * 4;
        image.data[pixelOffset] =
          this.colourLut[lutOffset];
        image.data[pixelOffset + 1] =
          this.colourLut[lutOffset + 1];
        image.data[pixelOffset + 2] =
          this.colourLut[lutOffset + 2];
        image.data[pixelOffset + 3] = 255;
      }
      const position = column.sequence % raster.slots;
      if (column.discontinuity) {
        for (let bin = 0; bin < raster.bins; bin++) {
          const pixelOffset = bin * 4;
          image.data[pixelOffset] = 255;
          image.data[pixelOffset + 1] = 107;
          image.data[pixelOffset + 2] = 107;
          image.data[pixelOffset + 3] = 255;
        }
      }
      raster.context.clearRect(
        position,
        0,
        1,
        raster.bins);
      raster.context.putImageData(image, position, 0);
      raster.latestPosition = position;
      raster.latestSequence = column.sequence;
      raster.paintedCount = Math.min(
        raster.slots,
        raster.paintedCount + 1);
    }

    drawRasterPanel(
      context,
      channel,
      plotLeft,
      panelTop,
      plotWidth,
      panelHeight,
      lowHz,
      highHz,
      binFrequency) {
      const raster = this.rastersByChannel.get(channel);
      if (!raster || raster.paintedCount <= 0 ||
          typeof context.drawImage !== "function") {
        return false;
      }
      const lowBin = clamp(
        Math.floor(lowHz / Math.max(binFrequency, 1e-9)),
        0,
        raster.bins - 1);
      const highBin = clamp(
        Math.ceil(highHz / Math.max(binFrequency, 1e-9)),
        lowBin,
        raster.bins - 1);
      const sourceY = raster.bins - 1 - highBin;
      const sourceHeight = Math.max(
        1,
        highBin - lowBin + 1);
      context.save();
      context.imageSmoothingEnabled = this.smoothScroll;

      if (this.smoothScroll) {
        const cursor =
          this.displayCursorByChannel.get(channel);
        if (!Number.isFinite(cursor)) {
          context.restore();
          return false;
        }
        const displaySlots = raster.displaySlots;
        const firstAvailableSequence = Math.max(
          0,
          raster.latestSequence - raster.paintedCount + 1);
        const windowStart =
          cursor - displaySlots + 1;
        const drawStart = Math.max(
          windowStart,
          firstAvailableSequence);
        const drawEnd = Math.min(
          cursor + 1,
          raster.latestSequence + 1);
        let remaining = Math.max(0, drawEnd - drawStart);
        let absoluteSource = drawStart;
        let destinationX =
          plotLeft +
          (drawStart - windowStart) *
          plotWidth / displaySlots;
        while (remaining > 1e-9) {
          const sourceX =
            ((absoluteSource % raster.slots) +
              raster.slots) % raster.slots;
          const part = Math.min(
            remaining,
            raster.slots - sourceX);
          const destinationWidth =
            part * plotWidth / displaySlots;
          context.drawImage(
            raster.canvas,
            sourceX,
            sourceY,
            part,
            sourceHeight,
            destinationX,
            panelTop,
            destinationWidth,
            panelHeight);
          absoluteSource += part;
          remaining -= part;
          destinationX += destinationWidth;
        }
      }
      else if (this.settings.wrapDisplay) {
        context.drawImage(
          raster.canvas,
          0,
          sourceY,
          raster.displaySlots,
          sourceHeight,
          plotLeft,
          panelTop,
          plotWidth,
          panelHeight);
      }
      else if (raster.paintedCount < raster.displaySlots) {
        const destinationWidth =
          plotWidth * raster.paintedCount / raster.displaySlots;
        context.drawImage(
          raster.canvas,
          0,
          sourceY,
          raster.paintedCount,
          sourceHeight,
          plotLeft + plotWidth - destinationWidth,
          panelTop,
          destinationWidth,
          panelHeight);
      }
      else {
        const oldest =
          (raster.latestPosition + 1) % raster.displaySlots;
        const firstWidth = raster.displaySlots - oldest;
        const firstDestinationWidth =
          plotWidth * firstWidth / raster.displaySlots;
        if (firstWidth > 0) {
          context.drawImage(
            raster.canvas,
            oldest,
            sourceY,
            firstWidth,
            sourceHeight,
            plotLeft,
            panelTop,
            firstDestinationWidth,
            panelHeight);
        }
        if (oldest > 0) {
          context.drawImage(
            raster.canvas,
            0,
            sourceY,
            oldest,
            sourceHeight,
            plotLeft + firstDestinationWidth,
            panelTop,
            plotWidth - firstDestinationWidth,
            panelHeight);
        }
      }
      context.restore();
      return true;
    }

    performanceMetrics() {
      const percentile = (values, fraction) => {
        if (!values.length) return 0;
        const ordered = values.slice().sort(
          (left, right) => left - right);
        return ordered[Math.min(
          ordered.length - 1,
          Math.floor(ordered.length * fraction))];
      };
      return {
        acceptedFrames: this.acceptedFrameCount,
        renders: this.renderDurationsMs.length,
        renderDurationP50Ms:
          percentile(this.renderDurationsMs, 0.5),
        renderDurationP95Ms:
          percentile(this.renderDurationsMs, 0.95),
        renderIntervalP50Ms:
          percentile(this.renderIntervalsMs, 0.5),
        renderIntervalP95Ms:
          percentile(this.renderIntervalsMs, 0.95),
        cachedRaster: this.rastersByChannel.size > 0,
        retainedFrames: this.columns.length,
        queuedFrames: this.pendingColumns.length,
        presentedFrames: this.presentedFrameCount,
        presentationDelayMs: this.presentationDelayMs,
        smoothScroll: this.smoothScroll,
        presentationDepthMs: Math.max(
          0,
          this.lastReceivedTimeMs - this.lastTimeMs)
      };
    }

    recordRenderPerformance(startedAt) {
      const finishedAt = typeof performance !== "undefined"
        ? performance.now()
        : Date.now();
      this.renderDurationsMs.push(
        Math.max(0, finishedAt - startedAt));
      if (this.renderDurationsMs.length > 180) {
        this.renderDurationsMs.shift();
      }
      if (this.lastRenderStartMs > 0) {
        this.renderIntervalsMs.push(
          Math.max(0, startedAt - this.lastRenderStartMs));
        if (this.renderIntervalsMs.length > 180) {
          this.renderIntervalsMs.shift();
        }
      }
      this.lastRenderStartMs = startedAt;
    }

    render() {
      const renderStartedAt = typeof performance !== "undefined"
        ? performance.now()
        : Date.now();
      this.lastRenderWallMs = Date.now();
      const { width, height, ratio } = resizeCanvas(this.canvas);
      const context = this.canvas.getContext("2d");
      context.fillStyle = "#06111e";
      context.fillRect(0, 0, width, height);
      if (!this.columns.length) {
        context.fillStyle = "#8aa0af";
        context.font =
          `${Math.max(12, 12 * ratio)}px Cascadia Mono, Consolas, monospace`;
        context.fillText(
          this.running
            ? "Waiting for FFT frames"
            : "Start processing to receive FFT frames",
          16 * ratio,
          28 * ratio);
        this.recordRenderPerformance(renderStartedAt);
        return;
      }

      const fixedTimeScale = Boolean(this.settings.timeScaleFixed);
      const windowSeconds = Math.max(
        0.5,
        Number(this.settings.displayLength) || 20);
      const pixelsPerSlice = Math.max(
        1,
        Math.trunc(Number(this.settings.pixelsPerSlics) || 1));
      const sourceBins = Math.max(
        ...this.columns.map(
          (column) => column.firstBin + column.values.length),
        1);
      const nyquist = this.sampleRateHz > 0
        ? this.sampleRateHz / 2
        : sourceBins - 1;
      const frequencyLimits = Array.isArray(this.settings.frequencyLimits)
        ? this.settings.frequencyLimits.map(Number)
        : [0, 0];
      const lowHz = clamp(
        Number.isFinite(frequencyLimits[0]) ? frequencyLimits[0] : 0,
        0,
        nyquist);
      const requestedHigh = Number(frequencyLimits[1]);
      const highHz = clamp(
        Number.isFinite(requestedHigh) && requestedHigh > 0
          ? requestedHigh
          : nyquist,
        lowHz,
        nyquist);
      const [minimumDb, maximumDb] = amplitudeRange(this.settings);
      const dbRange = Math.max(1, maximumDb - minimumDb);
      const plotLeft = this.settings.showScale === false ? 0 : 58 * ratio;
      const plotRight = this.settings.showScale === false ? 0 : 44 * ratio;
      const plotBottom =
        this.settings.showScale === false ? height : height - 22 * ratio;
      const plotWidth = Math.max(1, width - plotLeft - plotRight);
      const panelGap = Math.max(1, ratio);
      const panelHeight = Math.max(
        1,
        (plotBottom -
          panelGap * (this.panelChannels.length - 1)) /
          this.panelChannels.length);
      const binFrequency = nyquist / Math.max(1, sourceBins - 1);
      let visibleFrameCount = 0;

      for (let panel = 0; panel < this.panelChannels.length; panel++) {
        const channel = this.panelChannels[panel];
        const panelTop = panel * (panelHeight + panelGap);
        const panelBottom = panelTop + panelHeight;
        context.fillStyle = panel % 2 ? "#071421" : "#06111e";
        context.fillRect(
          plotLeft,
          panelTop,
          plotWidth,
          panelHeight);
        let visible = this.columns.filter(
          (column) => column.channel === channel);
        const slots = Math.max(
          1,
          Math.floor(plotWidth / (pixelsPerSlice * ratio)));
        const fixedFrameSlots = Math.max(
          1,
          this.sampleRateHz > 0 && this.fftHop > 0
            ? Math.round(
                windowSeconds * this.sampleRateHz / this.fftHop)
            : slots);
        if (fixedTimeScale) {
          visible = visible.slice(-fixedFrameSlots);
        }
        else {
          visible = visible.slice(-slots);
        }
        visibleFrameCount += visible.length;
        const latestSequence =
          visible.length > 0
            ? visible[visible.length - 1].sequence
            : 0;

        const rasterDrawn = fixedTimeScale &&
          this.drawRasterPanel(
            context,
            channel,
            plotLeft,
            panelTop,
            plotWidth,
            panelHeight,
            lowHz,
            highHz,
            binFrequency);
        if (rasterDrawn) {
          visibleFrameCount -= visible.length;
          visibleFrameCount +=
            this.rastersByChannel.get(channel)?.paintedCount || 0;
        }
        else {
          for (let index = 0; index < visible.length; index++) {
            const column = visible[index];
            let x;
            let columnWidth;
            if (fixedTimeScale) {
              // Fallback for browsers without a detached canvas. It keeps the
              // same Java image-position semantics as the cached path.
              const framePosition = this.settings.wrapDisplay
                ? column.sequence % fixedFrameSlots
                : Math.max(
                    0,
                    fixedFrameSlots - 1 -
                      (latestSequence - column.sequence));
              x = plotLeft +
                framePosition * plotWidth / fixedFrameSlots;
              columnWidth = Math.max(
                1,
                plotWidth / fixedFrameSlots);
            }
            else {
              columnWidth = pixelsPerSlice * ratio;
              x = this.settings.wrapDisplay
                ? plotLeft +
                    (column.sequence % slots) * columnWidth
                : plotLeft + plotWidth -
                    (visible.length - index) * columnWidth;
            }
            for (let offset = 0;
                 offset < column.values.length;
                 offset++) {
              const bin = column.firstBin + offset;
              const frequency = bin * binFrequency;
              if (frequency < lowHz || frequency > highHz) continue;
              const db = this.calibratedFftDb(
                column.values[offset],
                channel);
              const level = clamp(
                (db - minimumDb) / dbRange,
                0,
                1);
              const [red, green, blue] = spectrogramColour(
                level,
                this.settings.colourMap);
              const yFraction = (frequency - lowHz) /
                Math.max(1e-9, highHz - lowHz);
              const y =
                panelBottom - yFraction * panelHeight;
              const binHeight = Math.max(
                ratio,
                panelHeight * binFrequency /
                  Math.max(1e-9, highHz - lowHz));
              context.fillStyle =
                `rgb(${red},${green},${blue})`;
              context.fillRect(
                Math.floor(x),
                Math.floor(y - binHeight),
                Math.ceil(columnWidth),
                Math.ceil(binHeight));
            }
            if (column.discontinuity) {
              context.strokeStyle = "#ff6b6b";
              context.lineWidth = ratio;
              context.beginPath();
              context.moveTo(x, panelTop);
              context.lineTo(x, panelBottom);
              context.stroke();
            }
          }
        }

        if (this.settings.showScale !== false) {
          context.fillStyle = "rgba(6, 17, 30, 0.9)";
          context.fillRect(0, panelTop, plotLeft, panelHeight);
          context.strokeStyle = "#3f607c";
          context.lineWidth = ratio;
          context.beginPath();
          context.moveTo(plotLeft, panelTop);
          context.lineTo(plotLeft, panelBottom);
          context.stroke();
          context.fillStyle = "#9eb5c7";
          context.font =
            `${Math.max(9, 9 * ratio)}px Cascadia Mono, Consolas, monospace`;
          context.textAlign = "right";
          context.fillText(
            `${(highHz / 1000).toFixed(1)} kHz`,
            plotLeft - 5 * ratio,
            panelTop + 11 * ratio);
          context.fillText(
            `${(lowHz / 1000).toFixed(1)} kHz`,
            plotLeft - 5 * ratio,
            panelBottom - 4 * ratio);
          context.textAlign = "left";
          context.fillText(
            `ch ${channel}`,
            plotLeft + 5 * ratio,
            panelTop + 12 * ratio);
        }
      }

      if (this.settings.showScale !== false) {
        context.fillStyle = "rgba(6, 17, 30, 0.9)";
        context.fillRect(width - plotRight, 0, plotRight, plotBottom);
        context.fillRect(0, plotBottom, width, height - plotBottom);
        context.strokeStyle = "#3f607c";
        context.lineWidth = ratio;
        context.beginPath();
        context.moveTo(plotLeft, plotBottom);
        context.lineTo(width, plotBottom);
        context.stroke();
        const legendSteps = 24;
        for (let step = 0; step < legendSteps; step++) {
          const level = step / Math.max(1, legendSteps - 1);
          const [red, green, blue] = spectrogramColour(
            1 - level,
            this.settings.colourMap);
          context.fillStyle = `rgb(${red},${green},${blue})`;
          context.fillRect(
            width - plotRight + 4 * ratio,
            level * plotBottom,
            7 * ratio,
            Math.ceil(plotBottom / legendSteps) + 1);
        }
        context.fillStyle = "#9eb5c7";
        context.font =
          `${Math.max(10, 10 * ratio)}px Cascadia Mono, Consolas, monospace`;
        context.textAlign = "left";
        context.fillText(
          `${maximumDb.toFixed(0)}`,
          width - plotRight + 14 * ratio,
          12 * ratio);
        context.fillText(
          `${minimumDb.toFixed(0)}`,
          width - plotRight + 14 * ratio,
          plotBottom - 4 * ratio);
        context.textAlign = "left";
        context.fillText(
          fixedTimeScale
            ? (this.settings.wrapDisplay
                ? `wrap ${windowSeconds.toFixed(1)} s`
                : `-${windowSeconds.toFixed(1)} s`)
            : `${pixelsPerSlice} px / FFT slice`,
          plotLeft + 4 * ratio,
          height - 6 * ratio);
        context.textAlign = "right";
        context.fillText(
          this.settings.wrapDisplay ? "wrap cursor" : "latest",
          width - plotRight - 4 * ratio,
          height - 6 * ratio);
      }

      this.setStatus(
        `Live · ${this.panelChannels.length} ` +
        `${this.panelChannels.length === 1 ? "panel" : "panels"} · ` +
        `ch ${this.panelChannels.join(", ")} · ` +
        `${(lowHz / 1000).toFixed(1)}-${(highHz / 1000).toFixed(1)} kHz · ` +
        `${visibleFrameCount} frames`);
      this.recordRenderPerformance(renderStartedAt);
    }

    dispose() {
      if (this.disposed) return;
      this.disposed = true;
      this.abortController?.abort();
      this.abortController = null;
      if (this.renderTimer !== null) {
        clearTimeout(this.renderTimer);
        this.renderTimer = null;
      }
      if (this.presentationTimer !== null) {
        clearTimeout(this.presentationTimer);
        this.presentationTimer = null;
      }
      if (this.presentationAnimationFrame !== null &&
          typeof cancelAnimationFrame === "function") {
        cancelAnimationFrame(this.presentationAnimationFrame);
        this.presentationAnimationFrame = null;
      }
      this.pendingColumns.length = 0;
      this.resizeObserver?.disconnect();
      this.resizeObserver = null;
      delete this.canvas.__pamguardSpectrogramMetrics;
    }
  }

  function orderedRange(value, fallback, options = {}) {
    const values = Array.isArray(value)
      ? value.slice(0, 2).map(Number)
      : [];
    if (values.length !== 2 ||
        values.some((entry) => !Number.isFinite(entry))) {
      return fallback;
    }
    const low = Math.min(values[0], values[1]);
    const high = Math.max(values[0], values[1]);
    return [
      options.positive ? Math.max(Number.EPSILON, low) : low,
      options.positive ? Math.max(Number.EPSILON, high) : high
    ];
  }

  function fftComplex(real, imaginary, invert) {
    const length = real.length;
    for (let index = 1, reversed = 0;
         index < length;
         index++) {
      let bit = length >> 1;
      for (; reversed & bit; bit >>= 1) {
        reversed ^= bit;
      }
      reversed ^= bit;
      if (index < reversed) {
        [real[index], real[reversed]] =
          [real[reversed], real[index]];
        [imaginary[index], imaginary[reversed]] =
          [imaginary[reversed], imaginary[index]];
      }
    }
    for (let span = 2; span <= length; span <<= 1) {
      const angle =
        (invert ? 2 : -2) * Math.PI / span;
      const rotationReal = Math.cos(angle);
      const rotationImaginary = Math.sin(angle);
      for (let start = 0; start < length; start += span) {
        let currentReal = 1;
        let currentImaginary = 0;
        for (let offset = 0; offset < span / 2; offset++) {
          const upperReal = real[start + offset];
          const upperImaginary = imaginary[start + offset];
          const lowerReal =
            real[start + offset + span / 2] * currentReal -
            imaginary[start + offset + span / 2] *
              currentImaginary;
          const lowerImaginary =
            real[start + offset + span / 2] *
              currentImaginary +
            imaginary[start + offset + span / 2] *
              currentReal;
          real[start + offset] = upperReal + lowerReal;
          imaginary[start + offset] =
            upperImaginary + lowerImaginary;
          real[start + offset + span / 2] =
            upperReal - lowerReal;
          imaginary[start + offset + span / 2] =
            upperImaginary - lowerImaginary;
          const nextReal =
            currentReal * rotationReal -
            currentImaginary * rotationImaginary;
          currentImaginary =
            currentReal * rotationImaginary +
            currentImaginary * rotationReal;
          currentReal = nextReal;
        }
      }
    }
    if (invert) {
      for (let index = 0; index < length; index++) {
        real[index] /= length;
        imaginary[index] /= length;
      }
    }
  }

  function nextBinaryLength(sampleCount) {
    let length = 1;
    const required = Math.max(1, Math.ceil(Number(sampleCount) || 0));
    while (length < required) length <<= 1;
    return length;
  }

  function computeClickPowerSpectra(waveforms, sampleRateHz) {
    const channels = (Array.isArray(waveforms) ? waveforms : [])
      .filter((waveform) =>
        Array.isArray(waveform) && waveform.length);
    if (!channels.length) return null;

    const fftLength = nextBinaryLength(
      Math.max(...channels.map((waveform) => waveform.length)));
    const binCount = Math.max(1, fftLength / 2);
    const spectra = [];
    const totalPower = new Float64Array(binCount);
    let maximum = 0;

    for (const waveform of channels) {
      const real = new Array(fftLength).fill(0);
      const imaginary = new Array(fftLength).fill(0);
      const denominator = waveform.length - 1;
      for (let index = 0;
           index < Math.min(waveform.length, fftLength);
           index++) {
        // PAMGuard ClickDetection.applyHanningWindow applies the window to
        // the captured click before RawDataTransforms zero-pads it.
        const window = denominator > 0
          ? 0.5 * (
              1 - Math.cos(2 * Math.PI * index / denominator))
          : 1;
        real[index] = (Number(waveform[index]) || 0) * window;
      }
      fftComplex(real, imaginary, false);
      const power = new Float64Array(binCount);
      let peakBin = 0;
      let peakPower = 0;
      for (let bin = 0; bin < binCount; bin++) {
        const value =
          real[bin] * real[bin] +
          imaginary[bin] * imaginary[bin];
        power[bin] = value;
        totalPower[bin] += value;
        maximum = Math.max(maximum, value);
        if (value > peakPower) {
          peakPower = value;
          peakBin = bin;
        }
      }
      spectra.push({
        power,
        peakBin,
        peakPower,
        peakFrequencyHz:
          peakBin * sampleRateHz / fftLength
      });
    }

    let peakBin = 0;
    let peakPower = 0;
    for (let bin = 0; bin < totalPower.length; bin++) {
      if (totalPower[bin] > peakPower) {
        peakPower = totalPower[bin];
        peakBin = bin;
      }
    }
    return {
      fftLength,
      spectra,
      totalPower,
      maximum,
      peakBin,
      peakPower,
      peakFrequencyHz:
        peakBin * sampleRateHz / fftLength
    };
  }

  function computeClickWigner(waveform) {
    // This is the established pre-project Wigner-Ville calculation: centre
    // a 128-sample window on the click peak, form the analytic signal, then
    // FFT the conjugate lag product at every time sample.
    const size = 128;
    let peak = 0;
    let peakAt = 0;
    for (let index = 0; index < waveform.length; index++) {
      const magnitude = Math.abs(Number(waveform[index]) || 0);
      if (magnitude > peak) {
        peak = magnitude;
        peakAt = index;
      }
    }
    const start = Math.max(
      0,
      Math.min(
        peakAt - size / 2,
        waveform.length - size));
    const real = new Array(size).fill(0);
    const imaginary = new Array(size).fill(0);
    for (let index = 0; index < size; index++) {
      real[index] = Number(waveform[start + index]) || 0;
    }
    fftComplex(real, imaginary, false);
    for (let bin = 1; bin < size / 2; bin++) {
      real[bin] *= 2;
      imaginary[bin] *= 2;
    }
    for (let bin = size / 2 + 1; bin < size; bin++) {
      real[bin] = 0;
      imaginary[bin] = 0;
    }
    fftComplex(real, imaginary, true);

    const values = [];
    let maximum = 0;
    for (let time = 0; time < size; time++) {
      const kernelReal = new Array(size).fill(0);
      const kernelImaginary = new Array(size).fill(0);
      const lagLimit = Math.min(
        time,
        size - 1 - time);
      for (let lag = -lagLimit;
           lag <= lagLimit;
           lag++) {
        const index = (lag + size) % size;
        kernelReal[index] =
          real[time + lag] * real[time - lag] +
          imaginary[time + lag] *
            imaginary[time - lag];
        kernelImaginary[index] =
          imaginary[time + lag] * real[time - lag] -
          real[time + lag] *
            imaginary[time - lag];
      }
      fftComplex(
        kernelReal,
        kernelImaginary,
        false);
      const column = new Float64Array(size);
      for (let bin = 0; bin < size; bin++) {
        const value = Math.max(0, kernelReal[bin]);
        column[bin] = value;
        maximum = Math.max(maximum, value);
      }
      values.push(column);
    }
    return {
      values,
      rows: size,
      columns: size,
      maximum,
      start
    };
  }

  function displayElement(tagName, options = {}) {
    const element = document.createElement(tagName);
    if (options.className) element.className = options.className;
    if (options.text !== undefined) {
      element.textContent = options.text;
    }
    for (const [name, value] of Object.entries(
      options.attributes || {})) {
      element.setAttribute(name, String(value));
    }
    return element;
  }

  class ProjectClickDisplay {
    constructor(options) {
      this.canvas = options.canvas;
      this.status = options.status;
      this.settings = options.settings || {};
      this.sourceBlockId = options.sourceBlockId || "";
      this.bearingBlockId = options.bearingBlockId || "";
      this.sampleRateHz = Number(options.sampleRateHz) || 0;
      this.api = options.api;
      this.headers = options.headers || (() => ({}));
      this.onError = options.onError || (() => {});
      this.clickDetectorUnitId = options.clickDetectorUnitId || "";
      this.controlsRoot = options.controlsRoot || null;
      this.detailRoot = options.detailRoot || null;
      this.detailStatus = options.detailStatus || null;
      this.waveformCanvas = options.waveformCanvas || null;
      this.spectrumCanvas = options.spectrumCanvas || null;
      this.spectrumStatus = options.spectrumStatus || null;
      this.wignerCanvas = options.wignerCanvas || null;
      this.wignerStatus = options.wignerStatus || null;
      this.running = Boolean(options.running);
      const requestedPresentationDelay =
        Number(options.presentationDelayMs);
      this.presentationDelayMs = this.running
        ? clamp(
            Number.isFinite(requestedPresentationDelay)
              ? requestedPresentationDelay
              : 1000,
            0,
            5000)
        : 0;
      this.continuousTimeline = Boolean(
        this.running && this.sourceBlockId);
      this.clicks = [];
      this.clickKeys = new Set();
      this.bearings = new Map();
      this.lastTimeMs = 0;
      this.latestSourceTimeMs = 0;
      this.latestArrivalWallMs = 0;
      this.sourceUsesWallClock = false;
      this.displayEndMs = null;
      this.lastAnimationWallMs = 0;
      this.animationFrame = null;
      this.acceptedClickCount = 0;
      this.renderDurationsMs = [];
      this.renderIntervalsMs = [];
      this.lastRenderStartMs = 0;
      this.selected = null;
      this.markedKeys = new Set();
      this.trackedEvents = [];
      this.eventByUid = new Map();
      this.trackedBusy = false;
      this.trackedMessage = null;
      this.trackedEventSelect = null;
      this.trackedReassignSelect = null;
      this.trackedEventList = null;
      this.trackedMenu = null;
      this.hitPoints = [];
      this.renderPending = false;
      this.disposed = false;
      this.controllers = [];
      this.resizeObserver = typeof ResizeObserver === "function"
        ? new ResizeObserver(() => this.scheduleRender())
        : null;
      this.resizeObserver?.observe(this.canvas);
      this.canvas.__pamguardClickMetrics = () =>
        this.performanceMetrics();
      this.canvas.addEventListener("click", (event) =>
        this.selectNearest(event));
      this.canvas.addEventListener("contextmenu", (event) =>
        this.showTrackedClickMenu(event));
      if (this.controlsRoot && this.clickDetectorUnitId) {
        this.mountTrackedControls();
        void this.refreshTrackedEvents().catch((error) =>
          this.reportTrackedError(error));
      }
      if (this.continuousTimeline) {
        this.startAnimation();
      }
      else {
        this.scheduleRender();
      }
      if (this.running && this.sourceBlockId) {
        void this.connect();
      }
      else {
        this.setStatus(
          this.sourceBlockId
            ? "Start processing to receive clicks"
            : "Click source unavailable");
      }
    }

    setStatus(message) {
      if (!this.disposed && this.status) {
        this.status.textContent = message;
      }
    }

    trackedPath(suffix = "") {
      return `/v1/projects/active/click-detectors/` +
        `${encodeURIComponent(this.clickDetectorUnitId)}/` +
        `tracked-events${suffix}`;
    }

    async trackedRequest(path, options = {}) {
      const response = await fetch(this.api(path), {
        ...options,
        headers: {
          ...this.headers(),
          ...(options.body
            ? { "Content-Type": "application/json" }
            : {}),
          ...(options.headers || {})
        }
      });
      let body = {};
      try {
        body = await response.json();
      }
      catch {
        body = {};
      }
      if (!response.ok) {
        const error = new Error(
          body.message || body.error ||
          `Tracked-event request failed (${response.status})`);
        error.status = response.status;
        error.code = body.code;
        error.body = body;
        throw error;
      }
      return body;
    }

    mountTrackedControls() {
      this.controlsRoot.classList.add("tracked-click-controls");
      this.controlsRoot.setAttribute(
        "data-tracked-click-controls",
        this.clickDetectorUnitId);
      const heading = displayElement("div", {
        className: "tracked-click-heading"
      });
      heading.append(
        displayElement("strong", { text: "Manual click trains" }));
      this.trackedMessage = displayElement("span", {
        className: "tracked-click-message",
        text: "Loading tracked eventsâ€¦",
        attributes: {
          role: "status",
          "data-tracked-click-message": "true"
        }
      });
      heading.append(this.trackedMessage);

      const actions = displayElement("div", {
        className: "tracked-click-actions"
      });
      const newEvent = this.trackedButton(
        "New Click Train",
        "new-event",
        () => this.assignMarked(null));
      this.trackedEventSelect = displayElement("select", {
        attributes: {
          "aria-label": "Tracked event",
          "data-tracked-click-event-select": "true"
        }
      });
      const assign = this.trackedButton(
        "Assign marked",
        "assign",
        () => {
          const eventId = Number(this.trackedEventSelect.value);
          if (Number.isInteger(eventId) && eventId > 0) {
            return this.assignMarked(eventId);
          }
        });
      const remove = this.trackedButton(
        "Remove marked",
        "remove",
        () => this.removeMarked());
      const localise = this.trackedButton(
        "Localise event",
        "localise",
        () => this.localiseSelectedEvent());
      actions.append(
        newEvent,
        this.trackedEventSelect,
        assign,
        remove,
        localise);

      const reassign = displayElement("div", {
        className: "tracked-click-actions tracked-click-reassign"
      });
      reassign.append(displayElement("span", {
        text: "Whole train:"
      }));
      this.trackedReassignSelect = displayElement("select", {
        attributes: {
          "aria-label": "Reassignment target event",
          "data-tracked-click-reassign-select": "true"
        }
      });
      reassign.append(
        this.trackedReassignSelect,
        this.trackedButton(
          "Reassign",
          "reassign",
          () => this.reassignSelectedEvent()));

      this.trackedEventList = displayElement("div", {
        className: "tracked-click-event-list",
        attributes: {
          "data-tracked-click-event-list": "true"
        }
      });
      this.controlsRoot.append(
        heading,
        actions,
        reassign,
        this.trackedEventList);
      this.updateTrackedControls();
    }

    trackedButton(label, action, callback) {
      const button = displayElement("button", {
        text: label,
        attributes: {
          type: "button",
          "data-tracked-click-action": action
        }
      });
      button.addEventListener("click", () => {
        if (this.trackedBusy) return;
        void Promise.resolve(callback()).catch((error) =>
          this.reportTrackedError(error));
      });
      return button;
    }

    markedClicks() {
      return this.clicks.filter((click) =>
        this.markedKeys.has(click.key));
    }

    clickLocator(click) {
      return {
        uid: click.uid,
        startSample: click.startSample,
        channelBitmap: click.channelBitmap
      };
    }

    async refreshTrackedEvents(message = "") {
      if (!this.clickDetectorUnitId) return;
      const body = await this.trackedRequest(this.trackedPath());
      this.trackedEvents = Array.isArray(body.events)
        ? body.events
        : [];
      this.eventByUid.clear();
      for (const event of this.trackedEvents) {
        for (const click of event.clicks || []) {
          this.eventByUid.set(Number(click.uid), Number(event.eventId));
        }
      }
      this.updateTrackedControls(message);
      this.scheduleRender();
    }

    async withTrackedBusy(operation) {
      if (this.trackedBusy) return;
      this.trackedBusy = true;
      this.updateTrackedControls("Updating tracked eventsâ€¦");
      try {
        await operation();
      }
      finally {
        this.trackedBusy = false;
        this.updateTrackedControls();
      }
    }

    async assignMarked(eventId) {
      const clicks = this.markedClicks();
      if (!clicks.length) {
        throw new Error(
          "Select a click, or Shift-click several clicks, first");
      }
      await this.withTrackedBusy(async () => {
        const event = await this.trackedRequest(
          this.trackedPath(":assign"),
          {
            method: "POST",
            body: JSON.stringify({
              clicks: clicks.map((click) => this.clickLocator(click)),
              eventId
            })
          });
        await this.refreshTrackedEvents(
          `${clicks.length} ` +
          `${clicks.length === 1 ? "click" : "clicks"} assigned to ` +
          `Click Train ${event.eventId}`);
      });
    }

    async removeMarked() {
      const clicks = this.markedClicks().filter((click) =>
        this.eventByUid.has(click.uid));
      if (!clicks.length) {
        throw new Error("No marked click belongs to a tracked event");
      }
      await this.withTrackedBusy(async () => {
        for (const click of clicks) {
          await this.trackedRequest(
            `/v1/projects/active/click-detectors/` +
            `${encodeURIComponent(this.clickDetectorUnitId)}/` +
            `tracked-clicks/${encodeURIComponent(click.uid)}`,
            { method: "DELETE" });
        }
        await this.refreshTrackedEvents(
          `${clicks.length} ` +
          `${clicks.length === 1 ? "click" : "clicks"} removed`);
      });
    }

    selectedEventId() {
      const selected = Number(this.trackedEventSelect?.value);
      return Number.isInteger(selected) && selected > 0
        ? selected
        : null;
    }

    async reassignSelectedEvent() {
      const source = this.selectedEventId();
      const target = Number(this.trackedReassignSelect?.value);
      if (!source ||
          !Number.isInteger(target) ||
          target <= 0 ||
          source === target) {
        throw new Error(
          "Choose different source and target click trains");
      }
      await this.withTrackedBusy(async () => {
        await this.trackedRequest(
          this.trackedPath(
            `/${encodeURIComponent(source)}:reassign`),
          {
            method: "POST",
            body: JSON.stringify({ targetEventId: target })
          });
        await this.refreshTrackedEvents(
          `Click Train ${source} reassigned to ${target}`);
      });
    }

    async localiseSelectedEvent() {
      const eventId = this.selectedEventId();
      if (!eventId) {
        throw new Error("Choose a click train to localise");
      }
      try {
        const result = await this.trackedRequest(
          this.trackedPath(
            `/${encodeURIComponent(eventId)}:localise`),
          {
            method: "POST",
            body: "{}"
          });
        this.updateTrackedControls(
          result.message || `Click Train ${eventId} localised`);
      }
      catch (error) {
        if (error.status === 409 && error.body) {
          this.updateTrackedControls(
            error.body.message || error.body.error);
          return;
        }
        throw error;
      }
    }

    updateTrackedControls(message = "") {
      if (!this.controlsRoot) return;
      const selectedEvent = this.selectedEventId();
      const current =
        selectedEvent ||
        Number(this.trackedEvents[0]?.eventId) ||
        0;
      const replaceOptions = (select, exclude = null) => {
        if (!select) return;
        const previous = Number(select.value);
        select.replaceChildren();
        if (!this.trackedEvents.length) {
          const option = displayElement("option", {
            text: "No click trains",
            attributes: { value: "" }
          });
          select.append(option);
          select.disabled = true;
          return;
        }
        select.disabled = false;
        for (const event of this.trackedEvents) {
          if (Number(event.eventId) === exclude) continue;
          select.append(displayElement("option", {
            text: `Click Train ${event.eventId} ` +
              `(${event.clickCount} clicks)`,
            attributes: { value: event.eventId }
          }));
        }
        const preferred = [...select.children].some((option) =>
          Number(option.value) === previous)
          ? previous
          : [...select.children].some((option) =>
            Number(option.value) === current)
            ? current
            : select.children[0]?.value ?? "";
        select.value = String(preferred);
      };
      replaceOptions(this.trackedEventSelect);
      replaceOptions(
        this.trackedReassignSelect,
        this.selectedEventId());
      if (this.trackedEventSelect) {
        this.trackedEventSelect.onchange = () =>
          this.updateTrackedControls();
      }
      this.trackedEventList?.replaceChildren(
        ...this.trackedEvents.map((event) => {
          const row = displayElement("button", {
            className: "tracked-click-event-row",
            attributes: {
              type: "button",
              "data-tracked-click-event-id": event.eventId
            }
          });
          const status = event.localisation?.available
            ? "localised"
            : event.localisation?.status || "unavailable";
          row.textContent =
            `Click Train ${event.eventId} Â· ` +
            `${event.clickCount} clicks Â· ${status}`;
          row.addEventListener("click", () => {
            this.trackedEventSelect.value = String(event.eventId);
            this.updateTrackedControls();
          });
          return row;
        }));
      const marked = this.markedClicks();
      if (this.trackedMessage) {
        this.trackedMessage.textContent = message ||
          `${this.trackedEvents.length} ` +
          `${this.trackedEvents.length === 1 ? "event" : "events"} Â· ` +
          `${marked.length} marked`;
      }
      for (const button of this.controlsRoot.querySelectorAll(
        "[data-tracked-click-action]")) {
        const action = button.getAttribute(
          "data-tracked-click-action");
        const requiresMark = ["new-event", "assign", "remove"]
          .includes(action);
        const requiresEvents = ["assign", "localise", "reassign"]
          .includes(action);
        button.disabled = this.trackedBusy ||
          (requiresMark && !marked.length) ||
          (requiresEvents && !this.trackedEvents.length) ||
          (action === "reassign" && this.trackedEvents.length < 2);
      }
    }

    reportTrackedError(error) {
      this.updateTrackedControls(error?.message || String(error));
      this.onError(error);
    }

    eventColour(eventId) {
      const palette = [
        "#32b7e8",
        "#f28e2b",
        "#59a14f",
        "#e15759",
        "#b07aa1",
        "#76b7b2",
        "#edc948",
        "#ff9da7"
      ];
      return palette[(Math.max(1, Number(eventId)) - 1) % palette.length];
    }

    async readNdjson(blockId, history, accept, controller, label) {
      const query = new URLSearchParams({
        format: "ndjson",
        history: String(history)
      });
      const response = await fetch(
        this.api(
          `/data-blocks/${encodeURIComponent(blockId)}/stream?${query}`),
        {
          headers: this.headers(),
          signal: controller.signal
        });
      if (!response.ok || !response.body) {
        throw new Error(`${label} stream unavailable (${response.status})`);
      }
      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffered = "";
      while (!controller.signal.aborted) {
        const { done, value } = await reader.read();
        if (done) break;
        buffered += decoder.decode(value, { stream: true });
        let newline;
        while ((newline = buffered.indexOf("\n")) >= 0) {
          const line = buffered.slice(0, newline).trim();
          buffered = buffered.slice(newline + 1);
          if (!line) continue;
          accept.call(this, JSON.parse(line));
        }
      }
      if (!controller.signal.aborted && label === "Click") {
        this.setStatus("Click stream ended");
      }
    }

    async connect() {
      const clickController = new AbortController();
      this.controllers.push(clickController);
      this.setStatus("Connecting to Click Detector…");
      try {
        const clickPromise = this.readNdjson(
          this.sourceBlockId,
          2048,
          this.acceptClick,
          clickController,
          "Click");
        if (this.bearingBlockId) {
          const bearingController = new AbortController();
          this.controllers.push(bearingController);
          void this.readNdjson(
            this.bearingBlockId,
            2048,
            this.acceptBearing,
            bearingController,
            "Bearing").catch((error) => {
              if (!bearingController.signal.aborted && !this.disposed) {
                this.onError(error);
              }
            });
        }
        this.setStatus("Live · waiting for clicks");
        await clickPromise;
      }
      catch (error) {
        if (clickController.signal.aborted || this.disposed) return;
        this.setStatus(error?.message || String(error));
        this.onError(error);
      }
    }

    clickTimeMs(unit, payload) {
      const explicit = Number(payload?.timeMs ?? unit?.timeMs);
      if (Number.isFinite(explicit)) return explicit;
      const sample = Number(payload?.startSample ?? unit?.startSample);
      return this.sampleRateHz > 0 && Number.isFinite(sample)
        ? sample * 1000 / this.sampleRateHz
        : Date.now();
    }

    acceptClick(unit) {
      const payload = unit?.payload;
      const startSample = Number(payload?.startSample ?? unit?.startSample);
      const channelBitmap = Number(
        payload?.channelBitmap ?? unit?.channelBitmap);
      if (!Number.isFinite(startSample) ||
          !Number.isInteger(channelBitmap) ||
          channelBitmap <= 0) return;
      const key = `${startSample}:${channelBitmap}`;
      if (this.clickKeys.has(key)) return;
      if (this.settings.showEchoes === false && payload?.echo === true) return;
      const selectedBitmap = Number(this.settings.channelBitmap) || 0;
      if (selectedBitmap && (selectedBitmap & channelBitmap) === 0) return;
      const click = {
        key,
        uid: Number(unit.uid) || 0,
        startSample,
        timeMs: this.clickTimeMs(unit, payload),
        channelBitmap,
        signalExcessDb: Number(payload?.signalExcessDb),
        durationSamples: Number(payload?.durationSamples) || 0,
        waveform: Array.isArray(payload?.waveform)
          ? payload.waveform
          : [],
        echo: payload?.echo === true,
        discontinuity: Boolean(unit.discontinuity),
        bearingDegrees: this.bearings.get(startSample) ?? null,
        iciSeconds: null
      };
      if (!Number.isFinite(click.signalExcessDb)) {
        click.signalExcessDb = 0;
      }
      this.acceptedClickCount++;
      this.clickKeys.add(key);
      this.clicks.push(click);
      this.clicks.sort((left, right) =>
        left.startSample - right.startSample ||
        left.channelBitmap - right.channelBitmap);
      this.recalculateIci();
      this.lastTimeMs = Math.max(this.lastTimeMs, click.timeMs);
      if (click.timeMs >= this.latestSourceTimeMs) {
        this.latestSourceTimeMs = click.timeMs;
        this.latestArrivalWallMs = Date.now();
        this.sourceUsesWallClock = click.timeMs > 100000000000;
      }
      const windowSeconds = Math.max(
        1,
        Number(this.settings.timeWindowSeconds) || 20);
      const cutoff = this.lastTimeMs -
        Math.max(120, windowSeconds * 6) * 1000;
      while (this.clicks.length > 2 &&
             (this.clicks[0].timeMs < cutoff ||
              this.clicks.length > 5000)) {
        const removed = this.clicks.shift();
        this.clickKeys.delete(removed.key);
        this.markedKeys.delete(removed.key);
        if (this.selected === removed) this.selected = null;
      }
      this.scheduleRender();
    }

    acceptBearing(unit) {
      const payload = unit?.payload;
      const startSample = Number(payload?.clickStartSample);
      const bearing = Number(payload?.azimuthDegrees);
      if (!payload?.valid ||
          !Number.isFinite(startSample) ||
          !Number.isFinite(bearing)) return;
      this.bearings.set(startSample, bearing);
      for (const click of this.clicks) {
        if (click.startSample === startSample) {
          click.bearingDegrees = bearing;
        }
      }
      if (this.selected?.startSample === startSample) {
        this.renderSelectedClick();
      }
      if (this.bearings.size > 6000) {
        const oldest = this.clicks[0]?.startSample ?? startSample;
        for (const key of this.bearings.keys()) {
          if (key < oldest) this.bearings.delete(key);
        }
      }
      this.scheduleRender();
    }

    recalculateIci() {
      const previous = new Map();
      for (const click of this.clicks) {
        const prior = previous.get(click.channelBitmap);
        click.iciSeconds = prior && this.sampleRateHz > 0
          ? (click.startSample - prior.startSample) / this.sampleRateHz
          : null;
        previous.set(click.channelBitmap, click);
      }
    }

    estimatedLiveTimeMs(now = Date.now()) {
      if (this.sourceUsesWallClock) {
        return now;
      }
      if (this.latestSourceTimeMs > 0 &&
          this.latestArrivalWallMs > 0) {
        return this.latestSourceTimeMs +
          Math.max(0, now - this.latestArrivalWallMs);
      }
      return this.latestSourceTimeMs || now;
    }

    advanceTimeline(now = Date.now()) {
      if (!this.continuousTimeline ||
          this.presentationDelayMs <= 0 ||
          this.latestSourceTimeMs <= 0) return;
      const liveTimeMs = this.estimatedLiveTimeMs(now);
      if (!Number.isFinite(this.displayEndMs)) {
        this.displayEndMs =
          liveTimeMs - this.presentationDelayMs;
        this.lastAnimationWallMs = now;
        return;
      }
      const elapsedMs = this.lastAnimationWallMs > 0
        ? Math.min(
            500,
            Math.max(0, now - this.lastAnimationWallMs))
        : 0;
      const lagMs = liveTimeMs - this.displayEndMs;
      const skew = clamp(
        (lagMs - this.presentationDelayMs) /
          Math.max(1, this.presentationDelayMs),
        -0.3,
        0.3);
      this.displayEndMs = Math.min(
        liveTimeMs,
        this.displayEndMs + elapsedMs * (1 + skew));
      if (lagMs >
          this.presentationDelayMs * 3 + 1000) {
        this.displayEndMs =
          liveTimeMs - this.presentationDelayMs;
      }
      this.lastAnimationWallMs = now;
    }

    startAnimation() {
      if (this.animationFrame !== null || this.disposed ||
          !this.continuousTimeline ||
          typeof requestAnimationFrame !== "function") return;
      this.animationFrame = requestAnimationFrame(() => {
        this.animationFrame = null;
        if (this.disposed) return;
        this.advanceTimeline(Date.now());
        this.render();
        this.startAnimation();
      });
    }

    scheduleRender() {
      if (this.animationFrame !== null) return;
      if (this.renderPending || this.disposed) return;
      this.renderPending = true;
      requestAnimationFrame(() => {
        this.renderPending = false;
        if (!this.disposed) this.render();
      });
    }

    drawPanel(
      context,
      bounds,
      label,
      range,
      valueOf,
      visible,
      timeRange,
      options = {}) {
      const { left, top, width, height, ratio } = bounds;
      const axis = 56 * ratio;
      const bottom = 17 * ratio;
      const plotLeft = left + axis;
      const plotTop = top + 4 * ratio;
      const plotWidth = Math.max(1, width - axis - 5 * ratio);
      const plotHeight = Math.max(1, height - bottom - 8 * ratio);
      context.fillStyle = options.background || "#071521";
      context.fillRect(left, top, width, height);
      context.strokeStyle = "#29465b";
      context.lineWidth = ratio;
      context.fillStyle = "#8faabd";
      context.font =
        `${Math.max(9, 9 * ratio)}px Cascadia Mono, Consolas, monospace`;
      context.textAlign = "right";
      for (let grid = 0; grid <= 4; grid++) {
        const fraction = grid / 4;
        const y = plotTop + plotHeight * (1 - fraction);
        context.beginPath();
        context.moveTo(plotLeft, y);
        context.lineTo(plotLeft + plotWidth, y);
        context.stroke();
        const transformed =
          range[0] + fraction * (range[1] - range[0]);
        const value = options.logarithmic
          ? 10 ** transformed
          : transformed;
        context.fillText(
          options.format ? options.format(value) : value.toFixed(1),
          plotLeft - 5 * ratio,
          y + 3 * ratio);
      }
      context.save();
      context.translate(left + 11 * ratio, top + height / 2);
      context.rotate(-Math.PI / 2);
      context.textAlign = "center";
      context.fillStyle = "#afc4d2";
      context.fillText(label, 0, 0);
      context.restore();

      const [startMs, endMs] = timeRange;
      for (const click of visible) {
        const raw = valueOf(click);
        if (!Number.isFinite(raw)) continue;
        const value = options.logarithmic
          ? Math.log10(Math.max(raw, Number.EPSILON))
          : raw;
        if (value < range[0] || value > range[1]) continue;
        const x = plotLeft +
          (click.timeMs - startMs) /
            Math.max(1, endMs - startMs) * plotWidth;
        const y = plotTop + plotHeight *
          (1 - (value - range[0]) / Math.max(
            Number.EPSILON,
            range[1] - range[0]));
        const marked = this.markedKeys.has(click.key);
        const eventId = this.eventByUid.get(click.uid);
        const size = marked ? 5 * ratio : 3 * ratio;
        context.fillStyle = marked
          ? "#ffffff"
          : eventId
            ? this.eventColour(eventId)
            : click.echo ? "#8293a1" : "#32b7e8";
        context.fillRect(
          x - size / 2,
          y - size / 2,
          size,
          size);
        if (eventId && typeof context.strokeRect === "function") {
          context.strokeStyle = this.eventColour(eventId);
          context.lineWidth = Math.max(ratio, 1);
          context.strokeRect(
            x - size,
            y - size,
            size * 2,
            size * 2);
        }
        if (click.discontinuity) {
          context.strokeStyle = "#ff6b6b";
          context.beginPath();
          context.moveTo(x, plotTop);
          context.lineTo(x, plotTop + plotHeight);
          context.stroke();
        }
        this.hitPoints.push({ x, y, click });
      }
      context.fillStyle = "#7893a5";
      context.textAlign = "left";
      context.fillText(
        `−${((endMs - startMs) / 1000).toFixed(0)} s`,
        plotLeft,
        top + height - 3 * ratio);
      context.textAlign = "right";
      context.fillText(
        "now",
        plotLeft + plotWidth,
        top + height - 3 * ratio);
    }

    selectedWaveform() {
      return this.selectedWaveforms()?.[0] || null;
    }

    selectedWaveforms() {
      const waveform = this.selected?.waveform;
      if (!Array.isArray(waveform) || !waveform.length) return null;
      if (Array.isArray(waveform[0])) {
        const channels = waveform.filter(
          (channel) =>
            Array.isArray(channel) && channel.length);
        return channels.length ? channels : null;
      }
      return [waveform];
    }

    renderSelectedClick() {
      if (!this.detailRoot || !this.waveformCanvas ||
          !this.wignerCanvas) return;
      if (!this.selected) {
        this.detailRoot.hidden = true;
        return;
      }
      this.detailRoot.hidden = false;
      if (this.detailStatus) {
        this.detailStatus.textContent =
          `sample ${this.selected.startSample} · ` +
          `${this.selected.durationSamples || "?"} samples · ` +
          `${this.selected.signalExcessDb.toFixed(1)} dB SE` +
          (Number.isFinite(this.selected.bearingDegrees)
            ? ` · ${this.selected.bearingDegrees.toFixed(1)}°`
            : "");
      }

      const waveforms = this.selectedWaveforms();
      const waveform = waveforms?.[0] || null;
      const waveformSize = resizeCanvas(this.waveformCanvas);
      const waveformContext =
        this.waveformCanvas.getContext("2d");
      waveformContext.fillStyle = "#06111e";
      waveformContext.fillRect(
        0,
        0,
        waveformSize.width,
        waveformSize.height);
      const spectrumSize = this.spectrumCanvas
        ? resizeCanvas(this.spectrumCanvas)
        : null;
      const spectrumContext = this.spectrumCanvas
        ? this.spectrumCanvas.getContext("2d")
        : null;
      if (spectrumContext && spectrumSize) {
        spectrumContext.fillStyle = "#06111e";
        spectrumContext.fillRect(
          0,
          0,
          spectrumSize.width,
          spectrumSize.height);
      }
      const wignerSize = resizeCanvas(this.wignerCanvas);
      const wignerContext =
        this.wignerCanvas.getContext("2d");
      wignerContext.fillStyle = "#06111e";
      wignerContext.fillRect(
        0,
        0,
        wignerSize.width,
        wignerSize.height);

      if (!waveform?.length) {
        waveformContext.fillStyle = "#8aa0af";
        waveformContext.font =
          `${Math.max(11, 11 * waveformSize.ratio)}px ` +
          `Cascadia Mono, Consolas, monospace`;
        waveformContext.fillText(
          "No waveform stored for this click",
          12 * waveformSize.ratio,
          24 * waveformSize.ratio);
        if (this.wignerStatus) {
          this.wignerStatus.textContent =
            "Wigner plot unavailable without waveform samples";
        }
        if (this.spectrumStatus) {
          this.spectrumStatus.textContent =
            "Spectrum unavailable without waveform samples";
        }
        return;
      }

      let peak = Number.EPSILON;
      for (const sample of waveform) {
        peak = Math.max(
          peak,
          Math.abs(Number(sample) || 0));
      }
      waveformContext.strokeStyle = "#32b7e8";
      waveformContext.lineWidth =
        Math.max(1, waveformSize.ratio);
      waveformContext.beginPath();
      for (let index = 0; index < waveform.length; index++) {
        const x =
          index /
          Math.max(1, waveform.length - 1) *
          waveformSize.width;
        const y =
          waveformSize.height / 2 -
          (Number(waveform[index]) || 0) / peak *
          (waveformSize.height / 2 -
            4 * waveformSize.ratio);
        if (index === 0) {
          waveformContext.moveTo(x, y);
        }
        else {
          waveformContext.lineTo(x, y);
        }
      }
      waveformContext.stroke();

      const spectrum = computeClickPowerSpectra(
        waveforms,
        this.sampleRateHz);
      if (spectrumContext && spectrumSize && spectrum) {
        const ratio = spectrumSize.ratio;
        const left = 43 * ratio;
        const top = 9 * ratio;
        const right = 8 * ratio;
        const bottom = 21 * ratio;
        const width = Math.max(
          1,
          spectrumSize.width - left - right);
        const height = Math.max(
          1,
          spectrumSize.height - top - bottom);
        spectrumContext.strokeStyle = "#29465b";
        spectrumContext.lineWidth = Math.max(1, ratio);
        spectrumContext.fillStyle = "#8faabd";
        spectrumContext.font =
          `${Math.max(9, 9 * ratio)}px ` +
          `Cascadia Mono, Consolas, monospace`;
        for (let grid = 0; grid <= 4; grid++) {
          const fraction = grid / 4;
          const y = top + height * (1 - fraction);
          spectrumContext.beginPath();
          spectrumContext.moveTo(left, y);
          spectrumContext.lineTo(left + width, y);
          spectrumContext.stroke();
          spectrumContext.textAlign = "right";
          spectrumContext.fillText(
            fraction.toFixed(2),
            left - 5 * ratio,
            y + 3 * ratio);
        }
        spectrumContext.textAlign = "left";
        spectrumContext.fillText(
          "0",
          left,
          top + height + 15 * ratio);
        spectrumContext.textAlign = "right";
        spectrumContext.fillText(
          `${(this.sampleRateHz / 2000).toFixed(1)} kHz`,
          left + width,
          top + height + 15 * ratio);

        const colours = [
          "#32b7e8",
          "#ffb347",
          "#8ee36b",
          "#e779d6"
        ];
        const scale = Math.max(
          Number.EPSILON,
          spectrum.maximum * 1.1);
        for (let channel = 0;
             channel < spectrum.spectra.length;
             channel++) {
          const channelSpectrum = spectrum.spectra[channel];
          spectrumContext.strokeStyle =
            colours[channel % colours.length];
          spectrumContext.lineWidth = Math.max(1, ratio);
          spectrumContext.beginPath();
          for (let bin = 0;
               bin < channelSpectrum.power.length;
               bin++) {
            const x = left +
              bin /
              Math.max(1, channelSpectrum.power.length) *
              width;
            const y = top + height * (
              1 - channelSpectrum.power[bin] / scale);
            if (bin === 0) {
              spectrumContext.moveTo(x, y);
            }
            else {
              spectrumContext.lineTo(x, y);
            }
          }
          spectrumContext.stroke();

          const peakX = left +
            channelSpectrum.peakBin /
            Math.max(1, channelSpectrum.power.length) *
            width;
          const peakY = top + height * (
            1 - channelSpectrum.peakPower / scale);
          spectrumContext.fillStyle =
            colours[channel % colours.length];
          spectrumContext.fillRect(
            peakX - 2 * ratio,
            peakY - 2 * ratio,
            4 * ratio,
            4 * ratio);
        }
      }
      if (this.spectrumStatus && spectrum) {
        const channelPeaks = spectrum.spectra.map(
          (channel, index) =>
            `ch ${index} ${(
              channel.peakFrequencyHz / 1000
            ).toFixed(2)} kHz`);
        this.spectrumStatus.textContent =
          `FFT ${spectrum.fftLength} · peak ` +
          `${(spectrum.peakFrequencyHz / 1000).toFixed(2)} kHz` +
          (channelPeaks.length > 1
            ? ` · ${channelPeaks.join(" · ")}`
            : "");
      }

      const wigner = computeClickWigner(waveform);
      const raster = createRasterCanvas(
        wigner.columns,
        wigner.rows);
      const rasterContext = raster?.getContext?.("2d");
      if (raster && rasterContext &&
          typeof rasterContext.createImageData === "function" &&
          typeof rasterContext.putImageData === "function") {
        const image = rasterContext.createImageData(
          wigner.columns,
          wigner.rows);
        for (let time = 0;
             time < wigner.columns;
             time++) {
          for (let bin = 0; bin < wigner.rows; bin++) {
            const level = wigner.maximum > 0
              ? Math.sqrt(
                  wigner.values[time][bin] /
                  wigner.maximum)
              : 0;
            const colour = spectrogramColour(level, "HOT");
            const pixel =
              ((wigner.rows - 1 - bin) *
                wigner.columns + time) * 4;
            image.data[pixel] = colour[0];
            image.data[pixel + 1] = colour[1];
            image.data[pixel + 2] = colour[2];
            image.data[pixel + 3] = 255;
          }
        }
        rasterContext.putImageData(image, 0, 0);
        wignerContext.imageSmoothingEnabled = true;
        wignerContext.drawImage(
          raster,
          0,
          0,
          wignerSize.width,
          wignerSize.height);
      }
      if (this.wignerStatus) {
        this.wignerStatus.textContent =
          `${wigner.columns} samples · ` +
          `0–${(this.sampleRateHz / 2000).toFixed(1)} kHz`;
      }
    }

    performanceMetrics() {
      const percentile = (values, fraction) => {
        if (!values.length) return 0;
        const ordered = values.slice().sort(
          (left, right) => left - right);
        return ordered[Math.min(
          ordered.length - 1,
          Math.floor(ordered.length * fraction))];
      };
      return {
        acceptedClicks: this.acceptedClickCount,
        retainedClicks: this.clicks.length,
        renders: this.renderDurationsMs.length,
        renderDurationP50Ms:
          percentile(this.renderDurationsMs, 0.5),
        renderDurationP95Ms:
          percentile(this.renderDurationsMs, 0.95),
        renderIntervalP50Ms:
          percentile(this.renderIntervalsMs, 0.5),
        renderIntervalP95Ms:
          percentile(this.renderIntervalsMs, 0.95),
        continuousTimeline: this.continuousTimeline,
        presentationDelayMs: this.presentationDelayMs,
        presentationDepthMs:
          Number.isFinite(this.displayEndMs)
            ? Math.max(
                0,
                this.estimatedLiveTimeMs() -
                  this.displayEndMs)
            : 0
      };
    }

    recordRenderPerformance(startedAt) {
      const finishedAt = typeof performance !== "undefined"
        ? performance.now()
        : Date.now();
      this.renderDurationsMs.push(
        Math.max(0, finishedAt - startedAt));
      if (this.renderDurationsMs.length > 180) {
        this.renderDurationsMs.shift();
      }
      if (this.lastRenderStartMs > 0) {
        this.renderIntervalsMs.push(
          Math.max(0, startedAt - this.lastRenderStartMs));
        if (this.renderIntervalsMs.length > 180) {
          this.renderIntervalsMs.shift();
        }
      }
      this.lastRenderStartMs = startedAt;
    }

    render() {
      const renderStartedAt = typeof performance !== "undefined"
        ? performance.now()
        : Date.now();
      const { width, height, ratio } = resizeCanvas(this.canvas);
      const context = this.canvas.getContext("2d");
      context.fillStyle = "#06111e";
      context.fillRect(0, 0, width, height);
      if (!this.clicks.length) {
        context.fillStyle = "#8aa0af";
        context.font =
          `${Math.max(12, 12 * ratio)}px Cascadia Mono, Consolas, monospace`;
        context.fillText(
          this.running
            ? "Waiting for Click Detector output"
            : "Start processing to receive clicks",
          16 * ratio,
          28 * ratio);
        this.recordRenderPerformance(renderStartedAt);
        return;
      }
      const windowSeconds = Math.max(
        1,
        Number(this.settings.timeWindowSeconds) || 20);
      const endMs =
        this.continuousTimeline &&
        Number.isFinite(this.displayEndMs)
          ? this.displayEndMs
          : this.lastTimeMs;
      const startMs = endMs - windowSeconds * 1000;
      const visible = this.clicks.filter(
        (click) => click.timeMs >= startMs && click.timeMs <= endMs);
      const panels = [
        {
          label: "Bearing",
          range: orderedRange(
            this.settings.bearingLimitsDegrees,
            [0, 180]),
          value: (click) => click.bearingDegrees,
          format: (value) => `${value.toFixed(0)}°`
        },
        {
          label: "Excess",
          range: orderedRange(
            this.settings.amplitudeLimitsDb,
            [0, 30]),
          value: (click) => click.signalExcessDb,
          format: (value) => `${value.toFixed(0)} dB`
        },
        {
          label: "ICI",
          range: orderedRange(
            this.settings.iciLimitsSeconds,
            [0.001, 3],
            { positive: true }).map((value) => Math.log10(value)),
          value: (click) => click.iciSeconds,
          logarithmic: true,
          format: (value) => value >= 1
            ? `${value.toFixed(1)} s`
            : `${(value * 1000).toFixed(0)} ms`
        }
      ];
      this.hitPoints = [];
      const panelHeight = height / panels.length;
      panels.forEach((panel, index) => this.drawPanel(
        context,
        {
          left: 0,
          top: index * panelHeight,
          width,
          height: panelHeight,
          ratio
        },
        panel.label,
        panel.range,
        panel.value,
        visible,
        [startMs, endMs],
        panel));
      const bearingCount = visible.filter(
        (click) => Number.isFinite(click.bearingDegrees)).length;
      this.setStatus(
        `Live · ${visible.length} clicks / ${windowSeconds.toFixed(0)} s · ` +
        `${bearingCount} bearings · ${this.clicks.length} retained` +
        (this.continuousTimeline
          ? ` · ${(this.presentationDelayMs / 1000).toFixed(1)} s delay`
          : "") +
        (this.selected
          ? ` · selected sample ${this.selected.startSample}`
          : ""));
      this.recordRenderPerformance(renderStartedAt);
    }

    selectNearest(event) {
      if (!this.hitPoints.length) return;
      const bounds = this.canvas.getBoundingClientRect();
      const ratio = Math.max(1, window.devicePixelRatio || 1);
      const x = (event.clientX - bounds.left) * ratio;
      const y = (event.clientY - bounds.top) * ratio;
      let nearest = null;
      let distance = 14 * ratio;
      for (const hit of this.hitPoints) {
        const candidate = Math.hypot(hit.x - x, hit.y - y);
        if (candidate < distance) {
          nearest = hit.click;
          distance = candidate;
        }
      }
      if (nearest) {
        this.selected = nearest;
        if (event.shiftKey) {
          if (this.markedKeys.has(nearest.key)) {
            this.markedKeys.delete(nearest.key);
          }
          else {
            this.markedKeys.add(nearest.key);
          }
        }
        else {
          this.markedKeys.clear();
          this.markedKeys.add(nearest.key);
        }
        this.renderSelectedClick();
        this.updateTrackedControls();
        this.scheduleRender();
      }
    }

    showTrackedClickMenu(event) {
      if (!this.controlsRoot || !this.clickDetectorUnitId) return;
      event.preventDefault();
      this.selectNearest(event);
      const click = this.selected;
      if (!click) return;
      this.trackedMenu?.remove();
      const menu = displayElement("div", {
        className: "tracked-click-menu",
        attributes: {
          role: "menu",
          "data-tracked-click-menu": "true"
        }
      });
      const item = (label, callback) => {
        const button = displayElement("button", {
          text: label,
          attributes: { type: "button", role: "menuitem" }
        });
        button.addEventListener("click", () => {
          menu.remove();
          this.trackedMenu = null;
          void Promise.resolve(callback()).catch((error) =>
            this.reportTrackedError(error));
        });
        menu.append(button);
      };
      const membership = this.eventByUid.get(click.uid);
      if (membership) {
        item(
          `Remove Click from Train Id ${membership}`,
          () => this.removeMarked());
      }
      item("New Click Train", () => this.assignMarked(null));
      for (const trackedEvent of this.trackedEvents) {
        if (Number(trackedEvent.eventId) === membership) continue;
        item(
          `Click Train ${trackedEvent.eventId}`,
          () => this.assignMarked(Number(trackedEvent.eventId)));
      }
      menu.style.left = `${event.offsetX ?? 0}px`;
      menu.style.top = `${event.offsetY ?? 0}px`;
      this.controlsRoot.parentElement?.append(menu);
      this.trackedMenu = menu;
    }

    dispose() {
      if (this.disposed) return;
      this.disposed = true;
      for (const controller of this.controllers) controller.abort();
      this.controllers = [];
      this.trackedMenu?.remove();
      this.trackedMenu = null;
      if (this.animationFrame !== null &&
          typeof cancelAnimationFrame === "function") {
        cancelAnimationFrame(this.animationFrame);
        this.animationFrame = null;
      }
      this.resizeObserver?.disconnect();
      this.resizeObserver = null;
      delete this.canvas.__pamguardClickMetrics;
    }
  }

  class ProjectLevelMeter {
    constructor(options) {
      this.container = options.container;
      this.status = options.status;
      this.settings = options.settings || {};
      this.sourceBlockId = options.sourceBlockId || "";
      this.api = options.api;
      this.headers = options.headers || (() => ({}));
      this.onError = options.onError || (() => {});
      this.running = Boolean(options.running);
      this.blockMetadata = null;
      this.latest = null;
      this.disposed = false;
      this.abortController = null;
      this.render();
      if (this.running && this.sourceBlockId) {
        void this.connect();
      }
      else {
        this.setStatus(
          this.sourceBlockId
            ? "Start processing to receive levels"
            : "Level source unavailable");
      }
    }

    setStatus(message) {
      if (!this.disposed && this.status) {
        this.status.textContent = message;
      }
    }

    async loadMetadata(signal) {
      const response = await fetch(this.api("/data-blocks"), {
        headers: this.headers(),
        signal
      });
      if (!response.ok) {
        throw new Error(
          `Level source metadata unavailable (${response.status})`);
      }
      const catalogue = await response.json();
      this.blockMetadata = catalogue.dataBlocks?.find(
        (block) => block.id === this.sourceBlockId) || null;
    }

    async connect() {
      const controller = new AbortController();
      this.abortController = controller;
      this.setStatus("Connecting to level stream\u2026");
      try {
        await this.loadMetadata(controller.signal);
        const query = new URLSearchParams({
          format: "ndjson",
          history: "1"
        });
        const response = await fetch(
          this.api(
            `/data-blocks/${encodeURIComponent(this.sourceBlockId)}` +
            `/stream?${query}`),
          {
            headers: this.headers(),
            signal: controller.signal
          });
        if (!response.ok || !response.body) {
          throw new Error(
            `Level stream unavailable (${response.status})`);
        }
        const reader = response.body.getReader();
        const decoder = new TextDecoder();
        let buffered = "";
        while (!controller.signal.aborted) {
          const { done, value } = await reader.read();
          if (done) break;
          buffered += decoder.decode(value, { stream: true });
          let newline;
          while ((newline = buffered.indexOf("\n")) >= 0) {
            const line = buffered.slice(0, newline).trim();
            buffered = buffered.slice(newline + 1);
            if (line) this.accept(JSON.parse(line));
          }
        }
        if (!controller.signal.aborted) {
          this.setStatus("Level stream ended");
        }
      }
      catch (error) {
        if (controller.signal.aborted || this.disposed) return;
        this.setStatus(error?.message || String(error));
        this.onError(error);
      }
    }

    accept(unit) {
      const payload = unit?.payload || {};
      if (!Array.isArray(payload.rmsDbfs) ||
          !Array.isArray(payload.peakDbfs)) {
        return;
      }
      this.latest = {
        rmsDbfs: payload.rmsDbfs.map(Number),
        peakDbfs: payload.peakDbfs.map(Number),
        measuredFrames: Number(payload.measuredFrames) || 0
      };
      this.render();
    }

    scale() {
      const minLevel = Number.isInteger(Number(this.settings.minLevel)) &&
        Number(this.settings.minLevel) < 0
        ? Number(this.settings.minLevel)
        : -80;
      const reference = Number(this.settings.scaleReference) || 0;
      const scaleType = Number(this.settings.scaleType) === 1
        ? "RMS"
        : "peak";
      const calibration =
        this.blockMetadata?.calibrationDbOffsetByChannel || [];
      if (reference === 1) {
        const voltsPeakToPeak =
          Number(this.blockMetadata?.voltsPeakToPeak);
        if (!Number.isFinite(voltsPeakToPeak) ||
            voltsPeakToPeak <= 0) {
          return {
            available: false,
            message:
              "Volt reference unavailable: source voltage metadata is " +
              "not declared."
          };
        }
        const offset = 20 * Math.log10(voltsPeakToPeak / 2);
        const maximum =
          Math.ceil(20 * Math.log10(Math.ceil(voltsPeakToPeak / 2)));
        return {
          available: Number.isFinite(maximum),
          minimum: maximum + minLevel,
          maximum,
          offsets: [],
          commonOffset: offset,
          label: `dB re. 1V ${scaleType}`
        };
      }
      if (reference === 2) {
        if (!Array.isArray(calibration) || !calibration.length ||
            calibration.some((value) =>
              !Number.isFinite(Number(value)))) {
          return {
            available: false,
            message:
              "Micropascal reference unavailable: this source is not " +
              "calibrated."
          };
        }
        const offsets = calibration.map(Number);
        const maximum = Math.ceil(Math.max(...offsets));
        return {
          available: true,
          minimum: maximum + minLevel,
          maximum,
          offsets,
          commonOffset: 0,
          label: `dB re. 1\u00b5Pa ${scaleType}`
        };
      }
      return {
        available: true,
        minimum: minLevel,
        maximum: 0,
        offsets: [],
        commonOffset: 0,
        label: `dB re. FS ${scaleType}`
      };
    }

    render() {
      this.container.replaceChildren();
      if (!this.latest) {
        this.container.append(displayElement("p", {
          className: "level-meter-empty",
          text: this.running
            ? "Waiting for raw-audio levels"
            : "Start processing to receive levels"
        }));
        return;
      }
      const scale = this.scale();
      if (!scale.available) {
        this.container.append(displayElement("p", {
          className: "level-meter-empty",
          text: scale.message
        }));
        this.setStatus(scale.message);
        return;
      }
      const values = Number(this.settings.scaleType) === 1
        ? this.latest.rmsDbfs
        : this.latest.peakDbfs;
      const heading = displayElement("div", {
        className: "project-level-meter-scale"
      });
      heading.append(
        displayElement("span", {
          text: scale.minimum.toFixed(0)
        }),
        displayElement("strong", { text: scale.label }),
        displayElement("span", {
          text: scale.maximum.toFixed(0)
        }));
      this.container.append(heading);
      values.forEach((rawValue, channel) => {
        const offset =
          Number(scale.offsets[channel] ?? scale.commonOffset) || 0;
        const value = rawValue + offset;
        const fraction = clamp(
          (value - scale.minimum) /
            Math.max(1e-12, scale.maximum - scale.minimum),
          0,
          1);
        const row = displayElement("div", {
          className: "project-level-meter-row",
          attributes: {
            "data-level-meter-channel": channel
          }
        });
        const label = displayElement("span", {
          className: "project-level-meter-channel",
          text: `Ch ${channel}`
        });
        const track = displayElement("div", {
          className: "project-level-meter-track"
        });
        const fill = displayElement("div", {
          className:
            "project-level-meter-fill " +
            (value >= scale.maximum - 10 ? "is-high" : "")
        });
        fill.style.width = `${(fraction * 100).toFixed(3)}%`;
        track.append(fill);
        row.append(
          label,
          track,
          displayElement("output", {
            text: `${value.toFixed(1)} dB`
          }));
        this.container.append(row);
      });
      this.setStatus(
        `Live \u00b7 ${values.length} channel${
          values.length === 1 ? "" : "s"} \u00b7 ` +
        `${this.latest.measuredFrames} measured frames`);
    }

    dispose() {
      if (this.disposed) return;
      this.disposed = true;
      this.abortController?.abort();
      this.abortController = null;
    }
  }

  function mountSpectrogram(options) {
    return new ProjectSpectrogram(options);
  }

  function mountClickDisplay(options) {
    return new ProjectClickDisplay(options);
  }

  function mountLevelMeter(options) {
    return new ProjectLevelMeter(options);
  }

  globalThis.PamguardProjectDisplays = Object.freeze({
    mountSpectrogram,
    mountClickDisplay,
    mountLevelMeter
  });
})();
