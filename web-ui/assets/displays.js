    // ================= composable operator workspace ======================

    const workspaceStorageKey = "pamguard.operatorWorkspace.v1";
    const workspaceDisplays = new Map();
    let workspaceBlocks = [];
    let workspaceNextDisplayId = 1;
    let workspaceSharedTimeMs = 0;
    const workspaceSharedTimeByGroup = new Map();
    const workspaceCursorByGroup = new Map();
    let workspaceActiveTabId = "";
    const displayCallbacks = {
      activateTab: () => {},
      focusControlledUnit: () => {}
    };

    function updateWorkspaceTabs(preferredId = "") {
      const arrangement = $("workspaceArrangement").value || "grid";
      const tabs = $("operatorTabs");
      const grid = $("operatorGrid");
      grid.classList.toggle("tabs", arrangement === "tabs");
      tabs.classList.toggle("visible", arrangement === "tabs");
      if (preferredId && workspaceDisplays.has(preferredId)) {
        workspaceActiveTabId = preferredId;
      }
      if (!workspaceDisplays.has(workspaceActiveTabId)) {
        workspaceActiveTabId = workspaceDisplays.keys().next().value || "";
      }
      tabs.replaceChildren(...Array.from(
        workspaceDisplays.values(),
        (display) => {
          const button = document.createElement("button");
          button.type = "button";
          button.role = "tab";
          button.textContent = display.config.name || display.type || display.id;
          button.classList.toggle(
            "active",
            display.id === workspaceActiveTabId);
          button.addEventListener("click", () => {
            if (display.popoutWindow && !display.popoutWindow.closed) {
              display.popoutWindow.focus();
              return;
            }
            workspaceActiveTabId = display.id;
            updateWorkspaceTabs();
            display.scheduleRender?.();
          });
          return button;
        }));
      for (const display of workspaceDisplays.values()) {
        display.element?.classList.toggle(
          "tab-active",
          arrangement !== "tabs" ||
            display.id === workspaceActiveTabId);
      }
    }

    function setWorkspaceDisplayHeight(display, heightPx) {
      const height = Math.max(180, Math.min(900, Number(heightPx) || 300));
      display.config.heightPx = height;
      if (display.canvas) display.canvas.style.height = `${height}px`;
      if (display.list) display.list.style.height = `${height}px`;
      display.scheduleRender?.();
    }

    async function fullscreenWorkspaceDisplay(display) {
      if (!document.fullscreenElement) {
        await display.element.requestFullscreen();
      }
      else {
        await document.exitFullscreen();
      }
      display.scheduleRender?.();
    }

    function popOutWorkspaceDisplay(display) {
      if (display.popoutWindow && !display.popoutWindow.closed) {
        display.popoutWindow.focus();
        return;
      }
      const popup = window.open(
        "",
        `pamguard-${display.id}`,
        "popup,width=1100,height=720,resizable=yes");
      if (!popup) {
        display.status.textContent = "pop-out blocked";
        return;
      }
      const placeholder = document.createComment(
        `workspace display ${display.id}`);
      display.element.before(placeholder);
      popup.document.title = `PAMGuard · ${display.config.name}`;
      for (const style of document.querySelectorAll("style")) {
        popup.document.head.append(style.cloneNode(true));
      }
      popup.document.body.style.margin = "0";
      popup.document.body.style.background = "#071514";
      popup.document.body.append(display.element);
      display.element.classList.add("wide", "tab-active");
      display.popoutWindow = popup;
      const restore = () => {
        if (placeholder.parentNode) {
          placeholder.parentNode.insertBefore(display.element, placeholder);
          placeholder.remove();
        }
        display.element.classList.toggle("wide", Boolean(display.config.wide));
        display.popoutWindow = null;
        updateWorkspaceTabs(display.id);
        display.scheduleRender?.();
      };
      popup.addEventListener("beforeunload", restore, { once: true });
      popup.addEventListener("resize", () => display.scheduleRender?.());
      display.scheduleRender?.();
      updateWorkspaceTabs();
    }

    function workspaceFftBlocks() {
      return workspaceBlocks.filter((block) => block.dataType === "pamguard.fft");
    }

    function workspaceAuthHeaders() {
      const headers = {};
      const apiKey = $("apiKey").value.trim();
      if (apiKey) {
        headers["X-API-Key"] = apiKey;
      }
      return headers;
    }

    function workspaceEmptyState() {
      const grid = $("operatorGrid");
      if (workspaceDisplays.size || grid.querySelector(".operator-empty")) {
        return;
      }
      const empty = document.createElement("div");
      empty.className = "operator-empty";
      empty.textContent = workspaceBlocks.length
        ? "Add a display to build an operator layout."
        : "No executable data blocks are available. Configure and run a module graph first.";
      grid.append(empty);
    }

    class WorkspaceSpectrogram {
      constructor(config = {}) {
        this.id = config.id || `spectrogram-${workspaceNextDisplayId++}`;
        const numericId = Number(String(this.id).split("-").pop());
        if (Number.isFinite(numericId)) {
          workspaceNextDisplayId = Math.max(workspaceNextDisplayId, numericId + 1);
        }
        this.config = {
          type: "spectrogram",
          name: String(config.name || "Spectrogram"),
          sourceBlockId: config.sourceBlockId || workspaceFftBlocks()[0]?.id || "",
          channel: Number(config.channel ?? 0),
          minimumFrequencyHz: Number(config.minimumFrequencyHz ?? 0),
          maximumFrequencyHz: Number(config.maximumFrequencyHz ?? 0),
          minimumDb: Number(config.minimumDb ?? -100),
          maximumDb: Number(config.maximumDb ?? -20),
          timeWindowSeconds: Number(config.timeWindowSeconds ?? 10),
          colourMap: String(config.colourMap || "heat"),
          scrollMode: String(config.scrollMode || "scroll"),
          frozen: Boolean(config.frozen),
          syncGroup: String(config.syncGroup || "default"),
          cadenceMs: Number(config.cadenceMs ?? 0),
          overlayColour: String(config.overlayColour || "#ffcf40"),
          waveformBlockId: String(config.waveformBlockId || ""),
          overlayBlockIds: Array.isArray(config.overlayBlockIds)
            ? config.overlayBlockIds
            : [],
          wide: Boolean(config.wide),
          heightPx: Number(config.heightPx ?? 300)
        };
        this.columns = [];
        this.overlays = [];
        this.overlayControllers = [];
        this.waveformController = null;
        this.waveformSamples = [];
        this.abortController = null;
        this.renderPending = false;
        this.lastTimeMs = 0;
        this.lastAcceptedTimeMs = 0;
        this.frozenEndMs = Number(config.frozenEndMs || 0);
        this.buildElement();
        this.connect();
        this.connectOverlays();
        this.connectWaveform();
      }

      buildElement() {
        const empty = $("operatorGrid").querySelector(".operator-empty");
        if (empty) {
          empty.remove();
        }
        this.element = document.createElement("article");
        this.element.className = `operator-display${this.config.wide ? " wide" : ""}`;
        this.element.dataset.displayId = this.id;
        this.element.innerHTML = `
          <div class="operator-display-head">
            <strong class="display-title" contenteditable="true"></strong>
            <span class="operator-status">connecting</span>
            <button class="secondary display-fullscreen" title="Toggle full screen">Full screen</button>
            <button class="secondary display-popout" title="Open on another monitor">Pop out</button>
            <button class="secondary display-duplicate" title="Duplicate display">Duplicate</button>
            <button class="secondary display-remove" title="Remove display">Remove</button>
          </div>
          <div class="operator-controls">
            <div><label>FFT source<select class="display-source"></select></label></div>
            <div><label>Channel<input class="display-channel" type="number" min="0" value="${this.config.channel}"></label></div>
            <div><label>Low Hz<input class="display-low" type="number" min="0" value="${this.config.minimumFrequencyHz}"></label></div>
            <div><label>High Hz<input class="display-high" type="number" min="0" value="${this.config.maximumFrequencyHz}"></label></div>
            <div><label>Floor dB<input class="display-min-db" type="number" value="${this.config.minimumDb}"></label></div>
            <div><label>Ceiling dB<input class="display-max-db" type="number" value="${this.config.maximumDb}"></label></div>
            <div><label>Window s<input class="display-window" type="number" min="0.5" step="0.5" value="${this.config.timeWindowSeconds}"></label></div>
            <div><label>Colour<select class="display-colour"><option value="heat">Heat</option><option value="grayscale">Grayscale</option><option value="ocean">Ocean</option></select></label></div>
            <div><label>Mode<select class="display-scroll"><option value="scroll">Scroll</option><option value="wrap">Wrap</option></select></label></div>
            <div><label>Sync group<input class="display-sync-group" value="${this.config.syncGroup}"></label></div>
            <div><label>Cadence ms<input class="display-cadence" type="number" min="0" step="10" value="${this.config.cadenceMs}"></label></div>
            <div><label>Overlay colour<input class="display-overlay-colour" type="color" value="${this.config.overlayColour}"></label></div>
            <div><label>Live<input class="display-live" type="checkbox" ${this.config.frozen ? "" : "checked"}></label></div>
            <div><label>Waveform<select class="display-waveform-source"><option value="">Off</option></select></label></div>
            <div><label>Overlays<select class="display-overlays" multiple size="2"></select></label></div>
            <div><label>Size<select class="display-size"><option value="half">Half</option><option value="wide">Full width</option></select></label></div>
            <div><label>Height px<input class="display-height" type="number" min="180" max="900" step="20" value="${this.config.heightPx}"></label></div>
          </div>
          <canvas width="900" height="300"></canvas>`;
        $("operatorGrid").append(this.element);
        this.canvas = this.element.querySelector("canvas");
        this.status = this.element.querySelector(".operator-status");
        this.title = this.element.querySelector(".display-title");
        this.title.textContent = this.config.name;
        this.canvas.addEventListener("pointermove", (event) => {
          if (!Number.isFinite(this.renderStartMs) ||
              !Number.isFinite(this.renderEndMs)) {
            return;
          }
          const bounds = this.canvas.getBoundingClientRect();
          const x = Math.max(
            0,
            Math.min(1, (event.clientX - bounds.left) / bounds.width));
          const y = Math.max(
            0,
            Math.min(1, (event.clientY - bounds.top) / bounds.height));
          workspaceCursorByGroup.set(this.config.syncGroup, {
            timeMs: this.renderStartMs +
              x * (this.renderEndMs - this.renderStartMs),
            frequencyHz: this.renderHighHz -
              y * (this.renderHighHz - this.renderLowHz),
            sourceBlockId: this.config.sourceBlockId
          });
          for (const display of workspaceDisplays.values()) {
            if (display instanceof WorkspaceSpectrogram &&
                display.config.syncGroup === this.config.syncGroup) {
              display.scheduleRender();
            }
          }
        });
        this.canvas.addEventListener("click", async () => {
          const mark = workspaceCursorByGroup.get(
            this.config.syncGroup);
          const moduleId = $("graphOperatorInput").value;
          if (!mark || !moduleId) {
            this.status.textContent =
              "Add an operator-input module to record display marks";
            return;
          }
          try {
            await requestJson(api(
              `/module-runtime/operator-inputs/${encodeURIComponent(
                moduleId)}/events`), {
              method: "POST",
              headers: { "Content-Type": "application/json" },
              body: JSON.stringify({
                category: "display-mark",
                label:
                  `${this.config.name} mark at ${mark.frequencyHz.toFixed(1)} Hz`,
                notes: `source=${mark.sourceBlockId}; syncGroup=${this.config.syncGroup}`,
                value: mark.frequencyHz,
                timeMs: Math.round(mark.timeMs)
              })
            });
            this.status.textContent = "display mark recorded";
          }
          catch (error) {
            this.status.textContent = `mark failed: ${error}`;
          }
        });
        this.title.addEventListener("blur", () => {
          this.config.name = this.title.textContent.trim() || "Spectrogram";
          this.title.textContent = this.config.name;
          updateWorkspaceTabs(this.id);
        });
        this.sourceSelect = this.element.querySelector(".display-source");
        this.overlaySelect = this.element.querySelector(".display-overlays");
        this.waveformSelect =
          this.element.querySelector(".display-waveform-source");
        this.populateSources();
        this.element.querySelector(".display-size").value =
          this.config.wide ? "wide" : "half";
        this.element.querySelector(".display-colour").value =
          this.config.colourMap;
        this.element.querySelector(".display-scroll").value =
          this.config.scrollMode;
        setWorkspaceDisplayHeight(this, this.config.heightPx);
        this.element.querySelector(".display-remove").addEventListener("click", () => {
          this.destroy();
          workspaceDisplays.delete(this.id);
          workspaceEmptyState();
          updateWorkspaceTabs();
        });
        this.element.querySelector(".display-fullscreen").addEventListener(
          "click",
          () => fullscreenWorkspaceDisplay(this).catch((error) => {
            this.status.textContent = String(error);
          }));
        this.element.querySelector(".display-popout").addEventListener(
          "click",
          () => popOutWorkspaceDisplay(this));
        this.element.querySelector(".display-duplicate").addEventListener("click", () => {
          const duplicate = this.serialize();
          delete duplicate.id;
          duplicate.name = `${this.config.name} copy`;
          addWorkspaceDisplay("spectrogram", duplicate);
        });
        this.sourceSelect.addEventListener("change", () => {
          this.config.sourceBlockId = this.sourceSelect.value;
          const block = this.sourceBlock();
          this.config.maximumFrequencyHz = block ? block.sampleRateHz / 2 : 0;
          this.element.querySelector(".display-high").value =
            this.config.maximumFrequencyHz;
          this.columns = [];
          this.connect();
        });
        this.overlaySelect.addEventListener("change", () => {
          this.config.overlayBlockIds = Array.from(
            this.overlaySelect.selectedOptions,
            (option) => option.value);
          this.overlays = [];
          this.connectOverlays();
        });
        this.waveformSelect.addEventListener("change", () => {
          this.config.waveformBlockId = this.waveformSelect.value;
          this.waveformSamples = [];
          this.connectWaveform();
        });
        const bindNumber = (selector, property, minimum = -Infinity) => {
          this.element.querySelector(selector).addEventListener("change", (event) => {
            const value = Number(event.target.value);
            if (Number.isFinite(value) && value >= minimum) {
              this.config[property] = value;
              this.scheduleRender();
            }
          });
        };
        bindNumber(".display-channel", "channel", 0);
        bindNumber(".display-low", "minimumFrequencyHz", 0);
        bindNumber(".display-high", "maximumFrequencyHz", 0);
        bindNumber(".display-min-db", "minimumDb");
        bindNumber(".display-max-db", "maximumDb");
        bindNumber(".display-window", "timeWindowSeconds", 0.5);
        bindNumber(".display-cadence", "cadenceMs", 0);
        this.element.querySelector(".display-channel").addEventListener(
          "change",
          () => {
            this.columns = [];
            this.waveformSamples = [];
            this.connect();
            this.connectOverlays();
            this.connectWaveform();
          });
        this.element.querySelector(".display-cadence").addEventListener(
          "change",
          () => {
            this.columns = [];
            this.connect();
          });
        this.element.querySelector(".display-colour").addEventListener(
          "change",
          (event) => {
            this.config.colourMap = event.target.value;
            this.scheduleRender();
          });
        this.element.querySelector(".display-scroll").addEventListener(
          "change",
          (event) => {
            this.config.scrollMode = event.target.value;
            this.scheduleRender();
          });
        this.element.querySelector(".display-sync-group").addEventListener(
          "change",
          (event) => {
            this.config.syncGroup =
              event.target.value.trim() || "default";
            this.scheduleRender();
          });
        this.element.querySelector(".display-overlay-colour").addEventListener(
          "change",
          (event) => {
            this.config.overlayColour = event.target.value;
            this.scheduleRender();
          });
        this.element.querySelector(".display-live").addEventListener(
          "change",
          (event) => {
            this.config.frozen = !event.target.checked;
            this.frozenEndMs = this.config.frozen
              ? this.lastTimeMs
              : 0;
            this.scheduleRender();
          });
        this.element.querySelector(".display-height").addEventListener(
          "change",
          (event) => setWorkspaceDisplayHeight(this, event.target.value));
        this.element.querySelector(".display-size").addEventListener("change", (event) => {
          this.config.wide = event.target.value === "wide";
          this.element.classList.toggle("wide", this.config.wide);
          this.scheduleRender();
        });
      }

      populateSources() {
        const prior = this.config.sourceBlockId;
        this.sourceSelect.replaceChildren();
        for (const block of workspaceFftBlocks()) {
          this.sourceSelect.add(new Option(
            `${block.name} · ${(block.sampleRateHz / 1000).toFixed(1)} kHz`,
            block.id));
        }
        if (workspaceFftBlocks().some((block) => block.id === prior)) {
          this.sourceSelect.value = prior;
        }
        else if (prior) {
          this.sourceSelect.add(
            new Option(`Missing source · ${prior}`, prior),
            0);
          this.sourceSelect.value = prior;
          this.status.textContent = "source unavailable";
        }
        else {
          this.config.sourceBlockId = this.sourceSelect.value || "";
        }
        this.overlaySelect.replaceChildren();
        for (const block of workspaceBlocks.filter(
          (candidate) => candidate.dataType === "pamguard.click" ||
                         candidate.capabilities?.includes("detections"))) {
          const option = new Option(block.name, block.id);
          option.selected = this.config.overlayBlockIds.includes(block.id);
          this.overlaySelect.add(option);
        }
        this.config.overlayBlockIds = Array.from(
          this.overlaySelect.selectedOptions,
          (option) => option.value);
        const waveformPrior = this.config.waveformBlockId;
        this.waveformSelect.replaceChildren(new Option("Off", ""));
        for (const raw of workspaceBlocks.filter(
          (candidate) => candidate.dataType === "pamguard.raw-audio")) {
          this.waveformSelect.add(new Option(raw.name, raw.id));
        }
        if (Array.from(this.waveformSelect.options).some(
          (option) => option.value === waveformPrior)) {
          this.waveformSelect.value = waveformPrior;
        }
        else if (waveformPrior) {
          this.waveformSelect.add(
            new Option(`Missing source · ${waveformPrior}`, waveformPrior),
            1);
          this.waveformSelect.value = waveformPrior;
        }
        const block = this.sourceBlock();
        if (block && this.config.maximumFrequencyHz <= 0) {
          this.config.maximumFrequencyHz = block.sampleRateHz / 2;
          this.element.querySelector(".display-high").value =
            this.config.maximumFrequencyHz;
        }
      }

      connectWaveform() {
        this.waveformController?.abort();
        this.waveformController = null;
        if (!this.config.waveformBlockId) {
          this.waveformSamples = [];
          this.scheduleRender();
          return;
        }
        const controller = new AbortController();
        this.waveformController = controller;
        this.readWaveformStream(controller);
      }

      async readWaveformStream(controller) {
        try {
          const response = await fetch(
            api(`/data-blocks/${encodeURIComponent(
              this.config.waveformBlockId)}/stream?history=16&channels=${
                encodeURIComponent(this.config.channel)}`),
            { headers: workspaceAuthHeaders(), signal: controller.signal });
          if (!response.ok || !response.body) {
            throw new Error(`waveform stream unavailable (${response.status})`);
          }
          const reader = response.body.getReader();
          const decoder = new TextDecoder();
          let buffer = "";
          while (!controller.signal.aborted) {
            const { done, value } = await reader.read();
            if (done) break;
            buffer += decoder.decode(value, { stream: true });
            let newline;
            while ((newline = buffer.indexOf("\n")) >= 0) {
              const line = buffer.slice(0, newline).trim();
              buffer = buffer.slice(newline + 1);
              if (!line) continue;
              const unit = JSON.parse(line);
              const payload = unit.payload;
              const channelCount = Number(payload?.channelCount || 0);
              const sourceChannels = Array.isArray(payload?.sourceChannels)
                ? payload.sourceChannels.map(Number)
                : [];
              const payloadChannel = sourceChannels.length
                ? sourceChannels.indexOf(this.config.channel)
                : this.config.channel;
              if (!channelCount || payloadChannel < 0 ||
                  payloadChannel >= channelCount ||
                  !Array.isArray(payload.interleavedPcm)) continue;
              for (let frame = 0;
                   frame * channelCount + payloadChannel <
                     payload.interleavedPcm.length;
                   frame++) {
                this.waveformSamples.push(Number(
                  payload.interleavedPcm[
                    frame * channelCount + payloadChannel]));
              }
              const source = workspaceBlocks.find(
                (candidate) =>
                  candidate.id === this.config.waveformBlockId);
              const capacity = Math.max(
                32,
                Math.ceil((source?.sampleRateHz || 48000) *
                  this.config.timeWindowSeconds));
              if (this.waveformSamples.length > capacity) {
                this.waveformSamples.splice(
                  0,
                  this.waveformSamples.length - capacity);
              }
              this.scheduleRender();
            }
          }
        }
        catch (error) {
          if (!controller.signal.aborted) {
            this.status.textContent = String(error);
          }
        }
      }

      connectOverlays() {
        for (const controller of this.overlayControllers) {
          controller.abort();
        }
        this.overlayControllers = [];
        for (const blockId of this.config.overlayBlockIds) {
          const controller = new AbortController();
          this.overlayControllers.push(controller);
          this.readOverlayStream(blockId, controller);
        }
      }

      async readOverlayStream(blockId, controller) {
        try {
          const response = await fetch(
            api(`/data-blocks/${encodeURIComponent(
              blockId)}/stream?history=100&channels=${
                encodeURIComponent(this.config.channel)}`),
            { headers: workspaceAuthHeaders(), signal: controller.signal });
          if (!response.ok || !response.body) {
            throw new Error(`overlay stream unavailable (${response.status})`);
          }
          const reader = response.body.getReader();
          const decoder = new TextDecoder();
          let buffer = "";
          while (!controller.signal.aborted) {
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
                continue;
              }
              const unit = JSON.parse(line);
              this.overlays.push({
                blockId,
                timeMs: Number(unit.timeMs),
                durationSamples: Number(unit.durationSamples || 0),
                channelBitmap: Number(unit.channelBitmap || 0)
              });
              const cutoff = workspaceSharedTimeMs -
                Math.max(60, this.config.timeWindowSeconds * 3) * 1000;
              while (this.overlays.length > 2 &&
                     this.overlays[0].timeMs < cutoff) {
                this.overlays.shift();
              }
              this.scheduleRender();
            }
          }
        }
        catch (error) {
          if (!controller.signal.aborted) {
            this.status.textContent = String(error);
          }
        }
      }

      sourceBlock() {
        return workspaceBlocks.find((block) => block.id === this.config.sourceBlockId);
      }

      async connect() {
        if (this.abortController) {
          this.abortController.abort();
        }
        if (!this.config.sourceBlockId) {
          this.status.textContent = "no source";
          return;
        }
        const controller = new AbortController();
        this.abortController = controller;
        this.status.textContent = "connecting";
        try {
          const parameters = new URLSearchParams({
            history: "256",
            channels: String(this.config.channel),
            cadenceMs: String(Math.max(0, this.config.cadenceMs || 0))
          });
          const response = await fetch(
            api(`/data-blocks/${encodeURIComponent(
              this.config.sourceBlockId)}/stream?${parameters}`),
            { headers: workspaceAuthHeaders(), signal: controller.signal });
          if (!response.ok || !response.body) {
            throw new Error(`stream unavailable (${response.status})`);
          }
          this.status.textContent = "live";
          const reader = response.body.getReader();
          const decoder = new TextDecoder();
          let buffer = "";
          while (!controller.signal.aborted) {
            const { done, value } = await reader.read();
            if (done) {
              break;
            }
            buffer += decoder.decode(value, { stream: true });
            let newline;
            while ((newline = buffer.indexOf("\n")) >= 0) {
              const line = buffer.slice(0, newline).trim();
              buffer = buffer.slice(newline + 1);
              if (line) {
                this.acceptUnit(JSON.parse(line));
              }
            }
          }
          if (!controller.signal.aborted) {
            this.status.textContent = "stream ended";
          }
        }
        catch (error) {
          if (!controller.signal.aborted) {
            this.status.textContent = String(error);
          }
        }
      }

      acceptUnit(unit) {
        if (unit.sourceBlockId !== this.config.sourceBlockId ||
            unit.payload?.channel !== this.config.channel ||
            !Array.isArray(unit.payload?.magnitudeSquared)) {
          return;
        }
        const timeMs = Number(unit.timeMs);
        this.lastTimeMs = Number.isFinite(timeMs) ? timeMs : this.lastTimeMs;
        if (this.config.cadenceMs > 0 &&
            this.lastAcceptedTimeMs > 0 &&
            this.lastTimeMs - this.lastAcceptedTimeMs <
              this.config.cadenceMs) {
          return;
        }
        this.lastAcceptedTimeMs = this.lastTimeMs;
        workspaceSharedTimeMs = Math.max(workspaceSharedTimeMs, this.lastTimeMs);
        const groupTime =
          workspaceSharedTimeByGroup.get(this.config.syncGroup) || 0;
        workspaceSharedTimeByGroup.set(
          this.config.syncGroup,
          Math.max(groupTime, this.lastTimeMs));
        this.columns.push({
          timeMs: this.lastTimeMs,
          values: unit.payload.magnitudeSquared,
          discontinuity: Boolean(unit.discontinuity)
        });
        const cutoff = this.lastTimeMs - Math.max(60, this.config.timeWindowSeconds * 3) * 1000;
        while (this.columns.length > 2 && this.columns[0].timeMs < cutoff) {
          this.columns.shift();
        }
        this.scheduleRender();
        if ($("workspaceSyncTime").checked) {
          for (const display of workspaceDisplays.values()) {
            if (display !== this &&
                display.config.syncGroup === this.config.syncGroup) {
              display.scheduleRender();
            }
          }
        }
      }

      scheduleRender() {
        if (this.renderPending) {
          return;
        }
        this.renderPending = true;
        requestAnimationFrame(() => {
          this.renderPending = false;
          this.render();
        });
      }

      render() {
        resizeCanvas(this.canvas);
        const ctx = this.canvas.getContext("2d");
        const width = this.canvas.width;
        const height = this.canvas.height;
        ctx.fillStyle = "#071514";
        ctx.fillRect(0, 0, width, height);
        const block = this.sourceBlock();
        if (!block || !this.columns.length) {
          ctx.fillStyle = "#8aa0af";
          ctx.font = `${Math.max(12, width / 75)}px Cascadia Mono, Consolas, monospace`;
          ctx.fillText(block ? "Waiting for FFT frames" : "Select an FFT source", 18, 30);
          return;
        }
        const synchronizedEnd =
          workspaceSharedTimeByGroup.get(this.config.syncGroup) ||
          workspaceSharedTimeMs;
        const liveEnd = $("workspaceSyncTime").checked
          ? synchronizedEnd
          : this.lastTimeMs;
        const endMs = this.config.frozen && this.frozenEndMs
          ? this.frozenEndMs
          : liveEnd;
        const startMs = endMs - this.config.timeWindowSeconds * 1000;
        const visible = this.columns.filter((column) => column.timeMs >= startMs && column.timeMs <= endMs);
        if (!visible.length) {
          return;
        }
        const bins = visible[0].values.length;
        const nyquist = block.sampleRateHz / 2;
        const lowHz = Math.max(0, Math.min(this.config.minimumFrequencyHz, nyquist));
        const highHz = Math.max(lowHz, Math.min(
          this.config.maximumFrequencyHz || nyquist,
          nyquist));
        this.renderStartMs = startMs;
        this.renderEndMs = endMs;
        this.renderLowHz = lowHz;
        this.renderHighHz = highHz;
        const firstBin = Math.max(0, Math.floor(lowHz / nyquist * (bins - 1)));
        const lastBin = Math.min(bins - 1, Math.ceil(highHz / nyquist * (bins - 1)));
        const frequencyBins = Math.max(1, lastBin - firstBin + 1);
        const dbRange = Math.max(1, this.config.maximumDb - this.config.minimumDb);
        const columnWidth = Math.max(1, width / Math.max(1, visible.length));
        for (let index = 0; index < visible.length; index++) {
          const column = visible[index];
          const values = column.values;
          const timeFraction = (column.timeMs - startMs) /
            Math.max(1, endMs - startMs);
          const x = this.config.scrollMode === "wrap"
            ? ((column.timeMs / 1000) % this.config.timeWindowSeconds) /
              this.config.timeWindowSeconds * width
            : timeFraction * width;
          for (let bin = firstBin; bin <= lastBin; bin++) {
            const db = 10 * Math.log10(Math.max(Number(values[bin]), 1e-12));
            const level = Math.max(
              0,
              Math.min(1, (db - this.config.minimumDb) / dbRange));
            const [red, green, blue] =
              this.config.colourMap === "grayscale"
                ? [level * 255, level * 255, level * 255]
                : this.config.colourMap === "ocean"
                  ? [level * 40, level * 220, 80 + level * 175]
                  : heatRgb(level);
            const y = lastBin - bin;
            ctx.fillStyle = `rgb(${red},${green},${blue})`;
            ctx.fillRect(
              Math.floor(x),
              Math.floor(y * height / frequencyBins),
              Math.ceil(columnWidth),
              Math.ceil(height / frequencyBins));
          }
          if (column.discontinuity) {
            ctx.strokeStyle = "#ff6b6b";
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, height);
            ctx.stroke();
          }
        }
        const overlayColours = ["#ffcf40", "#ff6b6b", "#55e6c1", "#c792ea"];
        for (const overlay of this.overlays) {
          if (overlay.timeMs < startMs || overlay.timeMs > endMs) {
            continue;
          }
          const x = (overlay.timeMs - startMs) /
            Math.max(1, endMs - startMs) * width;
          const colourIndex = Math.max(
            0,
            this.config.overlayBlockIds.indexOf(overlay.blockId));
          ctx.strokeStyle = colourIndex === 0
            ? this.config.overlayColour
            : overlayColours[colourIndex % overlayColours.length];
          ctx.lineWidth = Math.max(1, window.devicePixelRatio || 1);
          ctx.beginPath();
          ctx.moveTo(x, 0);
          ctx.lineTo(x, height);
          ctx.stroke();
        }
        if (this.waveformSamples.length) {
          ctx.fillStyle = "rgba(7, 21, 20, 0.72)";
          ctx.fillRect(0, height * 0.72, width, height * 0.28);
          ctx.strokeStyle = "#e8f0f2";
          ctx.lineWidth = Math.max(1, window.devicePixelRatio || 1);
          ctx.beginPath();
          const stride = Math.max(
            1,
            Math.floor(this.waveformSamples.length / width));
          for (let x = 0; x < width; x++) {
            const sample = this.waveformSamples[Math.min(
              this.waveformSamples.length - 1,
              x * stride)];
            const y = height * 0.86 -
              Math.max(-1, Math.min(1, sample)) * height * 0.12;
            if (x === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
          }
          ctx.stroke();
        }
        const cursor = workspaceCursorByGroup.get(
          this.config.syncGroup);
        if (cursor &&
            cursor.timeMs >= startMs &&
            cursor.timeMs <= endMs) {
          const cursorX = (cursor.timeMs - startMs) /
            Math.max(1, endMs - startMs) * width;
          ctx.strokeStyle = "#f5f7fa";
          ctx.lineWidth = Math.max(1, window.devicePixelRatio || 1);
          ctx.beginPath();
          ctx.moveTo(cursorX, 0);
          ctx.lineTo(cursorX, height);
          ctx.stroke();
          ctx.fillStyle = "#f5f7fa";
          ctx.font = "11px Cascadia Mono, Consolas, monospace";
          ctx.fillText(
            `${new Date(cursor.timeMs).toISOString().slice(11, 23)} · ` +
            `${cursor.frequencyHz.toFixed(1)} Hz`,
            Math.min(width - 190, cursorX + 5),
            16);
        }
        this.status.textContent =
          `${(lowHz / 1000).toFixed(1)}–${(highHz / 1000).toFixed(1)} kHz · ` +
          `${visible.length} frames · ch ${this.config.channel}` +
          `${this.config.frozen ? " · frozen" : ""}` +
          ` · sync ${this.config.syncGroup}`;
      }

      serialize() {
        return { id: this.id, ...this.config };
      }

      destroy() {
        if (this.popoutWindow && !this.popoutWindow.closed) {
          this.popoutWindow.close();
        }
        if (this.abortController) {
          this.abortController.abort();
          this.abortController = null;
        }
        for (const controller of this.overlayControllers) {
          controller.abort();
        }
        this.overlayControllers = [];
        this.waveformController?.abort();
        this.waveformController = null;
        this.element.remove();
      }
    }

    function addWorkspaceSpectrogram(config = {}) {
      const display = new WorkspaceSpectrogram(config);
      workspaceDisplays.set(display.id, display);
      updateWorkspaceTabs(display.id);
      return display;
    }

    function firstNumericPayloadValue(value) {
      if (typeof value === "number" && Number.isFinite(value)) return value;
      if (Array.isArray(value)) {
        for (const item of value) {
          const found = firstNumericPayloadValue(item);
          if (found !== null) return found;
        }
      }
      else if (value && typeof value === "object") {
        for (const [key, item] of Object.entries(value)) {
          if (["channel", "startSample", "endSample", "timeMs",
               "durationSamples", "clickIndex", "trainId"].includes(key)) {
            continue;
          }
          const found = firstNumericPayloadValue(item);
          if (found !== null) return found;
        }
      }
      return null;
    }

    class WorkspaceBlockDisplay {
      constructor(type, config = {}) {
        this.type = type;
        this.id = config.id || `${type}-${workspaceNextDisplayId++}`;
        const labels = {
          events: "Click / event list",
          waveform: "Raw waveform",
          level: "Level meter",
          timeplot: "Generic time plot"
        };
        this.config = {
          type,
          name: String(config.name || labels[type]),
          sourceBlockId: String(config.sourceBlockId || ""),
          channel: Number(config.channel ?? 0),
          timeWindowSeconds: Number(config.timeWindowSeconds ?? 5),
          wide: Boolean(config.wide),
          heightPx: Number(config.heightPx ?? 300)
        };
        this.abortController = null;
        this.units = [];
        this.samples = [];
        this.points = [];
        this.levelMeasurements = [];
        this.renderPending = false;
        this.buildElement();
        this.connect();
      }

      compatibleBlocks() {
        if (this.type === "waveform") {
          return workspaceBlocks.filter(
            (block) => block.dataType === "pamguard.raw-audio");
        }
        if (this.type === "level") {
          return workspaceBlocks.filter((block) =>
            block.dataType === "pamguard.raw-audio" ||
            block.dataType === "pamguard.level-measurement");
        }
        if (this.type === "timeplot") {
          return workspaceBlocks.filter((block) =>
            block.capabilities?.some((capability) =>
              ["timeseries", "measurements"].includes(capability)) ||
            ["pamguard.click-trigger-background",
             "pamguard.click-trigger-function",
             "pamguard.fft-noise",
             "pamguard.noise-band",
             "pamguard.ishmael-function"].includes(block.dataType));
        }
        return workspaceBlocks.filter((block) =>
          block.dataType === "pamguard.click" ||
          block.capabilities?.some((capability) =>
            ["detections", "annotations", "grouped", "measurements"]
              .includes(capability)));
      }

      buildElement() {
        $("operatorGrid").querySelector(".operator-empty")?.remove();
        this.element = document.createElement("article");
        this.element.className =
          `operator-display${this.config.wide ? " wide" : ""}`;
        this.element.dataset.displayId = this.id;
        const sourceLabel =
          this.type === "events" ? "Event source" :
          this.type === "timeplot" ? "Time-series source" :
          this.type === "level" ? "Level source" :
          "Raw source";
        const body = this.type === "events"
          ? `<div class="operator-event-list"></div>`
          : `<canvas width="900" height="300"></canvas>`;
        this.element.innerHTML = `
          <div class="operator-display-head">
            <strong class="display-title" contenteditable="true"></strong>
            <span class="operator-status">connecting</span>
            <button class="secondary display-fullscreen">Full screen</button>
            <button class="secondary display-popout">Pop out</button>
            <button class="secondary display-duplicate">Duplicate</button>
            <button class="secondary display-remove">Remove</button>
          </div>
          <div class="operator-controls">
            <div><label>${sourceLabel}<select class="display-source"></select></label></div>
            <div><label>Channel<input class="display-channel" type="number" min="0" value="${this.config.channel}"></label></div>
            <div><label>Window s<input class="display-window" type="number" min="0.5" step="0.5" value="${this.config.timeWindowSeconds}"></label></div>
            <div><label>Size<select class="display-size"><option value="half">Half</option><option value="wide">Full width</option></select></label></div>
            <div><label>Height px<input class="display-height" type="number" min="180" max="900" step="20" value="${this.config.heightPx}"></label></div>
          </div>
          ${body}`;
        $("operatorGrid").append(this.element);
        this.status = this.element.querySelector(".operator-status");
        this.sourceSelect = this.element.querySelector(".display-source");
        this.canvas = this.element.querySelector("canvas");
        this.list = this.element.querySelector(".operator-event-list");
        const title = this.element.querySelector(".display-title");
        title.textContent = this.config.name;
        title.addEventListener("blur", () => {
          this.config.name = title.textContent.trim() || this.type;
          title.textContent = this.config.name;
          updateWorkspaceTabs(this.id);
        });
        this.populateSources();
        this.element.querySelector(".display-size").value =
          this.config.wide ? "wide" : "half";
        setWorkspaceDisplayHeight(this, this.config.heightPx);
        this.element.querySelector(".display-remove").addEventListener(
          "click",
          () => {
            this.destroy();
            workspaceDisplays.delete(this.id);
            workspaceEmptyState();
            updateWorkspaceTabs();
          });
        this.element.querySelector(".display-fullscreen").addEventListener(
          "click",
          () => fullscreenWorkspaceDisplay(this).catch((error) => {
            this.status.textContent = String(error);
          }));
        this.element.querySelector(".display-popout").addEventListener(
          "click",
          () => popOutWorkspaceDisplay(this));
        this.element.querySelector(".display-duplicate").addEventListener(
          "click",
          () => {
            const duplicate = this.serialize();
            delete duplicate.id;
            duplicate.name = `${this.config.name} copy`;
            addWorkspaceDisplay(this.type, duplicate);
          });
        this.sourceSelect.addEventListener("change", () => {
          this.config.sourceBlockId = this.sourceSelect.value;
          this.units = [];
          this.samples = [];
          this.points = [];
          this.levelMeasurements = [];
          this.connect();
        });
        this.element.querySelector(".display-channel").addEventListener(
          "change",
          (event) => {
            this.config.channel = Math.max(0, Number(event.target.value) || 0);
            this.samples = [];
            this.connect();
          });
        this.element.querySelector(".display-window").addEventListener(
          "change",
          (event) => {
            this.config.timeWindowSeconds = Math.max(
              0.5,
              Number(event.target.value) || 5);
            this.scheduleRender();
          });
        this.element.querySelector(".display-size").addEventListener(
          "change",
          (event) => {
            this.config.wide = event.target.value === "wide";
            this.element.classList.toggle("wide", this.config.wide);
            this.scheduleRender();
          });
        this.element.querySelector(".display-height").addEventListener(
          "change",
          (event) => setWorkspaceDisplayHeight(this, event.target.value));
      }

      populateSources() {
        const blocks = this.compatibleBlocks();
        const prior = this.config.sourceBlockId;
        this.sourceSelect.replaceChildren();
        for (const block of blocks) {
          this.sourceSelect.add(new Option(block.name, block.id));
        }
        if (blocks.some((block) => block.id === prior)) {
          this.sourceSelect.value = prior;
        }
        else if (prior) {
          this.sourceSelect.add(
            new Option(`Missing source · ${prior}`, prior),
            0);
          this.sourceSelect.value = prior;
          this.status.textContent = "source unavailable";
        }
        else {
          this.config.sourceBlockId = this.sourceSelect.value || "";
        }
      }

      async connect() {
        this.abortController?.abort();
        if (!this.config.sourceBlockId) {
          this.status.textContent = "no compatible source";
          this.scheduleRender();
          return;
        }
        const controller = new AbortController();
        this.abortController = controller;
        this.status.textContent = "backfilling";
        try {
          const parameters = new URLSearchParams({ history: "100" });
          if (this.type === "waveform" ||
              this.type === "level" ||
              this.type === "events") {
            parameters.set("channels", String(this.config.channel));
          }
          const response = await fetch(
            api(`/data-blocks/${encodeURIComponent(
              this.config.sourceBlockId)}/stream?${parameters}`),
            { headers: workspaceAuthHeaders(), signal: controller.signal });
          if (!response.ok || !response.body) {
            throw new Error(`stream unavailable (${response.status})`);
          }
          this.status.textContent = "live";
          const reader = response.body.getReader();
          const decoder = new TextDecoder();
          let buffer = "";
          while (!controller.signal.aborted) {
            const { done, value } = await reader.read();
            if (done) break;
            buffer += decoder.decode(value, { stream: true });
            let newline;
            while ((newline = buffer.indexOf("\n")) >= 0) {
              const line = buffer.slice(0, newline).trim();
              buffer = buffer.slice(newline + 1);
              if (line) this.acceptUnit(JSON.parse(line));
            }
          }
          if (!controller.signal.aborted) this.status.textContent = "source ended";
        }
        catch (error) {
          if (!controller.signal.aborted) this.status.textContent = String(error);
        }
      }

      acceptUnit(unit) {
        if (unit.sourceBlockId !== this.config.sourceBlockId) return;
        if (this.type === "events") {
          const bitmap = Number(unit.channelBitmap || 0);
          if (bitmap && this.config.channel < 32 &&
              (bitmap & (1 << this.config.channel)) === 0) return;
          this.units.push(unit);
          if (this.units.length > 200) this.units.shift();
        }
        else if (this.type === "timeplot") {
          const value = firstNumericPayloadValue(unit.payload);
          if (value === null) return;
          const timeMs = Number(unit.timeMs);
          this.points.push({
            timeMs: Number.isFinite(timeMs) ? timeMs : Date.now(),
            value
          });
          const cutoff = this.points.at(-1).timeMs -
            Math.max(1, this.config.timeWindowSeconds) * 3000;
          while (this.points.length > 2 &&
                 this.points[0].timeMs < cutoff) {
            this.points.shift();
          }
        }
        else if (this.type === "level" &&
                 unit.typeId === "pamguard.level-measurement") {
          const rmsDbfs = Number(
            unit.payload?.rmsDbfs?.[this.config.channel]);
          const peakDbfs = Number(
            unit.payload?.peakDbfs?.[this.config.channel]);
          if (!Number.isFinite(rmsDbfs) || !Number.isFinite(peakDbfs)) {
            return;
          }
          this.levelMeasurements.push({
            timeMs: Number.isFinite(Number(unit.timeMs))
              ? Number(unit.timeMs)
              : Date.now(),
            rmsDbfs,
            peakDbfs
          });
          if (this.levelMeasurements.length > 1000) {
            this.levelMeasurements.shift();
          }
        }
        else {
          const payload = unit.payload;
          const channels = Number(payload?.channelCount || 0);
          const sourceChannels = Array.isArray(payload?.sourceChannels)
            ? payload.sourceChannels.map(Number)
            : [];
          const payloadChannel = sourceChannels.length
            ? sourceChannels.indexOf(this.config.channel)
            : this.config.channel;
          if (!channels || payloadChannel < 0 ||
              payloadChannel >= channels ||
              !Array.isArray(payload.interleavedPcm)) return;
          for (let frame = 0;
               frame * channels + payloadChannel <
                 payload.interleavedPcm.length;
               frame++) {
            this.samples.push(Number(
              payload.interleavedPcm[frame * channels + payloadChannel]));
          }
          const block = workspaceBlocks.find(
            (candidate) => candidate.id === this.config.sourceBlockId);
          const capacity = Math.max(
            32,
            Math.ceil((block?.sampleRateHz || 48000) *
              this.config.timeWindowSeconds));
          if (this.samples.length > capacity) {
            this.samples.splice(0, this.samples.length - capacity);
          }
        }
        this.scheduleRender();
      }

      scheduleRender() {
        if (this.renderPending) return;
        this.renderPending = true;
        requestAnimationFrame(() => {
          this.renderPending = false;
          this.render();
        });
      }

      render() {
        if (this.type === "events") {
          this.list.replaceChildren(...this.units.slice(-100).reverse().map(
            (unit) => {
              const row = document.createElement("div");
              row.className = "operator-event-row";
              const time = document.createElement("span");
              time.textContent = Number.isFinite(Number(unit.timeMs))
                ? new Date(Number(unit.timeMs)).toISOString().slice(11, 23)
                : "sample";
              const kind = document.createElement("span");
              kind.textContent = String(unit.typeId || "event")
                .replace("pamguard.", "");
              const summary = document.createElement("code");
              summary.textContent =
                `sample ${unit.startSample} · ${JSON.stringify(unit.payload)}`;
              row.append(time, kind, summary);
              return row;
            }));
          this.status.textContent =
            `${this.units.length} retained event${this.units.length === 1 ? "" : "s"}`;
          return;
        }
        resizeCanvas(this.canvas);
        const context = this.canvas.getContext("2d");
        const width = this.canvas.width;
        const height = this.canvas.height;
        context.fillStyle = "#071514";
        context.fillRect(0, 0, width, height);
        if (this.type === "level" && this.levelMeasurements.length) {
          const latest = this.levelMeasurements.at(-1);
          const meterWidth = Math.max(1, width - 80);
          const normalizedRms = Math.max(
            0,
            Math.min(1, (latest.rmsDbfs + 100) / 100));
          context.fillStyle = "#20353a";
          context.fillRect(40, height / 2 - 24, meterWidth, 48);
          context.fillStyle =
            latest.peakDbfs > -1 ? "#ff6b6b" : "#55e6c1";
          context.fillRect(
            40,
            height / 2 - 24,
            meterWidth * normalizedRms,
            48);
          context.fillStyle = "#e8f0f2";
          context.font = "14px Cascadia Mono, Consolas, monospace";
          context.fillText(
            `RMS ${latest.rmsDbfs.toFixed(1)} dBFS  ` +
            `Peak ${latest.peakDbfs.toFixed(1)} dBFS`,
            40,
            height / 2 + 58);
          this.status.textContent =
            `ch ${this.config.channel} · typed level · ` +
            `${latest.rmsDbfs.toFixed(1)} dBFS`;
          return;
        }
        if (this.type === "timeplot") {
          if (!this.points.length) {
            context.fillStyle = "#8aa0af";
            context.fillText("Waiting for numeric samples", 18, 30);
            return;
          }
          const end = this.points.at(-1).timeMs;
          const start = end - this.config.timeWindowSeconds * 1000;
          const visible = this.points.filter((point) => point.timeMs >= start);
          const values = visible.map((point) => point.value);
          const minimum = Math.min(...values);
          const maximum = Math.max(...values);
          const span = Math.max(1e-12, maximum - minimum);
          context.strokeStyle = "#55e6c1";
          context.lineWidth = Math.max(1, window.devicePixelRatio || 1);
          context.beginPath();
          visible.forEach((point, index) => {
            const x = (point.timeMs - start) /
              Math.max(1, end - start) * width;
            const y = height - 12 -
              (point.value - minimum) / span * (height - 24);
            if (index === 0) context.moveTo(x, y);
            else context.lineTo(x, y);
          });
          context.stroke();
          context.fillStyle = "#e8f0f2";
          context.font = "12px Cascadia Mono, Consolas, monospace";
          context.fillText(
            `${minimum.toPrecision(4)} … ${maximum.toPrecision(4)} · ` +
            `${visible.length} samples`,
            12,
            18);
          this.status.textContent =
            `${visible.length} numeric samples · latest ${values.at(-1).toPrecision(5)}`;
          return;
        }
        if (!this.samples.length) {
          context.fillStyle = "#8aa0af";
          context.fillText("Waiting for raw audio", 18, 30);
          return;
        }
        let sum = 0;
        let peak = 0;
        for (const value of this.samples) {
          sum += value * value;
          peak = Math.max(peak, Math.abs(value));
        }
        const rms = Math.sqrt(sum / this.samples.length);
        if (this.type === "level") {
          const meterWidth = Math.max(1, width - 80);
          context.fillStyle = "#20353a";
          context.fillRect(40, height / 2 - 24, meterWidth, 48);
          context.fillStyle = peak > 0.9 ? "#ff6b6b" : "#55e6c1";
          context.fillRect(
            40,
            height / 2 - 24,
            meterWidth * Math.min(1, rms),
            48);
          context.fillStyle = "#e8f0f2";
          context.font = "14px Cascadia Mono, Consolas, monospace";
          context.fillText(
            `RMS ${20 * Math.log10(Math.max(rms, 1e-12)).toFixed(1)} dBFS  ` +
            `Peak ${20 * Math.log10(Math.max(peak, 1e-12)).toFixed(1)} dBFS`,
            40,
            height / 2 + 58);
        }
        else {
          context.strokeStyle = "#55e6c1";
          context.lineWidth = Math.max(1, window.devicePixelRatio || 1);
          context.beginPath();
          const stride = Math.max(1, Math.floor(this.samples.length / width));
          for (let x = 0; x < width; x++) {
            const index = Math.min(
              this.samples.length - 1,
              x * stride);
            const y = height / 2 -
              Math.max(-1, Math.min(1, this.samples[index])) * height * 0.45;
            if (x === 0) context.moveTo(x, y);
            else context.lineTo(x, y);
          }
          context.stroke();
        }
        this.status.textContent =
          `ch ${this.config.channel} · RMS ${(
            20 * Math.log10(Math.max(rms, 1e-12))).toFixed(1)} dBFS`;
      }

      serialize() {
        return { id: this.id, ...this.config };
      }

      destroy() {
        if (this.popoutWindow && !this.popoutWindow.closed) {
          this.popoutWindow.close();
        }
        this.abortController?.abort();
        this.element.remove();
      }
    }

    class WorkspaceRuntimeDisplay {
      constructor(type, config = {}) {
        this.type = type;
        this.id = config.id || `${type}-${workspaceNextDisplayId++}`;
        this.config = {
          type,
          name: String(config.name ||
            (type === "status" ? "Module status" : "Data map")),
          syncGroup: String(config.syncGroup || "default"),
          wide: Boolean(config.wide),
          heightPx: Number(config.heightPx ?? 300)
        };
        this.destroyed = false;
        this.buildElement();
        this.refresh();
        this.timer = window.setInterval(() => this.refresh(), 2000);
      }

      buildElement() {
        $("operatorGrid").querySelector(".operator-empty")?.remove();
        this.element = document.createElement("article");
        this.element.className =
          `operator-display${this.config.wide ? " wide" : ""}`;
        this.element.dataset.displayId = this.id;
        this.element.innerHTML = `
          <div class="operator-display-head">
            <strong class="display-title" contenteditable="true"></strong>
            <span class="operator-status">loading</span>
            <button class="secondary display-fullscreen">Full screen</button>
            <button class="secondary display-popout">Pop out</button>
            <button class="secondary display-duplicate">Duplicate</button>
            <button class="secondary display-remove">Remove</button>
          </div>
          <div class="operator-controls">
            <div><label>Size<select class="display-size"><option value="half">Half</option><option value="wide">Full width</option></select></label></div>
            <div><label>Height px<input class="display-height" type="number" min="180" max="900" step="20" value="${this.config.heightPx}"></label></div>
            ${this.type === "datamap"
              ? `<div><label>Sync group<input class="display-sync-group" value="${this.config.syncGroup}"></label></div>`
              : ""}
            <button class="secondary display-refresh">Refresh now</button>
          </div>
          <div class="operator-event-list"></div>`;
        $("operatorGrid").append(this.element);
        this.status = this.element.querySelector(".operator-status");
        this.list = this.element.querySelector(".operator-event-list");
        const title = this.element.querySelector(".display-title");
        title.textContent = this.config.name;
        title.addEventListener("blur", () => {
          this.config.name = title.textContent.trim() || this.type;
          title.textContent = this.config.name;
          updateWorkspaceTabs(this.id);
        });
        this.element.querySelector(".display-size").value =
          this.config.wide ? "wide" : "half";
        setWorkspaceDisplayHeight(this, this.config.heightPx);
        this.element.querySelector(".display-size").addEventListener(
          "change",
          (event) => {
            this.config.wide = event.target.value === "wide";
            this.element.classList.toggle("wide", this.config.wide);
          });
        this.element.querySelector(".display-height").addEventListener(
          "change",
          (event) => setWorkspaceDisplayHeight(this, event.target.value));
        this.element.querySelector(".display-sync-group")?.addEventListener(
          "change",
          (event) => {
            this.config.syncGroup =
              event.target.value.trim() || "default";
            event.target.value = this.config.syncGroup;
          });
        this.element.querySelector(".display-refresh").addEventListener(
          "click",
          () => this.refresh());
        this.element.querySelector(".display-fullscreen").addEventListener(
          "click",
          () => fullscreenWorkspaceDisplay(this).catch((error) => {
            this.status.textContent = String(error);
          }));
        this.element.querySelector(".display-popout").addEventListener(
          "click",
          () => popOutWorkspaceDisplay(this));
        this.element.querySelector(".display-duplicate").addEventListener(
          "click",
          () => {
            const duplicate = this.serialize();
            delete duplicate.id;
            duplicate.name = `${this.config.name} copy`;
            addWorkspaceDisplay(this.type, duplicate);
          });
        this.element.querySelector(".display-remove").addEventListener(
          "click",
          () => {
            this.destroy();
            workspaceDisplays.delete(this.id);
            workspaceEmptyState();
            updateWorkspaceTabs();
          });
      }

      async refresh() {
        try {
          const runtime = await requestJson(api("/module-runtime/status"));
          if (this.destroyed) return;
          const rows = [];
          if (this.type === "status") {
            for (const module of runtime.modules || []) {
              rows.push({
                first: module.state,
                second: module.name,
                moduleId: module.moduleId,
                text: `${module.typeId} · ${module.outputs.length} output block(s)`
              });
            }
          }
          else {
            for (const module of runtime.modules || []) {
              for (const output of module.outputs || []) {
                const stats = output.stats || {};
                rows.push({
                  first: output.dataType.replace("pamguard.", ""),
                  second: output.name,
                  blockId: output.id,
                  oldestTimeMs: output.oldestTimeMs == null
                    ? Number.NaN
                    : Number(output.oldestTimeMs),
                  latestTimeMs: output.latestTimeMs == null
                    ? Number.NaN
                    : Number(output.latestTimeMs),
                  text:
                    `published ${stats.published || 0} · history ${stats.historySize || 0}` +
                    ` · subscribers ${stats.subscriberCount || 0}` +
                    ` · dropped ${stats.dropped || 0}` +
                    ` · queued ${stats.queuedUnits || 0}` +
                    (output.latestTimeMs != null
                      ? ` · ${new Date(Number(output.oldestTimeMs)).toISOString()} → ` +
                        new Date(Number(output.latestTimeMs)).toISOString()
                      : "")
                });
              }
            }
          }
          this.list.replaceChildren(...rows.map((entry) => {
            const row = document.createElement("div");
            row.className = "operator-event-row";
            const first = document.createElement("span");
            first.textContent = entry.first;
            const second = document.createElement("span");
            second.textContent = entry.second;
            const text = document.createElement("code");
            text.textContent = entry.text;
            row.append(first, second, text);
            if (this.type === "status" && entry.moduleId) {
              row.tabIndex = 0;
              row.title = "Open this module in the graph editor";
              const openModule = () => {
                displayCallbacks.activateTab("graph");
                window.setTimeout(() => {
                  displayCallbacks.focusControlledUnit(
                    entry.moduleId);
                }, 300);
              };
              row.addEventListener("click", openModule);
              row.addEventListener("keydown", (event) => {
                if (event.key === "Enter" || event.key === " ") {
                  event.preventDefault();
                  openModule();
                }
              });
            }
            else if (this.type === "datamap" &&
                Number.isFinite(entry.latestTimeMs)) {
              row.tabIndex = 0;
              row.title =
                `Navigate synchronized displays to ${new Date(
                  entry.latestTimeMs).toISOString()}`;
              const navigate = () => {
                workspaceSharedTimeByGroup.set(
                  this.config.syncGroup,
                  entry.latestTimeMs);
                workspaceCursorByGroup.set(
                  this.config.syncGroup,
                  {
                    timeMs: entry.latestTimeMs,
                    frequencyHz: 0,
                    sourceBlockId: entry.blockId
                  });
                for (const display of workspaceDisplays.values()) {
                  if (display instanceof WorkspaceSpectrogram &&
                      display.config.syncGroup ===
                        this.config.syncGroup) {
                    display.config.frozen = true;
                    display.frozenEndMs = entry.latestTimeMs;
                    const live =
                      display.element.querySelector(".display-live");
                    if (live) live.checked = false;
                    display.scheduleRender();
                  }
                }
                this.status.textContent =
                  `navigated ${this.config.syncGroup} to ` +
                  new Date(entry.latestTimeMs).toISOString();
              };
              row.addEventListener("click", navigate);
              row.addEventListener("keydown", (event) => {
                if (event.key === "Enter" || event.key === " ") {
                  event.preventDefault();
                  navigate();
                }
              });
            }
            return row;
          }));
          this.status.textContent =
            `graph r${runtime.graphRevision} · ` +
            `${runtime.running ? "running" : "stopped"} · ${rows.length} rows`;
        }
        catch (error) {
          if (!this.destroyed) this.status.textContent = String(error);
        }
      }

      populateSources() {}
      connect() { this.refresh(); }
      scheduleRender() {}
      serialize() { return { id: this.id, ...this.config }; }

      destroy() {
        this.destroyed = true;
        window.clearInterval(this.timer);
        if (this.popoutWindow && !this.popoutWindow.closed) {
          this.popoutWindow.close();
        }
        this.element.remove();
      }
    }

    const workspaceDisplayProviders = new Map([
      ["spectrogram", {
        name: "Spectrogram",
        create: (config) => new WorkspaceSpectrogram(config)
      }],
      ["events", {
        name: "Click / event list",
        create: (config) => new WorkspaceBlockDisplay("events", config)
      }],
      ["waveform", {
        name: "Raw waveform",
        create: (config) => new WorkspaceBlockDisplay("waveform", config)
      }],
      ["level", {
        name: "Level meter",
        create: (config) => new WorkspaceBlockDisplay("level", config)
      }],
      ["timeplot", {
        name: "Generic time plot",
        create: (config) => new WorkspaceBlockDisplay("timeplot", config)
      }],
      ["status", {
        name: "Module status",
        create: (config) => new WorkspaceRuntimeDisplay("status", config)
      }],
      ["datamap", {
        name: "Data map / block health",
        create: (config) => new WorkspaceRuntimeDisplay("datamap", config)
      }]
    ]);
    $("workspaceDisplayType").replaceChildren(
      ...Array.from(
        workspaceDisplayProviders,
        ([type, provider]) => new Option(provider.name, type)));

    function addWorkspaceDisplay(type, config = {}) {
      const provider = workspaceDisplayProviders.get(type);
      if (!provider) {
        throw new Error(`Unknown workspace display type: ${type}`);
      }
      const display = provider.create(config);
      workspaceDisplays.set(display.id, display);
      updateWorkspaceTabs(display.id);
      return display;
    }

    const workspaceAudioMonitor = {
      context: null,
      node: null,
      abortController: null,
      sourceBlockId: "",
      selectedChannels: [],
      lastSourceLatencyMs: 0,
      serverDroppedFrames: 0,

      rawBlocks() {
        return workspaceBlocks.filter((block) => block.dataType === "pamguard.raw-audio");
      },

      async populateDevices() {
        const select = $("workspaceAudioDevice");
        const prior = select.value;
        const options = [new Option("System default", "")];
        if (navigator.mediaDevices?.enumerateDevices) {
          const devices = await navigator.mediaDevices.enumerateDevices();
          for (const device of devices.filter(
            (candidate) => candidate.kind === "audiooutput")) {
            options.push(new Option(
              device.label || `Audio output ${options.length}`,
              device.deviceId));
          }
        }
        select.replaceChildren(...options);
        if (Array.from(select.options).some(
          (option) => option.value === prior)) {
          select.value = prior;
        }
      },

      populateSources() {
        const select = $("workspaceAudioSource");
        const prior = select.value || this.sourceBlockId;
        select.replaceChildren();
        for (const block of this.rawBlocks()) {
          select.add(new Option(
            `${block.name} · ${(block.sampleRateHz / 1000).toFixed(1)} kHz`,
            block.id));
        }
        if (this.rawBlocks().some((block) => block.id === prior)) {
          select.value = prior;
        }
        else if (prior) {
          select.add(
            new Option(`Missing source · ${prior}`, prior),
            0);
          select.value = prior;
          $("workspaceAudioStatus").textContent =
            "selected source unavailable";
        }
        this.sourceBlockId = select.value || "";
      },

      parseChannels(block) {
        const values = $("workspaceAudioChannels").value
          .split(",")
          .map((value) => Number(value.trim()))
          .filter((value) => Number.isInteger(value) && value >= 0);
        const maximumChannel = (() => {
          let highest = -1;
          for (let channel = 0; channel < 32; channel++) {
            if ((block.channelBitmap & (2 ** channel)) !== 0) {
              highest = channel;
            }
          }
          return highest;
        })();
        const unique = [...new Set(values)];
        if (!unique.length || unique.some(
          (channel) =>
            channel > maximumChannel ||
            (block.channelBitmap & (2 ** channel)) === 0)) {
          throw new Error(
            `Choose available source channels from bitmap ${block.channelBitmap}`);
        }
        return unique;
      },

      async start() {
        await this.stop();
        const block = this.rawBlocks().find(
          (candidate) => candidate.id === $("workspaceAudioSource").value);
        if (!block) {
          throw new Error("Select a raw-audio data block");
        }
        const channels = this.parseChannels(block);
        const gain = Number($("workspaceAudioGain").value);
        if (!Number.isFinite(gain) || gain < 0) {
          throw new Error("Audio gain must be zero or greater");
        }
        const AudioContextClass = window.AudioContext || window.webkitAudioContext;
        if (!AudioContextClass || !window.AudioWorkletNode) {
          throw new Error("This browser does not support AudioWorklet monitoring");
        }
        const requestedOutputRate = Math.max(
          0,
          Number($("workspaceAudioRate").value) || 0);
        const context = new AudioContextClass({
          latencyHint: "interactive",
          sampleRate: requestedOutputRate || block.sampleRateHz
        });
        const outputDevice = $("workspaceAudioDevice").value;
        if (outputDevice && typeof context.setSinkId === "function") {
          await context.setSinkId(outputDevice);
        }
        const processorSource = `
          class PamguardMonitorProcessor extends AudioWorkletProcessor {
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
                if (event.data.type === "gain") {
                  this.gain = event.data.value;
                  return;
                }
                if (event.data.type === "highpass") {
                  this.highPassHz = event.data.value;
                  return;
                }
                if (event.data.type === "latency") {
                  this.latencyFrames = Math.max(
                    128,
                    event.data.value);
                  return;
                }
                const item = event.data;
                const frames = item.samples.length / item.channels;
                this.queue.push(item);
                this.bufferedFrames += frames;
                while (this.bufferedFrames > this.latencyFrames * 2 &&
                       this.queue.length > 1) {
                  const dropped = this.queue.shift();
                  const frames =
                    dropped.samples.length / dropped.channels;
                  this.bufferedFrames -= frames;
                  this.droppedFrames += frames;
                }
              };
            }
            process(inputs, outputs) {
              const output = outputs[0];
              const frames = output[0]?.length || 0;
              for (let frame = 0; frame < frames; frame++) {
                while (!this.current ||
                       this.offset >= this.current.samples.length / this.current.channels) {
                  this.current = this.queue.shift() || null;
                  this.offset = 0;
                  if (!this.current) {
                    break;
                  }
                }
                if (!this.current) {
                  this.underrunFrames++;
                  continue;
                }
                for (let channel = 0; channel < output.length; channel++) {
                  const sourceChannel = Math.min(
                    channel,
                    this.current.channels - 1);
                  const input =
                    this.current.samples[
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
          registerProcessor("pamguard-monitor", PamguardMonitorProcessor);`;
        const workletUrl = URL.createObjectURL(new Blob(
          [processorSource],
          { type: "application/javascript" }));
        try {
          await context.audioWorklet.addModule(workletUrl);
        }
        finally {
          URL.revokeObjectURL(workletUrl);
        }
        const mix = $("workspaceAudioMix").value;
        const outputChannels = mix === "mono"
          ? 1
          : mix === "stereo"
            ? 2
            : channels.length;
        const node = new AudioWorkletNode(context, "pamguard-monitor", {
          numberOfInputs: 0,
          numberOfOutputs: 1,
          outputChannelCount: [outputChannels]
        });
        node.port.postMessage({
          type: "gain",
          value: $("workspaceAudioMute").checked ? 0 : gain
        });
        const latencyMs = Math.max(
          20,
          Math.min(
            2000,
            Number($("workspaceAudioLatency").value) || 100));
        node.port.postMessage({
          type: "latency",
          value: context.sampleRate * latencyMs / 1000
        });
        const highPassHz = Math.max(
          0,
          Number($("workspaceAudioHighPass").value) || 0);
        node.port.postMessage({ type: "highpass", value: highPassHz });
        node.port.onmessage = (event) => {
          if (event.data?.type !== "health") return;
          const bufferedMs = event.data.bufferedFrames /
            context.sampleRate * 1000;
          const outputLatencyMs =
            (Number(context.baseLatency || 0) +
             Number(context.outputLatency || 0)) * 1000;
          const estimatedLatencyMs =
            this.lastSourceLatencyMs +
            bufferedMs +
            outputLatencyMs;
          $("workspaceAudioStatus").textContent =
            `live · ${(context.sampleRate / 1000).toFixed(1)} kHz · ` +
            `~${estimatedLatencyMs.toFixed(0)} ms ingest→output · ` +
            `${bufferedMs.toFixed(0)} ms buffered · ` +
            `${event.data.underrunFrames} underrun frames · ` +
            `${this.serverDroppedFrames} transport / ` +
            `${event.data.droppedFrames} output frames dropped`;
        };
        node.connect(context.destination);
        await context.resume();

        this.context = context;
        this.node = node;
        this.sourceBlockId = block.id;
        this.selectedChannels = channels;
        this.lastSourceLatencyMs = 0;
        this.serverDroppedFrames = 0;
        const controller = new AbortController();
        this.abortController = controller;
        $("workspaceAudioStart").disabled = true;
        $("workspaceAudioStop").disabled = false;
        $("workspaceAudioStatus").textContent =
          `live · ${(context.sampleRate / 1000).toFixed(1)} kHz · ch ${channels.join(",")}`;

        try {
          const response = await fetch(
            api(
              `/data-blocks/${encodeURIComponent(block.id)}/audio-f32le?channels=${encodeURIComponent(channels.join(","))}&format=framed`),
            { headers: workspaceAuthHeaders(), signal: controller.signal });
          if (!response.ok || !response.body) {
            throw new Error(`audio stream unavailable (${response.status})`);
          }
          const reader = response.body.getReader();
          const streamChannels = Number(
            response.headers.get("X-PAMGuard-Channel-Count"));
          if (streamChannels !== channels.length) {
            throw new Error("audio stream channel contract changed");
          }
          if (response.headers.get("X-PAMGuard-Audio-Framing") !== "pga1") {
            throw new Error("audio stream framing contract changed");
          }
          const mixedChannels = outputChannels;
          let byteCarry = new Uint8Array(0);
          let resampleBuffer = [];
          let resamplePosition = 0;
          const sourcePerOutput =
            block.sampleRateHz / context.sampleRate;
          while (!controller.signal.aborted) {
            const { done, value } = await reader.read();
            if (done) {
              break;
            }
            const bytes = new Uint8Array(byteCarry.length + value.length);
            bytes.set(byteCarry);
            bytes.set(value, byteCarry.length);
            let cursor = 0;
            while (bytes.length - cursor >= 40) {
              if (bytes[cursor] !== 0x50 ||
                  bytes[cursor + 1] !== 0x47 ||
                  bytes[cursor + 2] !== 0x41 ||
                  bytes[cursor + 3] !== 0x31) {
                throw new Error("invalid PGA1 audio frame magic");
              }
              const header = new DataView(
                bytes.buffer,
                bytes.byteOffset + cursor,
                40);
              const headerSize = header.getUint32(4, true);
              const packetChannels = header.getUint32(8, true);
              const packetFrames = header.getUint32(12, true);
              if (headerSize !== 40 ||
                  packetChannels !== channels.length ||
                  packetFrames > 10000000) {
                throw new Error("invalid PGA1 audio frame header");
              }
              const packetBytes =
                headerSize + packetFrames * packetChannels * 4;
              if (bytes.length - cursor < packetBytes) {
                break;
              }
              const timeUnixMs = Number(header.getBigInt64(16, true));
              this.lastSourceLatencyMs =
                timeUnixMs > 1000000000000
                ? Math.max(0, Date.now() - timeUnixMs)
                : 0;
              this.serverDroppedFrames = Number(
                header.getBigUint64(32, true));
              const samples = new DataView(
                bytes.buffer,
                bytes.byteOffset + cursor + headerSize,
                packetFrames * packetChannels * 4);
              for (let frameIndex = 0;
                   frameIndex < packetFrames;
                   frameIndex++) {
                const frame = [];
                for (let channel = 0;
                     channel < packetChannels;
                     channel++) {
                  frame.push(samples.getFloat32(
                    (frameIndex * packetChannels + channel) * 4,
                    true));
                }
                if (mix === "mono") {
                  resampleBuffer.push(
                    frame.reduce((sum, sample) => sum + sample, 0) /
                    frame.length);
                }
                else if (mix === "stereo") {
                  resampleBuffer.push(frame[0] || 0);
                  resampleBuffer.push(frame[1] ?? frame[0] ?? 0);
                }
                else {
                  resampleBuffer.push(...frame);
                }
              }
              cursor += packetBytes;
            }
            byteCarry = bytes.slice(cursor);

            const bufferedFrames =
              Math.floor(resampleBuffer.length / mixedChannels);
            const output = [];
            while (resamplePosition + 1 < bufferedFrames) {
              const before = Math.floor(resamplePosition);
              const fraction = resamplePosition - before;
              for (let channel = 0; channel < mixedChannels; channel++) {
                const first =
                  resampleBuffer[before * mixedChannels + channel];
                const second =
                  resampleBuffer[(before + 1) * mixedChannels + channel];
                output.push(first + (second - first) * fraction);
              }
              resamplePosition += sourcePerOutput;
            }
            const consumedFrames = Math.floor(resamplePosition);
            if (consumedFrames > 0) {
              resampleBuffer.splice(
                0,
                consumedFrames * mixedChannels);
              resamplePosition -= consumedFrames;
            }
            if (output.length) {
              const selected = new Float32Array(output);
              node.port.postMessage(
                { samples: selected, channels: mixedChannels },
                [selected.buffer]);
            }
          }
          if (!controller.signal.aborted) {
            throw new Error("audio data-block stream ended");
          }
        }
        catch (error) {
          if (!controller.signal.aborted) {
            $("workspaceAudioStatus").textContent = String(error);
            await this.stop(false);
          }
        }
      },

      async stop(updateStatus = true) {
        if (this.abortController) {
          this.abortController.abort();
          this.abortController = null;
        }
        if (this.node) {
          this.node.disconnect();
          this.node = null;
        }
        if (this.context) {
          await this.context.close();
          this.context = null;
        }
        $("workspaceAudioStart").disabled = false;
        $("workspaceAudioStop").disabled = true;
        if (updateStatus) {
          $("workspaceAudioStatus").textContent = "stopped";
        }
      }
    };

    function currentWorkspaceLayout() {
      return {
        schemaVersion: 1,
        name: $("workspaceName").value.trim() || "Workspace",
        synchronizedTime: $("workspaceSyncTime").checked,
        arrangement: $("workspaceArrangement").value || "grid",
        audio: {
          sourceBlockId: $("workspaceAudioSource").value,
          channels: $("workspaceAudioChannels").value,
          mix: $("workspaceAudioMix").value,
          gain: Number($("workspaceAudioGain").value),
          muted: $("workspaceAudioMute").checked,
          highPassHz: Number($("workspaceAudioHighPass").value),
          outputRateHz: Number($("workspaceAudioRate").value),
          latencyMs: Number($("workspaceAudioLatency").value),
          deviceId: $("workspaceAudioDevice").value
        },
        displays: Array.from(
          workspaceDisplays.values(),
          (display) => display.serialize())
      };
    }

    async function saveWorkspaceLayout() {
      const workspaceId = $("workspaceId").value.trim();
      if (!/^[A-Za-z0-9_.-]{1,128}$/.test(workspaceId)) {
        $("workspaceMeta").textContent =
          "Workspace ID may only contain letters, numbers, dot, dash, and underscore";
        return;
      }
      const layout = {
        ...currentWorkspaceLayout(),
        id: workspaceId
      };
      localStorage.setItem(workspaceStorageKey, JSON.stringify(layout));
      try {
        await requestJson(
          api(`/workspaces/${encodeURIComponent(workspaceId)}`),
          {
            method: "PUT",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(layout)
          });
        await refreshWorkspaceList(workspaceId);
        $("workspaceMeta").textContent =
          `Saved ${layout.name} · ${layout.displays.length} display${layout.displays.length === 1 ? "" : "s"}`;
      }
      catch (error) {
        $("workspaceMeta").textContent =
          `Saved locally; service save failed: ${error}`;
      }
    }

    function clearWorkspaceDisplays() {
      for (const display of workspaceDisplays.values()) {
        display.destroy();
      }
      workspaceDisplays.clear();
      workspaceEmptyState();
      updateWorkspaceTabs();
    }

    function loadWorkspaceLayout(providedLayout) {
      let layout = providedLayout;
      if (layout === undefined) {
        try {
          layout = JSON.parse(
            localStorage.getItem(workspaceStorageKey) || "null");
        }
        catch {
          localStorage.removeItem(workspaceStorageKey);
        }
      }
      clearWorkspaceDisplays();
      if (layout?.schemaVersion === 1 && Array.isArray(layout.displays)) {
        $("workspaceId").value = layout.id || $("workspaceId").value;
        $("workspaceName").value =
          layout.name || $("workspaceName").value;
        $("workspaceSyncTime").checked = layout.synchronizedTime !== false;
        $("workspaceArrangement").value =
          layout.arrangement === "tabs" ? "tabs" : "grid";
        if (layout.audio) {
          workspaceAudioMonitor.sourceBlockId = layout.audio.sourceBlockId || "";
          $("workspaceAudioChannels").value = layout.audio.channels || "0";
          $("workspaceAudioMix").value = layout.audio.mix || "direct";
          $("workspaceAudioGain").value = Number(layout.audio.gain ?? 1);
          $("workspaceAudioMute").checked = Boolean(layout.audio.muted);
          $("workspaceAudioHighPass").value =
            Number(layout.audio.highPassHz ?? 0);
          $("workspaceAudioRate").value =
            Number(layout.audio.outputRateHz ?? 0);
          $("workspaceAudioLatency").value =
            Number(layout.audio.latencyMs ?? 100);
          $("workspaceAudioDevice").value =
            String(layout.audio.deviceId || "");
          workspaceAudioMonitor.populateSources();
        }
        for (const config of layout.displays) {
          addWorkspaceDisplay(config.type || "spectrogram", config);
        }
        updateWorkspaceTabs();
      }
      else if (workspaceFftBlocks().length) {
        addWorkspaceSpectrogram();
      }
    }

    async function refreshWorkspaceList(selectedId = "") {
      try {
        const result = await requestJson(api("/workspaces"));
        const select = $("workspaceSaved");
        const prior = selectedId || select.value;
        select.replaceChildren(
          ...Array.from(result.workspaces || [], (workspace) =>
            new Option(
              `${workspace.name} · ${workspace.displayCount} displays`,
              workspace.id)));
        if (Array.from(select.options).some(
            (option) => option.value === prior)) {
          select.value = prior;
        }
      }
      catch {
        $("workspaceSaved").replaceChildren(
          new Option("Service workspaces unavailable", ""));
      }
    }

    async function loadSavedWorkspace() {
      const id = $("workspaceSaved").value;
      if (!id) return;
      try {
        const layout = await requestJson(
          api(`/workspaces/${encodeURIComponent(id)}`));
        loadWorkspaceLayout(layout);
        localStorage.setItem(workspaceStorageKey, JSON.stringify(layout));
        $("workspaceMeta").textContent = `Loaded ${layout.name || id}`;
      }
      catch (error) {
        $("workspaceMeta").textContent = `Workspace load failed: ${error}`;
      }
    }

    async function deleteSavedWorkspace() {
      const id = $("workspaceSaved").value;
      if (!id || !window.confirm(`Delete saved workspace '${id}'?`)) return;
      try {
        await requestJson(
          api(`/workspaces/${encodeURIComponent(id)}`),
          { method: "DELETE" });
        await refreshWorkspaceList();
        $("workspaceMeta").textContent = `Deleted saved workspace ${id}`;
      }
      catch (error) {
        $("workspaceMeta").textContent = `Workspace delete failed: ${error}`;
      }
    }

    async function refreshWorkspaceSources(loadLayout = false) {
      try {
        const result = await requestJson(api("/data-blocks"));
        workspaceBlocks = Array.isArray(result.dataBlocks) ? result.dataBlocks : [];
        workspaceAudioMonitor.populateSources();
        for (const display of workspaceDisplays.values()) {
          const priorSource = display.config.sourceBlockId;
          display.populateSources();
          if (!loadLayout) {
            display.connect();
            if (display instanceof WorkspaceSpectrogram &&
                priorSource !== display.config.sourceBlockId) {
              display.columns = [];
              display.connectOverlays();
              display.connectWaveform();
            }
          }
        }
        $("workspaceMeta").textContent =
          `${workspaceFftBlocks().length} FFT source${workspaceFftBlocks().length === 1 ? "" : "s"} · graph r${result.graphRevision}`;
        if (loadLayout) {
          loadWorkspaceLayout();
        }
      }
      catch (error) {
        $("workspaceMeta").textContent = `Data-block discovery failed: ${error}`;
      }
    }

    $("workspaceAddSpectrogram").addEventListener("click", () => {
      addWorkspaceDisplay("spectrogram");
    });
    $("workspaceAddDisplay").addEventListener("click", () => {
      addWorkspaceDisplay($("workspaceDisplayType").value);
    });
    $("workspaceRefreshSources").addEventListener("click", () => {
      refreshWorkspaceSources(false);
    });
    $("workspaceSaveLayout").addEventListener("click", saveWorkspaceLayout);
    $("workspaceLoadSaved").addEventListener("click", loadSavedWorkspace);
    $("workspaceDeleteSaved").addEventListener(
      "click",
      deleteSavedWorkspace);
    $("workspaceResetLayout").addEventListener("click", () => {
      localStorage.removeItem(workspaceStorageKey);
      clearWorkspaceDisplays();
      $("workspaceArrangement").value = "grid";
      if (workspaceFftBlocks().length) {
        addWorkspaceSpectrogram();
      }
      $("workspaceMeta").textContent = "Layout reset";
    });
    $("workspaceSyncTime").addEventListener("change", () => {
      for (const display of workspaceDisplays.values()) {
        display.scheduleRender();
      }
    });
    $("workspaceArrangement").addEventListener(
      "change",
      () => updateWorkspaceTabs());
    $("workspaceAudioSource").addEventListener("change", () => {
      workspaceAudioMonitor.sourceBlockId = $("workspaceAudioSource").value;
    });
    $("workspaceAudioRefreshDevices").addEventListener(
      "click",
      () => workspaceAudioMonitor.populateDevices().catch((error) => {
        $("workspaceAudioStatus").textContent = String(error);
      }));
    $("workspaceAudioGain").addEventListener("change", () => {
      const gain = Number($("workspaceAudioGain").value);
      if (workspaceAudioMonitor.node && Number.isFinite(gain) && gain >= 0) {
        workspaceAudioMonitor.node.port.postMessage({
          type: "gain",
          value: $("workspaceAudioMute").checked ? 0 : gain
        });
      }
    });
    $("workspaceAudioMute").addEventListener("change", () => {
      const gain = Number($("workspaceAudioGain").value) || 0;
      workspaceAudioMonitor.node?.port.postMessage({
        type: "gain",
        value: $("workspaceAudioMute").checked ? 0 : gain
      });
    });
    $("workspaceAudioLatency").addEventListener("change", () => {
      if (!workspaceAudioMonitor.node || !workspaceAudioMonitor.context) {
        return;
      }
      const latencyMs = Math.max(
        20,
        Math.min(
          2000,
          Number($("workspaceAudioLatency").value) || 100));
      workspaceAudioMonitor.node.port.postMessage({
        type: "latency",
        value: workspaceAudioMonitor.context.sampleRate *
          latencyMs / 1000
      });
    });
    $("workspaceAudioHighPass").addEventListener("change", () => {
      const highPassHz = Math.max(
        0,
        Number($("workspaceAudioHighPass").value) || 0);
      workspaceAudioMonitor.node?.port.postMessage({
        type: "highpass",
        value: highPassHz
      });
    });
    $("workspaceAudioStart").addEventListener("click", async () => {
      try {
        await workspaceAudioMonitor.start();
      }
      catch (error) {
        $("workspaceAudioStatus").textContent = String(error);
        await workspaceAudioMonitor.stop(false);
      }
    });
    $("workspaceAudioStop").addEventListener("click", () => {
      workspaceAudioMonitor.stop();
    });
    async function initializeWorkspace() {
      await refreshWorkspaceSources(false);
      await refreshWorkspaceList();
      if (localStorage.getItem(workspaceStorageKey)) {
        loadWorkspaceLayout();
      }
      else if ($("workspaceSaved").value) {
        await loadSavedWorkspace();
      }
      else {
        loadWorkspaceLayout(null);
      }
    }

    let displayControllerMounted = false;
    const displayController = Object.freeze({
      configure({
        activateTab,
        focusControlledUnit
      } = {}) {
        if (displayControllerMounted) {
          throw new Error(
            "Display callbacks cannot change while mounted");
        }
        if (activateTab !== undefined) {
          if (typeof activateTab !== "function") {
            throw new TypeError("activateTab must be a function");
          }
          displayCallbacks.activateTab = activateTab;
        }
        if (focusControlledUnit !== undefined) {
          if (typeof focusControlledUnit !== "function") {
            throw new TypeError(
              "focusControlledUnit must be a function");
          }
          displayCallbacks.focusControlledUnit =
            focusControlledUnit;
        }
      },
      mount() {
        if (displayControllerMounted) {
          throw new Error(
            "Display controller is already mounted");
        }
        displayControllerMounted = true;
        initializeWorkspace().catch((error) => {
          $("workspaceMeta").textContent =
            `Workspace initialization failed: ${error}`;
        });
        workspaceAudioMonitor.populateDevices().catch(() => {});
      },
      async dispose() {
        if (!displayControllerMounted) return;
        displayControllerMounted = false;
        clearWorkspaceDisplays();
        await workspaceAudioMonitor.stop(false);
      },
      refreshSources: refreshWorkspaceSources,
      get state() {
        return Object.freeze({
          blocks: structuredClone(workspaceBlocks),
          displayIds: Array.from(workspaceDisplays.keys())
        });
      }
    });

    globalThis.PamguardModules = Object.freeze({
      ...(globalThis.PamguardModules || {}),
      displays: displayController
    });
