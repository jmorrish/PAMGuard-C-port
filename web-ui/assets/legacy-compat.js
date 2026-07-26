    const {
      byId: $,
      formatNumber,
      resizeCanvas,
      heatRgb,
      heatColor
    } = globalThis.PamguardPlatform.dom;
    const {
      api,
      requestJson,
      requestText,
      dispose: disposeHttpClient
    } = globalThis.PamguardPlatform.http.createHttpClient({
      getBaseUrl: () => $("apiBase").value,
      getApiKey: () => $("apiKey").value
    });
    let nextStartSample = 0;
    let nextArchiveCursor = null;

    function log(value) {
      $("log").textContent = typeof value === "string" ? value : JSON.stringify(value, null, 2);
    }

    function contourPointCount(region) {
      return Array.isArray(region.contourPoints) ? region.contourPoints.length : 0;
    }

    function renderContourSummaries(regions) {
      const contours = Array.isArray(regions) ? regions : [];
      const list = $("contourSummaryList");
      list.textContent = "";
      $("contourMeta").textContent = contours.length
        ? `${contours.length} contour${contours.length === 1 ? "" : "s"}`
        : "No contours yet";

      if (!contours.length) {
        const empty = document.createElement("div");
        empty.className = "contour-card";
        const title = document.createElement("strong");
        title.textContent = "Waiting for contours";
        const hint = document.createElement("span");
        hint.textContent = "Enable whistle regions, send PCM, or flush a session to inspect contour timing and frequency summaries.";
        empty.append(title, hint);
        list.append(empty);
        return;
      }

      contours
        .slice()
        .sort((left, right) => {
          const duration = (right.durationSamples || 0) - (left.durationSamples || 0);
          return duration !== 0 ? duration : contourPointCount(right) - contourPointCount(left);
        })
        .slice(0, 6)
        .forEach((region) => {
          const card = document.createElement("div");
          card.className = "contour-card";
          const title = document.createElement("strong");
          title.textContent = `Contour ${region.regionNumber ?? "?"}, ch ${region.channel ?? "?"}`;
          const duration = document.createElement("span");
          duration.textContent = `duration ${formatNumber((region.durationSeconds || 0) * 1000, 2)} ms, ${region.durationSamples ?? "n/a"} samples`;
          const span = document.createElement("span");
          span.textContent = `span ${formatNumber((region.timeSpanSeconds || 0) * 1000, 2)} ms, start ${region.startSample ?? "n/a"} to ${region.lastStartSample ?? "n/a"}`;
          const frequency = document.createElement("span");
          frequency.textContent = region.minFrequencyHz !== undefined && region.maxFrequencyHz !== undefined
            ? `frequency ${formatNumber(region.minFrequencyHz, 1)}-${formatNumber(region.maxFrequencyHz, 1)} Hz`
            : `frequency bins ${region.minFrequencyBin ?? "n/a"}-${region.maxFrequencyBin ?? "n/a"}`;
          const peak = document.createElement("span");
          peak.textContent = region.meanPeakHz !== undefined
            ? `mean peak ${formatNumber(region.meanPeakHz, 1)} Hz`
            : `mean peak bin ${formatNumber(region.meanPeakBin, 2)}`;
          const sweep = document.createElement("span");
          sweep.textContent = region.peakSweepRateHzPerSecond !== undefined
            ? `sweep ${formatNumber(region.peakSweepRateHzPerSecond, 1)} Hz/s`
            : `sweep ${formatNumber(region.peakSweepRateBinsPerSecond, 1)} bins/s`;
          const points = document.createElement("span");
          points.textContent = `${contourPointCount(region)} contour point${contourPointCount(region) === 1 ? "" : "s"}, ${region.totalPixels ?? "n/a"} pixels`;
          card.append(title, duration, span, frequency, peak, sweep, points);
          list.append(card);
        });
    }

    function renderMonitoring(result) {
      const list = $("monitorList");
      const meta = $("monitorMeta");
      const noiseBands = Array.isArray(result.noiseBands) ? result.noiseBands : [];
      const fftNoise = Array.isArray(result.fftNoise) ? result.fftNoise : [];
      const ltsa = Array.isArray(result.ltsa) ? result.ltsa : [];
      const detectionSources = [
        ["Energy sum", result.ishmaelDetections],
        ["Sgram corr", result.sgramCorrDetections],
        ["Match filt", result.matchFiltDetections],
      ];
      const mtResults = Array.isArray(result.matchedTemplateClassifications)
        ? result.matchedTemplateClassifications : [];
      const cards = [];

      noiseBands.slice(-2).forEach((entry) => {
        const card = document.createElement("div");
        card.className = "contour-card";
        const title = document.createElement("strong");
        title.textContent = `Noise bands ch ${entry.channel} @ ${entry.endSample ?? "n/a"}`;
        const levels = document.createElement("span");
        const rms = Array.isArray(entry.rmsDb) ? entry.rmsDb : [];
        levels.textContent = rms.length
          ? `rms dB: ${rms.map((v) => formatNumber(v, 1)).join(", ")}`
          : "no bands";
        card.append(title, levels);
        cards.push(card);
      });

      fftNoise.slice(-2).forEach((entry) => {
        const card = document.createElement("div");
        card.className = "contour-card";
        const title = document.createElement("strong");
        title.textContent = `FFT noise ch ${entry.channel} @ ${entry.endSample ?? "n/a"}`;
        const levels = document.createElement("span");
        const bands = Array.isArray(entry.bands) ? entry.bands : [];
        levels.textContent = bands.map((band) =>
          `${band.name || "band"}: mean ${formatNumber(band.meanDb, 1)}, median ${formatNumber(band.medianDb, 1)} dB`
        ).join("; ") || "no bands";
        card.append(title, levels);
        cards.push(card);
      });

      ltsa.slice(-2).forEach((entry) => {
        const card = document.createElement("div");
        card.className = "contour-card";
        const title = document.createElement("strong");
        title.textContent = `LTSA ch ${entry.channel} [${entry.startTimeMs}..${entry.endTimeMs}) ms`;
        const info = document.createElement("span");
        const mags = Array.isArray(entry.magnitude) ? entry.magnitude : [];
        let peakBin = 0;
        let peakVal = -Infinity;
        mags.forEach((v, i) => { if (v > peakVal) { peakVal = v; peakBin = i; } });
        info.textContent = `${entry.nFft ?? 0} slices, ${mags.length} bins, peak bin ${peakBin} (${formatNumber(peakVal, 3)})`;
        card.append(title, info);
        cards.push(card);
      });

      detectionSources.forEach(([label, detections]) => {
        (Array.isArray(detections) ? detections : []).slice(-3).forEach((det) => {
          const card = document.createElement("div");
          card.className = "contour-card";
          const title = document.createElement("strong");
          title.textContent = `${label} detection ch ${det.channel}`;
          const info = document.createElement("span");
          info.textContent = `start ${det.startSample}, ${det.durationSamples} samples, peak ${formatNumber(det.peakHeight, 3)} @ ${det.peakTimeSample}`;
          const band = document.createElement("span");
          band.textContent = `band ${formatNumber(det.lowFreqHz, 0)}-${formatNumber(det.highFreqHz, 0)} Hz`;
          card.append(title, info, band);
          cards.push(card);
        });
      });

      mtResults.slice(-3).forEach((entry) => {
        const card = document.createElement("div");
        card.className = "contour-card";
        const title = document.createElement("strong");
        title.textContent = `Matched template click ${entry.clickIndex}: ${entry.classified ? "CLASSIFIED" : "not classified"}`;
        const info = document.createElement("span");
        const results = Array.isArray(entry.results) ? entry.results : [];
        info.textContent = results
          .map((r, i) => `#${i} thr ${formatNumber(r.threshold, 3)} (match ${formatNumber(r.matchCorr, 3)})`)
          .join("; ") || "no template results";
        card.append(title, info);
        cards.push(card);
      });

      if (!cards.length) {
        meta.textContent = "No monitoring output in the latest result";
        return;
      }
      meta.textContent = `${noiseBands.length} filter-bank noise, ${fftNoise.length} FFT noise, ${ltsa.length} LTSA, ` +
        detectionSources.map(([label, d]) => `${Array.isArray(d) ? d.length : 0} ${label.toLowerCase()}`).join(", ") +
        `, ${mtResults.length} MT`;
      list.replaceChildren(...cards);
    }

    function updateMetrics(result, ingestClickData = true) {
      if (ingestClickData) {
        ingestClicks(result);
      }
      $("mFrames").textContent = result.spectrogramFrames ?? 0;
      $("mClicks").textContent = totalClicksSeen;
      $("mFeatures").textContent = Array.isArray(result.clickFeatures) ? result.clickFeatures.length : 0;
      $("mClasses").textContent = Array.isArray(result.clickClassifications) ? result.clickClassifications.length : 0;
      $("mTrains").textContent = Array.isArray(result.clickTrains) ? result.clickTrains.length : 0;
      $("mTrainLocs").textContent = Array.isArray(result.clickTrainLocalisations) ? result.clickTrainLocalisations.length : 0;
      $("mTrainBearings").textContent = Array.isArray(result.clickTrainBearings) ? result.clickTrainBearings.length : 0;
      $("mLocs").textContent = Array.isArray(result.clickLocalisations) ? result.clickLocalisations.length : 0;
      $("mBearings").textContent = Array.isArray(result.clickBearings) ? result.clickBearings.length : 0;
      $("mPeaks").textContent = Array.isArray(result.whistlePeaks) ? result.whistlePeaks.length : 0;
      $("mRegions").textContent = Array.isArray(result.whistleRegions) ? result.whistleRegions.length : 0;
      $("mNoiseBands").textContent = Array.isArray(result.noiseBands) ? result.noiseBands.length : 0;
      $("mFftNoise").textContent = Array.isArray(result.fftNoise) ? result.fftNoise.length : 0;
      $("mLtsa").textContent = Array.isArray(result.ltsa) ? result.ltsa.length : 0;
      $("mIshmael").textContent = Array.isArray(result.ishmaelDetections) ? result.ishmaelDetections.length : 0;
      $("mSgramCorr").textContent = Array.isArray(result.sgramCorrDetections) ? result.sgramCorrDetections.length : 0;
      $("mMatchFilt").textContent = Array.isArray(result.matchFiltDetections) ? result.matchFiltDetections.length : 0;
      $("mMatchedTemplate").textContent = Array.isArray(result.matchedTemplateClassifications)
        ? result.matchedTemplateClassifications.filter((entry) => entry.classified).length : 0;
      $("mContinuity").textContent = result.sampleContinuity || "n/a";
      renderMonitoring(result);
      renderContourSummaries(result.whistleRegions || []);
      // In live mode the scrolling waterfall owns the canvas; otherwise a
      // one-shot result draws its own frames (and preview-less results keep
      // the last image rather than blanking).
      if (!liveViewActive && Array.isArray(result.spectrogram)) {
        drawSpectrogram(result.spectrogram, result.whistleRegions || []);
      }
    }

    function drawSpectrogram(frames, regions = []) {
      const canvas = $("spectrogramCanvas");
      const ctx = canvas.getContext("2d");
      resizeCanvas(canvas);
      ctx.fillStyle = "#071514";
      ctx.fillRect(0, 0, canvas.width, canvas.height);

      if (!Array.isArray(frames) || frames.length === 0) {
        $("spectrogramMeta").textContent = "No frame data requested yet";
        ctx.fillStyle = "rgba(234, 248, 244, 0.66)";
        ctx.font = `${Math.max(13, canvas.width / 70)}px Cascadia Mono, Consolas, monospace`;
        ctx.fillText("Send a PCM block to render FFT magnitudes", 18, 34);
        return;
      }

      const channel = frames[0].channel;
      const channelFrames = frames.filter((frame) => frame.channel === channel && Array.isArray(frame.magnitudeSquared));
      const bins = Math.max(...channelFrames.map((frame) => frame.magnitudeSquared.length));
      if (!channelFrames.length || !Number.isFinite(bins) || bins <= 0) {
        $("spectrogramMeta").textContent = "No magnitude bins";
        return;
      }

      let maxDb = -Infinity;
      const dbFrames = channelFrames.map((frame) => frame.magnitudeSquared.map((value) => {
        const db = 10 * Math.log10(Math.max(value, 1e-18));
        maxDb = Math.max(maxDb, db);
        return db;
      }));
      const minDb = maxDb - 72;
      const columnWidth = canvas.width / channelFrames.length;
      const rowHeight = canvas.height / bins;

      dbFrames.forEach((frameBins, x) => {
        frameBins.forEach((db, y) => {
          const unit = (db - minDb) / (maxDb - minDb || 1);
          ctx.fillStyle = heatColor(unit);
          ctx.fillRect(
            Math.floor(x * columnWidth),
            Math.floor(canvas.height - (y + 1) * rowHeight),
            Math.ceil(columnWidth),
            Math.ceil(rowHeight)
          );
        });
      });

      ctx.save();
      ctx.lineWidth = Math.max(2, canvas.width / 450);
      ctx.strokeStyle = "rgba(255, 250, 237, 0.95)";
      ctx.fillStyle = "rgba(11, 79, 74, 0.95)";
      const sliceToX = new Map();
      channelFrames.forEach((frame, index) => sliceToX.set(frame.slice, index));
      regions
        .filter((region) => region.channel === channel && Array.isArray(region.contourPoints))
        .forEach((region) => {
          let started = false;
          ctx.beginPath();
          region.contourPoints.forEach((point) => {
            if (!sliceToX.has(point.slice)) {
              return;
            }
            const x = (sliceToX.get(point.slice) + 0.5) * columnWidth;
            const yBin = point.peakBin / (channelFrames[0].binStride || 1);
            if (yBin < 0 || yBin >= bins) {
              return;
            }
            const y = canvas.height - (yBin + 0.5) * rowHeight;
            if (!started) {
              ctx.moveTo(x, y);
              started = true;
            } else {
              ctx.lineTo(x, y);
            }
            ctx.fillRect(x - 2, y - 2, 4, 4);
          });
          if (started) {
            ctx.stroke();
          }
        });
      ctx.restore();

      $("spectrogramMeta").textContent = `channel ${channel}, ${channelFrames.length} frames, ${bins} bins`;
    }

    function linearHydrophones(channels, spacingM) {
      return Array.from({ length: channels }, (_, i) => ({
        channel: i,
        xM: i * spacingM,
        yM: 0,
        zM: 0,
        sensitivityDb: 0
      }));
    }

    function arrayGeometry(channels, spacingM) {
      const speedOfSoundMps = Number($("speedOfSoundMps").value);
      const rawGeometry = $("arrayGeometryJson").value.trim();
      if (!rawGeometry) {
        return {
          id: "browser-linear-array",
          speedOfSoundMps,
          hydrophones: linearHydrophones(channels, spacingM)
        };
      }

      const parsed = JSON.parse(rawGeometry);
      if (Array.isArray(parsed)) {
        return {
          id: "browser-custom-array",
          speedOfSoundMps,
          hydrophones: parsed
        };
      }
      if (!parsed || !Array.isArray(parsed.hydrophones)) {
        throw new Error("Advanced hydrophone JSON must be an array or an object with a hydrophones array");
      }
      return {
        id: parsed.id || "browser-custom-array",
        speedOfSoundMps: Number(parsed.speedOfSoundMps ?? speedOfSoundMps),
        hydrophones: parsed.hydrophones
      };
    }

    function clickIirFilter(prefix) {
      let arbitrary = {};
      const arbitraryText = $(`${prefix}Arbitrary`).value.trim();
      if (arbitraryText) {
        arbitrary = JSON.parse(arbitraryText);
        if (!Array.isArray(arbitrary.frequenciesHz) ||
            !Array.isArray(arbitrary.gainsDb)) {
          throw new Error(`${prefix} arbitrary FIR JSON needs frequenciesHz and gainsDb arrays`);
        }
      }
      return {
        type: $(`${prefix}Type`).value,
        band: $(`${prefix}Band`).value,
        order: Number($(`${prefix}Order`).value),
        highPassFreq: Number($(`${prefix}HighPassFreq`).value),
        lowPassFreq: Number($(`${prefix}LowPassFreq`).value),
        passBandRipple: Number($(`${prefix}Ripple`).value),
        stopBandRipple: Number($(`${prefix}StopRipple`).value),
        chebyGamma: Number($(`${prefix}ChebyGamma`).value),
        arbitraryFrequenciesHz: arbitrary.frequenciesHz || [],
        arbitraryGainsDb: arbitrary.gainsDb || []
      };
    }

    function sessionBody() {
      const channels = Number($("channels").value);
      const spacingM = Number($("hydrophoneSpacingM").value);
      const defaultChannelBitmap = Math.pow(2, channels) - 1;
      const clickChannelBitmap = $("clickChannelBitmap").value ? Number($("clickChannelBitmap").value) : defaultChannelBitmap;
      const clickTriggerBitmap = $("clickTriggerBitmap").value ? Number($("clickTriggerBitmap").value) : clickChannelBitmap;
      const clickGroupingType = $("clickGroupingType").value;
      let clickChannelGroups = clickGroupingType === "singles"
        ? Array.from({ length: channels }, (_, channel) => channel)
        : Array(channels).fill(0);
      if (clickGroupingType === "user") {
        clickChannelGroups = $("clickChannelGroups").value
          .split(",")
          .map((value) => Number(value.trim()));
        if (clickChannelGroups.length !== channels ||
            clickChannelGroups.some((value) => !Number.isInteger(value) || value < 0 || value > 31)) {
          throw new Error(`User channel groups must contain ${channels} comma-separated integers from 0 to 31`);
        }
      }
      const whistleChannelBitmap = $("whistleChannelBitmap").value
        ? Number($("whistleChannelBitmap").value)
        : defaultChannelBitmap;
      const whistleGroupingType = $("whistleGroupingType").value;
      let whistleChannelGroups = whistleGroupingType === "singles"
        ? Array.from({ length: channels }, (_, channel) => channel)
        : Array(channels).fill(0);
      if (whistleGroupingType === "user") {
        whistleChannelGroups = $("whistleChannelGroups").value
          .split(",")
          .map((value) => Number(value.trim()));
        if (whistleChannelGroups.length !== channels ||
            whistleChannelGroups.some((value) =>
              !Number.isInteger(value) || value < 0 || value > 31)) {
          throw new Error(`Whistle channel groups must contain ${channels} comma-separated integers from 0 to 31`);
        }
      }
      const clickFeatureFftLength = $("clickFeatureFftLength").value
        ? Number($("clickFeatureFftLength").value)
        : Number($("fftLength").value);
      const classifierPreset = $("classifierPreset").value;
      let standardTypes = classifierPreset === "both"
        ? ["beakedWhale", "porpoise"]
        : classifierPreset === "none" ? [] : [classifierPreset];
      const classifierTypesJson = $("classifierTypesJson").value.trim();
      if (classifierTypesJson) {
        standardTypes = JSON.parse(classifierTypesJson);
        if (!Array.isArray(standardTypes)) {
          throw new Error("Classifier type JSON override must be an array");
        }
      }
      const monitoringJson = $("monitoringJson").value.trim();
      let monitoringExtras = {};
      if (monitoringJson) {
        monitoringExtras = JSON.parse(monitoringJson);
        if (!monitoringExtras || Array.isArray(monitoringExtras) || typeof monitoringExtras !== "object") {
          throw new Error("Monitoring JSON must be an object (e.g. {\"noiseBand\":{...}})");
        }
      }
      const monitoringModuleObject = (id, label) => {
        const text = $(id).value.trim();
        const value = text ? JSON.parse(text) : {};
        if (!value || Array.isArray(value) || typeof value !== "object") {
          throw new Error(`${label} settings JSON must be an object`);
        }
        return value;
      };
      const fftNoiseBands = JSON.parse($("fftNoiseBandsJson").value);
      if (!Array.isArray(fftNoiseBands)) {
        throw new Error("FFT noise frequency bands JSON must be an array");
      }
      const monitoringModules = {
        fftNoise: {
          enabled: $("fftNoiseEnabled").value === "true",
          channelBitmap: Number($("fftNoiseChannelBitmap").value),
          measurementIntervalSeconds: Number($("fftNoiseInterval").value),
          nMeasures: Number($("fftNoiseMeasures").value),
          useAll: $("fftNoiseUseAll").value === "true",
          bands: fftNoiseBands
        },
        noiseBand: {
          enabled: $("noiseBandEnabled").value === "true",
          bandType: $("noiseBandType").value,
          minFrequencyHz: Number($("noiseBandMinFrequency").value),
          maxFrequencyHz: Number($("noiseBandMaxFrequency").value),
          referenceFrequencyHz: Number($("noiseBandReferenceFrequency").value),
          iirOrder: Number($("noiseBandIirOrder").value),
          outputIntervalSeconds: Number($("noiseBandOutputInterval").value)
        },
        ltsa: {
          enabled: $("ltsaEnabled").value === "true",
          intervalSeconds: Number($("ltsaInterval").value)
        },
        ishmael: {
          ...monitoringModuleObject("ishmaelJson", "Ishmael energy"),
          enabled: $("ishmaelEnabled").value === "true"
        },
        sgramCorr: {
          ...monitoringModuleObject("sgramCorrJson", "Spectrogram correlation"),
          enabled: $("sgramCorrEnabled").value === "true"
        },
        matchFilt: {
          ...monitoringModuleObject("matchFiltJson", "Matched filter"),
          enabled: $("matchFiltEnabled").value === "true"
        },
        matchedTemplate: {
          ...monitoringModuleObject("matchedTemplateJson", "Matched template"),
          enabled: $("matchedTemplateEnabled").value === "true"
        }
      };
      const sweepPreset = $("sweepPreset").value;
      const sweepStandardTypes = sweepPreset === "both"
        ? ["beakedWhale", "porpoise"]
        : sweepPreset === "none" ? [] : [sweepPreset];
      let sweepTypes = [];
      const sweepTypesJson = $("sweepTypesJson").value.trim();
      if (sweepTypesJson) {
        sweepTypes = JSON.parse(sweepTypesJson);
        if (!Array.isArray(sweepTypes)) {
          throw new Error("Sweep classifier type JSON must be an array");
        }
      }
      const classifierAlgorithm = $("classifierAlgorithm").value;
      const delayTypeSettingsJson = $("delayTypeSettingsJson").value.trim();
      let delayTypeSettings = [];
      if (delayTypeSettingsJson) {
        delayTypeSettings = JSON.parse(delayTypeSettingsJson);
        if (!Array.isArray(delayTypeSettings)) {
          throw new Error("Per-click-type delay overrides must be a JSON array");
        }
      }
      const angleVetoesJson = $("angleVetoesJson").value.trim();
      let angleVetoes = [];
      if (angleVetoesJson) {
        angleVetoes = JSON.parse(angleVetoesJson);
        if (!Array.isArray(angleVetoes)) {
          throw new Error("Detector angle vetoes must be a JSON array");
        }
      }
      const trainMhtJson = $("trainMhtJson").value.trim();
      const trainMht = trainMhtJson ? JSON.parse(trainMhtJson) : {};
      if (!trainMht || Array.isArray(trainMht) || typeof trainMht !== "object") {
        throw new Error("MHT settings JSON must be an object");
      }
      const trainClassifierJson = $("trainClassifierJson").value.trim();
      const trainClassifier = trainClassifierJson
        ? JSON.parse(trainClassifierJson)
        : {};
      if (!trainClassifier || Array.isArray(trainClassifier) ||
          typeof trainClassifier !== "object") {
        throw new Error("Train classifier settings JSON must be an object");
      }
      return {
        ...monitoringExtras,
        ...monitoringModules,
        sessionId: $("sessionId").value,
        sourceId: "browser-synthetic",
        ownerId: $("ownerId").value.trim() || undefined,
        tenantId: $("tenantId").value.trim() || undefined,
        sampleRateHz: Number($("sampleRate").value),
        channelCount: channels,
        fft: {
          length: Number($("fftLength").value),
          hop: Number($("fftHop").value),
          windowType: $("fftWindowType").value,
          channels: Array.from({ length: channels }, (_, i) => i)
        },
        array: arrayGeometry(channels, spacingM),
        click: {
          enabled: $("clickEnabled").value === "true",
          localisation: $("clickLocalisationEnabled").value === "true",
          channelBitmap: clickChannelBitmap,
          triggerBitmap: clickTriggerBitmap,
          groupingType: clickGroupingType,
          channelGroups: clickChannelGroups,
          thresholdDb: Number($("clickThresholdDb").value),
          minTriggerChannels: Number($("minTriggerChannels").value),
          shortFilter: Number($("shortFilter").value),
          longFilter: Number($("longFilter").value),
          longFilter2: Number($("longFilter2").value),
          preSample: Number($("preSample").value),
          postSample: Number($("postSample").value),
          minSep: Number($("minSep").value),
          maxLength: Number($("maxLength").value),
          angleVetoes,
          preFilter: clickIirFilter("preFilter"),
          triggerFilter: clickIirFilter("triggerFilter"),
          delayMeasurement: {
            filterBearings: $("delayFilterBearings").value === "true",
            filter: {
              band: $("delayFilterBand").value,
              highPassFreq: Number($("delayHighPassFreq").value),
              lowPassFreq: Number($("delayLowPassFreq").value)
            },
            envelopeBearings: $("delayEnvelopeBearings").value === "true",
            useLeadingEdge: $("delayUseLeadingEdge").value === "true",
            upSample: Number($("delayUpSample").value),
            useRestrictedBins: $("delayUseRestrictedBins").value === "true",
            restrictedBins: Number($("delayRestrictedBins").value),
            typeSettings: delayTypeSettings
          },
          publishTriggerFunction: $("publishTriggerFunction").value === "true",
          noise: {
            sampleWaveforms: $("sampleClickNoise").value === "true",
            waveformIntervalSeconds: Number($("clickNoiseIntervalSeconds").value),
            storeBackground: $("storeClickBackground").value === "true",
            backgroundIntervalMilliseconds:
              Number($("clickBackgroundIntervalSeconds").value) * 1000
          },
          echo: {
            runOnline: $("echoRunOnline").value === "true",
            discardEchoes: $("echoDiscard").value === "true",
            maxIntervalSeconds: Number($("echoMaxIntervalSeconds").value)
          },
          featuresEnabled: $("featuresEnabled").value === "true",
          features: {
            fftLength: clickFeatureFftLength,
            lengthEnergyFraction: Number($("lengthEnergyFraction").value),
            widthEnergyFraction: Number($("widthEnergyFraction").value),
            energyBandsHz: [
              [Number($("energyBand1Low").value), Number($("energyBand1High").value)],
              [Number($("energyBand2Low").value), Number($("energyBand2High").value)]
            ],
            peakFrequencySearchHz: [Number($("peakSearchLow").value), Number($("peakSearchHigh").value)],
            meanFrequencyRangeHz: [Number($("meanRangeLow").value), Number($("meanRangeHigh").value)]
          },
          classifier: {
            type: classifierAlgorithm,
            runOnline: $("classifyOnline").value === "true",
            discardUnclassifiedClicks: $("discardUnclassifiedClicks").value === "true",
            basic: {
              enabled: classifierAlgorithm === "basic" && standardTypes.length > 0,
              standardTypes
            },
            sweep: {
              enabled: classifierAlgorithm === "sweep" &&
                (sweepStandardTypes.length > 0 || sweepTypes.length > 0),
              checkAllClassifiers: $("sweepCheckAll").value === "true",
              standardTypes: sweepStandardTypes,
              types: sweepTypes
            }
          },
          train: {
            enabled: $("trainEnabled").value === "true",
            algorithm: $("trainAlgorithm").value,
            maxIciSeconds: Number($("maxIciSeconds").value),
            minClicks: Number($("trainMinClicks").value),
            mht: trainMht,
            classifier: {
              ...trainClassifier,
              enabled: $("trainClassifierEnabled").value === "true"
            }
          }
        },
        whistle: {
          enabled: $("whistlePeakEnabled").value === "true",
          regionEnabled: $("whistleRegionEnabled").value === "true",
          channelBitmap: whistleChannelBitmap,
          groupingType: whistleGroupingType,
          channelGroups: whistleChannelGroups,
          detectionThresholdDb: Number($("whistleThresholdDb").value),
          peakTimeConstant0: Number($("peakTimeConstant0").value),
          peakTimeConstant1: Number($("peakTimeConstant1").value),
          maxPercentOverThreshold: Number($("whistleMaxPercent").value),
          minPeakWidth: Number($("minPeakWidth").value),
          maxPeakWidth: Number($("maxPeakWidth").value),
          searchBin0: Number($("searchBin0").value),
          searchBin1: Number($("searchBin1").value),
          warmupSlices: Number($("warmupSlices").value),
          minFrequencyHz: Number($("whistleMinFrequencyHz").value),
          maxFrequencyHz: Number($("whistleMaxFrequencyHz").value),
          backgroundIntervalSeconds:
            Number($("whistleBackgroundIntervalSeconds").value),
          minPixels: Number($("whistleMinPixels").value),
          minLength: Number($("whistleMinLength").value),
          connectType: Number($("connectType").value),
          keepShapeStubs: $("keepShapeStubs").value === "true",
          fragmentationMethod: Number($("fragmentationMethod").value),
          maxCrossLength: Number($("maxCrossLength").value),
          rejectFirstQuarterSecond: $("rejectFirstQuarterSecond").value === "true",
          noise: {
            medianFilter: $("whistleNoiseMedian").value === "true",
            medianFilterLength: Number($("whistleNoiseMedianLength").value),
            averageSubtraction: $("whistleNoiseAverage").value === "true",
            updateConstant: Number($("whistleNoiseUpdateConstant").value),
            kernelSmoothing: $("whistleNoiseKernel").value === "true",
            threshold: $("whistleNoiseThreshold").value === "true",
            thresholdDb: Number($("whistleNoiseThresholdDb").value),
            finalOutput: Number($("whistleNoiseFinalOutput").value)
          }
        }
      };
    }

    function syntheticPcm() {
      const sampleRate = Number($("sampleRate").value);
      const channels = Number($("channels").value);
      const frames = Number($("chunkFrames").value);
      const toneHz = Number($("toneHz").value);
      const pcm = new Float32Array(frames * channels);
      for (let frame = 0; frame < frames; frame++) {
        const tone = Math.sin((2 * Math.PI * toneHz * frame) / sampleRate) * 0.22;
        const click = frame >= 800 && frame < 810 ? (frame % 2 === 0 ? 0.95 : -0.95) : 0;
        for (let channel = 0; channel < channels; channel++) {
          const scale = channel === 0 ? 1 : 0.84;
          pcm[frame * channels + channel] = tone * scale + click * scale;
        }
      }
      return pcm;
    }

    function downloadText(filename, text, contentType) {
      const blob = new Blob([text], { type: contentType });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = filename;
      document.body.append(link);
      link.click();
      link.remove();
      URL.revokeObjectURL(url);
    }

    function archiveEventParams({ cursor = null } = {}) {
      const params = new URLSearchParams();
      const type = $("archiveEventType").value;
      if (type) {
        params.set("type", type);
      }
      if ($("ownerId").value.trim()) {
        params.set("ownerId", $("ownerId").value.trim());
      }
      if ($("tenantId").value.trim()) {
        params.set("tenantId", $("tenantId").value.trim());
      }
      params.set("limit", $("archiveEventLimit").value || "25");
      if ($("archiveStartFrom").value) {
        params.set("startSampleFrom", $("archiveStartFrom").value);
      }
      if ($("archiveStartTo").value) {
        params.set("startSampleTo", $("archiveStartTo").value);
      }
      if ($("archiveOverlapFrom").value) {
        params.set("overlapStartSample", $("archiveOverlapFrom").value);
      }
      if ($("archiveOverlapTo").value) {
        params.set("overlapEndSample", $("archiveOverlapTo").value);
      }
      if (cursor !== null) {
        params.set("cursor", String(cursor));
      }
      return params;
    }

    function formatMs(value) {
      if (value === null || value === undefined) {
        return "n/a";
      }
      if (value < 1000) {
        return `${Math.round(value)} ms`;
      }
      return `${(value / 1000).toFixed(1)} s`;
    }

    function renderSessionList(result) {
      const sessions = Array.isArray(result.sessions) ? result.sessions : [];
      const list = $("sessionList");
      list.textContent = "";
      $("sessionListMeta").textContent = `${sessions.length} session${sessions.length === 1 ? "" : "s"}`;

      if (!sessions.length) {
        const empty = document.createElement("div");
        empty.className = "event-card";
        const title = document.createElement("strong");
        title.textContent = "No active sessions";
        const hint = document.createElement("span");
        hint.textContent = "Create a session or start an ingest worker.";
        empty.append(title, hint);
        list.append(empty);
        return;
      }

      sessions.forEach((session) => {
        const status = session.status || {};
        const card = document.createElement("div");
        card.className = "event-card";
        const title = document.createElement("strong");
        title.textContent = session.sessionId || "session";

        const source = document.createElement("span");
        source.textContent = `source ${session.sourceId || "n/a"}`;
        const owner = document.createElement("span");
        owner.textContent = `owner ${session.ownerId || "n/a"}`;
        const tenant = document.createElement("span");
        tenant.textContent = `tenant ${session.tenantId || "n/a"}`;
        const state = document.createElement("span");
        state.textContent = `state ${status.activityState || "unknown"}`;
        const chunks = document.createElement("span");
        chunks.textContent = `chunks ${status.chunksReceived ?? 0}`;
        const idle = document.createElement("span");
        idle.textContent = `idle ${formatMs(status.idleMs)}`;
        const continuity = document.createElement("span");
        continuity.textContent = `continuity ${status.lastSampleContinuity || "n/a"}`;
        const outputs = document.createElement("span");
        outputs.textContent = `outputs ${status.totalDetectorOutputs ?? 0}`;
        const channels = document.createElement("span");
        channels.textContent = `channels ${session.channelCount ?? "n/a"}`;
        const readiness = session.array?.clickLocalisationReadiness || {};
        const localisation = document.createElement("span");
        const missingGeometry = Array.isArray(readiness.missingClickHydrophoneChannels) && readiness.missingClickHydrophoneChannels.length
          ? `, missing hydrophones ${readiness.missingClickHydrophoneChannels.join(",")}`
          : "";
        localisation.textContent = `click localisation ${readiness.mode || "n/a"}${missingGeometry}`;
        const detail = document.createElement("code");
        detail.textContent = `nextSample=${status.nextExpectedStartSample ?? "n/a"} meanProcessMs=${typeof status.meanProcessMs === "number" ? status.meanProcessMs.toFixed(3) : "n/a"} timelineOk=${status.sampleTimelineOk ?? "n/a"}`;

        card.append(title, source, owner, tenant, state, chunks, idle, continuity, outputs, channels, localisation, detail);
        list.append(card);
      });
    }

    function renderArchiveEvents(result) {
      const events = Array.isArray(result.events) ? result.events : [];
      const list = $("archiveEventList");
      list.textContent = "";
      nextArchiveCursor = typeof result.nextCursor === "number" ? result.nextCursor : null;
      $("eventNextButton").disabled = nextArchiveCursor === null;
      if (nextArchiveCursor !== null) {
        $("archiveCursor").value = String(nextArchiveCursor);
      }
      const owner = result.ownerId ? `, owner ${result.ownerId}` : "";
      const tenant = result.tenantId ? `, tenant ${result.tenantId}` : "";
      $("archiveMeta").textContent = `${events.length} events${result.type ? `, ${result.type}` : ""}${owner}${tenant}`;

      if (!events.length) {
        const empty = document.createElement("div");
        empty.className = "event-card";
        const title = document.createElement("strong");
        title.textContent = "No matching detector events";
        const hint = document.createElement("span");
        hint.textContent = "Try sending PCM, flushing, or widening the sample range.";
        empty.append(title, hint);
        list.append(empty);
        return;
      }

      events.forEach((event) => {
        const card = document.createElement("div");
        card.className = "event-card";
        const title = document.createElement("strong");
        title.textContent = event.type || "event";
        const start = document.createElement("span");
        start.textContent = `start ${event.startSample ?? "n/a"}`;
        const end = document.createElement("span");
        end.textContent = event.endSample === undefined ? "instant" : `end ${event.endSample}`;
        const group = document.createElement("span");
        group.textContent = event.channelGroup || "channel n/a";
        const trains = document.createElement("span");
        trains.textContent = Array.isArray(event.relatedTrainIds) && event.relatedTrainIds.length
          ? `trains ${event.relatedTrainIds.join(",")}`
          : "trains n/a";
        const payload = document.createElement("code");
        payload.textContent = JSON.stringify(event.payload || {});
        card.append(title, start, end, group, trains, payload);
        list.append(card);
      });
    }

    async function loadArchiveEvents(cursor = null) {
      const sessionId = encodeURIComponent($("sessionId").value);
      const params = archiveEventParams({ cursor });
      const result = await requestJson(api(`/sessions/${sessionId}/archive/detections?${params}`));
      renderArchiveEvents(result);
      log(result);
    }

    // ---- live sound-card capture ----

    let livePollTimer = null;
    let livePollSeq = 0;
    const captureStatusByModule = new Map();
    let captureLegacyStatus = null;
    let captureStatusGraphRevision = 0;
    let captureStatusEnabled = false;
    let captureStatusRefresh = null;
    let captureStatusTimer = null;
    let captureAcquisitions = [];
    const legacyCompatibilityCallbacks = {
      refreshWorkspaceSources: async () => {},
      activateTab: () => {}
    };

    function applyCaptureStatus(result) {
      captureStatusEnabled = result?.captureEnabled === true;
      captureStatusGraphRevision =
        Number(result?.currentGraphRevision) || 0;
      captureStatusByModule.clear();
      captureLegacyStatus = null;
      for (const capture of result?.captures || []) {
        if (capture.moduleId) {
          captureStatusByModule.set(capture.moduleId, capture);
        }
        else if (capture.sessionId === $("sessionId").value) {
          captureLegacyStatus = capture;
        }
      }
    }

    function selectedCaptureTarget() {
      const selected = $("captureTarget").value;
      if (selected.startsWith("module:")) {
        return {
          moduleId: selected.slice("module:".length),
          expectedGraphRevision:
            Number(graphDraft?.revision ??
              graphRuntimeStatus?.graphRevision ??
              captureStatusGraphRevision)
        };
      }
      return { sessionId: $("sessionId").value };
    }

    function selectedCaptureStatus() {
      const target = selectedCaptureTarget();
      return target.moduleId
        ? captureStatusByModule.get(target.moduleId) || null
        : captureLegacyStatus;
    }

    function captureStatusSummary(capture) {
      if (!capture) return "Capture stopped";
      const sourceType = capture.kind === "dshow"
        ? "device"
        : "stream";
      return `Capturing ${sourceType} '${capture.source}' (pid ${capture.pid})`;
    }

    function updateCaptureTargets() {
      const select = $("captureTarget");
      const prior = select.value;
      const activeOutsideDraft = Array.from(
        captureStatusByModule.values()).filter(
          (capture) => !captureAcquisitions.some(
            (module) => module.id === capture.moduleId));
      select.replaceChildren(
        ...captureAcquisitions.map((module) =>
          new Option(
            `Module graph · ${module.name}` +
              (captureStatusByModule.has(module.id)
                ? " · capturing"
                : ""),
            `module:${module.id}`)),
        ...activeOutsideDraft.map((capture) =>
          new Option(
            `Applied acquisition · ${capture.moduleId} · capturing`,
            `module:${capture.moduleId}`)),
        new Option(
          "Legacy fixed session" +
            (captureLegacyStatus ? " · capturing" : ""),
          "session"));
      if (Array.from(select.options).some(
          (option) => option.value === prior)) {
        select.value = prior;
      }
      else if (captureAcquisitions.length) {
        select.value =
          `module:${captureAcquisitions[0].id}`;
      }
      updateCaptureControls();
    }

    function updateCaptureControls() {
      const capture = selectedCaptureStatus();
      $("captureStartButton").disabled =
        !captureStatusEnabled || capture !== null;
      $("captureStopButton").disabled =
        !captureStatusEnabled || capture === null;
      if (capture) {
        $("captureMeta").textContent = captureStatusSummary(capture);
      }
      else if (!captureStatusEnabled) {
        $("captureMeta").textContent =
          "Disabled — start the service with PAMGUARD_CAPTURE_ENABLED=1";
      }
      else {
        const target = selectedCaptureTarget();
        $("captureMeta").textContent = target.moduleId
          ? `Capture stopped for acquisition '${target.moduleId}'`
          : `Capture stopped for legacy session '${target.sessionId}'`;
      }
    }

    function updateGraphCaptureIndicators() {
      for (const node of document.querySelectorAll(
        '.graph-node[data-module-id]')) {
        const moduleId = node.dataset.moduleId;
        const indicator = node.querySelector(".graph-capture-state");
        if (!indicator) continue;
        const capture = captureStatusByModule.get(moduleId);
        indicator.classList.toggle("running", Boolean(capture));
        indicator.textContent = capture
          ? `Capture running · ${capture.kind} · pid ${capture.pid}`
          : "Capture stopped";
        indicator.title = capture
          ? `${capture.source} · graph revision ${capture.graphRevision}`
          : "No capture process is registered for this acquisition.";
      }
    }

    async function refreshCaptureStatus({ quiet = false } = {}) {
      if (captureStatusRefresh !== null) {
        return captureStatusRefresh;
      }
      captureStatusRefresh = (async () => {
        try {
          const result = await requestJson(api("/capture/status"));
          applyCaptureStatus(result);
          updateCaptureTargets();
          updateCaptureControls();
          updateGraphCaptureIndicators();
          return result;
        }
        catch (error) {
          if (!quiet) {
            $("captureMeta").textContent =
              `Capture status unavailable: ${error}`;
          }
          return null;
        }
        finally {
          captureStatusRefresh = null;
        }
      })();
      return captureStatusRefresh;
    }

    async function refreshCaptureDevices() {
      const select = $("captureDevice");
      const meta = $("captureMeta");
      try {
        const health = await requestJson(api("/health"));
        if (!health.captureEnabled) {
          captureStatusEnabled = false;
          meta.textContent = "Disabled — start the service with PAMGUARD_CAPTURE_ENABLED=1";
          select.replaceChildren(new Option("Capture disabled on the server", ""));
          updateCaptureControls();
          return;
        }
        captureStatusEnabled = true;
        const listing = await requestJson(api("/capture/devices"));
        const audio = (listing.devices || []).filter((d) => d.type === "audio");
        if (!audio.length) {
          meta.textContent = "No audio capture devices found on the server";
          select.replaceChildren(new Option("No audio devices", ""));
          return;
        }
        select.replaceChildren(...audio.map((d) => new Option(d.name, d.name)));
        meta.textContent = `${audio.length} audio device${audio.length === 1 ? "" : "s"} — uses the session's sample rate & channels`;
        updateCaptureControls();
      } catch (error) {
        meta.textContent = String(error);
      }
    }

    // ---- live scrolling waterfall (PAMGuard-style display) ----
    // Full-spectrum preview frames arrive with each capture chunk; new
    // columns are painted onto an offscreen 1px-per-cell canvas that
    // scrolls left, then scaled onto the visible canvas. Polling runs at
    // ~3 Hz, so the display advances as chunks arrive rather than in
    // 2-second batches.
    const live = {
      canvas: null,
      ctx: null,
      bins: 0,
      maxDb: -Infinity,
      totalSlices: 0,
      sampleRateHz: 0,
      // Interpolated scrolling: the display cursor runs BEHIND the live
      // edge by a user-set delay and tracks it with a gentle rate
      // correction (a delay-locked loop, ±30% max skew), so arrival jitter
      // smaller than the delay is fully absorbed — the image never freezes
      // at the live edge and never jump-snaps.
      columnRateHz: 0,
      displayedCols: 0,
      delaySeconds: 1.0,
      firstFrameStartSample: null,
      lastFrameStartSample: null,
      hopSamples: 0,
      lastClickPaint: 0,
      lastRafTime: 0,
      rafId: null
    };
    const liveWaterfallWidth = 2400; // columns of history kept offscreen

    function ensureLiveCanvas(bins) {
      if (live.canvas && live.bins === bins) {
        return;
      }
      live.canvas = document.createElement("canvas");
      live.canvas.width = liveWaterfallWidth;
      live.canvas.height = bins;
      live.ctx = live.canvas.getContext("2d");
      live.ctx.fillStyle = "#071514";
      live.ctx.fillRect(0, 0, live.canvas.width, live.canvas.height);
      live.bins = bins;
      live.maxDb = -Infinity;
      live.totalSlices = 0;
      live.displayedCols = 0;
      live.columnRateHz = 0;
      live.firstFrameStartSample = null;
      live.lastFrameStartSample = null;
      live.hopSamples = 0;
      live.lastClickPaint = 0;
    }

    function appendLiveFrames(frames) {
      const channel = frames[0].channel;
      const slices = frames.filter((f) => f.channel === channel && Array.isArray(f.magnitudeSquared));
      if (!slices.length) {
        return;
      }
      const bins = slices[0].magnitudeSquared.length;
      ensureLiveCanvas(bins);
      if (live.firstFrameStartSample === null) {
        live.firstFrameStartSample = slices[0].startSample;
      }
      if (slices.length >= 2) {
        const hop = slices[1].startSample - slices[0].startSample;
        if (hop > 0) {
          live.hopSamples = hop;
        }
      }
      else if (live.lastFrameStartSample !== null) {
        const hop = slices[0].startSample - live.lastFrameStartSample;
        if (hop > 0) {
          live.hopSamples = hop;
        }
      }
      live.lastFrameStartSample = slices[slices.length - 1].startSample;

      // Auto-gain: track the loudest bin with a slow decay so a single
      // burst does not permanently compress the display.
      let batchMax = -Infinity;
      const dbSlices = slices.map((slice) => slice.magnitudeSquared.map((value) => {
        const db = 10 * Math.log10(Math.max(value, 1e-12));
        if (db > batchMax) {
          batchMax = db;
        }
        return db;
      }));
      live.maxDb = Math.max(batchMax, live.maxDb - 1.0);

      const width = Math.min(dbSlices.length, liveWaterfallWidth);
      live.ctx.drawImage(live.canvas, -width, 0);
      const img = live.ctx.createImageData(width, bins);
      const range = 60;
      const floor = live.maxDb - range;
      for (let x = 0; x < width; x++) {
        const slice = dbSlices[dbSlices.length - width + x];
        for (let bin = 0; bin < bins; bin++) {
          const [r, g, b] = heatRgb((slice[bin] - floor) / range);
          const y = bins - 1 - bin; // low frequency at the bottom
          const at = (y * width + x) * 4;
          img.data[at] = r;
          img.data[at + 1] = g;
          img.data[at + 2] = b;
          img.data[at + 3] = 255;
        }
      }
      live.ctx.putImageData(img, live.canvas.width - width, 0);
      live.totalSlices += slices.length;

      // Measure the column rate from the data itself (startSample spacing),
      // so the glide speed is exact whatever the session's FFT hop is.
      if (live.hopSamples > 0 && live.sampleRateHz > 0) {
        live.columnRateHz = live.sampleRateHz / live.hopSamples;
      }
      if (live.displayedCols === 0 && live.columnRateHz > 0) {
        // Start the cursor a full delay behind the live edge.
        live.displayedCols = Math.max(0, live.totalSlices - live.delaySeconds * live.columnRateHz);
      }
    }

    function liveTargetLagCols() {
      return Math.max(2, live.delaySeconds * live.columnRateHz);
    }

    function drawLiveView() {
      if (!live.canvas) {
        return;
      }
      const canvas = $("spectrogramCanvas");
      const ctx = canvas.getContext("2d");
      resizeCanvas(canvas);
      // Reserve the delay's worth of columns (plus margin) at the buffer's
      // right edge so the window ending at the display cursor always has
      // source pixels to read.
      const reserve = Math.min(
        Math.ceil(liveTargetLagCols() + live.columnRateHz * 0.5) + 2,
        Math.floor(live.canvas.width / 2));
      const windowCols = Math.max(1, live.canvas.width - reserve);
      // The window ends at the display cursor; its source x is how far the
      // cursor lags the buffer's right edge, inside the reserve.
      const lag = live.totalSlices - live.displayedCols;
      const srcX = Math.max(0, reserve - lag);
      ctx.fillStyle = "#071514";
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      // Smoothing on: sub-column offsets interpolate horizontally (the
      // glide) and the bin-to-pixel downscale averages vertically.
      ctx.imageSmoothingEnabled = true;
      ctx.drawImage(live.canvas, srcX, 0, windowCols, live.canvas.height,
                    0, 0, canvas.width, canvas.height);
      const nyquistKhz = live.sampleRateHz > 0 ? (live.sampleRateHz / 2000).toFixed(1) : "?";
      $("spectrogramMeta").textContent =
        `Live 0–${nyquistKhz} kHz · ${live.bins} bins · delay ${live.delaySeconds.toFixed(1)} s · auto gain`;
    }

    function liveRafLoop(now) {
      if (!liveViewActive || !live.canvas) {
        live.rafId = liveViewActive ? requestAnimationFrame(liveRafLoop) : null;
        return;
      }
      // Cap dt so a backgrounded tab resumes with a bounded step.
      const dt = live.lastRafTime > 0 ? Math.min((now - live.lastRafTime) / 1000, 0.5) : 0;
      live.lastRafTime = now;
      if (live.columnRateHz > 0 && live.displayedCols > 0) {
        const targetLag = liveTargetLagCols();
        const lag = live.totalSlices - live.displayedCols;
        // Delay-locked loop: skew the display rate by up to ±30% toward the
        // target lag. Jitter smaller than the delay is absorbed entirely —
        // no freezing at the live edge, no snapping.
        const skew = Math.max(-0.3, Math.min(0.3, (lag - targetLag) / targetLag));
        const rate = live.columnRateHz * (1 + skew);
        live.displayedCols = Math.min(live.displayedCols + rate * dt, live.totalSlices);
        // Only a gross disturbance (tab slept, stream outage) re-anchors.
        if (lag > targetLag * 3 + live.columnRateHz) {
          live.displayedCols = live.totalSlices - targetLag;
        }
      }
      drawLiveView();
      // Keep the sparse click plots on the same delayed, continuously
      // advancing cursor as the spectrogram. Twenty paints per second is
      // smooth without redrawing three canvases at display refresh rate.
      if (now - live.lastClickPaint >= 50) {
        live.lastClickPaint = now;
        drawClickScatters();
      }
      live.rafId = requestAnimationFrame(liveRafLoop);
    }

    function ensureLiveRaf() {
      if (live.rafId === null) {
        live.lastRafTime = 0;
        live.rafId = requestAnimationFrame(liveRafLoop);
      }
    }

    function stopLiveRaf() {
      if (live.rafId !== null) {
        cancelAnimationFrame(live.rafId);
        live.rafId = null;
      }
    }

    // The live view consumes the PUSH stream: the service writes each
    // result the moment the engine produces it (NDJSON, blank-line
    // heartbeats), so the waterfall advances at the audio chunk cadence
    // (~50 ms for captures) with no polling. Metric tiles are DOM-heavy,
    // so they throttle to ~2 Hz while the canvas paints every result.
    let liveViewActive = false;
    let liveStreamAbort = null;
    let lastMetricsPaint = 0;

    function stopLiveView() {
      liveViewActive = false;
      stopLiveRaf();
      if (liveStreamAbort !== null) {
        liveStreamAbort.abort();
        liveStreamAbort = null;
      }
      if (livePollTimer !== null) {
        clearInterval(livePollTimer);
        livePollTimer = null;
      }
    }

    function handleLiveResult(result) {
      if (Array.isArray(result.spectrogram) && result.spectrogram.length) {
        appendLiveFrames(result.spectrogram);
        ensureLiveRaf();
      }
      // Clicks are sparse; ingest every result (deduplicated) so the click
      // detector display misses nothing between metric repaints.
      ingestClicks(result);
      const now = performance.now();
      if (now - lastMetricsPaint > 500) {
        lastMetricsPaint = now;
        // This result was already ingested above. Re-ingesting a multi-click
        // batch here used to look like a backwards sample jump and clear the
        // entire client-side click history.
        updateMetrics(result, false);
      }
    }

    async function startLiveStream(sessionId) {
      stopLiveView();
      liveViewActive = true;
      live.canvas = null;
      live.sampleRateHz = Number($("sampleRate").value) || 0;
      const controller = new AbortController();
      liveStreamAbort = controller;
      const headers = {};
      const apiKey = $("apiKey").value.trim();
      if (apiKey) {
        headers["X-API-Key"] = apiKey;
      }
      try {
        const response = await fetch(
          api(`/sessions/${encodeURIComponent(sessionId)}/results/stream?sinceSeq=0`),
          { headers, signal: controller.signal });
        if (!response.ok || !response.body) {
          throw new Error(`stream unavailable (${response.status})`);
        }
        const reader = response.body.getReader();
        const decoder = new TextDecoder();
        let buffer = "";
        while (liveViewActive) {
          const { done, value } = await reader.read();
          if (done) {
            break;
          }
          buffer += decoder.decode(value, { stream: true });
          let newline;
          while ((newline = buffer.indexOf("\n")) >= 0) {
            const line = buffer.slice(0, newline).trim();
            buffer = buffer.slice(newline + 1);
            if (!line) {
              continue; // heartbeat
            }
            handleLiveResult(JSON.parse(line));
          }
        }
        if (liveViewActive) {
          $("captureMeta").textContent = "Live stream ended";
          liveViewActive = false;
        }
      } catch (error) {
        if (controller.signal.aborted) {
          return; // deliberate stop
        }
        // Older service without the stream endpoint, or a proxy that
        // buffers chunked responses: fall back to fast polling.
        startLivePoll(sessionId);
      }
    }

    function startLivePoll(sessionId) {
      liveViewActive = true;
      livePollSeq = 0;
      live.canvas = null;
      live.sampleRateHz = Number($("sampleRate").value) || 0;
      let polling = false;
      livePollTimer = setInterval(async () => {
        if (polling) {
          return; // skip if the previous poll is still in flight
        }
        polling = true;
        try {
          const feed = await requestJson(api(`/sessions/${encodeURIComponent(sessionId)}/results?sinceSeq=${livePollSeq}`));
          if (feed.latestSeq !== undefined) {
            livePollSeq = feed.latestSeq;
          }
          const results = Array.isArray(feed.results) ? feed.results : [];
          for (const result of results) {
            handleLiveResult(result);
          }
        } catch (error) {
          // Session gone or service stopped: stop polling quietly.
          stopLiveView();
          $("captureMeta").textContent = `Live poll stopped: ${error}`;
        } finally {
          polling = false;
        }
      }, 150);
    }

    $("displayDelay").addEventListener("input", () => {
      live.delaySeconds = Number($("displayDelay").value);
      $("displayDelayValue").textContent = `${live.delaySeconds.toFixed(1)} s`;
      // The delay-locked loop converges to the new lag smoothly on its own.
    });

    $("captureSourceType").addEventListener("change", () => {
      const useUrl = $("captureSourceType").value === "url";
      $("captureDeviceRow").style.display = useUrl ? "none" : "";
      $("captureUrlRow").style.display = useUrl ? "" : "none";
      $("captureRefreshButton").style.display = useUrl ? "none" : "";
      $("captureMeta").textContent = useUrl
        ? "The stream's audio is resampled to the session's sample rate & channels"
        : "Uses the session's sample rate & channels";
    });

    $("captureRefreshButton").addEventListener("click", refreshCaptureDevices);
    $("captureTarget").addEventListener("change", updateCaptureControls);

    $("captureStartButton").addEventListener("click", async () => {
      const useUrl = $("captureSourceType").value === "url";
      const device = $("captureDevice").value;
      const url = $("captureUrl").value.trim();
      const meta = $("captureMeta");
      if (useUrl && !/^https?:\/\//.test(url)) {
        meta.textContent = "Enter a stream URL starting with http:// or https://";
        return;
      }
      if (!useUrl && !device) {
        meta.textContent = "Pick an audio device first (Refresh devices)";
        return;
      }
      try {
        const target = selectedCaptureTarget();
        const moduleTarget = target.moduleId || "";
        if (!moduleTarget) {
          // The legacy fixed-session route remains available while the
          // composable graph becomes the primary operator workflow.
          try {
            await requestJson(api("/sessions"), {
              method: "POST",
              headers: { "Content-Type": "application/json" },
              body: JSON.stringify(sessionBody())
            });
          } catch (createError) {
            if (!String(createError).toLowerCase().includes("exist")) {
              throw createError;
            }
          }
        }
        const sessionId = $("sessionId").value;
        const startBody = useUrl
          ? { ...target, url }
          : { ...target, device };
        const result = await requestJson(api("/capture/start"), {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(startBody)
        });
        await refreshCaptureStatus();
        meta.textContent = `Capturing '${useUrl ? url : device}' into ${
          moduleTarget ? `module '${moduleTarget}'` : `session '${sessionId}'`} (pid ${result.pid})`;
        if (moduleTarget) {
          await legacyCompatibilityCallbacks.
            refreshWorkspaceSources(false);
          legacyCompatibilityCallbacks.activateTab("workspace");
        }
        else {
          resetClickDisplay();
          startLiveStream(sessionId);
        }
        log(result);
      } catch (error) {
        meta.textContent = String(error);
        log(String(error));
      }
    });

    $("captureStopButton").addEventListener("click", async () => {
      const meta = $("captureMeta");
      stopLiveView();
      try {
        const target = selectedCaptureTarget();
        if (!selectedCaptureStatus()) {
          updateCaptureControls();
          return;
        }
        const result = await requestJson(api("/capture/stop"), {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(target)
        });
        await refreshCaptureStatus();
        meta.textContent = target.moduleId
          ? `Capture stopped for acquisition '${target.moduleId}'`
          : `Capture stopped for legacy session '${target.sessionId}'`;
        log(result);
      } catch (error) {
        meta.textContent = String(error);
        log(String(error));
      }
    });
    $("listSessionsButton").addEventListener("click", async () => {
      try {
        const params = new URLSearchParams();
        if ($("ownerId").value.trim()) {
          params.set("ownerId", $("ownerId").value.trim());
        }
        if ($("tenantId").value.trim()) {
          params.set("tenantId", $("tenantId").value.trim());
        }
        const query = params.toString();
        const result = await requestJson(api(query ? `/sessions?${query}` : "/sessions"));
        renderSessionList(result);
        log(result);
      } catch (error) {
        log(String(error));
      }
    });

    $("createButton").addEventListener("click", async () => {
      try {
        const result = await requestJson(api("/sessions"), {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(sessionBody())
        });
        nextStartSample = 0;
        nextArchiveCursor = null;
        $("eventNextButton").disabled = true;
        resetClickDisplay();
        log(result);
      } catch (error) {
        log(String(error));
      }
    });

    $("sendButton").addEventListener("click", async () => {
      try {
        const pcm = syntheticPcm();
        const params = new URLSearchParams({
          startSample: String(nextStartSample),
          includeSpectrogram: "true",
          includeClickWaveforms: $("includeClickWaveforms").value,
          includeClickSpectra: $("includeClickSpectra").value,
          spectrogramMaxBins: $("previewBins").value,
          spectrogramBinStride: $("binStride").value
        });
        const result = await requestJson(api(`/sessions/${encodeURIComponent($("sessionId").value)}/pcm-f32le?${params}`), {
          method: "POST",
          headers: { "Content-Type": "application/octet-stream" },
          body: pcm.buffer
        });
        if (typeof result.nextExpectedStartSample === "number") {
          nextStartSample = result.nextExpectedStartSample;
        }
        updateMetrics(result);
        log(result);
      } catch (error) {
        log(String(error));
      }
    });

    $("flushButton").addEventListener("click", async () => {
      try {
        const result = await requestJson(api(`/sessions/${encodeURIComponent($("sessionId").value)}/flush`), {
          method: "POST"
        });
        updateMetrics(result);
        log(result);
      } catch (error) {
        log(String(error));
      }
    });

    $("deleteButton").addEventListener("click", async () => {
      try {
        const result = await requestJson(api(`/sessions/${encodeURIComponent($("sessionId").value)}`), { method: "DELETE" });
        nextStartSample = 0;
        nextArchiveCursor = null;
        $("eventNextButton").disabled = true;
        stopLiveView();
        resetClickDisplay();
        log(result);
      } catch (error) {
        log(String(error));
      }
    });

    $("eventTailButton").addEventListener("click", async () => {
      try {
        await loadArchiveEvents(null);
      } catch (error) {
        log(String(error));
      }
    });

    $("eventCursorButton").addEventListener("click", async () => {
      try {
        await loadArchiveEvents(Number($("archiveCursor").value || 0));
      } catch (error) {
        log(String(error));
      }
    });

    $("eventNextButton").addEventListener("click", async () => {
      if (nextArchiveCursor === null) {
        return;
      }
      try {
        await loadArchiveEvents(nextArchiveCursor);
      } catch (error) {
        log(String(error));
      }
    });

    $("eventCsvButton").addEventListener("click", async () => {
      try {
        const sessionId = encodeURIComponent($("sessionId").value);
        const params = archiveEventParams();
        const csv = await requestText(api(`/sessions/${sessionId}/archive/detections.csv?${params}`));
        downloadText(`detections-${$("sessionId").value || "session"}.csv`, csv, "text/csv");
        log(`Downloaded ${csv.split(/\r?\n/).filter(Boolean).length - 1} detection rows as CSV.`);
      } catch (error) {
        log(String(error));
      }
    });

    // Dialog close buttons (dialogs use plain divs, not method="dialog").
    document.querySelectorAll("[data-close]").forEach((button) => {
      button.addEventListener("click", () => $(button.dataset.close).close());
    });

    const clickPaneFields = {
      detection: [
        "clickEnabled", "clickThresholdDb", "shortFilter", "longFilter",
        "longFilter2", "preSample", "postSample", "minSep", "maxLength"
      ],
      filters: [
        "preFilterType", "preFilterBand", "preFilterOrder",
        "preFilterHighPassFreq", "preFilterLowPassFreq", "preFilterRipple",
        "preFilterStopRipple", "preFilterChebyGamma", "preFilterArbitrary",
        "triggerFilterType", "triggerFilterBand", "triggerFilterOrder",
        "triggerFilterHighPassFreq", "triggerFilterLowPassFreq",
        "triggerFilterRipple", "triggerFilterStopRipple",
        "triggerFilterChebyGamma", "triggerFilterArbitrary"
      ],
      channels: [
        "minTriggerChannels", "clickChannelBitmap", "clickTriggerBitmap",
        "clickGroupingType", "clickChannelGroups"
      ],
      classification: [
        "classifierAlgorithm", "classifierPreset", "classifyOnline",
        "discardUnclassifiedClicks", "sweepPreset", "sweepCheckAll",
        "classifierTypesJson", "sweepTypesJson"
      ],
      echo: [
        "echoRunOnline", "echoDiscard", "echoMaxIntervalSeconds",
        "angleVetoesJson"
      ],
      delay: [
        "clickLocalisationEnabled", "delayFilterBearings", "delayFilterBand",
        "delayHighPassFreq", "delayLowPassFreq", "delayEnvelopeBearings",
        "delayUseLeadingEdge", "delayUpSample", "delayUseRestrictedBins",
        "delayRestrictedBins", "delayTypeSettingsJson"
      ],
      noise: [
        "sampleClickNoise", "clickNoiseIntervalSeconds",
        "storeClickBackground", "clickBackgroundIntervalSeconds",
        "publishTriggerFunction", "featuresEnabled"
      ],
      trains: [
        "trainEnabled", "trainAlgorithm", "maxIciSeconds", "trainMinClicks",
        "trainClassifierEnabled", "trainMhtJson", "trainClassifierJson"
      ]
    };
    const clickPaneDescriptions = {
      detection: "Trigger threshold, smoothing constants, waveform capture, separation, and maximum click length.",
      filters: "PAMGuard pre-filter and trigger-filter methods. Unsupported methods are rejected by the service rather than silently substituted.",
      channels: "Detection and trigger bitmaps, minimum coincident triggers, and PAMGuard channel grouping.",
      classification: "Basic or Sweep classification, online/discard policy, presets, and full custom type JSON.",
      echo: "Online echo rejection and the ordered list of inclusive absolute-angle veto ranges.",
      delay: "Correlation preprocessing, envelope/leading-edge modes, upsampling, restricted samples, and per-type overrides.",
      noise: "Click-noise waveforms, trigger-background sampling, trigger-function output, and feature extraction.",
      trains: "ICI or MHT formation, minimum train rules, and the complete MHT/classifier JSON settings surface."
    };

    function activateClickPane(name) {
      const dialog = $("dlgClick");
      if (!dialog || !clickPaneFields[name]) {
        return;
      }
      dialog.querySelectorAll(".grid > div").forEach((field) => {
        field.hidden = true;
      });
      dialog.querySelectorAll(".grid > h4").forEach((heading) => {
        heading.hidden = true;
      });
      clickPaneFields[name].forEach((id) => {
        const input = $(id);
        const field = input && input.closest(".grid > div");
        if (field) {
          field.hidden = false;
        }
      });
      dialog.querySelectorAll("[data-click-pane-target]").forEach((button) => {
        const selected = button.dataset.clickPaneTarget === name;
        button.classList.toggle("active", selected);
        button.setAttribute("aria-selected", selected ? "true" : "false");
      });
      $("clickPaneMeta").textContent = clickPaneDescriptions[name];
    }

    document.querySelectorAll("[data-click-pane-target]").forEach((button) => {
      button.addEventListener("click", () => activateClickPane(button.dataset.clickPaneTarget));
    });
    activateClickPane("detection");

    const monitoringPaneFields = {
      fftNoise: [
        "fftNoiseEnabled", "fftNoiseChannelBitmap", "fftNoiseInterval",
        "fftNoiseMeasures", "fftNoiseUseAll", "fftNoiseBandsJson"
      ],
      noiseBand: [
        "noiseBandEnabled", "noiseBandType", "noiseBandMinFrequency",
        "noiseBandMaxFrequency", "noiseBandReferenceFrequency",
        "noiseBandIirOrder", "noiseBandOutputInterval"
      ],
      ltsa: ["ltsaEnabled", "ltsaInterval"],
      ishmael: ["ishmaelEnabled", "ishmaelJson"],
      sgramCorr: ["sgramCorrEnabled", "sgramCorrJson"],
      matchFilt: ["matchFiltEnabled", "matchFiltJson"],
      matchedTemplate: ["matchedTemplateEnabled", "matchedTemplateJson"],
      advanced: ["monitoringJson"]
    };
    const monitoringPaneDescriptions = {
      fftNoise: "PAMGuard noiseMonitor FFT-band interval statistics, channel selection, cadence, and user frequency bands.",
      noiseBand: "PAMGuard noiseBandMonitor fractional-octave IIR filter bank and output cadence.",
      ltsa: "Long-term spectral averages formed from the configured FFT stream.",
      ishmael: "Ishmael energy-sum detector. The complete ported settings object is accepted as JSON.",
      sgramCorr: "Ishmael spectrogram-correlation segments, spread, threshold, and timing settings.",
      matchFilt: "Ishmael time-domain matched-filter kernel, channel selection, threshold, and timing settings.",
      matchedTemplate: "PAMGuard matched-template click classifier settings and match/reject waveform pairs.",
      advanced: "Top-level expert overrides and acquisition calibration. Structured panes take precedence for their module keys."
    };

    function activateMonitoringPane(name) {
      const dialog = $("dlgMonitoring");
      if (!dialog || !monitoringPaneFields[name]) {
        return;
      }
      dialog.querySelectorAll(".grid > div").forEach((field) => {
        field.hidden = true;
      });
      monitoringPaneFields[name].forEach((id) => {
        const input = $(id);
        const field = input && input.closest(".grid > div");
        if (field) {
          field.hidden = false;
        }
      });
      dialog.querySelectorAll("[data-monitoring-pane-target]").forEach((button) => {
        const selected = button.dataset.monitoringPaneTarget === name;
        button.classList.toggle("active", selected);
        button.setAttribute("aria-selected", selected ? "true" : "false");
      });
      $("monitoringPaneMeta").textContent = monitoringPaneDescriptions[name];
    }

    document.querySelectorAll("[data-monitoring-pane-target]").forEach((button) => {
      button.addEventListener("click", () =>
        activateMonitoringPane(button.dataset.monitoringPaneTarget));
    });
    activateMonitoringPane("fftNoise");

    // ================= click detector display ==============================
    // PAMGuard's click display family: bearing-time, amplitude-time, ICI,
    // and a waveform + Wigner-Ville panel for a selected click. Clicks are
    // accumulated client-side from live results (deduplicated by start
    // sample and reset only by an explicit new session/capture action).

    const clickStore = [];
    const seenClickStarts = new Set();
    let totalClicksSeen = 0;
    let selectedClick = null;
    const scatterHitMaps = {};

    function clickSampleRate() {
      return live.sampleRateHz || Number($("sampleRate").value) || 48000;
    }

    function resetClickDisplay() {
      clickStore.length = 0;
      seenClickStarts.clear();
      totalClicksSeen = 0;
      selectedClick = null;
      drawSelectedClick();
      drawClickScatters();
      $("mClicks").textContent = "0";
    }

    function clickDisplayCursorSeconds() {
      const sr = clickSampleRate();
      if (liveViewActive && live.canvas && live.firstFrameStartSample !== null &&
          live.hopSamples > 0 && sr > 0) {
        const displayedIndex = Math.max(0, live.displayedCols - 1);
        return (live.firstFrameStartSample + displayedIndex * live.hopSamples) / sr;
      }
      return clickStore.length ? clickStore[clickStore.length - 1].t : 0;
    }

    function ingestClicks(result) {
      const clicks = Array.isArray(result.clicks) ? result.clicks : [];
      if (!clicks.length) {
        return;
      }
      const locs = Array.isArray(result.clickLocalisations) ? result.clickLocalisations : [];
      const bearingByIndex = new Map();
      for (const loc of locs) {
        let bearing = null;
        for (const delay of loc.delays || []) {
          if (typeof delay.pairBearingDegrees === "number") {
            bearing = delay.pairBearingDegrees;
            break;
          }
        }
        if (bearing === null && loc.lsqBearing && typeof loc.lsqBearing.bearingDegrees === "number") {
          bearing = loc.lsqBearing.bearingDegrees;
        }
        bearingByIndex.set(loc.clickIndex, bearing);
      }
      const sr = clickSampleRate();
      let added = false;
      clicks.forEach((click, index) => {
        if (typeof click.startSample !== "number") {
          return;
        }
        if (seenClickStarts.has(click.startSample)) {
          return; // duplicate delivery via stream/poll/manual result paths
        }
        seenClickStarts.add(click.startSample);
        totalClicksSeen++;
        const entry = {
          startSample: click.startSample,
          t: click.startSample / sr,
          durationSamples: click.durationSamples,
          amplitudeDb: click.signalExcessDb,
          bearingDeg: bearingByIndex.has(index) ? bearingByIndex.get(index) : null,
          iciSeconds: null,
          waveform: Array.isArray(click.waveform) && click.waveform.length ? click.waveform[0] : null,
          echo: click.echo === true
        };
        clickStore.push(entry);
        if (!selectedClick && entry.waveform) {
          selectedClick = entry;
          drawSelectedClick();
        }
        added = true;
      });
      if (added) {
        clickStore.sort((left, right) => left.startSample - right.startSample);
        for (let index = 0; index < clickStore.length; index++) {
          clickStore[index].iciSeconds = index > 0
            ? (clickStore[index].startSample - clickStore[index - 1].startSample) / sr
            : null;
        }
      }
      while (clickStore.length > 2000) {
        const removed = clickStore.shift();
        seenClickStarts.delete(removed.startSample);
      }
      if (added) {
        drawClickScatters();
      }
    }

    function drawScatter(canvasId, points, cursorTime, yOf, yMin, yMax, options = {}) {
      const canvas = $(canvasId);
      const ctx = canvas.getContext("2d");
      resizeCanvas(canvas);
      const w = canvas.width;
      const h = canvas.height;
      ctx.fillStyle = "#0a1310";
      ctx.fillRect(0, 0, w, h);
      // Horizontal grid with labels.
      ctx.strokeStyle = "rgba(140, 190, 178, 0.14)";
      ctx.fillStyle = "rgba(190, 215, 225, 0.55)";
      ctx.font = `${Math.max(10, h / 18)}px Cascadia Mono, Consolas, monospace`;
      for (let g = 0; g <= 4; g++) {
        const frac = g / 4;
        const y = h - frac * h;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
        const value = options.logY
          ? Math.pow(10, yMin + frac * (yMax - yMin))
          : yMin + frac * (yMax - yMin);
        ctx.fillText(options.format ? options.format(value) : value.toFixed(0), 6, Math.max(12, y - 4));
      }
      const windowSeconds = Number($("clickWindowSeconds").value);
      const hits = [];
      for (const entry of points) {
        const raw = yOf(entry);
        if (raw === null || raw === undefined || !Number.isFinite(raw)) {
          continue;
        }
        const value = options.logY ? Math.log10(Math.max(raw, 1e-6)) : raw;
        if (value < yMin || value > yMax) {
          continue;
        }
        const x = w * (1 - (cursorTime - entry.t) / windowSeconds);
        if (x < 0 || x > w) {
          continue;
        }
        const y = h - ((value - yMin) / (yMax - yMin)) * h;
        const size = Math.max(3, w / 400);
        ctx.fillStyle = entry.echo ? "rgba(150, 160, 170, 0.6)" : "#37b7eb";
        if (entry === selectedClick) {
          ctx.fillStyle = "#f2b544";
          ctx.fillRect(x - size, y - size, size * 2 + 1, size * 2 + 1);
        }
        else {
          ctx.fillRect(x - size / 2, y - size / 2, size, size);
        }
        hits.push({ x, y, entry });
      }
      scatterHitMaps[canvasId] = hits;
    }

    function drawClickScatters() {
      const windowSeconds = Number($("clickWindowSeconds").value);
      const cursorTime = clickDisplayCursorSeconds();
      const visible = clickStore.filter((entry) =>
        entry.t <= cursorTime && cursorTime - entry.t <= windowSeconds);

      drawScatter("btCanvas", visible, cursorTime, (e) => e.bearingDeg, 0, 180,
        { format: (v) => `${v.toFixed(0)}°` });
      drawScatter("ampCanvas", visible, cursorTime, (e) => e.amplitudeDb, 0,
        Math.max(24, ...visible.map((e) => e.amplitudeDb + 3)),
        { format: (v) => `${v.toFixed(0)} dB` });
      drawScatter("iciCanvas", visible, cursorTime, (e) => e.iciSeconds, -3, Math.log10(3),
        { logY: true, format: (v) => v >= 1 ? `${v.toFixed(1)} s` : `${(v * 1000).toFixed(0)} ms` });

      const withBearing = visible.filter((e) => e.bearingDeg !== null).length;
      $("btMeta").textContent = withBearing
        ? `${withBearing} clicks with bearings`
        : "needs ≥2 channels with localisation";
      $("ampMeta").textContent = visible.length
        ? `${visible.length} clicks in the ${windowSeconds} s window`
        : "no clicks in the current window";
      $("clickStoreMeta").textContent =
        `${totalClicksSeen} detected · ${clickStore.length} retained`;
    }

    ["btCanvas", "ampCanvas", "iciCanvas"].forEach((canvasId) => {
      $(canvasId).addEventListener("click", (event) => {
        const rect = $(canvasId).getBoundingClientRect();
        const ratio = window.devicePixelRatio || 1;
        const px = (event.clientX - rect.left) * ratio;
        const py = (event.clientY - rect.top) * ratio;
        let best = null;
        let bestDist = 14 * ratio;
        for (const hit of scatterHitMaps[canvasId] || []) {
          const dist = Math.hypot(hit.x - px, hit.y - py);
          if (dist < bestDist) {
            bestDist = dist;
            best = hit.entry;
          }
        }
        if (best) {
          selectedClick = best;
          drawSelectedClick();
          drawClickScatters();
        }
      });
    });

    $("clickWindowSeconds").addEventListener("change", drawClickScatters);

    // ---- Wigner-Ville of the selected click ----

    function fftComplex(re, im, invert) {
      const n = re.length;
      for (let i = 1, j = 0; i < n; i++) {
        let bit = n >> 1;
        for (; j & bit; bit >>= 1) {
          j ^= bit;
        }
        j ^= bit;
        if (i < j) {
          [re[i], re[j]] = [re[j], re[i]];
          [im[i], im[j]] = [im[j], im[i]];
        }
      }
      for (let len = 2; len <= n; len <<= 1) {
        const angle = (invert ? 2 : -2) * Math.PI / len;
        const wr = Math.cos(angle);
        const wi = Math.sin(angle);
        for (let i = 0; i < n; i += len) {
          let cr = 1;
          let ci = 0;
          for (let k = 0; k < len / 2; k++) {
            const ur = re[i + k];
            const ui = im[i + k];
            const vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
            const vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
            re[i + k] = ur + vr;
            im[i + k] = ui + vi;
            re[i + k + len / 2] = ur - vr;
            im[i + k + len / 2] = ui - vi;
            const ncr = cr * wr - ci * wi;
            ci = cr * wi + ci * wr;
            cr = ncr;
          }
        }
      }
      if (invert) {
        for (let i = 0; i < n; i++) {
          re[i] /= n;
          im[i] /= n;
        }
      }
    }

    function computeWigner(wave) {
      // Centre a power-of-two window on the click's peak.
      const N = 128;
      let peak = 0;
      let peakAt = 0;
      for (let i = 0; i < wave.length; i++) {
        const a = Math.abs(wave[i]);
        if (a > peak) {
          peak = a;
          peakAt = i;
        }
      }
      const start = Math.max(0, Math.min(peakAt - N / 2, wave.length - N));
      const x = new Float64Array(N);
      for (let i = 0; i < N; i++) {
        x[i] = start + i < wave.length ? wave[start + i] : 0;
      }
      // Analytic signal via FFT (zero negative frequencies).
      const are = Array.from(x);
      const aim = new Array(N).fill(0);
      fftComplex(are, aim, false);
      for (let k = 1; k < N / 2; k++) {
        are[k] *= 2;
        aim[k] *= 2;
      }
      for (let k = N / 2 + 1; k < N; k++) {
        are[k] = 0;
        aim[k] = 0;
      }
      fftComplex(are, aim, true);
      // WVD: FFT over the lag kernel z(n+tau) * conj(z(n-tau)). The lag
      // product of a tone at f oscillates at 2f, so the N lag-FFT bins
      // span 0..fs/2 (bin k = k*fs/(2N)) — keep them all.
      const rows = N;
      const wvd = [];
      let maxVal = 0;
      for (let n = 0; n < N; n++) {
        const kr = new Array(N).fill(0);
        const ki = new Array(N).fill(0);
        const L = Math.min(n, N - 1 - n);
        for (let tau = -L; tau <= L; tau++) {
          const pr = are[n + tau] * are[n - tau] + aim[n + tau] * aim[n - tau];
          const pi = aim[n + tau] * are[n - tau] - are[n + tau] * aim[n - tau];
          const idx = (tau + N) % N;
          kr[idx] = pr;
          ki[idx] = pi;
        }
        fftComplex(kr, ki, false);
        const column = new Float64Array(rows);
        for (let k = 0; k < rows; k++) {
          const value = Math.max(0, kr[k]);
          column[k] = value;
          if (value > maxVal) {
            maxVal = value;
          }
        }
        wvd.push(column);
      }
      return { wvd, rows, cols: N, maxVal, start };
    }

    function drawSelectedClick() {
      if (!selectedClick) {
        $("clickSelMeta").textContent = "click a point to select";
        $("wignerMeta").textContent = "";
        for (const canvasId of ["clickWaveCanvas", "wignerCanvas"]) {
          const canvas = $(canvasId);
          const ctx = canvas.getContext("2d");
          resizeCanvas(canvas);
          ctx.fillStyle = "#0a1310";
          ctx.fillRect(0, 0, canvas.width, canvas.height);
        }
        return;
      }
      const sr = clickSampleRate();
      $("clickSelMeta").textContent =
        `sample ${selectedClick.startSample} · ${selectedClick.durationSamples ?? "?"} samples · ` +
        `${formatNumber(selectedClick.amplitudeDb, 1)} dB SE` +
        (selectedClick.bearingDeg !== null ? ` · ${formatNumber(selectedClick.bearingDeg, 1)}°` : "");

      const wave = selectedClick.waveform;
      const waveCanvas = $("clickWaveCanvas");
      const waveCtx = waveCanvas.getContext("2d");
      resizeCanvas(waveCanvas);
      waveCtx.fillStyle = "#0a1310";
      waveCtx.fillRect(0, 0, waveCanvas.width, waveCanvas.height);
      if (!wave || !wave.length) {
        waveCtx.fillStyle = "rgba(190, 215, 225, 0.6)";
        waveCtx.font = "13px Cascadia Mono, Consolas, monospace";
        waveCtx.fillText("No waveform for this click (enable click waveforms)", 12, 26);
        $("wignerMeta").textContent = "";
        return;
      }
      let peak = 1e-9;
      for (const value of wave) {
        peak = Math.max(peak, Math.abs(value));
      }
      waveCtx.strokeStyle = "#37b7eb";
      waveCtx.lineWidth = Math.max(1, waveCanvas.width / 900);
      waveCtx.beginPath();
      for (let i = 0; i < wave.length; i++) {
        const px = (i / (wave.length - 1)) * waveCanvas.width;
        const py = waveCanvas.height / 2 - (wave[i] / peak) * (waveCanvas.height / 2 - 4);
        if (i === 0) {
          waveCtx.moveTo(px, py);
        }
        else {
          waveCtx.lineTo(px, py);
        }
      }
      waveCtx.stroke();

      const { wvd, rows, cols, maxVal } = computeWigner(wave);
      const wignerCanvas = $("wignerCanvas");
      const wignerCtx = wignerCanvas.getContext("2d");
      resizeCanvas(wignerCanvas);
      const off = document.createElement("canvas");
      off.width = cols;
      off.height = rows;
      const offCtx = off.getContext("2d");
      const img = offCtx.createImageData(cols, rows);
      for (let n = 0; n < cols; n++) {
        for (let k = 0; k < rows; k++) {
          const [r, g, b] = heatRgb(maxVal > 0 ? Math.pow(wvd[n][k] / maxVal, 0.5) : 0);
          const at = ((rows - 1 - k) * cols + n) * 4;
          img.data[at] = r;
          img.data[at + 1] = g;
          img.data[at + 2] = b;
          img.data[at + 3] = 255;
        }
      }
      offCtx.putImageData(img, 0, 0);
      wignerCtx.imageSmoothingEnabled = true;
      wignerCtx.drawImage(off, 0, 0, wignerCanvas.width, wignerCanvas.height);
      $("wignerMeta").textContent =
        `${cols} samples · 0–${(sr / 2000).toFixed(1)} kHz`;
    }

    let legacyCompatibilityMounted = false;
    const legacyCompatibilityController = Object.freeze({
      configure({
        refreshWorkspaceSources,
        activateTab
      } = {}) {
        if (legacyCompatibilityMounted) {
          throw new Error(
            "Legacy compatibility callbacks cannot change while mounted");
        }
        if (refreshWorkspaceSources !== undefined) {
          if (typeof refreshWorkspaceSources !== "function") {
            throw new TypeError(
              "refreshWorkspaceSources must be a function");
          }
          legacyCompatibilityCallbacks.refreshWorkspaceSources =
            refreshWorkspaceSources;
        }
        if (activateTab !== undefined) {
          if (typeof activateTab !== "function") {
            throw new TypeError("activateTab must be a function");
          }
          legacyCompatibilityCallbacks.activateTab = activateTab;
        }
      },
      updateAcquisitionList(acquisitions) {
        captureAcquisitions = structuredClone(acquisitions || []);
        updateCaptureTargets();
      },
      mount() {
        if (legacyCompatibilityMounted) {
          throw new Error(
            "Legacy compatibility controller is already mounted");
        }
        legacyCompatibilityMounted = true;
        refreshCaptureDevices();
      },
      dispose() {
        if (!legacyCompatibilityMounted) return;
        legacyCompatibilityMounted = false;
        stopLiveView();
      }
    });

    globalThis.PamguardModules = Object.freeze({
      ...(globalThis.PamguardModules || {}),
      legacyCompatibility: legacyCompatibilityController
    });
