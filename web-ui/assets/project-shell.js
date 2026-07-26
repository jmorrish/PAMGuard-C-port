(() => {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const platform = globalThis.PamguardPlatform;
  const projectDisplays = globalThis.PamguardProjectDisplays;
  const projectAudio = globalThis.PamguardProjectAudio;
  const projectArraySettings =
    globalThis.PamguardProjectArraySettings;
  const projectTemplates =
    globalThis.PamguardProjectTemplates;
  const projectClickSettings =
    globalThis.PamguardProjectClickSettings;
  const projectSignalRoutingSettings =
    globalThis.PamguardProjectSignalRoutingSettings;
  const projectFilterSettings =
    globalThis.PamguardProjectFilterSettings;
  const projectLevelMeterSettings =
    globalThis.PamguardProjectLevelMeterSettings;
  const projectNoiseLtsaSettings =
    globalThis.PamguardProjectNoiseLtsaSettings;
  const projectWhistleMoanSettings =
    globalThis.PamguardProjectWhistleMoanSettings;
  const projectIshmaelSettings =
    globalThis.PamguardProjectIshmaelSettings;
  const projectMhtClickTrainSettings =
    globalThis.PamguardProjectMhtClickTrainSettings;
  const projectMatchedTemplateSettings =
    globalThis.PamguardProjectMatchedTemplateSettings;
  const projectSoundRecorderSettings =
    globalThis.PamguardProjectSoundRecorderSettings;
  const projectClipGeneratorSettings =
    globalThis.PamguardProjectClipGeneratorSettings;
  if (!platform?.http ||
      !platform?.project ||
      !platform?.lifecycle ||
      !projectDisplays?.mountSpectrogram ||
      !projectDisplays?.mountClickDisplay ||
      !projectDisplays?.mountLevelMeter ||
      !projectAudio?.createMonitor ||
      !projectArraySettings?.mountEditor ||
      !projectTemplates?.createClickMonitoringPreview ||
      !projectClickSettings?.mountEditor ||
      !projectSignalRoutingSettings?.mountEditor ||
      !projectFilterSettings?.mountEditor ||
      !projectLevelMeterSettings?.mountEditor ||
      !projectNoiseLtsaSettings?.mountEditor ||
      !projectWhistleMoanSettings?.mountEditor ||
      !projectIshmaelSettings?.mountEditor ||
      !projectMhtClickTrainSettings?.mountEditor ||
      !projectMatchedTemplateSettings?.mountEditor ||
      !projectSoundRecorderSettings?.mountEditor ||
      !projectClipGeneratorSettings?.mountEditor) {
    throw new Error("PAMGuard project-shell platform is incomplete");
  }

  const UNIT_WIDTH = 292;
  const ROLE_TOP = 116;
  const ROLE_GAP = 30;
  const WORLD_WIDTH = 5000;
  const WORLD_HEIGHT = 3200;
  const STATUS_INTERVAL_MS = 2000;
  const SOUND_OUTPUT_LOCAL_STORAGE_KEY =
    "pamguard.sound-output.browser-preferences.v1";

  const state = {
    baseUrl: window.location.origin,
    apiKey: "",
    catalogue: null,
    active: null,
    inspection: null,
    runtime: null,
    ready: null,
    descriptorByType: new Map(),
    providerByType: new Map(),
    positions: new Map(),
    localDisplaySelections: new Map(),
    viewport: { x: 0, y: 0, zoom: 1 },
    selectedUnitId: null,
    activeTabId: "data-model",
    contextUnitId: null,
    commandBusy: false,
    runtimeBusy: false,
    conflicted: false,
    disposed: false,
    statusTimer: null,
    statusPollBusy: false,
    runtimeGeneration: 0,
    clientGeneration: 0,
    layoutDirty: false,
    statusPoll: 0,
    drag: null,
    connectionDraft: null,
    suppressGraphOutputClick: "",
    displayRuntimes: new Map(),
    soundOutputMonitors: new Map()
  };

  let httpClient = null;
  let projectClient = null;
  let activeApplication = null;
  let formDialogCloseBarrier = Promise.resolve();

  function createElement(tag, options = {}) {
    const element = document.createElement(tag);
    if (options.className) element.className = options.className;
    if (options.text !== undefined) {
      element.textContent = String(options.text);
    }
    if (options.type) element.type = options.type;
    if (options.id) element.id = options.id;
    if (options.title) element.title = options.title;
    if (options.attributes) {
      for (const [name, value] of Object.entries(options.attributes)) {
        if (value !== null && value !== undefined) {
          element.setAttribute(name, String(value));
        }
      }
    }
    return element;
  }

  function deepClone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  function humanize(value) {
    return String(value)
      .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
      .replace(/[_-]+/g, " ")
      .replace(/\b\w/g, (letter) => letter.toUpperCase());
  }

  function plural(count, singular, pluralValue = `${singular}s`) {
    return `${count} ${count === 1 ? singular : pluralValue}`;
  }

  function normalizedError(error) {
    if (error?.name === "AbortError") return "Request cancelled";
    if (typeof error?.message === "string" && error.message) {
      return error.message;
    }
    return String(error || "Unknown error");
  }

  function createClients() {
    disposeDisplayRuntimes();
    void disposeAllSoundOutputMonitors();
    httpClient?.dispose();
    projectClient?.dispose();
    state.clientGeneration += 1;
    state.runtimeGeneration += 1;
    httpClient = platform.http.createHttpClient({
      getBaseUrl: () => state.baseUrl,
      getApiKey: () => state.apiKey
    });
    projectClient = platform.project.createProjectClient({
      httpClient,
      getApiKey: () => state.apiKey
    });
  }

  function serviceJson(path, options = {}) {
    return httpClient.requestJson(httpClient.api(path), {
      headers: {
        "Accept": "application/json",
        ...(options.body === undefined
          ? {}
          : { "Content-Type": "application/json" }),
        ...(options.headers || {})
      },
      ...options,
      ...(options.body === undefined
        ? {}
        : {
            body: typeof options.body === "string"
              ? options.body
              : JSON.stringify(options.body)
          })
    });
  }

  function showToast(message, kind = "info", timeout = 4200) {
    if (state.disposed) return;
    const toast = createElement("div", {
      className: `toast toast-${kind}`,
      attributes: { role: kind === "error" ? "alert" : "status" }
    });
    const copy = createElement("span", { text: message });
    const close = createElement("button", {
      type: "button",
      className: "icon-button",
      text: "×",
      attributes: { "aria-label": "Dismiss message" }
    });
    close.addEventListener("click", () => toast.remove());
    toast.append(copy, close);
    $("toastRegion").append(toast);
    if (timeout > 0) {
      window.setTimeout(() => toast.remove(), timeout);
    }
  }

  function closeMenus() {
    for (const menu of document.querySelectorAll(".menu.open")) {
      menu.classList.remove("open");
      menu.querySelector(".menu-trigger")
        ?.setAttribute("aria-expanded", "false");
    }
  }

  function setConflict(error) {
    state.conflicted = true;
    $("conflictBanner").hidden = false;
    const detail = $("conflictBanner").querySelector("span");
    if (detail) {
      detail.textContent =
        "Your rejected edit was not retried or applied. " +
        (error?.currentEtag
          ? "Reload to reconcile with the current project."
          : "Reload before making another change.");
    }
    renderControls();
  }

  function clearConflict() {
    state.conflicted = false;
    $("conflictBanner").hidden = true;
  }

  function handleCommandError(error, context) {
    if (error instanceof platform.project.ProjectConflictError) {
      setConflict(error);
      showToast(
        `${context} was rejected because the project changed elsewhere.`,
        "error",
        0);
      return;
    }
    showToast(`${context}: ${normalizedError(error)}`, "error", 0);
  }

  function descriptorForUnit(unit) {
    return state.descriptorByType.get(unit.typeId) || null;
  }

  function unitById(unitId) {
    return state.active?.project?.controlledUnits
      ?.find((unit) => unit.id === unitId) || null;
  }

  function ownerUnitName(unitId) {
    return unitById(unitId)?.name || "Unknown controlled unit";
  }

  function sourceName(source) {
    if (!source) return "Unbound";
    const unit = unitById(source.unitId);
    const descriptor = unit ? descriptorForUnit(unit) : null;
    const role = descriptor?.outputs
      ?.find((output) => output.id === source.outputRole);
    return `${unit?.name || source.unitId} · ` +
      `${role?.name || source.outputRole}`;
  }

  function disposeDisplayRuntimes() {
    for (const runtime of state.displayRuntimes.values()) {
      runtime.dispose();
    }
    state.displayRuntimes.clear();
  }

  function streamHeaders() {
    return state.apiKey ? { "X-API-Key": state.apiKey } : {};
  }

  function soundOutputUnitFingerprint(unit) {
    return JSON.stringify({
      settings: unit?.settings || {},
      bindings: unit?.bindings || []
    });
  }

  function readSoundOutputPreferences() {
    try {
      const parsed = JSON.parse(
        localStorage.getItem(SOUND_OUTPUT_LOCAL_STORAGE_KEY) || "{}");
      return parsed && typeof parsed === "object" && !Array.isArray(parsed)
        ? parsed
        : {};
    }
    catch {
      return {};
    }
  }

  function soundOutputPreferenceKey(unitId) {
    return `${state.active?.project?.projectId || "no-project"}:${unitId}`;
  }

  function preferredSoundOutputDevice(unitId) {
    const preference =
      readSoundOutputPreferences()[soundOutputPreferenceKey(unitId)];
    return typeof preference?.deviceId === "string"
      ? preference.deviceId
      : "";
  }

  function rememberSoundOutputDevice(unitId, deviceId) {
    try {
      const preferences = readSoundOutputPreferences();
      preferences[soundOutputPreferenceKey(unitId)] = {
        deviceId: String(deviceId || "")
      };
      localStorage.setItem(
        SOUND_OUTPUT_LOCAL_STORAGE_KEY,
        JSON.stringify(preferences));
    }
    catch {
      // Private browsing and storage policy must not disable live listening.
    }
  }

  function notifySoundOutputEntry(entry) {
    for (const listener of entry.listeners) {
      try {
        listener(entry.status);
      }
      catch {
        // A stale dialog listener must not break the audio monitor.
      }
    }
  }

  function soundOutputMonitorEntry(unitId) {
    const unit = unitById(unitId);
    if (!unit || unit.typeId !== "pamguard.sound-output") return null;
    const projectId = state.active?.project?.projectId || "";
    const existing = state.soundOutputMonitors.get(unitId);
    if (existing && existing.projectId === projectId) return existing;
    if (existing) {
      state.soundOutputMonitors.delete(unitId);
      void existing.monitor.dispose().catch(() => {});
    }
    const entry = {
      unitId,
      projectId,
      unitFingerprint: soundOutputUnitFingerprint(unit),
      status: {
        phase: "stopped",
        sourceBlockId: "",
        message: "Stopped"
      },
      listeners: new Set(),
      monitor: null
    };
    entry.monitor = projectAudio.createMonitor({
      getBaseUrl: () => state.baseUrl,
      getHeaders: streamHeaders,
      onStatus: (status) => {
        entry.status = status;
        notifySoundOutputEntry(entry);
      }
    });
    state.soundOutputMonitors.set(unitId, entry);
    return entry;
  }

  function subscribeSoundOutputStatus(entry, listener) {
    entry.listeners.add(listener);
    listener(entry.status);
    return () => entry.listeners.delete(listener);
  }

  async function stopAllSoundOutputMonitors() {
    await Promise.all(
      Array.from(state.soundOutputMonitors.values(), (entry) =>
        entry.monitor.stop().catch(() => {})));
  }

  async function disposeAllSoundOutputMonitors() {
    const entries = Array.from(state.soundOutputMonitors.values());
    state.soundOutputMonitors.clear();
    for (const entry of entries) entry.listeners.clear();
    await Promise.all(
      entries.map((entry) => entry.monitor.dispose().catch(() => {})));
  }

  function reconcileSoundOutputMonitors(active) {
    const projectId = active?.project?.projectId || "";
    const units = new Map(
      (active?.project?.controlledUnits || [])
        .filter((unit) => unit.typeId === "pamguard.sound-output")
        .map((unit) => [unit.id, unit]));
    for (const [unitId, entry] of state.soundOutputMonitors) {
      const unit = units.get(unitId);
      if (entry.projectId === projectId &&
          unit &&
          entry.unitFingerprint === soundOutputUnitFingerprint(unit)) {
        continue;
      }
      state.soundOutputMonitors.delete(unitId);
      entry.listeners.clear();
      void entry.monitor.dispose().catch(() => {});
    }
  }

  function bitmapForChannelCount(channelCount) {
    const count = Number(channelCount);
    if (!Number.isInteger(count) || count <= 0) return 0;
    if (count >= 32) return 0xffffffff;
    return (2 ** count) - 1;
  }

  function portableChannelBitmap(value) {
    const bitmap = Number(value);
    return Number.isInteger(bitmap) &&
      bitmap >= 0 &&
      bitmap <= 0xffffffff
      ? bitmap
      : 0;
  }

  function sourceChannelBitmap(source, visited = new Set()) {
    if (!source?.unitId || visited.has(source.unitId)) return 0;
    visited.add(source.unitId);
    const unit = unitById(source.unitId);
    if (!unit) return 0;
    const direct = portableChannelBitmap(
      unit.settings?.channelBitmap ??
      unit.settings?.detector?.channelBitmap ??
      unit.settings?.fft?.channelMap);
    if (direct > 0) return direct;
    const byCount = bitmapForChannelCount(unit.settings?.nChannels);
    if (byCount > 0) return byCount;
    for (const binding of unit.bindings || []) {
      for (const candidate of binding.sources || []) {
        const derived = sourceChannelBitmap(candidate, visited);
        if (derived > 0) return derived;
      }
    }
    return 0;
  }

  function clickSourceChannelGroups(source) {
    const unit = source?.unitId ? unitById(source.unitId) : null;
    const detector = unit?.settings?.detector;
    const bitmap = portableChannelBitmap(detector?.channelBitmap);
    if (!detector || bitmap === 0) return [];
    const selectedChannels = [];
    for (let channel = 0; channel < 32; channel += 1) {
      if (Math.floor(bitmap / (2 ** channel)) % 2 === 1) {
        selectedChannels.push(channel);
      }
    }
    if (detector.groupingType === "all") return [bitmap];
    if (detector.groupingType !== "user") {
      return selectedChannels.map((channel) => 2 ** channel);
    }
    const grouped = new Map();
    for (const channel of selectedChannels) {
      const group = Number(detector.channelGroups?.[channel]);
      if (!Number.isInteger(group) || group < 0 || group > 31) {
        continue;
      }
      grouped.set(
        group,
        (grouped.get(group) || 0) + 2 ** channel);
    }
    return Array.from(grouped.entries())
      .sort(([left], [right]) => left - right)
      .map(([, groupBitmap]) => groupBitmap);
  }

  function soundOutputSourceReference(sourceCollector) {
    const value = sourceCollector?.select?.value || "";
    if (!value) return null;
    const [unitId, outputRole] = value.split("|");
    return unitId && outputRole ? { unitId, outputRole } : null;
  }

  function projectedBlockForSource(source) {
    if (!source) return null;
    return state.inspection?.projection?.publicOutputs
      ?.find((candidate) =>
        candidate.unitId === source.unitId &&
        candidate.outputRole === source.outputRole) || null;
  }

  function provisionalSoundOutputSourceMetadata(sourceCollector) {
    const source = soundOutputSourceReference(sourceCollector);
    const projection = projectedBlockForSource(source);
    return {
      source,
      sourceBlockId: projection?.blockId || "",
      sampleRateHz: sourceSampleRate(source),
      channelBitmap: sourceChannelBitmap(source),
      runtimeBlock: null,
      catalogueError: ""
    };
  }

  async function loadSoundOutputSourceMetadata(sourceCollector) {
    const provisional =
      provisionalSoundOutputSourceMetadata(sourceCollector);
    if (!provisional.sourceBlockId) return provisional;
    try {
      const catalogue = await serviceJson("/data-blocks");
      const block = catalogue.dataBlocks?.find(
        (candidate) => candidate.id === provisional.sourceBlockId);
      if (!block) return provisional;
      return {
        ...provisional,
        sampleRateHz:
          Number(block.sampleRateHz) > 0
            ? Number(block.sampleRateHz)
            : provisional.sampleRateHz,
        channelBitmap:
          portableChannelBitmap(block.channelBitmap) ||
          provisional.channelBitmap,
        runtimeBlock: block
      };
    }
    catch (error) {
      return {
        ...provisional,
        catalogueError: normalizedError(error)
      };
    }
  }

  function spectrogramSourceMetadata(display) {
    if (!display.source) {
      return {
        sourceBlockId: "",
        sampleRateHz: 0,
        fftLength: 0
      };
    }
    const projected = state.inspection?.projection?.displays
      ?.find((candidate) => candidate.displayId === display.id);
    const fftUnit = unitById(display.source.unitId);
    const rawBinding = fftUnit?.bindings?.find(
      (binding) => binding.inputRole === "rawAudio");
    const acquisition = rawBinding?.sources?.[0]
      ? unitById(rawBinding.sources[0].unitId)
      : null;
    return {
      sourceBlockId: projected?.sourceBlockId || "",
      sampleRateHz: Number(acquisition?.settings?.sampleRate) || 0,
      fftLength: Number(fftUnit?.settings?.fft?.fftLength) || 0
    };
  }

  function sourceSampleRate(source, visited = new Set()) {
    if (!source?.unitId || visited.has(source.unitId)) return 0;
    visited.add(source.unitId);
    const unit = unitById(source.unitId);
    if (!unit) return 0;
    const direct = Number(
      unit.settings?.outputSampleRateHz ??
      unit.settings?.sampleRate ??
      unit.settings?.sampleRateHz);
    if (Number.isFinite(direct) && direct > 0) return direct;
    for (const binding of unit.bindings || []) {
      for (const candidate of binding.sources || []) {
        const derived = sourceSampleRate(candidate, visited);
        if (derived > 0) return derived;
      }
    }
    return 0;
  }

  function clickDisplaySourceMetadata(display) {
    if (!display.source) {
      return {
        sourceBlockId: "",
        bearingBlockId: "",
        sampleRateHz: 0
      };
    }
    const projected = state.inspection?.projection?.displays
      ?.find((candidate) => candidate.displayId === display.id);
    const publicOutputs =
      state.inspection?.projection?.publicOutputs || [];
    const bearing = publicOutputs.find(
      (candidate) =>
        candidate.unitId === display.source.unitId &&
        candidate.outputRole === "bearings");
    const sourceUnit = unitById(display.source.unitId);
    const rawBinding = sourceUnit?.bindings?.find(
      (binding) => binding.inputRole === "rawAudio");
    return {
      sourceBlockId: projected?.sourceBlockId || "",
      bearingBlockId: bearing?.blockId || "",
      sampleRateHz:
        sourceSampleRate(rawBinding?.sources?.[0]) ||
        sourceSampleRate(display.source)
    };
  }

  function levelDisplaySourceMetadata(display) {
    if (!display.source) {
      return { sourceBlockId: "" };
    }
    const projected = state.inspection?.projection?.displays
      ?.find((candidate) => candidate.displayId === display.id);
    return {
      sourceBlockId: projected?.sourceBlockId || ""
    };
  }

  function adoptActive(active) {
    reconcileSoundOutputMonitors(active);
    state.active = active;
    state.localDisplaySelections.clear();
    clearConflict();
    syncPositions();
  }

  function syncPositions() {
    const units = state.active?.project?.controlledUnits || [];
    const persisted = new Map(
      (state.active?.project?.dataModelLayout?.nodes || [])
        .map((position) => [position.unitId, position]));
    const next = new Map();
    units.forEach((unit, index) => {
      const position = persisted.get(unit.id);
      next.set(unit.id, position
        ? { x: position.x, y: position.y }
        : {
            x: 72 + (index % 4) * 340,
            y: 74 + Math.floor(index / 4) * 250
          });
    });
    state.positions = next;
    state.viewport = deepClone(
      state.active?.project?.dataModelLayout?.viewport ||
        { x: 0, y: 0, zoom: 1 });
  }

  async function loadReadiness() {
    try {
      return await serviceJson("/ready");
    }
    catch (error) {
      if (error?.body &&
          typeof error.body === "object" &&
          typeof error.body.ready === "boolean") {
        return error.body;
      }
      throw error;
    }
  }

  async function refreshDerived({ render = true } = {}) {
    if (!state.active || state.disposed) return;
    const runtimeWasRunning = Boolean(state.runtime?.running);
    const [inspection, runtime, ready] = await Promise.all([
      projectClient.loadInspection(),
      serviceJson("/module-runtime/status"),
      loadReadiness()
    ]);
    if (state.disposed) return;
    state.inspection = inspection;
    state.runtime = runtime;
    state.ready = ready;
    if (runtimeWasRunning && !runtime.running) {
      await stopAllSoundOutputMonitors();
    }
    if (render) renderAll();
  }

  async function refreshAfterCommit(context) {
    try {
      await refreshDerived({ render: false });
    }
    catch (error) {
      if (error instanceof platform.project.ProjectConflictError) {
        setConflict(error);
        showToast(
          `${context} succeeded, but the project then changed in ` +
            "another client. Reload to reconcile before editing again.",
          "warning",
          0);
      }
      else {
        showToast(
          `${context} succeeded, but runtime/inspection status could ` +
            `not be refreshed: ${normalizedError(error)}`,
          "warning",
          0);
      }
    }
    renderAll();
  }

  async function reloadProject({ announce = false } = {}) {
    state.commandBusy = true;
    renderControls();
    try {
      const active = await projectClient.loadActive();
      adoptActive(active);
      renderAll();
      await refreshAfterCommit("Project reload");
      if (announce) showToast("Active project reloaded", "success");
    }
    catch (error) {
      handleCommandError(error, "Could not load the active project");
      renderDisconnected(error);
    }
    finally {
      state.commandBusy = false;
      renderControls();
    }
  }

  async function applyMutation(operations, successMessage) {
    if (!operations.length) return null;
    if (!canEditStructure()) {
      showToast(
        state.conflicted
          ? "Reload the project before making another change."
          : "Stop processing before changing this project.",
        "warning");
      return null;
    }
    state.commandBusy = true;
    renderControls();
    try {
      const result = await projectClient.mutate({
        validateOnly: false,
        operations
      });
      adoptActive(result.active);
      renderAll();
      await refreshAfterCommit("Project change");
      if (successMessage) showToast(successMessage, "success");
      return result;
    }
    catch (error) {
      handleCommandError(error, "Project change failed");
      return null;
    }
    finally {
      state.commandBusy = false;
      renderControls();
    }
  }

  function canEditStructure() {
    return Boolean(
      state.active &&
      !state.commandBusy &&
      !state.runtimeBusy &&
      !state.runtime?.running &&
      !state.conflicted);
  }

  function canConfigureUnit(unit) {
    if (canEditStructure()) return true;
    return Boolean(
      ["pamguard.sound-output", "pamguard.sound-recorder"]
        .includes(unit?.typeId) &&
      state.active &&
      !state.commandBusy &&
      !state.runtimeBusy &&
      !state.conflicted);
  }

  function renderDisconnected(error) {
    if (state.disposed) return;
    void disposeAllSoundOutputMonitors();
    state.active = null;
    state.inspection = null;
    state.runtime = null;
    state.positions = new Map();
    state.viewport = { x: 0, y: 0, zoom: 1 };
    state.selectedUnitId = null;
    state.activeTabId = "data-model";
    state.layoutDirty = false;
    $("projectName").textContent = "Project unavailable";
    $("projectStatus").textContent = "Disconnected";
    $("projectStatus").className = "status-pill status-invalid";
    $("projectRevision").textContent = "No active project";
    $("dirtyIndicator").hidden = true;
    renderPalette();
    renderApplicationMenus();
    renderGraph();
    renderDisplays();
    renderRuntimeStatus();
    $("serviceState").textContent =
      `Engine: ${normalizedError(error)}`;
    renderControls();
  }

  function renderProjectChrome() {
    if (state.disposed) return;
    const active = state.active;
    if (!active) return;
    $("projectName").textContent =
      active.project.metadata.name || "Untitled Project";
    $("dirtyIndicator").hidden =
      !active.dirty && !state.layoutDirty;
    $("projectRevision").textContent =
      `Working revision ${active.workingRevision}` +
      (active.savedRevision === null
        ? " · not saved"
        : ` · saved ${active.savedRevision}`) +
      (state.layoutDirty ? " · layout pending" : "");

    const projection = active.projection?.status || "invalid";
    const pill = $("projectStatus");
    const runtimeNotPrepared =
      projection === "runnable" &&
      state.ready?.projectRuntimePrepared === false;
    pill.textContent = runtimeNotPrepared
      ? "Runtime not ready"
      : projection === "runnable"
        ? "Ready"
      : projection === "needs-configuration"
        ? "Needs configuration"
        : "Invalid";
    pill.className = runtimeNotPrepared
      ? "status-pill status-invalid"
      : `status-pill status-${projection.replaceAll("-", "")}`;
  }

  function renderControls() {
    if (state.disposed) return;
    const running = Boolean(state.runtime?.running);
    const projection = state.active?.projection?.status;
    $("runtimeStart").disabled = Boolean(
      state.runtimeBusy ||
      state.commandBusy ||
      state.layoutDirty ||
      state.conflicted ||
      state.ready?.projectRuntimePrepared === false ||
      running ||
      projection !== "runnable" ||
      !state.active);
    $("runtimeStop").disabled = Boolean(
      state.runtimeBusy ||
      state.commandBusy ||
      state.layoutDirty ||
      state.conflicted ||
      !running);
    $("fileSave").disabled = Boolean(
      state.commandBusy ||
      state.layoutDirty ||
      !state.active ||
      state.conflicted);
    $("fileSaveAs").disabled = Boolean(
      state.commandBusy ||
      state.layoutDirty ||
      !state.active ||
      state.conflicted);
    $("fileNew").disabled = Boolean(
      state.commandBusy ||
      state.runtimeBusy ||
      !state.active ||
      state.conflicted);
    $("fileOpen").disabled = Boolean(
      state.commandBusy ||
      state.runtimeBusy ||
      !state.active ||
      state.conflicted);
    $("connectionSettings").disabled = Boolean(
      state.commandBusy ||
      state.runtimeBusy ||
      state.runtime?.running);
    $("openPalette").disabled = !canEditStructure();
    $("emptyAddModule").disabled = !canEditStructure();
    for (const control of document.querySelectorAll(
      "[data-project-layout-action]")) {
      control.disabled = !canEditStructure();
    }
    for (const control of document.querySelectorAll(
      "[data-requires-idle='true']")) {
      control.disabled =
        !canEditStructure() ||
        control.dataset.displayMaximumReached === "true";
    }
    for (const control of document.querySelectorAll(
      "[data-sound-output-settings-action]")) {
      control.disabled = !canConfigureUnit(
        unitById(control.dataset.soundOutputUnitId));
    }
    for (const control of document.querySelectorAll(
      "[data-sound-recorder-settings-action]")) {
      control.disabled = !canConfigureUnit(
        unitById(control.dataset.soundRecorderUnitId));
    }
    for (const control of document.querySelectorAll(
      "[role='menuitem'][data-acquisition-host-action]")) {
      control.disabled = Boolean(
        state.commandBusy ||
        state.runtimeBusy ||
        state.conflicted);
    }
    for (const control of document.querySelectorAll(
      ".controlled-unit-node [data-project-unit-action='configure']")) {
      const unitId = control.closest("[data-controlled-unit-id]")
        ?.getAttribute("data-controlled-unit-id");
      control.disabled = !canConfigureUnit(unitById(unitId));
    }
    for (const output of document.querySelectorAll(
      ".controlled-unit-node .port-output")) {
      output.disabled = !canEditStructure();
    }
    for (const input of document.querySelectorAll(
      ".controlled-unit-node .port-input")) {
      input.disabled = !canConfigureUnit(
        unitById(input.dataset.unitId));
    }
  }

  function renderRuntimeStatus() {
    if (state.disposed) return;
    const runtime = state.runtime;
    const running = Boolean(runtime?.running);
    $("runtimeState").textContent = state.runtimeBusy
      ? "Changing state…"
      : running ? "Running" : "Stopped";
    $("runtimeLed").className =
      `status-led ${running ? "status-led-running" : "status-led-off"}`;
    const projectReady = Boolean(
      state.ready &&
      state.ready.projectProjectionStatus === "runnable" &&
      state.ready.projectRuntimePrepared === true);
    if (state.ready && !projectReady) {
      const reason =
        state.ready.projectProjectionStatus !== "runnable"
          ? humanize(
            state.ready.projectProjectionStatus ||
              "invalid project")
          : state.ready.projectRuntimePrepared === false
            ? "runtime not prepared"
            : "project readiness unavailable";
      $("serviceState").textContent =
        `Engine: not ready · ${reason}`;
    }
    else {
      $("serviceState").textContent = runtime
        ? `Engine: online · ${runtime.authorityMode}`
        : "Engine: connecting";
    }

    const status = state.active?.projection?.status || "loading";
    $("projectionState").textContent =
      `Project: ${humanize(status)}`;
    const unitCount =
      state.active?.project?.controlledUnits?.length || 0;
    const blockCount =
      state.inspection?.projection?.dataBlocks?.length || 0;
    $("unitCount").textContent =
      plural(unitCount, "controlled unit");
    $("blockCount").textContent = plural(blockCount, "data block");
  }

  function currentModeAllowed(descriptor) {
    const mode = state.active?.project?.mode;
    return descriptor.instanceRules?.allowedModes?.includes(mode);
  }

  function effectiveInstanceRules(descriptor) {
    const rules = descriptor.instanceRules || {};
    const mode = state.active?.project?.mode;
    return rules.modeOverrides?.find(
      (override) => override.mode === mode) || rules;
  }

  function typeAtMaximum(descriptor) {
    const maximum = effectiveInstanceRules(descriptor).maximum;
    if (maximum === null || maximum === undefined) return false;
    const count = state.active?.project?.controlledUnits
      ?.filter((unit) => unit.typeId === descriptor.typeId).length || 0;
    return count >= maximum;
  }

  function renderPalette() {
    const container = $("paletteList");
    container.replaceChildren();
    if (!state.catalogue) {
      container.append(createElement("p", {
        className: "menu-empty",
        text: "Loading controlled-unit catalogue…"
      }));
      return;
    }

    const query = $("moduleSearch").value.trim().toLowerCase();
    const templateSearch =
      "click monitoring configuration template acquisition fft " +
      "spectrogram click detector sound output";
    const templateMatches =
      !query || templateSearch.includes(query);
    if (templateMatches) {
      const section = createElement("section", {
        className: "palette-group configuration-template-group"
      });
      section.append(createElement("h3", {
        text: "Configuration Templates"
      }));
      const button = createElement("button", {
        type: "button",
        className: "palette-item",
        title: "Preview and atomically add independent Acquisition, FFT, " +
          "Spectrogram, Click Detector, Click display, and Sound Output " +
          "branches.",
        attributes: {
          "data-configuration-template-action":
            projectTemplates.clickMonitoringTemplateId,
          "data-project-action": "add-configuration-template",
          "data-requires-idle": "true"
        }
      });
      const copy = createElement("span");
      copy.append(
        createElement("strong", {
          text: "Click monitoring configuration template"
        }),
        createElement("small", {
          text: "Preview a standard monitoring graph; every module remains " +
            "independent."
        }));
      button.append(
        createElement("span", {
          className: "palette-item-icon",
          text: "\u2442"
        }),
        copy);
      button.disabled = !canEditStructure();
      button.addEventListener("click", () => {
        $("modulePalette").classList.remove("open");
        void addClickMonitoringConfiguration();
      });
      section.append(button);
      container.append(section);
    }
    const available = state.catalogue.controlledUnitTypes.filter(
      (descriptor) => {
        if (descriptor.status?.availability !== "available") return false;
        const haystack = [
          descriptor.palette?.registeredName,
          descriptor.palette?.menuGroup,
          descriptor.palette?.tooltip,
          ...(descriptor.palette?.aliases || [])
        ].join(" ").toLowerCase();
        return !query || haystack.includes(query);
      });
    const groups = new Map();
    for (const descriptor of available) {
      const group = descriptor.palette?.menuGroup || "Other";
      if (!groups.has(group)) groups.set(group, []);
      groups.get(group).push(descriptor);
    }
    if (!groups.size && !templateMatches) {
      container.append(createElement("p", {
        className: "menu-empty",
        text: "No supported module matches that search."
      }));
      return;
    }
    for (const [group, descriptors] of groups) {
      const section = createElement("section", {
        className: "palette-group"
      });
      section.append(createElement("h3", { text: group }));
      for (const descriptor of descriptors) {
        const button = createElement("button", {
          type: "button",
          className: "palette-item",
          title: descriptor.palette?.tooltip || "",
          attributes: {
            "data-type-id": descriptor.typeId,
            "data-project-add-unit-type": descriptor.typeId,
            "data-project-action": "add-unit",
            "data-requires-idle": "true"
          }
        });
        const copy = createElement("span");
        copy.append(
          createElement("strong", {
            text: descriptor.palette?.registeredName || descriptor.typeId
          }),
          createElement("small", {
            text: descriptor.palette?.tooltip ||
              descriptor.javaAuthority?.className || ""
          }));
        button.append(
          createElement("span", {
            className: "palette-item-icon",
            text: moduleGlyph(descriptor)
          }),
          copy);
        const unavailable =
          typeAtMaximum(descriptor) ||
          !currentModeAllowed(descriptor) ||
          !canEditStructure();
        button.disabled = unavailable;
        if (typeAtMaximum(descriptor)) {
          button.title = "The PAMGuard maximum instance count is reached.";
        }
        button.addEventListener("click", () => {
          closeMenus();
          void addControlledUnit(descriptor.typeId);
        });
        section.append(button);
      }
      container.append(section);
    }
  }

  function moduleGlyph(descriptor) {
    const name = descriptor.palette?.registeredName || "";
    if (/acquisition/i.test(name)) return "≈";
    if (/fft|spectrogram/i.test(name)) return "∿";
    if (/display/i.test(name)) return "▣";
    return "◇";
  }

  function appendMenuGroup(container, name, items) {
    const label = createElement("span", {
      className: "menu-group-label",
      text: name
    });
    container.append(label);
    for (const item of items) container.append(item);
  }

  function renderApplicationMenus() {
    renderAddModulesMenu();
    renderSettingsMenu();
    renderDisplayMenu();
  }

  function renderAddModulesMenu() {
    const menu = $("addModulesMenu");
    menu.replaceChildren();
    if (!state.catalogue) {
      menu.append(createElement("span", {
        className: "menu-empty",
        text: "Loading controlled units…"
      }));
      return;
    }
    const templateButton = createElement("button", {
      type: "button",
      text: "Click monitoring configuration template\u2026",
      title: "Preview and atomically add the independent monitoring branches.",
      attributes: {
        role: "menuitem",
        "data-configuration-template-action":
          projectTemplates.clickMonitoringTemplateId,
        "data-project-action": "add-configuration-template",
        "data-requires-idle": "true"
      }
    });
    templateButton.disabled = !canEditStructure();
    templateButton.addEventListener("click", () => {
      closeMenus();
      void addClickMonitoringConfiguration();
    });
    appendMenuGroup(
      menu,
      "Configuration Templates",
      [templateButton]);
    const groups = new Map();
    for (const descriptor of state.catalogue.controlledUnitTypes) {
      if (descriptor.status?.availability !== "available") continue;
      const group = descriptor.palette?.menuGroup || "Other";
      if (!groups.has(group)) groups.set(group, []);
      const button = createElement("button", {
        type: "button",
        text: descriptor.palette?.registeredName || descriptor.typeId,
        title: descriptor.palette?.tooltip || "",
        attributes: {
          role: "menuitem",
          "data-project-add-unit-type": descriptor.typeId,
          "data-project-action": "add-unit",
          "data-requires-idle": "true"
        }
      });
      button.disabled =
        typeAtMaximum(descriptor) ||
        !currentModeAllowed(descriptor) ||
        !canEditStructure();
      button.addEventListener("click", () => {
        closeMenus();
        void addControlledUnit(descriptor.typeId);
      });
      groups.get(group).push(button);
    }
    for (const [group, items] of groups) {
      appendMenuGroup(menu, group, items);
    }
  }

  function renderSettingsMenu() {
    const menu = $("settingsMenu");
    menu.replaceChildren();
    const units = state.active?.project?.controlledUnits || [];
    const globalTypes = state.catalogue?.globalSettingsTypes || [];
    const globalItems = [];
    for (const descriptor of globalTypes) {
      if (descriptor.status?.availability !== "available") continue;
      const component =
        state.active?.project?.globalSettings?.components?.find(
          (candidate) => candidate.typeId === descriptor.typeId);
      if (!component) continue;
      const button = createElement("button", {
        type: "button",
        text: `${descriptor.name}\u2026`,
        attributes: {
          role: "menuitem",
          "data-global-settings-type": descriptor.typeId,
          "data-requires-idle": "true"
        }
      });
      button.disabled = !canEditStructure();
      button.addEventListener("click", () => {
        closeMenus();
        void configureGlobalSettings(descriptor.typeId);
      });
      globalItems.push(button);
    }
    if (globalItems.length) {
      appendMenuGroup(menu, "Global", globalItems);
    }
    if (!units.length && !globalItems.length) {
      menu.append(createElement("span", {
        className: "menu-empty",
        text: "No configurable project components are available."
      }));
      return;
    }
    const byGroup = new Map();
    for (const unit of units) {
      const descriptor = descriptorForUnit(unit);
      const group = descriptor?.palette?.menuGroup || "Other";
      if (!byGroup.has(group)) byGroup.set(group, []);
      const button = createElement("button", {
        type: "button",
        text: `${unit.name}…`,
        attributes: {
          role: "menuitem",
          ...(unit.typeId === "pamguard.sound-output"
            ? {
                "data-sound-output-settings-action": "",
                "data-sound-output-unit-id": unit.id
              }
            : unit.typeId === "pamguard.sound-recorder"
            ? {
                "data-sound-recorder-settings-action": "",
                "data-sound-recorder-unit-id": unit.id
              }
            : { "data-requires-idle": "true" })
        }
      });
      button.disabled = !canConfigureUnit(unit);
      button.addEventListener("click", () => {
        closeMenus();
        void configureUnit(unit.id);
      });
      byGroup.get(group).push(button);
      if (unit.typeId === "pamguard.acquisition") {
        const hostButton = createElement("button", {
          type: "button",
          text: `${unit.name} \u2014 Host input\u2026`,
          attributes: {
            role: "menuitem",
            "data-acquisition-host-action": unit.id
          }
        });
        hostButton.disabled = Boolean(
          state.commandBusy ||
          state.runtimeBusy ||
          state.conflicted);
        hostButton.addEventListener("click", () => {
          closeMenus();
          void configureAcquisitionHost(unit.id);
        });
        byGroup.get(group).push(hostButton);
      }
    }
    for (const [group, items] of byGroup) {
      appendMenuGroup(menu, group, items);
    }
  }

  function renderDisplayMenu() {
    const menu = $("displayMenu");
    menu.replaceChildren();
    const tabs = state.active?.project?.displayTabs || [];
    const owners = state.active?.project?.controlledUnits
      ?.filter((unit) => {
        const descriptor = descriptorForUnit(unit);
        return descriptor?.recipe
          ?.contributedDisplayProviderTypeIds?.length;
      }) || [];
    if (!owners.length) {
      menu.append(createElement("span", {
        className: "menu-empty",
        text: "Add a User Display controlled unit first."
      }));
      return;
    }

    for (const owner of owners) {
      const descriptor = descriptorForUnit(owner);
      const items = [];
      for (const providerTypeId of
        descriptor.recipe.contributedDisplayProviderTypeIds) {
        const provider = state.providerByType.get(providerTypeId);
        if (!provider ||
            provider.status?.availability !== "available") continue;
        const atMaximum =
          displayProviderAtMaximum(owner.id, provider);
        const button = createElement("button", {
          type: "button",
          text: `Add ${provider.name}…`,
          attributes: {
            role: "menuitem",
            "data-requires-idle": "true",
            "data-display-owner-unit-id": owner.id,
            "data-display-provider-type-id": provider.providerTypeId,
            "data-display-maximum-reached": atMaximum ? "true" : "false",
            "aria-disabled": atMaximum ? "true" : "false"
          }
        });
        button.disabled = !canEditStructure() || atMaximum;
        if (atMaximum) {
          button.title =
            `${provider.name} has reached its PAMGuard maximum of ` +
            `${provider.instanceRules.maximum} for ${owner.name}.`;
        }
        button.addEventListener("click", () => {
          closeMenus();
          void addDisplay(owner.id, provider.providerTypeId);
        });
        items.push(button);
      }
      const ownedTab = tabs.find(
        (tab) => tab.owner.unitId === owner.id);
      if (ownedTab) {
        const show = createElement("button", {
          type: "button",
          text: `Show ${ownedTab.name}`,
          attributes: { role: "menuitem" }
        });
        show.addEventListener("click", () => {
          closeMenus();
          activateTab(ownedTab.id);
        });
        items.push(show);
      }
      appendMenuGroup(menu, owner.name, items);
    }
  }

  function positionFor(unitId) {
    return state.positions.get(unitId) || { x: 40, y: 40 };
  }

  function displayProviderInstanceCount(ownerUnitId, providerTypeId) {
    return (state.active?.project?.displayTabs || [])
      .flatMap((tab) => tab.displays || [])
      .filter((display) =>
        display.owner?.unitId === ownerUnitId &&
        display.providerTypeId === providerTypeId)
      .length;
  }

  function displayProviderAtMaximum(ownerUnitId, provider) {
    const maximum = provider?.instanceRules?.maximum;
    return maximum !== null &&
      maximum !== undefined &&
      displayProviderInstanceCount(
        ownerUnitId,
        provider.providerTypeId) >= maximum;
  }

  function graphPortKey(unitId, roleId) {
    return `${unitId}\n${roleId}`;
  }

  function graphConnectionStatus(message) {
    const status = $("graphConnectionStatus");
    if (status) status.textContent = message;
  }

  function clearGraphConnectionPresentation() {
    $("dataModelCanvas")?.classList.remove("is-connecting");
    for (const output of document.querySelectorAll(
      ".port-output.is-connection-source")) {
      output.classList.remove("is-connection-source");
      output.setAttribute("aria-pressed", "false");
    }
    for (const input of document.querySelectorAll(
      ".port-input.is-compatible, .port-input.is-incompatible")) {
      input.classList.remove("is-compatible", "is-incompatible");
      input.removeAttribute("data-connection-state");
      input.removeAttribute("aria-disabled");
      const label = input.getAttribute("data-base-aria-label");
      if (label) input.setAttribute("aria-label", label);
    }
  }

  function cancelGraphConnection(message = "") {
    state.connectionDraft = null;
    clearGraphConnectionPresentation();
    graphConnectionStatus(message);
  }

  function graphInputTargets() {
    const targets = [];
    for (const unit of state.active?.project?.controlledUnits || []) {
      const descriptor = descriptorForUnit(unit);
      for (const input of descriptor?.inputs || []) {
        targets.push({ unit, input });
      }
    }
    return targets;
  }

  async function startGraphConnection(source) {
    if (!canEditStructure()) return null;
    cancelGraphConnection();
    const sourceUnit = unitById(source.unitId);
    const sourceDescriptor = sourceUnit && descriptorForUnit(sourceUnit);
    const output = sourceDescriptor?.outputs?.find(
      (candidate) => candidate.id === source.outputRole);
    if (!sourceUnit || !output) return null;

    const draft = {
      source: deepClone(source),
      sourceName: `${sourceUnit.name} — ${output.name}`,
      compatible: new Set(),
      ready: false,
      readyPromise: null
    };
    state.connectionDraft = draft;
    $("dataModelCanvas")?.classList.add("is-connecting");
    const sourceElement = document.querySelector(
      `.port-output[data-unit-id="${CSS.escape(source.unitId)}"]` +
      `[data-role-id="${CSS.escape(source.outputRole)}"]`);
    sourceElement?.classList.add("is-connection-source");
    sourceElement?.setAttribute("aria-pressed", "true");
    graphConnectionStatus(
      `Checking inputs compatible with ${draft.sourceName}.`);

    draft.readyPromise = (async () => {
      const compatibility = await Promise.all(
        graphInputTargets().map(async (target) => {
          const response = await projectClient.loadCompatibleSources(
            target.unit.id,
            target.input.id);
          const compatible = (response.sources || []).some(
            (candidate) =>
              candidate.unitId === source.unitId &&
              candidate.outputRole === source.outputRole);
          return { ...target, compatible };
        }));
      if (state.connectionDraft !== draft) return null;

      for (const target of compatibility) {
        const key = graphPortKey(target.unit.id, target.input.id);
        if (target.compatible) draft.compatible.add(key);
        const input = document.querySelector(
          `.port-input[data-unit-id="${CSS.escape(target.unit.id)}"]` +
          `[data-role-id="${CSS.escape(target.input.id)}"]`);
        if (!input) continue;
        input.classList.add(
          target.compatible ? "is-compatible" : "is-incompatible");
        input.setAttribute(
          "data-connection-state",
          target.compatible ? "compatible" : "incompatible");
        input.setAttribute(
          "aria-disabled",
          target.compatible ? "false" : "true");
        input.setAttribute(
          "aria-label",
          `${input.getAttribute("data-base-aria-label")}. ` +
          (target.compatible
            ? `Compatible with ${draft.sourceName}; activate to connect.`
            : `Incompatible with ${draft.sourceName}.`));
      }
      draft.ready = true;
      graphConnectionStatus(
        `${draft.sourceName} selected. ` +
        `${plural(draft.compatible.size, "compatible input")} available. ` +
        "Activate one, or press Escape to cancel.");
      return draft;
    })().catch((error) => {
      if (state.connectionDraft === draft) {
        cancelGraphConnection("Connection selection cancelled.");
        handleCommandError(error, "Could not discover compatible inputs");
      }
      return null;
    });
    return draft.readyPromise;
  }

  function inputAcceptsMultiple(input) {
    return input?.cardinality === "0..N" ||
      input?.cardinality === "1..N";
  }

  async function finishGraphConnection(target) {
    const draft = state.connectionDraft;
    if (!draft || !canEditStructure()) return;
    if (!draft.ready) await draft.readyPromise;
    if (state.connectionDraft !== draft) return;

    const key = graphPortKey(target.unitId, target.inputRole);
    if (!draft.compatible.has(key)) {
      graphConnectionStatus(
        `${draft.sourceName} is not compatible with that input.`);
      showToast(
        `${draft.sourceName} cannot feed that PAMGuard input.`,
        "warning");
      return;
    }
    const targetUnit = unitById(target.unitId);
    const descriptor = targetUnit && descriptorForUnit(targetUnit);
    const input = descriptor?.inputs?.find(
      (candidate) => candidate.id === target.inputRole);
    if (!targetUnit || !input) {
      cancelGraphConnection("Connection target is no longer available.");
      return;
    }
    const current = targetUnit.bindings?.find(
      (binding) => binding.inputRole === input.id)?.sources || [];
    const alreadyConnected = current.some(
      (source) =>
        source.unitId === draft.source.unitId &&
        source.outputRole === draft.source.outputRole);
    const next = inputAcceptsMultiple(input)
      ? [
          ...current.map((source) => ({
            unit: { id: source.unitId },
            outputRole: source.outputRole
          })),
          ...(alreadyConnected
            ? []
            : [{
                unit: { id: draft.source.unitId },
                outputRole: draft.source.outputRole
              }])
        ]
      : [{
          unit: { id: draft.source.unitId },
          outputRole: draft.source.outputRole
        }];
    const unchanged = current.length === next.length &&
      current.every((source, index) =>
        source.unitId === next[index].unit.id &&
        source.outputRole === next[index].outputRole);
    const sourceNameValue = draft.sourceName;
    cancelGraphConnection(
      unchanged
        ? `${sourceNameValue} is already connected.`
        : `Connecting ${sourceNameValue} to ${targetUnit.name}.`);
    if (unchanged) return;

    await applyMutation([
      {
        op: "setBinding",
        unit: { id: targetUnit.id },
        inputRole: input.id,
        sources: next
      }
    ], `${targetUnit.name} source connected to ${sourceNameValue}`);
  }

  function beginGraphOutputDrag(event, source) {
    if (event.button !== 0 || !canEditStructure()) return;
    event.stopPropagation();
    const startX = event.clientX;
    const startY = event.clientY;
    const outputKey = graphPortKey(source.unitId, source.outputRole);
    let dragging = false;
    let startPromise = null;
    const move = (moveEvent) => {
      if (dragging ||
          Math.hypot(
            moveEvent.clientX - startX,
            moveEvent.clientY - startY) < 5) return;
      dragging = true;
      startPromise = startGraphConnection(source);
    };
    const cleanup = () => {
      document.removeEventListener("pointermove", move);
      document.removeEventListener("pointerup", up);
      document.removeEventListener("pointercancel", cancel);
    };
    const up = async (upEvent) => {
      cleanup();
      if (!dragging) return;
      upEvent.preventDefault();
      state.suppressGraphOutputClick = outputKey;
      window.setTimeout(() => {
        if (state.suppressGraphOutputClick === outputKey) {
          state.suppressGraphOutputClick = "";
        }
      }, 0);
      await startPromise;
      const input = document.elementFromPoint(
        upEvent.clientX,
        upEvent.clientY)?.closest(".port-input");
      if (!input) {
        cancelGraphConnection("Connection selection cancelled.");
        return;
      }
      await finishGraphConnection({
        unitId: input.dataset.unitId,
        inputRole: input.dataset.roleId
      });
    };
    const cancel = () => {
      cleanup();
      if (dragging) {
        cancelGraphConnection("Connection selection cancelled.");
      }
    };
    document.addEventListener("pointermove", move);
    document.addEventListener("pointerup", up);
    document.addEventListener("pointercancel", cancel);
  }

  function renderGraph() {
    const units = state.active?.project?.controlledUnits || [];
    const nodes = $("modelNodes");
    nodes.replaceChildren();
    $("canvasEmpty").hidden = units.length !== 0;
    applyViewport();

    for (const unit of units) {
      const descriptor = descriptorForUnit(unit);
      const position = positionFor(unit.id);
      const issues = state.active?.projection?.issues
        ?.filter((issue) => issue.unitId === unit.id) || [];
      const node = createElement("article", {
        className:
          "controlled-unit-node " +
          (issues.length ? "node-needs-configuration" : "node-runnable"),
        attributes: {
          "data-controlled-unit-id": unit.id,
          "data-project-node-kind": "controlled-unit",
          tabindex: "0",
          "aria-label": `${unit.name} controlled unit`
        }
      });
      node.style.left = `${position.x}px`;
      node.style.top = `${position.y}px`;
      if (unit.id === state.selectedUnitId) {
        node.classList.add("selected");
      }

      const header = createElement("header", {
        className: "node-header"
      });
      const identity = createElement("div", {
        className: "node-identity"
      });
      identity.append(
        createElement("span", {
          className: "node-glyph",
          text: moduleGlyph(descriptor || { palette: {} })
        }),
        (() => {
          const copy = createElement("span");
          copy.append(
            createElement("strong", { text: unit.name }),
            createElement("small", {
              text: descriptor?.palette?.registeredName || unit.typeId
            }));
          return copy;
        })());
      const configure = createElement("button", {
        className: "node-configure icon-button",
        type: "button",
        text: "⚙",
        title: `Configure ${unit.name}`,
        attributes: {
          "aria-label": `Configure ${unit.name}`,
          "data-project-unit-action": "configure"
        }
      });
      configure.disabled = !canConfigureUnit(unit);
      configure.addEventListener("click", (event) => {
        event.stopPropagation();
        void configureUnit(unit.id);
      });
      header.append(identity, configure);
      header.addEventListener("pointerdown", (event) =>
        beginNodeDrag(event, unit.id, node));

      const status = createElement("div", {
        className: "node-status"
      });
      status.append(
        createElement("span", {
          className: issues.length ? "status-warn" : "status-ok",
          text: issues.length
            ? humanize(issues[0].class)
            : "Configured"
        }),
        createElement("span", {
          text: descriptor?.javaAuthority?.relationship || "controlled unit"
        }));

      const ports = createElement("div", { className: "node-ports" });
      const inputs = createElement("div", { className: "node-inputs" });
      const outputs = createElement("div", { className: "node-outputs" });
      for (const [index, input] of
        (descriptor?.inputs || []).entries()) {
        const binding = unit.bindings?.find(
          (candidate) => candidate.inputRole === input.id);
        const source = binding?.sources?.[0] || null;
        const row = createElement("button", {
          type: "button",
          className: "port-row port-input",
          title: source
            ? `Source: ${sourceName(source)}`
            : `Configure ${input.name}`,
          attributes: {
            "data-unit-id": unit.id,
            "data-port-direction": "input",
            "data-role-id": input.id,
            "data-role-index": index,
            "data-base-aria-label":
              `${input.name} input on ${unit.name}` +
              (source ? `; source ${sourceName(source)}` : "; unbound"),
            "aria-label":
              `${input.name} input on ${unit.name}` +
              (source ? `; source ${sourceName(source)}` : "; unbound")
          }
        });
        row.disabled = !canConfigureUnit(unit);
        row.append(
          createElement("i", {
            className: "port-socket",
            attributes: { "aria-hidden": "true" }
          }),
          createElement("span", { text: input.name }));
        row.addEventListener("click", (event) => {
          event.stopPropagation();
          if (state.connectionDraft) {
            void finishGraphConnection({
              unitId: unit.id,
              inputRole: input.id
            });
          }
          else {
            void configureUnit(unit.id);
          }
        });
        inputs.append(row);
      }
      for (const [index, output] of
        (descriptor?.outputs || []).entries()) {
        const row = createElement("button", {
          type: "button",
          className: "port-row port-output",
          title: `${output.dataType} · ${output.capabilities?.join(", ")}`,
          attributes: {
            "data-unit-id": unit.id,
            "data-port-direction": "output",
            "data-role-id": output.id,
            "data-role-index": index,
            "aria-label":
              `${output.name} output from ${unit.name}; ` +
              "click or drag to connect",
            "aria-pressed": "false"
          }
        });
        row.disabled = !canEditStructure();
        row.append(
          createElement("span", { text: output.name }),
          createElement("i", {
            className: "port-socket",
            attributes: { "aria-hidden": "true" }
          }));
        row.addEventListener("pointerdown", (event) =>
          beginGraphOutputDrag(event, {
            unitId: unit.id,
            outputRole: output.id
          }));
        row.addEventListener("click", (event) => {
          event.stopPropagation();
          const key = graphPortKey(unit.id, output.id);
          if (state.suppressGraphOutputClick === key) {
            state.suppressGraphOutputClick = "";
            return;
          }
          if (state.connectionDraft?.source.unitId === unit.id &&
              state.connectionDraft?.source.outputRole === output.id) {
            cancelGraphConnection("Connection selection cancelled.");
            return;
          }
          void startGraphConnection({
            unitId: unit.id,
            outputRole: output.id
          });
        });
        outputs.append(row);
      }
      ports.append(inputs, outputs);
      node.append(header, status, ports);
      node.addEventListener("click", () => {
        state.selectedUnitId = unit.id;
        renderGraph();
      });
      node.addEventListener("dblclick", () => void configureUnit(unit.id));
      node.addEventListener("contextmenu", (event) => {
        event.preventDefault();
        showContextMenu(event.clientX, event.clientY, unit.id);
      });
      node.addEventListener("keydown", (event) => {
        if (event.key === "Enter") void configureUnit(unit.id);
        if (event.key === "ContextMenu" ||
            (event.shiftKey && event.key === "F10")) {
          event.preventDefault();
          const bounds = node.getBoundingClientRect();
          showContextMenu(bounds.left + 40, bounds.top + 40, unit.id);
        }
      });
      nodes.append(node);
    }
    requestAnimationFrame(drawWires);
  }

  function applyViewport() {
    const viewport = state.viewport;
    $("modelWorld").style.transform =
      `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.zoom})`;
    $("zoomValue").textContent =
      `${Math.round(viewport.zoom * 100)}%`;
  }

  function drawWires() {
    const svg = $("modelWires");
    svg.replaceChildren();
    svg.setAttribute("viewBox", `0 0 ${WORLD_WIDTH} ${WORLD_HEIGHT}`);
    const units = state.active?.project?.controlledUnits || [];
    for (const target of units) {
      const targetDescriptor = descriptorForUnit(target);
      for (const binding of target.bindings || []) {
        const inputIndex = Math.max(
          0,
          targetDescriptor?.inputs
            ?.findIndex((input) => input.id === binding.inputRole) ?? 0);
        for (const source of binding.sources || []) {
          const sourceUnit = unitById(source.unitId);
          if (!sourceUnit) continue;
          const sourceDescriptor = descriptorForUnit(sourceUnit);
          const outputIndex = Math.max(
            0,
            sourceDescriptor?.outputs
              ?.findIndex(
                (output) => output.id === source.outputRole) ?? 0);
          const sourcePosition = positionFor(sourceUnit.id);
          const targetPosition = positionFor(target.id);
          const x1 = sourcePosition.x + UNIT_WIDTH;
          const y1 = sourcePosition.y + ROLE_TOP +
            outputIndex * ROLE_GAP;
          const x2 = targetPosition.x;
          const y2 = targetPosition.y + ROLE_TOP +
            inputIndex * ROLE_GAP;
          const bend = Math.max(80, Math.abs(x2 - x1) * 0.48);
            const path = createElement("path", {
              className: "model-wire",
              attributes: {
                "data-source-unit-id": source.unitId,
                "data-source-output-role": source.outputRole,
                "data-target-unit-id": target.id,
                "data-target-input-role": binding.inputRole,
                d: `M ${x1} ${y1} C ${x1 + bend} ${y1}, ` +
                  `${x2 - bend} ${y2}, ${x2} ${y2}`
              }
          });
          svg.append(path);
        }
      }
    }
  }

  function beginNodeDrag(event, unitId, node) {
    if (event.button !== 0 ||
        event.target.closest("button") ||
        !canEditStructure()) return;
    event.preventDefault();
    state.selectedUnitId = unitId;
    const start = positionFor(unitId);
    const zoom =
      state.active.project.dataModelLayout.viewport.zoom || 1;
    state.drag = {
      unitId,
      startX: event.clientX,
      startY: event.clientY,
      node,
      x: start.x,
      y: start.y,
      zoom,
      cancel: null
    };
    node.classList.add("dragging", "selected");
    const move = (moveEvent) => {
      if (!state.drag) return;
      const x = Math.max(
        0,
        state.drag.x +
          (moveEvent.clientX - state.drag.startX) / state.drag.zoom);
      const y = Math.max(
        0,
        state.drag.y +
          (moveEvent.clientY - state.drag.startY) / state.drag.zoom);
      state.positions.set(unitId, { x, y });
      node.style.left = `${x}px`;
      node.style.top = `${y}px`;
      drawWires();
    };
    const finish = (persist) => {
      document.removeEventListener("pointermove", move);
      document.removeEventListener("pointerup", end);
      document.removeEventListener("pointercancel", end);
      node.classList.remove("dragging");
      state.drag = null;
      if (persist && !state.disposed) scheduleLayoutSave();
    };
    const end = () => finish(true);
    state.drag.cancel = () => finish(false);
    document.addEventListener("pointermove", move);
    document.addEventListener("pointerup", end, { once: true });
    document.addEventListener("pointercancel", end, { once: true });
  }

  function currentLayout() {
    const viewport = state.viewport;
    return {
      nodes: (state.active?.project?.controlledUnits || []).map((unit) => {
        const position = positionFor(unit.id);
        return {
          unitId: unit.id,
          x: Math.round(position.x * 10) / 10,
          y: Math.round(position.y * 10) / 10
        };
      }),
      viewport: {
        x: Math.round(viewport.x * 10) / 10,
        y: Math.round(viewport.y * 10) / 10,
        zoom: Math.round(viewport.zoom * 1000) / 1000
      }
    };
  }

  function scheduleLayoutSave() {
    state.layoutDirty = true;
    renderProjectChrome();
    renderControls();
    void (async () => {
      const result = await applyMutation([
        {
          op: "replaceDataModelLayout",
          layout: currentLayout()
        }
      ]);
      if (result) {
        state.layoutDirty = false;
        renderProjectChrome();
        renderControls();
      }
    })();
  }

  function setViewport(next, persist = true) {
    if (persist && !canEditStructure()) return;
    state.viewport = {
      x: next.x,
      y: next.y,
      zoom: Math.max(0.1, Math.min(8, next.zoom))
    };
    applyViewport();
    if (persist && canEditStructure()) scheduleLayoutSave();
  }

  function arrangeGraph() {
    if (!canEditStructure()) return;
    const units = state.active.project.controlledUnits;
    const levelById = new Map();
    const visiting = new Set();
    const levelFor = (unit) => {
      if (levelById.has(unit.id)) return levelById.get(unit.id);
      if (visiting.has(unit.id)) return 0;
      visiting.add(unit.id);
      let level = 0;
      for (const binding of unit.bindings || []) {
        for (const source of binding.sources || []) {
          const sourceUnit = unitById(source.unitId);
          if (sourceUnit) level = Math.max(level, levelFor(sourceUnit) + 1);
        }
      }
      visiting.delete(unit.id);
      levelById.set(unit.id, level);
      return level;
    };
    const columns = new Map();
    for (const unit of units) {
      const level = levelFor(unit);
      if (!columns.has(level)) columns.set(level, []);
      columns.get(level).push(unit);
    }
    for (const [level, column] of columns) {
      column.forEach((unit, index) => {
        state.positions.set(unit.id, {
          x: 80 + level * 380,
          y: 80 + index * 245
        });
      });
    }
    renderGraph();
    scheduleLayoutSave();
  }

  function fitGraph() {
    const units = state.active?.project?.controlledUnits || [];
    if (!units.length) {
      setViewport({ x: 0, y: 0, zoom: 1 });
      return;
    }
    const canvas = $("dataModelCanvas").getBoundingClientRect();
    const positions = units.map((unit) => positionFor(unit.id));
    const minX = Math.min(...positions.map((position) => position.x));
    const minY = Math.min(...positions.map((position) => position.y));
    const maxX = Math.max(...positions.map(
      (position) => position.x + UNIT_WIDTH));
    const maxY = Math.max(...positions.map(
      (position) => position.y + 220));
    const zoom = Math.max(0.35, Math.min(
      1.25,
      Math.min(
        (canvas.width - 100) / Math.max(1, maxX - minX),
        (canvas.height - 100) / Math.max(1, maxY - minY))));
    setViewport({
      x: 50 - minX * zoom,
      y: 50 - minY * zoom,
      zoom
    });
  }

  function showContextMenu(x, y, unitId) {
    state.contextUnitId = unitId;
    state.selectedUnitId = unitId;
    const menu = $("unitContextMenu");
    menu.hidden = false;
    menu.style.left = `${Math.min(
      x,
      window.innerWidth - menu.offsetWidth - 12)}px`;
    menu.style.top = `${Math.min(
      y,
      window.innerHeight - menu.offsetHeight - 12)}px`;
    const unit = unitById(unitId);
    const configure = menu.querySelector(
      "[data-unit-action='configure']");
    if (configure) configure.disabled = !canConfigureUnit(unit);
    for (const item of menu.querySelectorAll(
      "[data-unit-action='rename'], " +
      "[data-unit-action='remove']")) {
      item.disabled = !canEditStructure();
    }
    renderGraph();
  }

  function hideContextMenu() {
    $("unitContextMenu").hidden = true;
    state.contextUnitId = null;
  }

  function activateTab(tabId) {
    const target = tabId === "data-model"
      ? $("dataModelTab")
      : document.querySelector(
        `[role="tab"][data-tab="${CSS.escape(tabId)}"]`);
    if (!target) tabId = "data-model";
    state.activeTabId = tabId;
    for (const tab of $("tabStrip").querySelectorAll("[role='tab']")) {
      const active = tab.dataset.tab === tabId;
      tab.classList.toggle("active", active);
      tab.setAttribute("aria-selected", String(active));
      tab.tabIndex = active ? 0 : -1;
    }
    for (const panel of $("tabHost").querySelectorAll(
      ":scope > .tab-panel, #displayPanels > .tab-panel")) {
      const active = panel.dataset.panel === tabId;
      panel.classList.toggle("active", active);
      panel.hidden = !active;
    }
  }

  function renderDisplays() {
    disposeDisplayRuntimes();
    for (const oldTab of $("tabStrip").querySelectorAll(
      "[data-pamguard-tab-kind='display']")) {
      oldTab.remove();
    }
    const panels = $("displayPanels");
    panels.replaceChildren();
    const tabs = state.active?.project?.displayTabs || [];
    const knownTabs = new Set(["data-model"]);

    for (const tab of tabs) {
      knownTabs.add(tab.id);
      const ownerId = tab.owner.unitId;
      const button = createElement("button", {
        type: "button",
        text: tab.name,
        attributes: {
          role: "tab",
          "aria-selected": "false",
          "data-tab": tab.id,
          "data-pamguard-tab-kind": "display",
          "data-owner-controlled-unit-id": ownerId,
          "aria-controls": `display-panel-${tab.id}`
        }
      });
      button.addEventListener("click", () => activateTab(tab.id));
      $("tabStrip").append(button);

      const panel = createElement("section", {
        className: "tab-panel",
        id: `display-panel-${tab.id}`,
        attributes: {
          role: "tabpanel",
          "data-panel": tab.id,
          "data-owner-controlled-unit-id": ownerId
        }
      });
      panel.hidden = true;
      const header = createElement("header", {
        className: "display-tab-header"
      });
      const copy = createElement("div");
      copy.append(
        createElement("span", {
          className: "eyebrow",
          text: ownerUnitName(ownerId)
        }),
        createElement("h2", { text: tab.name }));
      const add = createElement("button", {
        type: "button",
        text: "＋ Add display",
        attributes: { "data-requires-idle": "true" }
      });
      add.disabled = !canEditStructure();
      const ownerDescriptor = descriptorForUnit(unitById(ownerId));
      const firstProvider =
        ownerDescriptor?.recipe
          ?.contributedDisplayProviderTypeIds?.[0];
      add.addEventListener("click", () => {
        if (firstProvider) void addDisplay(ownerId, firstProvider);
      });
      header.append(copy, add);
      panel.append(header);

      const localSelection =
        state.localDisplaySelections.get(tab.id);
      const selectedDisplayId =
        (
          tab.displays.some(
            (display) => display.id === localSelection)
            ? localSelection
            : null
        ) ||
        tab.layout.selectedDisplayId ||
        tab.displays[0]?.id ||
        null;
      if (tab.layout.mode === "tabs" && tab.displays.length > 1) {
        const localTabs = createElement("nav", {
          className: "display-local-tabs",
          attributes: { "aria-label": `${tab.name} displays` }
        });
        for (const display of tab.displays) {
          const provider =
            state.providerByType.get(display.providerTypeId);
          const localTab = createElement("button", {
            type: "button",
            text: provider?.name || display.providerTypeId,
            attributes: {
              "aria-pressed": String(display.id === selectedDisplayId)
            }
          });
          localTab.addEventListener("click", () => {
            state.localDisplaySelections.set(tab.id, display.id);
            renderDisplays();
            if (!canEditStructure()) return;
            const displayTabs = deepClone(
              state.active.project.displayTabs);
            const next = displayTabs.find(
              (candidate) => candidate.id === tab.id);
            next.layout.selectedDisplayId = display.id;
            void applyMutation([
              { op: "replaceDisplayHierarchy", displayTabs }
            ], `${provider?.name || "Display"} selected`);
          });
          localTabs.append(localTab);
        }
        panel.append(localTabs);
      }

      const surfaceGrid = createElement("div", {
        className: `display-grid display-layout-${tab.layout.mode}`
      });
      surfaceGrid.style.setProperty(
        "--display-columns",
        String(tab.layout.columns || 12));
      if (!tab.displays.length) {
        const empty = createElement("div", {
          className: "display-placeholder"
        });
        empty.append(
          createElement("span", {
            className: "empty-symbol",
            text: "▣"
          }),
          createElement("h3", { text: "This display tab is empty" }),
          createElement("p", {
            text: "Use Display or Add display to create an " +
              "instance owned by this User Display controlled unit."
          }));
        surfaceGrid.append(empty);
      }
      for (const display of tab.displays) {
        const provider =
          state.providerByType.get(display.providerTypeId);
        const surface = createElement("article", {
          className: "display-surface",
          attributes: {
            "data-pamguard-display-instance-id": display.id,
            "data-owner-controlled-unit-id": display.owner.unitId
          }
        });
        const layoutItem = tab.layout.items.find(
          (item) => item.displayId === display.id);
        if (tab.layout.mode === "grid" && layoutItem) {
          surface.style.gridColumn =
            `${layoutItem.column + 1} / span ${layoutItem.width}`;
          surface.style.gridRow =
            `${layoutItem.row + 1} / span ${layoutItem.height}`;
        }
        if (tab.layout.mode === "tabs" &&
            display.id !== selectedDisplayId) {
          surface.hidden = true;
        }
        const displayHead = createElement("header");
        const displayCopy = createElement("div");
        displayCopy.append(
          createElement("strong", {
            text: provider?.name || display.providerTypeId
          }),
          createElement("small", {
            text: sourceName(display.source)
          }));
        const actions = createElement("div", {
          className: "display-actions"
        });
        const configure = createElement("button", {
          type: "button",
          className: "secondary",
          text: "Configure",
          attributes: {
            "data-requires-idle": "true",
            "data-project-display-action": "configure"
          }
        });
        configure.disabled = !canEditStructure();
        configure.addEventListener("click", () =>
          void configureDisplay(tab.id, display.id));
        const remove = createElement("button", {
          type: "button",
          className: "secondary danger",
          text: "Remove",
          attributes: {
            "data-requires-idle": "true",
            "data-project-display-action": "remove"
          }
        });
        remove.disabled = !canEditStructure();
        remove.addEventListener("click", () =>
          void removeDisplay(tab.id, display.id));
        actions.append(configure, remove);
        displayHead.append(displayCopy, actions);

        const visual = createElement("div", {
          className: "spectrogram-foundation"
        });
        let displayRuntime = null;
        if (display.providerTypeId ===
            "pamguard.spectrogram-display") {
          const canvas = createElement("canvas", {
            className: "project-spectrogram-canvas",
            attributes: {
              "aria-label":
                `${provider?.name || "Spectrogram"} live display`
            }
          });
          const runtimeMessage = createElement("div", {
            className: "display-runtime-message",
            attributes: {
              role: "status",
              "data-display-stream-status": display.id
            }
          });
          visual.append(canvas, runtimeMessage);
          const metadata = spectrogramSourceMetadata(display);
          displayRuntime = projectDisplays.mountSpectrogram({
            canvas,
            status: runtimeMessage,
            settings: display.settings,
            sourceBlockId: metadata.sourceBlockId,
            sampleRateHz: metadata.sampleRateHz,
            fftLength: metadata.fftLength,
            running: state.runtime?.running,
            api: (path) => httpClient.api(path),
            headers: streamHeaders
          });
        }
        else if (display.providerTypeId ===
            "pamguard.click-display") {
          visual.classList.add("click-display-foundation");
          const trackedControls = createElement("div", {
            className: "project-tracked-click-controls"
          });
          const canvas = createElement("canvas", {
            className: "project-click-display-canvas",
            attributes: {
              "aria-label":
              `${provider?.name || "Click display"} live display`
            }
          });
          const detail = createElement("div", {
            className: "project-click-detail",
            attributes: {
              "data-click-selected-detail": display.id
            }
          });
          detail.hidden = true;
          const detailStatus = createElement("div", {
            className: "project-click-detail-meta",
            attributes: {
              "data-click-selected-meta": display.id
            }
          });
          const waveformPanel = createElement("section", {
            className: "project-click-detail-panel"
          });
          waveformPanel.append(createElement("strong", {
            text: "Waveform"
          }));
          const waveformCanvas = createElement("canvas", {
            className: "project-click-waveform-canvas",
            attributes: {
              "aria-label": "Selected click waveform"
            }
          });
          waveformPanel.append(waveformCanvas);
          const spectrumPanel = createElement("section", {
            className: "project-click-detail-panel"
          });
          const spectrumHeading = createElement("div", {
            className: "project-click-detail-heading"
          });
          spectrumHeading.append(createElement("strong", {
            text: "FFT Spectrum"
          }));
          const spectrumStatus = createElement("span", {
            className: "project-click-spectrum-meta",
            attributes: {
              "data-click-spectrum-meta": display.id
            }
          });
          spectrumHeading.append(spectrumStatus);
          const spectrumCanvas = createElement("canvas", {
            className: "project-click-spectrum-canvas",
            attributes: {
              "aria-label": "Selected click power spectrum"
            }
          });
          spectrumPanel.append(
            spectrumHeading,
            spectrumCanvas);
          const wignerPanel = createElement("section", {
            className: "project-click-detail-panel"
          });
          const wignerHeading = createElement("div", {
            className: "project-click-detail-heading"
          });
          wignerHeading.append(createElement("strong", {
            text: "Wigner–Ville"
          }));
          const wignerStatus = createElement("span", {
            className: "project-click-wigner-meta",
            attributes: {
              "data-click-wigner-meta": display.id
            }
          });
          wignerHeading.append(wignerStatus);
          const wignerCanvas = createElement("canvas", {
            className: "project-click-wigner-canvas",
            attributes: {
              "aria-label":
                "Selected click Wigner–Ville distribution"
            }
          });
          wignerPanel.append(
            wignerHeading,
            wignerCanvas);
          detail.append(
            detailStatus,
            waveformPanel,
            spectrumPanel,
            wignerPanel);
          const runtimeMessage = createElement("div", {
            className: "display-runtime-message",
            attributes: {
              role: "status",
              "data-display-stream-status": display.id
            }
          });
          visual.append(
            trackedControls,
            canvas,
            detail,
            runtimeMessage);
          const metadata = clickDisplaySourceMetadata(display);
          displayRuntime = projectDisplays.mountClickDisplay({
            canvas,
            status: runtimeMessage,
            controlsRoot: trackedControls,
            detailRoot: detail,
            detailStatus,
            waveformCanvas,
            spectrumCanvas,
            spectrumStatus,
            wignerCanvas,
            wignerStatus,
            clickDetectorUnitId: display.owner.unitId,
            settings: display.settings,
            sourceBlockId: metadata.sourceBlockId,
            bearingBlockId: metadata.bearingBlockId,
            sampleRateHz: metadata.sampleRateHz,
            running: state.runtime?.running,
            api: (path) => httpClient.api(path),
            headers: streamHeaders
          });
        }
        else if (display.providerTypeId ===
            "pamguard.level-meter-display") {
          visual.classList.add("level-meter-display-foundation");
          const meter = createElement("div", {
            className: "project-level-meter",
            attributes: {
              "aria-label":
                `${provider?.name || "Level Meter"} live display`
            }
          });
          const runtimeMessage = createElement("div", {
            className: "display-runtime-message",
            attributes: {
              role: "status",
              "data-display-stream-status": display.id
            }
          });
          visual.append(meter, runtimeMessage);
          const metadata = levelDisplaySourceMetadata(display);
          const ownerSettings =
            unitById(display.owner.unitId)?.settings || {};
          displayRuntime = projectDisplays.mountLevelMeter({
            container: meter,
            status: runtimeMessage,
            settings: ownerSettings,
            sourceBlockId: metadata.sourceBlockId,
            running: state.runtime?.running,
            api: (path) => httpClient.api(path),
            headers: streamHeaders
          });
        }
        else {
          visual.append(createElement("div", {
            className: "display-runtime-message",
            text: "This display provider has no project-shell renderer yet."
          }));
        }
        const footer = createElement("footer");
        const limits = display.settings?.frequencyLimits;
        if (display.providerTypeId === "pamguard.click-display") {
          footer.textContent =
            `History ${display.settings?.timeWindowSeconds || 20} s · ` +
            `channels ${display.settings?.channelBitmap || "all"}`;
        }
        else if (display.providerTypeId ===
            "pamguard.level-meter-display") {
          const meterSettings =
            unitById(display.owner.unitId)?.settings || {};
          footer.textContent =
            `${Number(meterSettings.scaleType) === 1 ? "RMS" : "Peak"} ` +
            `\u00b7 ${Math.abs(Number(meterSettings.minLevel) || -80)} dB ` +
            "range";
        }
        else {
          footer.textContent = Array.isArray(limits)
            ? `Frequency ${limits[0]}–${limits[1] || "Nyquist"} Hz`
            : `Provider ${display.providerTypeId}`;
        }
        surface.append(displayHead, visual, footer);
        surfaceGrid.append(surface);
        if (displayRuntime) {
          state.displayRuntimes.set(display.id, displayRuntime);
        }
      }
      panel.append(surfaceGrid);
      panels.append(panel);
    }

    if (!knownTabs.has(state.activeTabId)) {
      state.activeTabId = "data-model";
    }
    activateTab(state.activeTabId);
  }

  function renderAll() {
    if (state.disposed) return;
    renderProjectChrome();
    renderPalette();
    renderApplicationMenus();
    renderGraph();
    renderDisplays();
    renderRuntimeStatus();
    renderControls();
  }

  function defaultUnitName(descriptor) {
    const base = descriptor.palette?.registeredName || "PAMGuard Module";
    const authorityClass = descriptor.javaAuthority?.className;
    const names = new Set(
      state.active.project.controlledUnits
        .filter((unit) => {
          const existing = descriptorForUnit(unit);
          return authorityClass && existing?.javaAuthority?.className
            ? existing.javaAuthority.className === authorityClass
            : unit.typeId === descriptor.typeId;
        })
        .map((unit) => unit.name.trim().toLocaleLowerCase("en-US")));
    const available = (candidate) =>
      !names.has(candidate.toLocaleLowerCase("en-US"));
    if (available(base)) return base;
    for (let suffix = 2; suffix < 100000; suffix += 1) {
      const candidate = `${base} ${suffix}`;
      if (available(candidate)) return candidate;
    }
    return `${base} ${platform.identifiers.uuidV4().slice(0, 8)}`;
  }

  async function addControlledUnit(typeId) {
    const descriptor = state.descriptorByType.get(typeId);
    if (!descriptor || !canEditStructure()) return;
    const content = createElement("div", {
      className: "dialog-stack"
    });
    const field = createElement("label", {
      className: "dialog-field"
    });
    field.append(createElement("span", { text: "Instance name" }));
    const name = createElement("input", {
      attributes: {
        name: "unitName",
        maxlength: "50",
        required: "required",
        autocomplete: "off"
      }
    });
    name.value = defaultUnitName(descriptor);
    field.append(name);
    content.append(
      createElement("p", {
        className: "dialog-intro",
        text: descriptor.palette?.tooltip || ""
      }),
      field);
    const dependencies = descriptor.inputs
      ?.filter((input) => input.defaultProvider) || [];
    if (dependencies.length) {
      content.append(createElement("div", {
        className: "dialog-callout",
        text: "If a required source is missing, PAMGuard will add and " +
          "bind its authoritative default provider in the same change."
      }));
    }
    const accepted = await showFormDialog({
      eyebrow: descriptor.palette?.menuGroup || "Add Modules",
      title: `Add ${descriptor.palette?.registeredName}`,
      body: content,
      acceptLabel: "Add module",
      focus: name
    });
    if (!accepted) return;
    const result = await applyMutation([
      {
        op: "addControlledUnit",
        clientRef: `add-${platform.identifiers.uuidV4()}`,
        typeId,
        name: name.value,
        dependencyPolicy: "add-defaults"
      }
    ], `${name.value} added to the Data Model`);
    if (result) {
      const created = result.createdEntities?.find(
        (entry) => entry.clientRef.startsWith("add-"));
      state.selectedUnitId = created?.id || null;
      renderGraph();
    }
  }

  async function addClickMonitoringConfiguration() {
    if (!canEditStructure()) return;
    const acquisitionCount =
      state.active.project.controlledUnits.filter(
        (unit) => unit.typeId === "pamguard.acquisition").length;
    const preview =
      projectTemplates.createClickMonitoringPreview({
        acquisitionCount
      });
    const accepted = await showFormDialog({
      eyebrow: "Add Modules \u00b7 Configuration Template",
      title: "Click monitoring configuration",
      body: preview.body,
      acceptLabel: preview.canCreate
        ? "Create configuration"
        : "Close",
      cancelHidden: !preview.canCreate,
      note: preview.canCreate
        ? "One atomic project change \u00b7 modules remain independent"
        : "No project change will be made"
    });
    if (!accepted || !preview.canCreate) return;

    const clientRef =
      `template-${platform.identifiers.uuidV4()}`;
    const result = await applyMutation([
      {
        op: "addConfigurationTemplate",
        clientRef,
        templateId: projectTemplates.clickMonitoringTemplateId
      }
    ], "Click monitoring configuration added to the Data Model");
    if (!result) return;
    const clickDetector = result.createdEntities?.find(
      (entry) =>
        entry.clientRef === `${clientRef}:clickDetector`);
    state.selectedUnitId = clickDetector?.id || null;
    state.activeTabId = "data-model";
    renderGraph();
  }

  function schemaTypes(schema) {
    return Array.isArray(schema?.type)
      ? schema.type
      : schema?.type ? [schema.type] : [];
  }

  function isNullable(schema) {
    return schemaTypes(schema).includes("null");
  }

  function appendSettingsFields(
    container,
    schema,
    value,
    collectors,
    path = []) {
    const properties = schema?.properties || {};
    for (const [key, propertySchema] of Object.entries(properties)) {
      const fieldPath = [...path, key];
      const current = value?.[key];
      const types = schemaTypes(propertySchema);
      const label = createElement("label", {
        className: "settings-field"
      });
      const heading = createElement("span", {
        className: "settings-label",
        text: humanize(key)
      });
      label.append(heading);

      if (types.includes("object") ||
          (!types.length && propertySchema.properties)) {
        const group = createElement("fieldset", {
          className: "settings-object"
        });
        group.append(createElement("legend", { text: humanize(key) }));
        appendSettingsFields(
          group,
          propertySchema,
          current || {},
          collectors,
          fieldPath);
        container.append(group);
        continue;
      }

      let control;
      if (types.includes("boolean")) {
        label.classList.add("settings-field-checkbox");
        control = createElement("input", {
          type: "checkbox",
          attributes: {
            "data-setting-path": fieldPath.join("/"),
            "data-setting-pointer": `/${fieldPath.join("/")}`
          }
        });
        control.checked = Boolean(current);
        label.replaceChildren(control, heading);
        collectors.push(() => ({
          path: fieldPath,
          value: control.checked
        }));
      }
      else if (types.includes("array")) {
        const itemTypes = schemaTypes(propertySchema.items || {});
        const simple = itemTypes.some(
          (type) => ["number", "integer", "string"].includes(type));
        control = simple
          ? createElement("input", {
              attributes: {
                "data-setting-path": fieldPath.join("/"),
                "data-setting-pointer": `/${fieldPath.join("/")}`,
                placeholder: "Comma-separated values"
              }
            })
          : createElement("textarea", {
              attributes: {
                "data-setting-path": fieldPath.join("/"),
                "data-setting-pointer": `/${fieldPath.join("/")}`,
                rows: "4"
              }
            });
        control.value = simple
          ? (Array.isArray(current) ? current.join(", ") : "")
          : JSON.stringify(current ?? [], null, 2);
        collectors.push(() => {
          let parsed;
          if (simple) {
            const entries = control.value.trim()
              ? control.value.split(",").map((entry) => entry.trim())
              : [];
            parsed = entries.map((entry) => {
              if (itemTypes.includes("integer")) {
                const number = Number(entry);
                if (!Number.isInteger(number)) {
                  throw new Error(`${humanize(key)} requires integers`);
                }
                return number;
              }
              if (itemTypes.includes("number")) {
                const number = Number(entry);
                if (!Number.isFinite(number)) {
                  throw new Error(`${humanize(key)} requires numbers`);
                }
                return number;
              }
              return entry;
            });
          }
          else {
            parsed = JSON.parse(control.value);
            if (!Array.isArray(parsed)) {
              throw new Error(`${humanize(key)} must be a JSON array`);
            }
          }
          return { path: fieldPath, value: parsed };
        });
      }
      else if (propertySchema.enum) {
        control = createElement("select", {
          attributes: {
            "data-setting-path": fieldPath.join("/"),
            "data-setting-pointer": `/${fieldPath.join("/")}`
          }
        });
        for (const optionValue of propertySchema.enum) {
          const option = createElement("option", {
            text: String(optionValue),
            attributes: { value: String(optionValue) }
          });
          if (optionValue === current) option.selected = true;
          control.append(option);
        }
        collectors.push(() => ({
          path: fieldPath,
          value: control.value
        }));
      }
      else if (types.includes("number") || types.includes("integer")) {
        control = createElement("input", {
          type: "number",
          attributes: {
            "data-setting-path": fieldPath.join("/"),
            "data-setting-pointer": `/${fieldPath.join("/")}`,
            step: types.includes("integer") ? "1" : "any",
            ...(propertySchema.minimum !== undefined
              ? { min: propertySchema.minimum }
              : {}),
            ...(propertySchema.maximum !== undefined
              ? { max: propertySchema.maximum }
              : {})
          }
        });
        control.value = current ?? "";
        collectors.push(() => {
          if (control.value === "" && isNullable(propertySchema)) {
            return { path: fieldPath, value: null };
          }
          const number = Number(control.value);
          if (!Number.isFinite(number) ||
              (types.includes("integer") && !Number.isInteger(number))) {
            throw new Error(`${humanize(key)} has an invalid number`);
          }
          return { path: fieldPath, value: number };
        });
      }
      else {
        control = createElement("input", {
          attributes: {
            "data-setting-path": fieldPath.join("/"),
            "data-setting-pointer": `/${fieldPath.join("/")}`,
            ...(propertySchema.minLength
              ? { minlength: propertySchema.minLength }
              : {})
          }
        });
        control.value = current ?? "";
        collectors.push(() => ({
          path: fieldPath,
          value: control.value === "" && isNullable(propertySchema)
            ? null
            : control.value
        }));
      }
      label.append(control);
      container.append(label);
    }
  }

  function setPath(target, path, value) {
    let cursor = target;
    path.forEach((key, index) => {
      if (index === path.length - 1) {
        cursor[key] = value;
      }
      else {
        const nextKey = path[index + 1];
        const needsArray =
          typeof nextKey === "number" ||
          (typeof nextKey === "string" && /^\d+$/.test(nextKey));
        if (!cursor[key] ||
            typeof cursor[key] !== "object" ||
            Array.isArray(cursor[key]) !== needsArray) {
          cursor[key] = needsArray ? [] : {};
        }
        cursor = cursor[key];
      }
    });
  }

  function collectSettings(base, collectors) {
    const result = deepClone(base || {});
    for (const collect of collectors) {
      const update = collect();
      setPath(result, update.path, update.value);
    }
    return result;
  }

  function settingsSectionSummary(settingsDescriptor) {
    const labels = settingsDescriptor?.sections
      ?.flatMap((section) => section.labels || []) || [];
    if (!labels.length) return null;
    const panel = createElement("details", {
      className: "authoritative-sections"
    });
    const summary = createElement("summary", {
      text: "PAMGuard settings sections"
    });
    const list = createElement("ul");
    for (const label of labels) {
      list.append(createElement("li", { text: label }));
    }
    panel.append(summary, list);
    return panel;
  }

  function labelledControl(labelText, control, options = {}) {
    const label = createElement("label", {
      className: `settings-field ${options.className || ""}`.trim()
    });
    const heading = createElement("span", {
      className: "settings-label",
      text: labelText
    });
    if (options.className?.includes("settings-field-checkbox")) {
      label.append(control, heading);
    }
    else {
      label.append(heading, control);
    }
    if (options.unit) {
      label.append(createElement("span", {
        className: "settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      label.append(createElement("small", {
        className: "settings-help",
        text: options.help
      }));
    }
    return label;
  }

  function settingNumberControl(pointer, value, options = {}) {
    const control = createElement("input", {
      type: "number",
      attributes: {
        "data-setting-pointer": pointer,
        step: options.step ?? "any",
        ...(options.min !== undefined ? { min: options.min } : {}),
        ...(options.max !== undefined ? { max: options.max } : {})
      }
    });
    control.value = value;
    return control;
  }

  function collectNumber(control, path, label, options = {}) {
    return () => {
      const value = Number(control.value);
      if (!Number.isFinite(value) ||
          (options.integer && !Number.isInteger(value)) ||
          (options.min !== undefined && value < options.min) ||
          (options.max !== undefined && value > options.max)) {
        throw new Error(`${label} has an invalid value`);
      }
      return { path, value };
    };
  }

  function selectedSourceUnit(sourceCollector) {
    const value = sourceCollector?.select?.value || "";
    if (!value) return null;
    const [unitId] = value.split("|");
    return unitById(unitId);
  }

  function globalArrayHydrophones() {
    const component = state.active?.project?.globalSettings?.components
      ?.find((candidate) =>
        candidate.typeId === "pamguard.array-manager");
    return Array.isArray(component?.settings?.hydrophones)
      ? component.settings.hydrophones
      : [];
  }

  function appendAcquisitionSettingsEditor(
    container,
    unit,
    collectors) {
    const settings = unit.settings || {};
    const tabs = [
      ["acquisition-source", "Data Source"],
      ["acquisition-channels", "Sampling & Channels"],
      ["acquisition-calibration", "Calibration & DC"]
    ];
    const tabList = createElement("div", {
      className: "settings-tab-list",
      attributes: {
        role: "tablist",
        "aria-label": `${unit.name} settings`
      }
    });
    const panels = new Map();
    for (const [id, label] of tabs) {
      const selected = id === "acquisition-source";
      const button = createElement("button", {
        type: "button",
        className: "settings-tab",
        text: label,
        attributes: {
          role: "tab",
          "aria-selected": selected ? "true" : "false",
          "aria-controls": `settings-panel-${id}`,
          "data-settings-tab": id
        }
      });
      const panel = createElement("section", {
        id: `settings-panel-${id}`,
        className: "settings-tab-panel",
        attributes: {
          role: "tabpanel",
          "data-settings-panel": id
        }
      });
      panel.hidden = !selected;
      button.addEventListener("click", () => {
        for (const candidate of tabList.querySelectorAll("[role='tab']")) {
          candidate.setAttribute(
            "aria-selected",
            candidate === button ? "true" : "false");
        }
        for (const [panelId, candidate] of panels) {
          candidate.hidden = panelId !== id;
        }
      });
      tabList.append(button);
      panels.set(id, panel);
    }
    container.append(tabList, ...panels.values());

    const sourcePanel = panels.get("acquisition-source");
    sourcePanel.append(createElement("p", {
      className: "section-help",
      text: "The PAMGuard data-source type is portable project data. " +
        "The exact audio device or HTTP(S) endpoint is assigned separately " +
        "on each host and is never saved in this project.",
      attributes: { "data-acquisition-host-binding-note": "" }
    }));
    const sourceType = createElement("input", {
      attributes: {
        "data-setting-pointer": "/daqSystemType",
        required: "required",
        maxlength: "100",
        autocomplete: "off"
      }
    });
    sourceType.value = settings.daqSystemType || "";
    collectors.push(() => {
      const value = sourceType.value.trim();
      if (!value) {
        throw new Error("Data source type cannot be empty");
      }
      return { path: ["daqSystemType"], value };
    });
    sourcePanel.append(labelledControl("Data source type", sourceType, {
      help: "Matches PAMGuard AcquisitionParameters.daqSystemType; " +
        "for example, Sound Card."
    }));

    const channelsPanel = panels.get("acquisition-channels");
    channelsPanel.append(createElement("p", {
      className: "section-help",
      text: "Software channels are contiguous. Each one selects a hardware " +
        "input and the hydrophone it represents in the global Array Manager."
    }));
    const sampleRate = settingNumberControl(
      "/sampleRate",
      settings.sampleRate,
      { min: 1, step: "any" });
    const channelCount = settingNumberControl(
      "/nChannels",
      settings.nChannels,
      { min: 1, max: 32, step: 1 });
    collectors.push(
      collectNumber(
        sampleRate,
        ["sampleRate"],
        "Sample rate",
        { min: 1 }),
      collectNumber(
        channelCount,
        ["nChannels"],
        "Number of channels",
        { integer: true, min: 1, max: 32 }));
    channelsPanel.append(
      labelledControl("Sample rate", sampleRate, {
        unit: "Hz",
        help: "PAMGuard default 48,000 Hz."
      }),
      labelledControl("Number of channels", channelCount, {
        help: "PAMGuard supports up to 32 active software channels."
      }));

    const channelGrid = createElement("div", {
      className: "acquisition-channel-grid",
      attributes: {
        "data-acquisition-channel-map": "",
        role: "group",
        "aria-label": "Acquisition channel mapping"
      }
    });
    const channelGridHead = createElement("div", {
      className: "acquisition-channel-row acquisition-channel-head"
    });
    for (const heading of [
      "Software channel",
      "Hardware channel",
      "Hydrophone"
    ]) {
      channelGridHead.append(createElement("span", { text: heading }));
    }
    channelGrid.append(channelGridHead);
    channelsPanel.append(channelGrid);

    const calibrationPanel = panels.get("acquisition-calibration");
    const voltsPeakToPeak = settingNumberControl(
      "/voltsPeak2Peak",
      settings.voltsPeak2Peak,
      { min: Number.MIN_VALUE, step: "any" });
    const preamplifierGain = settingNumberControl(
      "/preamplifier/gainDb",
      settings.preamplifier?.gainDb,
      { step: "any" });
    const bandwidthLow = settingNumberControl(
      "/preamplifier/bandwidthHz/0",
      settings.preamplifier?.bandwidthHz?.[0],
      { min: 0, step: "any" });
    const bandwidthHigh = settingNumberControl(
      "/preamplifier/bandwidthHz/1",
      settings.preamplifier?.bandwidthHz?.[1],
      { min: 0, step: "any" });
    collectors.push(
      collectNumber(
        voltsPeakToPeak,
        ["voltsPeak2Peak"],
        "Peak-to-peak voltage",
        { min: Number.MIN_VALUE }),
      collectNumber(
        preamplifierGain,
        ["preamplifier", "gainDb"],
        "Preamplifier gain"),
      collectNumber(
        bandwidthLow,
        ["preamplifier", "bandwidthHz", 0],
        "Preamplifier bandwidth low",
        { min: 0 }),
      () => {
        const low = Number(bandwidthLow.value);
        const high = Number(bandwidthHigh.value);
        if (!Number.isFinite(high) || high < 0 || high < low) {
          throw new Error(
            "Preamplifier bandwidth must be ordered and non-negative");
        }
        return {
          path: ["preamplifier", "bandwidthHz", 1],
          value: high
        };
      });
    calibrationPanel.append(
      labelledControl("Peak-to-peak voltage", voltsPeakToPeak, {
        unit: "V"
      }),
      labelledControl("Preamplifier gain", preamplifierGain, {
        unit: "dB"
      }),
      labelledControl("Preamplifier bandwidth low", bandwidthLow, {
        unit: "Hz",
        help: "Stored because it is part of PAMGuard's Preamplifier model."
      }),
      labelledControl("Preamplifier bandwidth high", bandwidthHigh, {
        unit: "Hz"
      }));

    const subtractDc = createElement("input", {
      type: "checkbox",
      attributes: { "data-setting-pointer": "/subtractDC" }
    });
    subtractDc.checked = Boolean(settings.subtractDC);
    const dcControls = createElement("div", {
      className: "conditional-settings"
    });
    const dcTimeConstant = settingNumberControl(
      "/dcTimeConstantSeconds",
      settings.dcTimeConstantSeconds,
      { min: Number.MIN_VALUE, step: "any" });
    const updateDcControls = () => {
      dcControls.hidden = !subtractDc.checked;
    };
    subtractDc.addEventListener("change", updateDcControls);
    updateDcControls();
    collectors.push(
      () => ({
        path: ["subtractDC"],
        value: subtractDc.checked
      }),
      collectNumber(
        dcTimeConstant,
        ["dcTimeConstantSeconds"],
        "DC subtraction time constant",
        { min: Number.MIN_VALUE }));
    calibrationPanel.append(
      labelledControl("Subtract DC", subtractDc, {
        className: "settings-field-checkbox",
        help: "Removes fixed input-device voltage offsets before analysis."
      }),
      dcControls);
    dcControls.append(labelledControl(
      "DC subtraction time constant",
      dcTimeConstant,
      { unit: "s" }));

    const useCalibrationOffsets = createElement("input", {
      type: "checkbox",
      attributes: {
        "data-setting-pointer": "/calibrationDbOffsetByChannel/enabled"
      }
    });
    useCalibrationOffsets.checked =
      Array.isArray(settings.calibrationDbOffsetByChannel) &&
      settings.calibrationDbOffsetByChannel.length > 0;
    const calibrationOffsets = createElement("div", {
      className: "acquisition-calibration-offsets conditional-settings",
      attributes: { "data-acquisition-calibration-offsets": "" }
    });
    const updateCalibrationVisibility = () => {
      calibrationOffsets.hidden = !useCalibrationOffsets.checked;
    };
    useCalibrationOffsets.addEventListener(
      "change",
      updateCalibrationVisibility);
    calibrationPanel.append(
      labelledControl(
        "Use per-channel runtime calibration offsets",
        useCalibrationOffsets,
        {
          className: "settings-field-checkbox",
          help: "Optional C++ runtime dB offsets. Normal PAMGuard " +
            "calibration comes from voltage, gain, and Array Manager data."
        }),
      calibrationOffsets);

    let hardwareValues = Array.isArray(settings.hardwareChannelList)
      ? settings.hardwareChannelList.slice()
      : [];
    let hydrophoneValues = Array.isArray(settings.hydrophoneList)
      ? settings.hydrophoneList.slice()
      : [];
    let calibrationValues =
      Array.isArray(settings.calibrationDbOffsetByChannel)
        ? settings.calibrationDbOffsetByChannel.slice()
        : [];
    let hardwareControls = [];
    let hydrophoneControls = [];
    let calibrationControls = [];

    const activeChannelCount = () => {
      const value = Number(channelCount.value);
      return Number.isInteger(value) && value >= 1 && value <= 32
        ? value
        : 0;
    };
    const preserveCurrentValues = () => {
      if (hardwareControls.length) {
        hardwareValues = hardwareControls.map(
          (control) => Number(control.value));
        hydrophoneValues = hydrophoneControls.map(
          (control) => Number(control.value));
        calibrationValues = calibrationControls.map(
          (control) => Number(control.value));
      }
    };
    const renderChannelMappings = () => {
      preserveCurrentValues();
      for (const row of channelGrid.querySelectorAll(
        ".acquisition-channel-row:not(.acquisition-channel-head)")) {
        row.remove();
      }
      calibrationOffsets.replaceChildren();
      hardwareControls = [];
      hydrophoneControls = [];
      calibrationControls = [];
      const count = activeChannelCount();
      const hydrophones = globalArrayHydrophones();
      for (let channel = 0; channel < count; channel++) {
        const row = createElement("div", {
          className: "acquisition-channel-row",
          attributes: { "data-software-channel": channel }
        });
        row.append(createElement("strong", {
          text: `Channel ${channel}`
        }));
        const hardware = settingNumberControl(
          `/hardwareChannelList/${channel}`,
          hardwareValues[channel] ?? channel,
          { min: 0, max: 31, step: 1 });
        hardware.setAttribute(
          "aria-label",
          `Hardware channel for software channel ${channel}`);
        const hydrophone = createElement("select", {
          attributes: {
            "data-setting-pointer": `/hydrophoneList/${channel}`,
            "aria-label": `Hydrophone for software channel ${channel}`
          }
        });
        hydrophones.forEach((item, index) => {
          const option = createElement("option", {
            text: `Hydrophone ${index}` +
              (item.type ? ` · ${item.type}` : ""),
            attributes: { value: index }
          });
          hydrophone.append(option);
        });
        const configuredHydrophone =
          hydrophoneValues[channel] ?? channel;
        if (configuredHydrophone >= hydrophones.length) {
          hydrophone.append(createElement("option", {
            text: `Missing hydrophone ${configuredHydrophone}`,
            attributes: {
              value: configuredHydrophone,
              "data-missing-hydrophone": ""
            }
          }));
        }
        hydrophone.value = String(configuredHydrophone);
        hardwareControls.push(hardware);
        hydrophoneControls.push(hydrophone);
        row.append(hardware, hydrophone);
        channelGrid.append(row);

        const calibration = settingNumberControl(
          `/calibrationDbOffsetByChannel/${channel}`,
          calibrationValues[channel] ?? 0,
          { step: "any" });
        calibrationControls.push(calibration);
        calibrationOffsets.append(labelledControl(
          `Channel ${channel} offset`,
          calibration,
          { unit: "dB" }));
      }
      updateCalibrationVisibility();
    };
    channelCount.addEventListener("input", renderChannelMappings);
    renderChannelMappings();

    collectors.push(
      () => {
        const count = activeChannelCount();
        const values = hardwareControls.map(
          (control) => Number(control.value));
        if (values.length !== count ||
            values.some((value) =>
              !Number.isInteger(value) || value < 0 || value > 31)) {
          throw new Error(
            "Each active software channel needs a hardware channel in 0..31");
        }
        return {
          path: ["hardwareChannelList"],
          value: values
        };
      },
      () => {
        const count = activeChannelCount();
        const hydrophoneCount = globalArrayHydrophones().length;
        const values = hydrophoneControls.map(
          (control) => Number(control.value));
        if (values.length !== count ||
            values.some((value) =>
              !Number.isInteger(value) ||
              value < 0 ||
              value >= hydrophoneCount)) {
          throw new Error(
            "Each active software channel needs an existing Array Manager " +
            "hydrophone");
        }
        return {
          path: ["hydrophoneList"],
          value: values
        };
      },
      () => {
        if (!useCalibrationOffsets.checked) {
          return {
            path: ["calibrationDbOffsetByChannel"],
            value: []
          };
        }
        const count = activeChannelCount();
        const values = calibrationControls.map(
          (control) => Number(control.value));
        if (values.length !== count ||
            values.some((value) => !Number.isFinite(value))) {
          throw new Error(
            "Calibration offsets require one finite dB value per channel");
        }
        return {
          path: ["calibrationDbOffsetByChannel"],
          value: values
        };
      });
  }

  function appendSoundOutputSettingsEditor(
    container,
    unit,
    sourceCollectors,
    collectors) {
    const settings = unit.settings || {};
    const audioSource = sourceCollectors.find(
      (candidate) => candidate.input.id === "audio");
    const tabs = [
      ["sound-output-playback", "Playback"],
      ["sound-output-sidebar", "Side Bar"]
    ];
    const tabList = createElement("div", {
      className: "settings-tab-list",
      attributes: {
        role: "tablist",
        "aria-label": `${unit.name} settings`
      }
    });
    const panels = new Map();
    for (const [id, label] of tabs) {
      const selected = id === "sound-output-playback";
      const button = createElement("button", {
        type: "button",
        className: "settings-tab",
        text: label,
        attributes: {
          role: "tab",
          "aria-selected": selected ? "true" : "false",
          "aria-controls": `settings-panel-${id}`,
          "data-settings-tab": id
        }
      });
      const panel = createElement("section", {
        id: `settings-panel-${id}`,
        className: "settings-tab-panel",
        attributes: {
          role: "tabpanel",
          "data-settings-panel": id
        }
      });
      panel.hidden = !selected;
      button.addEventListener("click", () => {
        for (const candidate of tabList.querySelectorAll("[role='tab']")) {
          candidate.setAttribute(
            "aria-selected",
            candidate === button ? "true" : "false");
        }
        for (const [panelId, candidate] of panels) {
          candidate.hidden = panelId !== id;
        }
      });
      tabList.append(button);
      panels.set(id, panel);
    }
    container.append(tabList, ...panels.values());

    const playbackPanel = panels.get("sound-output-playback");
    playbackPanel.append(createElement("p", {
      className: "section-help",
      text: "The raw-audio source is the Data Model connection above. " +
        "Channel selection and playback parameters are portable project " +
        "settings; the physical output device is local to this browser."
    }));

    const sourceSummary = createElement("div", {
      className: "sound-output-source-summary",
      attributes: {
        "data-sound-output-source-metadata": "",
        role: "status"
      }
    });
    playbackPanel.append(sourceSummary);

    const channels = createElement("fieldset", {
      className: "settings-choice-group sound-output-channels",
      attributes: {
        "data-sound-output-channels": "",
        "data-setting-pointer": "/channelBitmap"
      }
    });
    channels.append(createElement("legend", {
      text: "Playback channels"
    }));
    const channelActions = createElement("div", {
      className: "sound-output-channel-actions"
    });
    const selectAllChannels = createElement("button", {
      type: "button",
      className: "secondary",
      text: "All available",
      attributes: {
        "data-sound-output-channel-action": "all"
      }
    });
    const selectNoChannels = createElement("button", {
      type: "button",
      className: "secondary",
      text: "None",
      attributes: {
        "data-sound-output-channel-action": "none"
      }
    });
    channelActions.append(selectAllChannels, selectNoChannels);
    const channelChoices = createElement("div", {
      className: "settings-choice-list"
    });
    channels.append(channelActions, channelChoices);
    playbackPanel.append(channels);

    let selectedBitmap =
      portableChannelBitmap(settings.channelBitmap);
    let channelControls = [];
    let metadataGeneration = 0;
    let sourceMetadata =
      provisionalSoundOutputSourceMetadata(audioSource);
    let updateHighPassHelp = () => {};

    const selectedBitmapFromControls = () =>
      channelControls.reduce(
        (bitmap, item) =>
          bitmap + (item.control.checked ? 2 ** item.channel : 0),
        0);
    const preserveChannelSelection = () => {
      if (channelControls.length) {
        selectedBitmap = selectedBitmapFromControls();
      }
    };
    const updateSourceSummary = (metadata) => {
      if (!metadata.source) {
        sourceSummary.textContent =
          "Choose a playable raw-audio source before selecting channels.";
        sourceSummary.dataset.state = "unbound";
        return;
      }
      const channelCount = Array.from(
        { length: 32 },
        (_, channel) => channel)
        .filter((channel) =>
          Math.floor(metadata.channelBitmap / (2 ** channel)) % 2 === 1)
        .length;
      const sourceLabel = sourceName(metadata.source);
      sourceSummary.textContent =
        `${sourceLabel} · ` +
        `${metadata.sampleRateHz > 0
          ? `${metadata.sampleRateHz.toLocaleString()} Hz`
          : "sample rate unavailable"} · ` +
        `${plural(channelCount, "available channel")}` +
        (metadata.runtimeBlock
          ? " · runtime metadata"
          : metadata.catalogueError
            ? " · runtime catalogue unavailable"
            : " · projected metadata");
      sourceSummary.dataset.state =
        metadata.runtimeBlock ? "runtime" : "projected";
    };
    const renderChannels = (metadata) => {
      preserveChannelSelection();
      sourceMetadata = metadata;
      channelChoices.replaceChildren();
      channelControls = [];
      updateSourceSummary(metadata);
      const available = portableChannelBitmap(metadata.channelBitmap);
      for (let channel = 0; channel < 32; channel++) {
        if (Math.floor(available / (2 ** channel)) % 2 !== 1) continue;
        const control = createElement("input", {
          type: "checkbox",
          attributes: {
            "data-setting-pointer": `/channelBitmap/${channel}`,
            "data-sound-output-channel": channel
          }
        });
        control.checked =
          Math.floor(selectedBitmap / (2 ** channel)) % 2 === 1;
        channelControls.push({ channel, control });
        const label = createElement("label", {
          className: "settings-choice"
        });
        label.append(
          control,
          createElement("span", { text: `Channel ${channel}` }));
        channelChoices.append(label);
      }
      if (!channelControls.length) {
        channelChoices.append(createElement("p", {
          className: "section-help",
          text: metadata.source
            ? "The selected source does not currently expose playable " +
                "channels."
            : "Channel choices appear after a source is selected."
        }));
      }
      selectAllChannels.disabled = channelControls.length === 0;
      selectNoChannels.disabled = channelControls.length === 0;
      updateHighPassHelp();
    };
    const refreshSourceMetadata = async () => {
      const generation = ++metadataGeneration;
      renderChannels(
        provisionalSoundOutputSourceMetadata(audioSource));
      const metadata = await loadSoundOutputSourceMetadata(audioSource);
      if (generation !== metadataGeneration || state.disposed) return;
      renderChannels(metadata);
    };
    audioSource?.select.addEventListener(
      "change",
      () => void refreshSourceMetadata());
    selectAllChannels.addEventListener("click", () => {
      for (const item of channelControls) item.control.checked = true;
    });
    selectNoChannels.addEventListener("click", () => {
      for (const item of channelControls) item.control.checked = false;
    });
    collectors.push(() => ({
      path: ["channelBitmap"],
      value: channelControls.length
        ? selectedBitmapFromControls()
        : selectedBitmap
    }));
    renderChannels(sourceMetadata);
    void refreshSourceMetadata();

    const defaultSampleRate = createElement("input", {
      type: "checkbox",
      attributes: {
        "data-setting-pointer": "/defaultSampleRate"
      }
    });
    defaultSampleRate.checked = settings.defaultSampleRate !== false;
    const customRate = createElement("div", {
      className: "conditional-settings"
    });
    const playbackRate = settingNumberControl(
      "/playbackRateHz",
      settings.playbackRateHz,
      { min: Number.MIN_VALUE, step: "any" });
    const updateRateVisibility = () => {
      customRate.hidden = false;
      playbackRate.disabled = defaultSampleRate.checked;
    };
    defaultSampleRate.addEventListener("change", updateRateVisibility);
    updateRateVisibility();
    collectors.push(
      () => ({
        path: ["defaultSampleRate"],
        value: defaultSampleRate.checked
      }),
      collectNumber(
        playbackRate,
        ["playbackRateHz"],
        "Playback sample rate",
        { min: Number.MIN_VALUE }));
    playbackPanel.append(
      labelledControl(
        "Use browser default sample rate",
        defaultSampleRate,
        {
          className: "settings-field-checkbox",
          help: "Matches PAMGuard PlaybackParameters.defaultSampleRate."
        }),
      customRate);
    customRate.append(labelledControl(
      "Playback sample rate",
      playbackRate,
      {
        unit: "Hz",
        help: "PAMGuard's default explicit output rate is 48,000 Hz. " +
          "Clear “Use browser default sample rate” to edit it."
      }));

    const outputSection = createElement("section", {
      className: "sound-output-browser-settings",
      attributes: { "data-sound-output-browser-settings": "" }
    });
    outputSection.append(
      createElement("h4", { text: "Browser output" }),
      createElement("p", {
        className: "section-help",
        text: "This device choice is stored only in this browser. It is " +
          "never written to the PAMGuard project.",
        attributes: { "data-sound-output-host-note": "" }
      }));
    const device = createElement("select", {
      attributes: {
        "data-sound-output-device": "",
        "aria-label": "Browser audio output device"
      }
    });
    const refreshDevices = createElement("button", {
      type: "button",
      className: "secondary",
      text: "Refresh devices",
      attributes: {
        "data-sound-output-action": "refresh-devices"
      }
    });
    const deviceRow = createElement("div", {
      className: "sound-output-device-row"
    });
    deviceRow.append(device, refreshDevices);
    outputSection.append(deviceRow);

    const monitorActions = createElement("div", {
      className: "sound-output-monitor-actions"
    });
    const listen = createElement("button", {
      type: "button",
      text: "Listen",
      attributes: {
        "data-sound-output-action": "listen"
      }
    });
    const stop = createElement("button", {
      type: "button",
      className: "secondary",
      text: "Stop",
      attributes: {
        "data-sound-output-action": "stop"
      }
    });
    const monitorStatus = createElement("div", {
      className: "sound-output-monitor-status",
      attributes: {
        "data-sound-output-status": "",
        role: "status",
        "aria-live": "polite"
      }
    });
    monitorActions.append(listen, stop);
    outputSection.append(monitorActions, monitorStatus);
    playbackPanel.append(outputSection);

    const sidebarPanel = panels.get("sound-output-sidebar");
    sidebarPanel.append(createElement("p", {
      className: "section-help",
      text: "These are the portable equivalents of PAMGuard's live Sound " +
        "Output side-bar controls."
    }));
    const playbackSpeed = settingNumberControl(
      "/playbackSpeed",
      settings.playbackSpeed,
      { min: 0.03125, max: 32, step: "any" });
    const playbackGain = settingNumberControl(
      "/playbackGainDb",
      settings.playbackGainDb,
      { min: -80, max: 80, step: "any" });
    const highPass = settingNumberControl(
      "/hpFilter",
      settings.hpFilter,
      { min: 0, max: 0.5, step: "any" });
    const highPassHelp = createElement("p", {
      className: "section-help",
      attributes: { "data-sound-output-high-pass-derived": "" }
    });
    updateHighPassHelp = () => {
      const fraction = Number(highPass.value);
      const rate = Number(sourceMetadata.sampleRateHz);
      highPassHelp.textContent =
        Number.isFinite(fraction) && rate > 0
          ? `Current cutoff: ${(fraction * rate).toLocaleString()} Hz ` +
              `for the selected source.`
          : "The cutoff in hertz is derived from the selected source rate.";
    };
    highPass.addEventListener("input", updateHighPassHelp);
    collectors.push(
      collectNumber(
        playbackSpeed,
        ["playbackSpeed"],
        "Playback speed",
        { min: 0.03125, max: 32 }),
      collectNumber(
        playbackGain,
        ["playbackGainDb"],
        "Playback gain",
        { min: -80, max: 80 }),
      collectNumber(
        highPass,
        ["hpFilter"],
        "High-pass filter",
        { min: 0, max: 0.5 }));
    sidebarPanel.append(
      labelledControl("Playback speed", playbackSpeed, {
        unit: "×",
        help: "1 is normal speed; PAMGuard supports 1/32× through 32×."
      }),
      labelledControl("Gain", playbackGain, {
        unit: "dB"
      }),
      labelledControl("High-pass filter", highPass, {
        unit: "× source rate",
        help: "0 disables filtering; 0.5 is the maximum portable value."
      }),
      highPassHelp);
    updateHighPassHelp();

    const entry = soundOutputMonitorEntry(unit.id);
    const renderMonitorStatus = (status) => {
      const phase = status?.phase || "stopped";
      monitorStatus.dataset.phase = phase;
      const metrics = [];
      if (Number.isFinite(status?.estimatedLatencyMs)) {
        metrics.push(
          `${Math.round(status.estimatedLatencyMs)} ms estimated latency`);
      }
      if (Number.isFinite(status?.underrunFrames) &&
          status.underrunFrames > 0) {
        metrics.push(`${status.underrunFrames} underrun frames`);
      }
      if (Number.isFinite(status?.receivedFrames) &&
          status.receivedFrames > 0) {
        metrics.push(
          `${status.receivedFrames.toLocaleString()} received frames`);
      }
      monitorStatus.textContent =
        `${humanize(phase)}${status?.message
          ? ` · ${status.message}`
          : ""}${metrics.length ? ` · ${metrics.join(" · ")}` : ""}`;
      listen.disabled =
        !state.runtime?.running ||
        phase === "starting";
      stop.disabled = !["starting", "live", "error"].includes(phase);
    };
    const unsubscribe = subscribeSoundOutputStatus(
      entry,
      renderMonitorStatus);

    const setMonitorError = (error) => {
      entry.status = {
        phase: "error",
        sourceBlockId: "",
        message: normalizedError(error)
      };
      notifySoundOutputEntry(entry);
    };
    const enumerateDevices = async () => {
      refreshDevices.disabled = true;
      const selected = preferredSoundOutputDevice(unit.id);
      try {
        const devices = await entry.monitor.enumerateOutputDevices();
        device.replaceChildren(createElement("option", {
          text: "System default output",
          attributes: { value: "" }
        }));
        for (const item of devices) {
          device.append(createElement("option", {
            text: item.label,
            attributes: { value: item.id }
          }));
        }
        if (selected &&
            !Array.from(device.options).some(
              (option) => option.value === selected)) {
          device.append(createElement("option", {
            text: "Previously selected output (unavailable)",
            attributes: { value: selected }
          }));
        }
        device.value = selected;
      }
      catch (error) {
        setMonitorError(
          new Error(`Could not enumerate output devices: ${
            normalizedError(error)}`));
      }
      finally {
        refreshDevices.disabled = false;
      }
    };
    device.addEventListener("change", () =>
      rememberSoundOutputDevice(unit.id, device.value));
    refreshDevices.addEventListener(
      "click",
      () => void enumerateDevices());
    listen.addEventListener("click", async () => {
      listen.disabled = true;
      try {
        if (!state.runtime?.running) {
          throw new Error(
            "Start PAMGuard processing before listening");
        }
        const metadata =
          await loadSoundOutputSourceMetadata(audioSource);
        if (!metadata.runtimeBlock ||
            !metadata.sourceBlockId ||
            metadata.sampleRateHz <= 0 ||
            metadata.channelBitmap <= 0) {
          throw new Error(
            "The selected source is not available as a playable runtime " +
              "data block");
        }
        const portableSettings =
          collectSettings(unit.settings, collectors);
        rememberSoundOutputDevice(unit.id, device.value);
        await entry.monitor.start({
          sourceBlockId: metadata.sourceBlockId,
          sampleRateHz: metadata.sampleRateHz,
          channelBitmap: metadata.channelBitmap,
          settings: portableSettings,
          deviceId: device.value
        });
      }
      catch (error) {
        setMonitorError(error);
      }
      finally {
        renderMonitorStatus(entry.status);
      }
    });
    stop.addEventListener("click", async () => {
      stop.disabled = true;
      try {
        await entry.monitor.stop();
      }
      catch (error) {
        setMonitorError(error);
      }
    });
    renderMonitorStatus(entry.status);
    void enumerateDevices();

    return {
      cleanup() {
        metadataGeneration += 1;
        unsubscribe();
      }
    };
  }

  function appendFftSettingsEditor(
    container,
    unit,
    sourceCollectors,
    collectors) {
    const fft = unit.settings?.fft || {};
    const noise = unit.settings?.spectralNoise || {};
    const rawSource = sourceCollectors.find(
      (candidate) => candidate.input.id === "rawAudio");
    const tabs = [
      ["fft", "FFT"],
      ["click-removal", "Click Removal"],
      ["spectral-noise", "Spectral Noise Removal"]
    ];
    const tabList = createElement("div", {
      className: "settings-tab-list",
      attributes: {
        role: "tablist",
        "aria-label": `${unit.name} settings`
      }
    });
    const panels = new Map();
    for (const [id, label] of tabs) {
      const button = createElement("button", {
        type: "button",
        className: "settings-tab",
        text: label,
        attributes: {
          role: "tab",
          "aria-selected": id === "fft" ? "true" : "false",
          "aria-controls": `settings-panel-${id}`,
          "data-settings-tab": id
        }
      });
      const panel = createElement("section", {
        id: `settings-panel-${id}`,
        className: "settings-tab-panel",
        attributes: {
          role: "tabpanel",
          "data-settings-panel": id
        }
      });
      panel.hidden = id !== "fft";
      button.addEventListener("click", () => {
        for (const candidate of tabList.querySelectorAll("[role='tab']")) {
          candidate.setAttribute(
            "aria-selected",
            candidate === button ? "true" : "false");
        }
        for (const [panelId, candidate] of panels) {
          candidate.hidden = panelId !== id;
        }
      });
      tabList.append(button);
      panels.set(id, panel);
    }
    container.append(tabList, ...panels.values());

    const fftPanel = panels.get("fft");
    fftPanel.append(createElement("p", {
      className: "section-help",
      text: "Available channels and resolution are derived from the " +
        "selected raw-audio source."
    }));

    const channelFieldset = createElement("fieldset", {
      className: "settings-choice-group",
      attributes: { "data-setting-pointer": "/fft/channelMap" }
    });
    channelFieldset.append(createElement("legend", {
      text: "Channels"
    }));
    let channelInputs = [];
    const renderChannels = () => {
      channelFieldset.querySelector(".settings-choice-list")?.remove();
      const choices = createElement("div", {
        className: "settings-choice-list"
      });
      const sourceUnit = selectedSourceUnit(rawSource);
      const sourceChannelCount = Math.max(
        1,
        Math.min(
          32,
          Number(sourceUnit?.settings?.nChannels) ||
            Math.max(1, Math.ceil(Math.log2((fft.channelMap || 1) + 1)))));
      channelInputs = [];
      for (let channel = 0; channel < sourceChannelCount; channel++) {
        const input = createElement("input", {
          type: "checkbox",
          attributes: {
            value: channel,
            "aria-label": `Channel ${channel}`
          }
        });
        input.checked = ((Number(fft.channelMap) || 0) &
          (2 ** channel)) !== 0;
        const label = createElement("label", {
          className: "settings-choice"
        });
        label.append(input, createElement("span", {
          text: `Channel ${channel}`
        }));
        channelInputs.push(input);
        choices.append(label);
      }
      channelFieldset.append(choices);
    };
    renderChannels();
    rawSource?.select.addEventListener("change", renderChannels);
    collectors.push(() => {
      const channelMap = channelInputs.reduce(
        (bitmap, input) => input.checked
          ? bitmap + (2 ** Number(input.value))
          : bitmap,
        0);
      if (!channelMap) {
        throw new Error("Select at least one FFT channel");
      }
      return { path: ["fft", "channelMap"], value: channelMap };
    });
    fftPanel.append(channelFieldset);

    const fftLength = settingNumberControl(
      "/fft/fftLength",
      fft.fftLength,
      { min: 2, step: 1 });
    const fftHop = settingNumberControl(
      "/fft/fftHop",
      fft.fftHop,
      { min: 1, step: 1 });
    const windowFunction = createElement("select", {
      attributes: { "data-setting-pointer": "/fft/windowFunction" }
    });
    [
      [0, "Rectangular"],
      [1, "Hamming"],
      [2, "Hann"],
      [3, "Bartlett (Triangular)"],
      [4, "Blackman"],
      [5, "Blackman-Harris"]
    ].forEach(([value, label]) => {
      const option = createElement("option", {
        text: label,
        attributes: { value }
      });
      option.selected = Number(fft.windowFunction) === value;
      windowFunction.append(option);
    });
    collectors.push(
      collectNumber(
        fftLength,
        ["fft", "fftLength"],
        "FFT length",
        { integer: true, min: 2 }),
      collectNumber(
        fftHop,
        ["fft", "fftHop"],
        "FFT hop",
        { integer: true, min: 1 }),
      () => ({
        path: ["fft", "windowFunction"],
        value: Number(windowFunction.value)
      }));
    fftPanel.append(
      labelledControl("FFT length", fftLength, {
        unit: "samples",
        help: "Transform length; PAMGuard default 1024."
      }),
      labelledControl("FFT hop", fftHop, {
        unit: "samples",
        help: "Frame advance; 512 gives 50% overlap at the default length."
      }),
      labelledControl("Window function", windowFunction));

    const derived = createElement("dl", {
      className: "derived-settings",
      attributes: { "data-fft-derived": "" }
    });
    const derivedValues = {};
    for (const [key, label] of [
      ["sample-rate", "Input sample rate"],
      ["bin-width", "Frequency resolution"],
      ["time-step", "Frame interval"],
      ["overlap", "Overlap"]
    ]) {
      derived.append(
        createElement("dt", { text: label }),
        derivedValues[key] = createElement("dd", {
          attributes: { "data-derived-value": key }
        }));
    }
    const updateDerived = () => {
      const sampleRate = sourceSampleRate(
        soundOutputSourceReference(rawSource));
      const length = Number(fftLength.value) || 0;
      const hop = Number(fftHop.value) || 0;
      derivedValues["sample-rate"].textContent = sampleRate
        ? `${sampleRate.toLocaleString()} Hz`
        : "Select a configured source";
      derivedValues["bin-width"].textContent =
        sampleRate && length ? `${(sampleRate / length).toFixed(3)} Hz` : "—";
      derivedValues["time-step"].textContent =
        sampleRate && hop ? `${(hop / sampleRate * 1000).toFixed(3)} ms` : "—";
      derivedValues.overlap.textContent =
        length && hop
          ? `${((1 - hop / length) * 100).toFixed(1)}%`
          : "—";
    };
    fftLength.addEventListener("input", updateDerived);
    fftHop.addEventListener("input", updateDerived);
    rawSource?.select.addEventListener("change", updateDerived);
    updateDerived();
    fftPanel.append(derived);

    const clickPanel = panels.get("click-removal");
    const clickRemoval = createElement("input", {
      type: "checkbox",
      attributes: { "data-setting-pointer": "/fft/clickRemoval" }
    });
    clickRemoval.checked = Boolean(fft.clickRemoval);
    const clickControls = createElement("div", {
      className: "conditional-settings"
    });
    const clickThreshold = settingNumberControl(
      "/fft/clickThreshold",
      fft.clickThreshold,
      { step: "any" });
    const clickPower = settingNumberControl(
      "/fft/clickPower",
      fft.clickPower,
      { min: 2, step: 2 });
    const updateClickRemoval = () => {
      clickControls.hidden = !clickRemoval.checked;
    };
    clickRemoval.addEventListener("change", updateClickRemoval);
    updateClickRemoval();
    collectors.push(
      () => ({
        path: ["fft", "clickRemoval"],
        value: clickRemoval.checked
      }),
      collectNumber(
        clickThreshold,
        ["fft", "clickThreshold"],
        "Click threshold"),
      () => {
        const value = Number(clickPower.value);
        if (!Number.isInteger(value) || value < 2 || value % 2 !== 0) {
          throw new Error("Click power must be an even integer of 2 or more");
        }
        return { path: ["fft", "clickPower"], value };
      });
    clickPanel.append(
      labelledControl("Enable click removal", clickRemoval, {
        className: "settings-field-checkbox",
        help: "Suppress impulsive clicks before calculating FFT frames."
      }),
      clickControls);
    clickControls.append(
      labelledControl("Threshold", clickThreshold),
      labelledControl("Power", clickPower));

    const noisePanel = panels.get("spectral-noise");
    noisePanel.append(createElement("p", {
      className: "section-help",
      text: "Methods run in PAMGuard order: median filter, average " +
        "subtraction, kernel smoothing, then threshold."
    }));
    const noiseMethods = [
      {
        key: "medianFilter",
        label: "Median filter",
        controls: [
          ["medianFilterLength", "Filter length", "integer", 1]
        ]
      },
      {
        key: "averageSubtraction",
        label: "Average subtraction",
        controls: [
          ["updateConstant", "Update constant", "number", 0]
        ]
      },
      {
        key: "kernelSmoothing",
        label: "Gaussian kernel smoothing",
        controls: []
      },
      {
        key: "threshold",
        label: "Threshold",
        controls: [
          ["thresholdDb", "Threshold", "number", undefined, "dB"]
        ]
      }
    ];
    for (const method of noiseMethods) {
      const card = createElement("fieldset", {
        className: "noise-method"
      });
      const enabled = createElement("input", {
        type: "checkbox",
        attributes: {
          "data-setting-pointer": `/spectralNoise/${method.key}`,
          "aria-label": method.label
        }
      });
      enabled.checked = Boolean(noise[method.key]);
      const legend = createElement("legend");
      const legendLabel = createElement("label", {
        className: "settings-choice"
      });
      legendLabel.append(enabled, createElement("span", {
        text: method.label
      }));
      legend.append(legendLabel);
      card.append(legend);
      const methodControls = createElement("div", {
        className: "conditional-settings"
      });
      const updateMethod = () => {
        methodControls.hidden = !enabled.checked;
      };
      enabled.addEventListener("change", updateMethod);
      updateMethod();
      collectors.push(() => ({
        path: ["spectralNoise", method.key],
        value: enabled.checked
      }));
      for (const [key, label, type, min, unitLabel] of method.controls) {
        const control = settingNumberControl(
          `/spectralNoise/${key}`,
          noise[key],
          {
            min,
            step: type === "integer" ? 1 : "any"
          });
        collectors.push(collectNumber(
          control,
          ["spectralNoise", key],
          label,
          {
            integer: type === "integer",
            min
          }));
        methodControls.append(labelledControl(label, control, {
          unit: unitLabel
        }));
      }
      card.append(methodControls);
      noisePanel.append(card);
    }
    const finalOutput = createElement("select", {
      attributes: {
        "data-setting-pointer": "/spectralNoise/finalOutput"
      }
    });
    [
      [0, "Binary output"],
      [1, "Use threshold input"],
      [2, "Use raw threshold values"]
    ].forEach(([value, label]) => {
      const option = createElement("option", {
        text: label,
        attributes: { value }
      });
      option.selected = Number(noise.finalOutput) === value;
      finalOutput.append(option);
    });
    collectors.push(() => ({
      path: ["spectralNoise", "finalOutput"],
      value: Number(finalOutput.value)
    }));
    noisePanel.append(labelledControl("Threshold final output", finalOutput, {
      help: "This setting is retained even when thresholding is disabled."
    }));
  }

  async function configureAcquisitionHost(unitId) {
    const unit = unitById(unitId);
    if (!unit || unit.typeId !== "pamguard.acquisition" ||
        state.commandBusy || state.runtimeBusy ||
        state.conflicted) return;

    const listPath = "/v1/projects/active/acquisitions";
    const bindingPath =
      `/v1/projects/active/acquisitions/${encodeURIComponent(
        unit.id)}/host-binding`;
    const capturePath =
      `/v1/projects/active/acquisitions/${encodeURIComponent(
        unit.id)}/capture-status`;
    let inventory;
    let binding = null;
    let capture;
    try {
      inventory = await serviceJson(listPath);
      const summary = inventory.acquisitions?.find(
        (candidate) => candidate.unitId === unit.id);
      if (!summary) {
        throw new Error(
          "The Acquisition instance is not in the active project inventory");
      }
      if (summary.hostBindingRevision !== null) {
        binding = await serviceJson(bindingPath);
      }
      capture = await serviceJson(capturePath);
    }
    catch (error) {
      handleCommandError(
        error,
        `Could not load host input for ${unit.name}`);
      return;
    }

    const body = createElement("div", {
      className: "settings-sections settings-stack",
      attributes: {
        "data-acquisition-host-editor": unit.id
      }
    });
    const sourceSection = createElement("section", {
      className: "settings-section"
    });
    sourceSection.append(
      createElement("h3", { text: "Host input binding" }),
      createElement("p", {
        className: "section-help",
        text: "This selects the real source on this engine host. It is " +
          "deployment state keyed to this stable Acquisition instance. " +
          "Unrelated project and display edits retain it; it is never " +
          "written into the portable project."
      }));
    const configurationState = createElement("div", {
      className: "dialog-callout",
      text: binding
        ? `Configured \u00b7 binding revision ${binding.bindingRevision}`
        : "Needs configuration \u00b7 choose an exact device or HTTP(S) URL",
      attributes: {
        "data-acquisition-configuration-status":
          binding ? "configured" : "needsConfiguration"
      }
    });
    sourceSection.append(configurationState);

    const kind = createElement("select", {
      attributes: {
        "data-acquisition-host-kind": "",
        "aria-label": "Host input type"
      }
    });
    [
      ["", "No host binding"],
      ["device", "Audio device on this engine host"],
      ["url", "HTTP(S) audio stream"]
    ].forEach(([value, label]) =>
      kind.append(createElement("option", {
        text: label,
        attributes: { value }
      })));
    kind.value = binding?.source?.kind || "";
    sourceSection.append(labelledControl("Host input type", kind));

    const url = createElement("input", {
      type: "url",
      attributes: {
        "data-acquisition-host-url": "",
        maxlength: 4096,
        autocomplete: "off",
        placeholder: "https://example.invalid/live-audio"
      }
    });
    url.value =
      binding?.source?.kind === "url"
        ? binding.source.url
        : "";
    const urlRow = labelledControl("Stream URL", url, {
      help: "Only bounded HTTP(S) URLs without credentials are accepted."
    });

    const device = createElement("select", {
      attributes: {
        "data-acquisition-host-device": "",
        "aria-label": "Engine audio input device"
      }
    });
    const refreshDevices = createElement("button", {
      type: "button",
      className: "secondary",
      text: "Refresh devices",
      attributes: {
        "data-acquisition-host-action": "refresh-devices"
      }
    });
    const deviceControls = createElement("div", {
      className: "sound-output-device-row"
    });
    deviceControls.append(device, refreshDevices);
    const deviceRow = labelledControl(
      "Audio input device",
      deviceControls, {
        help: "The value must exactly match current FFmpeg/DirectShow " +
          "enumeration on this host."
      });
    const deviceMessage = createElement("small", {
      className: "section-help",
      attributes: {
        role: "status",
        "data-acquisition-device-status": ""
      }
    });
    sourceSection.append(urlRow, deviceRow, deviceMessage);

    const currentDevice =
      binding?.source?.kind === "device"
        ? binding.source.deviceName
        : "";
    async function loadDevices() {
      refreshDevices.disabled = true;
      deviceMessage.textContent = "Enumerating host audio devices\u2026";
      try {
        const result = await serviceJson("/capture/devices");
        device.replaceChildren();
        for (const item of result.devices || []) {
          device.append(createElement("option", {
            text: item.name +
              (item.type ? ` \u00b7 ${item.type}` : ""),
            attributes: { value: item.name }
          }));
        }
        if (currentDevice &&
            !Array.from(device.options).some(
              (option) => option.value === currentDevice)) {
          device.append(createElement("option", {
            text: `${currentDevice} \u00b7 currently unavailable`,
            attributes: { value: currentDevice }
          }));
        }
        if (currentDevice) device.value = currentDevice;
        deviceMessage.textContent = device.options.length
          ? `${device.options.length} input device${
              device.options.length === 1 ? "" : "s"} available`
          : "No audio input devices were reported.";
      }
      catch (error) {
        device.replaceChildren();
        if (currentDevice) {
          device.append(createElement("option", {
            text: `${currentDevice} \u00b7 enumeration unavailable`,
            attributes: { value: currentDevice }
          }));
        }
        deviceMessage.textContent =
          `Device enumeration unavailable: ${normalizedError(error)}`;
      }
      finally {
        refreshDevices.disabled = false;
      }
    }
    refreshDevices.addEventListener(
      "click",
      () => void loadDevices());

    function updateSourceVisibility() {
      urlRow.hidden = kind.value !== "url";
      deviceRow.hidden = kind.value !== "device";
      deviceMessage.hidden = kind.value !== "device";
      url.required = kind.value === "url";
      device.required = kind.value === "device";
      if (kind.value === "device" && !device.options.length) {
        void loadDevices();
      }
    }
    kind.addEventListener("change", updateSourceVisibility);
    updateSourceVisibility();

    const captureSection = createElement("section", {
      className: "settings-section"
    });
    captureSection.append(
      createElement("h3", { text: "Capture runtime" }),
      createElement("p", {
        className: "section-help",
        text: "Opening a project remains idle. Global Start prepares the " +
          "runtime and automatically opens configured host inputs. These " +
          "controls can stop or restart this Acquisition independently."
      }));
    const captureStatus = createElement("div", {
      className: "dialog-callout",
      attributes: {
        role: "status",
        "data-acquisition-capture-status": ""
      }
    });
    const captureActions = createElement("div", {
      className: "dialog-button-row"
    });
    const startCapture = createElement("button", {
      type: "button",
      text: "Start capture",
      attributes: {
        "data-acquisition-capture-action": "start"
      }
    });
    const stopCapture = createElement("button", {
      type: "button",
      className: "secondary",
      text: "Stop capture",
      attributes: {
        "data-acquisition-capture-action": "stop"
      }
    });
    captureActions.append(startCapture, stopCapture);
    captureSection.append(captureStatus, captureActions);
    body.append(sourceSection, captureSection);

    function renderCaptureStatus() {
      const running = Boolean(capture?.running);
      captureStatus.textContent = running
        ? `Running \u00b7 process ${capture.processId || "active"}`
        : capture?.captureEnabled
        ? "Stopped"
        : "Stopped \u00b7 host capture is disabled on this engine";
      captureStatus.dataset.state = running ? "running" : "stopped";
      startCapture.disabled = Boolean(
        running ||
        !binding ||
        !capture?.captureEnabled ||
        !state.runtime?.running);
      stopCapture.disabled = !running;
    }
    renderCaptureStatus();

    let captureBusy = false;
    async function changeCapture(action) {
      if (captureBusy) return;
      captureBusy = true;
      startCapture.disabled = true;
      stopCapture.disabled = true;
      try {
        await serviceJson(
          `/v1/projects/active/acquisitions/${encodeURIComponent(
            unit.id)}/capture:${action}`,
          {
            method: "POST",
            body: {
              expectedWorkingRevision:
                state.active.workingRevision
            }
          });
        capture = await serviceJson(capturePath);
        renderCaptureStatus();
        showToast(
          `${unit.name} capture ${
            action === "start" ? "started" : "stopped"}`,
          "success");
      }
      catch (error) {
        handleCommandError(
          error,
          `Could not ${action} ${unit.name} capture`);
      }
      finally {
        captureBusy = false;
        renderCaptureStatus();
      }
    }
    startCapture.addEventListener(
      "click",
      () => void changeCapture("start"));
    stopCapture.addEventListener(
      "click",
      () => void changeCapture("stop"));

    const bindingLocked = Boolean(state.runtime?.running);
    if (bindingLocked) {
      for (const control of [
        kind,
        url,
        device,
        refreshDevices
      ]) {
        control.disabled = true;
      }
      sourceSection.append(createElement("p", {
        className: "section-help",
        text: "Stop processing before changing the host input binding. " +
          "Capture Stop remains available above."
      }));
    }

    const accepted = await showFormDialog({
      eyebrow: "Sound Acquisition \u00b7 Host",
      title: `${unit.name} host input`,
      body,
      acceptLabel: bindingLocked ? "Close" : "Apply binding",
      cancelHidden: bindingLocked,
      note: bindingLocked
        ? "Processing is running \u00b7 binding is read-only"
        : "Host-only deployment state \u00b7 not saved in the project",
      focus: kind
    });
    if (!accepted || bindingLocked) return;

    let desiredSource = null;
    try {
      if (kind.value === "url") {
        const parsed = new URL(url.value);
        if (!["http:", "https:"].includes(parsed.protocol) ||
            parsed.username || parsed.password) {
          throw new Error(
            "Stream URL must be HTTP(S) and contain no credentials");
        }
        desiredSource = {
          kind: "url",
          url: parsed.href
        };
      }
      else if (kind.value === "device") {
        if (!device.value) {
          throw new Error("Choose an enumerated audio input device");
        }
        desiredSource = {
          kind: "device",
          deviceName: device.value
        };
      }
    }
    catch (error) {
      showToast(normalizedError(error), "error", 0);
      return;
    }

    const beforeSource = binding?.source || null;
    if (JSON.stringify(desiredSource) ===
        JSON.stringify(beforeSource)) return;
    state.commandBusy = true;
    renderControls();
    try {
      if (desiredSource === null) {
        if (binding) {
          await serviceJson(bindingPath, {
            method: "DELETE",
            body: {
              expectedWorkingRevision:
                state.active.workingRevision,
              expectedBindingRevision:
                binding.bindingRevision
            }
          });
        }
      }
      else {
        const result = await serviceJson(bindingPath, {
          method: "PUT",
          body: {
            expectedWorkingRevision:
              state.active.workingRevision,
            expectedBindingRevision:
              binding?.bindingRevision || 0,
            source: desiredSource
          }
        });
        binding = result.hostBinding;
      }
      try {
        state.ready = await loadReadiness();
      }
      catch (error) {
        showToast(
          `Host binding changed, but readiness refresh failed: ${
            normalizedError(error)}`,
          "warning",
          0);
      }
      renderAll();
      showToast(
        desiredSource
          ? `${unit.name} host input configured`
          : `${unit.name} host input removed`,
        "success");
    }
    catch (error) {
      handleCommandError(
        error,
        `Could not update host input for ${unit.name}`);
    }
    finally {
      state.commandBusy = false;
      renderControls();
    }
  }

  async function configureGlobalSettings(typeId) {
    const descriptor = state.catalogue?.globalSettingsTypes?.find(
      (candidate) => candidate.typeId === typeId);
    const component =
      state.active?.project?.globalSettings?.components?.find(
        (candidate) => candidate.typeId === typeId);
    if (!descriptor || !component || !canEditStructure()) return;

    const body = createElement("div", {
      className: "settings-sections settings-stack"
    });
    const section = createElement("section", {
      className: "settings-section",
      attributes: {
        "data-global-settings-editor": typeId
      }
    });
    body.append(section);

    let editor;
    if (typeId === "pamguard.array-manager") {
      editor = projectArraySettings.mountEditor({
        container: section,
        settings: component.settings,
        reportError: (error) =>
          showToast(normalizedError(error), "error", 0)
      });
    }
    else {
      const collectors = [];
      appendSettingsFields(
        section,
        descriptor.settings?.schema || {
          type: "object",
          properties: {}
        },
        component.settings,
        collectors);
      editor = {
        collect: () =>
          collectSettings(component.settings, collectors),
        focus: () =>
          section.querySelector("input, select, textarea")?.focus()
      };
    }

    const accepted = await showFormDialog({
      eyebrow: "Global Settings",
      title: descriptor.name,
      body,
      acceptLabel: "OK",
      note: "Stop required \u00b7 OK applies to the working project",
      focus: section.querySelector(
        "[data-array-setting='arrayName'], input, select, textarea")
    });
    if (!accepted) return;

    let settings;
    try {
      settings = editor.collect();
    }
    catch (error) {
      showToast(normalizedError(error), "error", 0);
      return;
    }
    if (JSON.stringify(settings) ===
        JSON.stringify(component.settings)) {
      return;
    }
    await applyMutation([
      {
        op: "replaceGlobalSettings",
        typeId,
        settingsVersion: component.settingsVersion,
        settings
      }
    ], `${descriptor.name} settings updated`);
  }

  async function configureUnit(unitId) {
    const unit = unitById(unitId);
    const descriptor = unit && descriptorForUnit(unit);
    if (!unit || !descriptor || !canConfigureUnit(unit)) return;
    const readOnlyProjectSettings = Boolean(
      state.runtime?.running &&
      ["pamguard.sound-output", "pamguard.sound-recorder"]
        .includes(unit.typeId));

    const body = createElement("div", {
      className: "settings-sections settings-stack"
    });
    const operations = [];
    const sourceCollectors = [];
    if (descriptor.inputs?.length) {
      const section = createElement("section", {
        className: "settings-section"
      });
      section.append(
        createElement("h3", { text: "Data Source" }),
        createElement("p", {
          className: "section-help",
          text: "This selector and the Data Model connection line are " +
            "the same persisted source binding."
      }));
      for (const input of descriptor.inputs) {
        const binding = unit.bindings?.find(
          (candidate) => candidate.inputRole === input.id);
        if (unit.typeId === "pamguard.clip-generator" &&
            input.id === "triggers") {
          sourceCollectors.push({
            input,
            select: null,
            before: binding?.sources || [],
            managedByDedicatedEditor: true
          });
          continue;
        }
        const row = createElement("label", {
          className: "settings-field"
        });
        row.append(createElement("span", {
          className: "settings-label",
          text: input.name
        }));
        const select = createElement("select", {
          attributes: { "data-input-role": input.id }
        });
        select.append(createElement("option", {
          text: input.cardinality === "1"
            ? "Choose a required source…"
            : "No source",
          attributes: { value: "" }
        }));
        try {
          const compatible = await projectClient.loadCompatibleSources(
            unit.id,
            input.id);
          for (const source of compatible.sources || []) {
            select.append(createElement("option", {
              text: sourceName({
                unitId: source.unitId,
                outputRole: source.outputRole
              }),
              attributes: {
                value: `${source.unitId}|${source.outputRole}`
              }
            }));
          }
        }
        catch (error) {
          handleCommandError(
            error,
            `Could not load sources for ${input.name}`);
          return;
        }
        const current = binding?.sources?.[0];
        if (current) {
          select.value = `${current.unitId}|${current.outputRole}`;
        }
        sourceCollectors.push({
          input,
          select,
          before: binding?.sources || []
        });
        row.append(select);
        section.append(row);
      }
      body.append(section);
    }

    const settingsSection = createElement("section", {
      className: "settings-section"
    });
    settingsSection.append(createElement("h3", {
      text: "Module Settings"
    }));
    const collectors = [];
    let editorCleanup = () => {};
    let collectDedicatedSettings = null;
    let collectedDedicatedBindings = null;
    if (unit.typeId === "pamguard.acquisition") {
      appendAcquisitionSettingsEditor(
        settingsSection,
        unit,
        collectors);
    }
    else if (unit.typeId === "pamguard.fft") {
      appendFftSettingsEditor(
        settingsSection,
        unit,
        sourceCollectors,
        collectors);
    }
    else if (unit.typeId === "pamguard.sound-output") {
      const editor = appendSoundOutputSettingsEditor(
        settingsSection,
        unit,
        sourceCollectors,
        collectors);
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.click-detector") {
      const editor = projectClickSettings.mountEditor({
        container: settingsSection,
        settings: unit.settings,
        reportError: (error) =>
          showToast(normalizedError(error), "error", 0)
      });
      collectDedicatedSettings = editor.collect;
    }
    else if (
        unit.typeId === "pamguard.matched-template-classifier") {
      const source = sourceCollectors.find(
        (candidate) => candidate.input.id === "clicks");
      const selectedSource = () => {
        const value = source?.select?.value || "";
        if (!value) return null;
        const [sourceUnitId, outputRole] = value.split("|");
        return sourceUnitId && outputRole
          ? { unitId: sourceUnitId, outputRole }
          : null;
      };
      const editor =
        projectMatchedTemplateSettings.mountEditor({
          container: settingsSection,
          settings: unit.settings,
          sourceSelect: source?.select || null,
          getSourceSampleRate: () =>
            sourceSampleRate(selectedSource()),
          reportError: (error) =>
            showToast(normalizedError(error), "error", 0)
        });
      collectDedicatedSettings = editor.collect;
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.amplifier" ||
        unit.typeId === "pamguard.patch-panel") {
      const rawSource = sourceCollectors.find(
        (candidate) => candidate.input.id === "rawAudio");
      const editor = projectSignalRoutingSettings.mountEditor({
        container: settingsSection,
        typeId: unit.typeId,
        settings: unit.settings,
        sourceSelect: rawSource?.select || null,
        getAvailableChannelBitmap: () => {
          const value = rawSource?.select?.value || "";
          if (!value) return 0;
          const [sourceUnitId, outputRole] = value.split("|");
          return sourceChannelBitmap({ unitId: sourceUnitId, outputRole });
        },
        reportError: (error) =>
          showToast(normalizedError(error), "error", 0)
      });
      collectDedicatedSettings = editor.collect;
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.filter" ||
        unit.typeId === "pamguard.decimator") {
      const rawSource = sourceCollectors.find(
        (candidate) => candidate.input.id === "rawAudio");
      const selectedSource = () => {
        const value = rawSource?.select?.value || "";
        if (!value) return null;
        const [sourceUnitId, outputRole] = value.split("|");
        return sourceUnitId && outputRole
          ? { unitId: sourceUnitId, outputRole }
          : null;
      };
      const editor = projectFilterSettings.mountEditor({
        container: settingsSection,
        typeId: unit.typeId,
        settings: unit.settings,
        sourceSelect: rawSource?.select || null,
        getAvailableChannelBitmap: () =>
          sourceChannelBitmap(selectedSource()),
        getSourceSampleRate: () =>
          sourceSampleRate(selectedSource()),
        reportError: (error) =>
          showToast(normalizedError(error), "error", 0)
      });
      collectDedicatedSettings = editor.collect;
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.level-meter") {
      const editor = projectLevelMeterSettings.mountEditor({
        container: settingsSection,
        settings: unit.settings
      });
      collectDedicatedSettings = editor.collect;
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.fft-noise-monitor" ||
        unit.typeId === "pamguard.noise-band-monitor" ||
        unit.typeId === "pamguard.ltsa") {
      const inputRole = unit.typeId === "pamguard.noise-band-monitor"
        ? "rawAudio"
        : "fft";
      const source = sourceCollectors.find(
        (candidate) => candidate.input.id === inputRole);
      const selectedSource = () => {
        const value = source?.select?.value || "";
        if (!value) return null;
        const [sourceUnitId, outputRole] = value.split("|");
        return sourceUnitId && outputRole
          ? { unitId: sourceUnitId, outputRole }
          : null;
      };
      const selectedFftSettings = () => {
        const selected = selectedSource();
        return selected
          ? unitById(selected.unitId)?.settings?.fft || {}
          : {};
      };
      const editor = projectNoiseLtsaSettings.mountEditor({
        container: settingsSection,
        typeId: unit.typeId,
        settings: unit.settings,
        sourceSelect: source?.select || null,
        getAvailableChannelBitmap: () =>
          sourceChannelBitmap(selectedSource()),
        getSourceSampleRate: () =>
          sourceSampleRate(selectedSource()),
        getSourceFftLength: () =>
          Number(selectedFftSettings().fftLength) || 0,
        getSourceFftHop: () =>
          Number(selectedFftSettings().fftHop) || 0,
        reportError: (error) =>
          showToast(normalizedError(error), "error", 0)
      });
      collectDedicatedSettings = editor.collect;
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.whistles-moans") {
      const source = sourceCollectors.find(
        (candidate) => candidate.input.id === "fft");
      const selectedSource = () => {
        const value = source?.select?.value || "";
        if (!value) return null;
        const [sourceUnitId, outputRole] = value.split("|");
        return sourceUnitId && outputRole
          ? { unitId: sourceUnitId, outputRole }
          : null;
      };
      const editor = projectWhistleMoanSettings.mountEditor({
        container: settingsSection,
        settings: unit.settings,
        sourceSelect: source?.select || null,
        getAvailableChannelBitmap: () =>
          sourceChannelBitmap(selectedSource()),
        getSourceSampleRate: () =>
          sourceSampleRate(selectedSource())
      });
      collectDedicatedSettings = editor.collect;
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.ishmael-energy-sum" ||
        unit.typeId === "pamguard.ishmael-sgram-corr" ||
        unit.typeId === "pamguard.ishmael-match-filter") {
      const inputRole =
        unit.typeId === "pamguard.ishmael-match-filter"
          ? "rawAudio"
          : "fft";
      const source = sourceCollectors.find(
        (candidate) => candidate.input.id === inputRole);
      const selectedSource = () => {
        const value = source?.select?.value || "";
        if (!value) return null;
        const [sourceUnitId, outputRole] = value.split("|");
        return sourceUnitId && outputRole
          ? { unitId: sourceUnitId, outputRole }
          : null;
      };
      const editor = projectIshmaelSettings.mountEditor({
        container: settingsSection,
        typeId: unit.typeId,
        settings: unit.settings,
        sourceSelect: source?.select || null,
        getAvailableChannelBitmap: () =>
          sourceChannelBitmap(selectedSource()),
        getSourceSampleRate: () =>
          sourceSampleRate(selectedSource()),
        reportError: (error) =>
          showToast(normalizedError(error), "error", 0)
      });
      collectDedicatedSettings = editor.collect;
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.mht-click-train") {
      const source = sourceCollectors.find(
        (candidate) => candidate.input.id === "clicks");
      const selectedSource = () => {
        const value = source?.select?.value || "";
        if (!value) return null;
        const [sourceUnitId, outputRole] = value.split("|");
        return sourceUnitId && outputRole
          ? { unitId: sourceUnitId, outputRole }
          : null;
      };
      const editor = projectMhtClickTrainSettings.mountEditor({
        container: settingsSection,
        settings: unit.settings,
        sourceSelect: source?.select || null,
        getAvailableChannelGroups: () =>
          clickSourceChannelGroups(selectedSource()),
        getAvailableChannelBitmap: () =>
          sourceChannelBitmap(selectedSource()),
        reportError: (error) =>
          showToast(normalizedError(error), "error", 0)
      });
      collectDedicatedSettings = editor.collect;
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.sound-recorder") {
      const source = sourceCollectors.find(
        (candidate) => candidate.input.id === "rawAudio");
      const statusPath =
        `/v1/projects/active/sound-recorders/${encodeURIComponent(
          unit.id)}/status`;
      const transportPath =
        `/v1/projects/active/sound-recorders/${encodeURIComponent(
          unit.id)}/transport`;
      const selectedSource = () => {
        const value = source?.select?.value || "";
        if (!value) return null;
        const [sourceUnitId, outputRole] = value.split("|");
        return sourceUnitId && outputRole
          ? { unitId: sourceUnitId, outputRole }
          : null;
      };
      const runtimeStatus = (status) => {
        if (!status?.deploymentReady) {
          return {
            state: "error",
            message:
              status?.deploymentError ||
              "Recording storage is not ready on this engine host."
          };
        }
        if (!status.runtimePrepared) {
          return {
            state: "error",
            message:
              "Recorder runtime is not prepared for this project revision."
          };
        }
        if (!status.runtimeRunning) {
          return {
            state: "off",
            message:
              "Recorder is Off. Start processing to enable transport."
          };
        }
        if (status.transport === "continuous") {
          return {
            state: "continuous",
            message: status.fileOpen
              ? `Continuous \u00b7 writing ${
                  status.currentFileName || "WAV"} \u00b7 ${
                  status.framesInCurrentFile} frames`
              : "Continuous \u00b7 waiting for raw audio"
          };
        }
        return {
          state: "off",
          message:
            `Off \u00b7 ${status.completedFileCount || 0} completed ${
              Number(status.completedFileCount) === 1 ? "file" : "files"}`
        };
      };
      const editor = projectSoundRecorderSettings.mountEditor({
        container: settingsSection,
        settings: unit.settings,
        sourceLabel: sourceName(selectedSource()),
        outputFolderLabel:
          "Assigned by this engine host; not stored in the portable project",
        availableChannelBitmap:
          sourceChannelBitmap(selectedSource()),
        onRuntimeAction: async (transport) => {
          const status = await serviceJson(transportPath, {
            method: "PUT",
            body: {
              expectedWorkingRevision:
                state.active.workingRevision,
              transport
            }
          });
          return runtimeStatus(status);
        },
        reportError: (error) =>
          showToast(normalizedError(error), "error", 0)
      });
      void serviceJson(statusPath)
        .then((status) =>
          editor.setRuntimeStatus(runtimeStatus(status)))
        .catch((error) =>
          editor.setRuntimeStatus({
            state: "error",
            message:
              `Recorder status unavailable: ${normalizedError(error)}`
          }));
      collectDedicatedSettings = editor.collect;
      editorCleanup = editor.cleanup;
    }
    else if (unit.typeId === "pamguard.clip-generator") {
      const rawSource = sourceCollectors.find(
        (candidate) => candidate.input.id === "rawAudio");
      const triggerBinding = sourceCollectors.find(
        (candidate) => candidate.input.id === "triggers");
      const selectedRawSource = () => {
        const value = rawSource?.select?.value || "";
        if (!value) return null;
        const [sourceUnitId, outputRole] = value.split("|");
        return sourceUnitId && outputRole
          ? { unitId: sourceUnitId, outputRole }
          : null;
      };
      const triggerCandidates = () =>
        (state.inspection?.projection?.publicOutputs || [])
          .map((output) => {
            const sourceUnit = unitById(output.unitId);
            return {
              unitId: output.unitId,
              outputRole: output.outputRole,
              name: sourceName(output),
              typeId: sourceUnit?.typeId || "",
              capabilities: output.capabilities || []
            };
          })
          .filter((candidate) =>
            candidate.typeId === "pamguard.click-detector" ||
            candidate.capabilities.includes("clip-trigger") ||
            candidate.outputRole === "spectrogramMark" ||
            candidate.outputRole === "spectrogramMarks");
      const editor = projectClipGeneratorSettings.mountEditor({
        container: settingsSection,
        settings: unit.settings,
        rawAudioSourceSelect: rawSource?.select || null,
        getRawAudioSourceName: () =>
          sourceName(selectedRawSource()),
        getAvailableTriggerSources: triggerCandidates,
        getBoundTriggerSources: () =>
          triggerBinding?.before || [],
        reportError: (error) =>
          showToast(normalizedError(error), "error", 0)
      });
      collectDedicatedSettings = () => {
        const configuration = editor.collectConfiguration();
        collectedDedicatedBindings = new Map([
          ["triggers", configuration.triggerSources]
        ]);
        return configuration.settings;
      };
      editorCleanup = editor.cleanup;
    }
    else {
      const summary = settingsSectionSummary(descriptor.settings);
      if (summary) settingsSection.append(summary);
      const schema = descriptor.settings?.schema || {
        type: "object",
        properties: {}
      };
      appendSettingsFields(
        settingsSection,
        schema,
        unit.settings,
        collectors);
      if (!Object.keys(schema.properties || {}).length) {
        settingsSection.append(createElement("p", {
          className: "section-help",
          text: "This controlled unit has no configurable fields in the " +
            "current Java-authoritative slice."
        }));
      }
    }
    body.append(settingsSection);
    if (readOnlyProjectSettings) {
      for (const control of body.querySelectorAll(
        "[data-input-role], [data-setting-pointer], " +
          "[data-signal-routing-action], " +
          "[data-noise-ltsa-action], [data-noise-standard-band], " +
          "[data-whistle-action], " +
          "[data-ishmael-action], [data-ishmael-kernel-file], " +
          "[data-mht-click-train-action], " +
          "[data-sound-recorder-action='reset-trigger-budget'], " +
          "[data-clip-generator-action], " +
          "[data-clip-generator-binding-source], " +
          "[data-matched-template-action]")) {
        control.disabled = true;
      }
    }

    const accepted = await showFormDialog({
      eyebrow: descriptor.palette?.menuGroup || "Settings",
      title: unit.name,
      body,
      acceptLabel: readOnlyProjectSettings ? "Close" : "OK",
      cancelHidden: readOnlyProjectSettings,
      note: readOnlyProjectSettings
        ? unit.typeId === "pamguard.sound-recorder"
          ? "Processing is running · portable settings are read-only; " +
              "recorder Off/Continuous controls remain available"
          : "Processing is running · portable settings are read-only; " +
              "browser Listen/Stop controls remain available"
        : descriptor.settings?.changeRules?.[0]?.policy ===
          "stop-required"
        ? "Stop required · OK applies immediately to the working project"
        : "OK applies immediately to the working project"
    });
    editorCleanup();
    if (!accepted || readOnlyProjectSettings) return;
    let settings;
    try {
      settings = collectDedicatedSettings
        ? collectDedicatedSettings()
        : collectSettings(unit.settings, collectors);
    }
    catch (error) {
      showToast(normalizedError(error), "error", 0);
      return;
    }
    if (JSON.stringify(settings) !== JSON.stringify(unit.settings)) {
      operations.push({
        op: "replaceSettings",
        unit: { id: unit.id },
        settingsVersion: unit.settingsVersion,
        settings
      });
    }
    for (const source of sourceCollectors) {
      if (source.managedByDedicatedEditor) {
        const selected =
          collectedDedicatedBindings?.get(source.input.id) || [];
        const sources = selected.map((entry) => ({
          unit: { id: entry.unitId },
          outputRole: entry.outputRole
        }));
        const before = source.before.map((entry) => ({
          unit: { id: entry.unitId },
          outputRole: entry.outputRole
        }));
        if (JSON.stringify(sources) !== JSON.stringify(before)) {
          operations.push({
            op: "setBinding",
            unit: { id: unit.id },
            inputRole: source.input.id,
            sources
          });
        }
        continue;
      }
      const sources = source.select.value
        ? (() => {
            const [id, outputRole] = source.select.value.split("|");
            return [{ unit: { id }, outputRole }];
          })()
        : [];
      const before = source.before.map((entry) => ({
        unit: { id: entry.unitId },
        outputRole: entry.outputRole
      }));
      if (JSON.stringify(sources) !== JSON.stringify(before)) {
        operations.push({
          op: "setBinding",
          unit: { id: unit.id },
          inputRole: source.input.id,
          sources
        });
      }
    }
    await applyMutation(
      operations,
      operations.length ? `${unit.name} settings updated` : null);
  }

  async function renameUnit(unitId) {
    const unit = unitById(unitId);
    if (!unit || !canEditStructure()) return;
    const body = createElement("div", { className: "dialog-stack" });
    const label = createElement("label", {
      className: "dialog-field"
    });
    label.append(createElement("span", { text: "Controlled-unit name" }));
    const input = createElement("input", {
      attributes: {
        maxlength: "50",
        required: "required",
        autocomplete: "off"
      }
    });
    input.value = unit.name;
    label.append(input);
    body.append(label);
    const accepted = await showFormDialog({
      eyebrow: "Data Model",
      title: `Rename ${unit.name}`,
      body,
      acceptLabel: "Rename",
      focus: input
    });
    if (!accepted || input.value === unit.name) return;
    await applyMutation([
      {
        op: "renameControlledUnit",
        unit: { id: unit.id },
        name: input.value
      }
    ], `${unit.name} renamed to ${input.value}`);
  }

  async function removeUnit(unitId) {
    const unit = unitById(unitId);
    if (!unit || !canEditStructure()) return;
    const body = createElement("div", { className: "dialog-stack" });
    body.append(
      createElement("p", {
        className: "dialog-intro",
        text: `Remove ${unit.name} from the active project?`
      }),
      createElement("div", {
        className: "dialog-callout dialog-callout-danger",
        text: "Any display tabs owned by this controlled unit will also " +
          "be removed. The change is immediate but remains unsaved until " +
          "you use File → Save."
      }));
    const leaveLabel = createElement("label", {
      className: "check-field"
    });
    const leave = createElement("input", { type: "checkbox" });
    leaveLabel.append(
      leave,
      createElement("span", {
        text: "Leave dependent modules unbound instead of rejecting removal"
      }));
    body.append(leaveLabel);
    const accepted = await showFormDialog({
      eyebrow: "Data Model",
      title: `Remove ${unit.name}`,
      body,
      acceptLabel: "Remove",
      dangerous: true
    });
    if (!accepted) return;
    await applyMutation([
      {
        op: "removeControlledUnit",
        unit: { id: unit.id },
        dependantPolicy: leave.checked ? "leave-unbound" : "reject"
      }
    ], `${unit.name} removed`);
  }

  async function inspectUnit(unitId) {
    const unit = unitById(unitId);
    if (!unit) return;
    const projection = state.inspection?.projection || {};
    $("inspectionTitle").textContent = unit.name;
    const body = $("inspectionBody");
    body.replaceChildren();

    const details = createElement("div", {
      className: "inspection-summary"
    });
    const descriptor = descriptorForUnit(unit);
    for (const [label, value] of [
      ["Controlled-unit type", descriptor?.palette?.registeredName],
      ["Java class", descriptor?.javaAuthority?.className],
      ["Stable identity", unit.id],
      ["Expansion recipe", `${unit.recipe.id} v${unit.recipe.version}`]
    ]) {
      const item = createElement("div");
      item.append(
        createElement("span", { text: label }),
        createElement("code", { text: value || "—" }));
      details.append(item);
    }
    body.append(details);

    const children = (projection.runtimeChildren || [])
      .filter((child) => child.ownerUnitId === unit.id);
    const blocks = (projection.dataBlocks || [])
      .filter((block) => block.ownerUnitId === unit.id);
    const inputs = (projection.publicInputs || [])
      .filter((input) => input.unitId === unit.id);
    appendInspectionCollection(
      body,
      "Processes",
      children,
      (child) => ({
        title: child.childRole,
        lines: [
          child.runtimeTypeId,
          `Runtime ID ${child.runtimeNodeId}`
        ]
      }));
    appendInspectionCollection(
      body,
      "Data blocks",
      blocks,
      (block) => ({
        title: block.runtimePortId,
        lines: [
          block.dataType,
          `Block ID ${block.blockId}`,
          (block.capabilities || []).join(", ")
        ]
      }));
    appendInspectionCollection(
      body,
      "Input subscriptions",
      inputs,
      (input) => ({
        title: input.inputRole,
        lines: [
          input.dataType,
          ...(input.sources || []).map(sourceName)
        ]
      }));
    $("inspectionDialog").showModal();
  }

  function appendInspectionCollection(container, title, values, describe) {
    const section = createElement("section", {
      className: "inspection-section"
    });
    section.append(createElement("h3", {
      text: `${title} · ${values.length}`
    }));
    if (!values.length) {
      section.append(createElement("p", {
        className: "section-help",
        text: `No ${title.toLowerCase()} are contributed by this unit.`
      }));
    }
    for (const value of values) {
      const description = describe(value);
      const card = createElement("article", {
        className: "inspection-card"
      });
      card.append(createElement("strong", { text: description.title }));
      for (const line of description.lines.filter(Boolean)) {
        card.append(createElement("code", { text: line }));
      }
      section.append(card);
    }
    container.append(section);
  }

  function displaySourceReference(select) {
    if (!select?.value) return null;
    const [unitId, outputRole] = select.value.split("|");
    return unitId && outputRole ? { unitId, outputRole } : null;
  }

  function appendSpectrogramSettingsEditor(
    container,
    sourceLabel,
    sourceSelect,
    settings,
    collectors) {
    container.dataset.spectrogramEditor = "";
    const tabs = [
      ["spectrogram-data-source", "Data Source"],
      ["spectrogram-scales", "Scales"],
      ["spectrogram-plugins", "Plug ins"],
      ["spectrogram-mark-observers", "Mark Observers"]
    ];
    const tabList = createElement("div", {
      className: "settings-tab-list",
      attributes: {
        role: "tablist",
        "aria-label": "Spectrogram settings"
      }
    });
    const panels = new Map();
    for (const [id, label] of tabs) {
      const selected = id === "spectrogram-data-source";
      const button = createElement("button", {
        type: "button",
        className: "settings-tab",
        text: label,
        attributes: {
          role: "tab",
          "aria-selected": selected ? "true" : "false",
          "aria-controls": `settings-panel-${id}`,
          "data-settings-tab": id
        }
      });
      const panel = createElement("section", {
        id: `settings-panel-${id}`,
        className: "settings-tab-panel",
        attributes: {
          role: "tabpanel",
          "data-settings-panel": id
        }
      });
      panel.hidden = !selected;
      button.addEventListener("click", () => {
        for (const candidate of tabList.querySelectorAll("[role='tab']")) {
          candidate.setAttribute(
            "aria-selected",
            candidate === button ? "true" : "false");
        }
        for (const [panelId, candidate] of panels) {
          candidate.hidden = panelId !== id;
        }
      });
      tabList.append(button);
      panels.set(id, panel);
    }
    container.append(tabList, ...panels.values());

    const sourcePanel = panels.get("spectrogram-data-source");
    sourcePanel.append(createElement("p", {
      className: "section-help",
      text: "The FFT connection belongs to this display instance. " +
        "Panel channels and scales are portable settings; no second source " +
        "name is stored inside the Spectrogram settings."
    }), sourceLabel);

    const sourceSummary = createElement("div", {
      className: "spectrogram-source-summary",
      attributes: {
        role: "status",
        "data-spectrogram-source-summary": ""
      }
    });
    sourcePanel.append(sourceSummary);

    const panelCount = settingNumberControl(
      "/nPanels",
      settings.nPanels ?? 1,
      { min: 1, max: 32, step: 1 });
    const panelCountRow = createElement("div", {
      className: "spectrogram-panel-count"
    });
    const onePerChannel = createElement("button", {
      type: "button",
      className: "secondary",
      text: "One panel per channel",
      attributes: { "data-spectrogram-one-per-channel": "" }
    });
    panelCountRow.append(
      labelledControl("Number of panels", panelCount, {
        help: "PAMGuard permits 1 to 32 panels. Channels may be reused " +
          "across panels."
      }),
      onePerChannel);
    sourcePanel.append(panelCountRow);

    const channelGroup = createElement("fieldset", {
      className: "settings-choice-group spectrogram-panel-channels",
      attributes: { "data-spectrogram-panel-channels": "" }
    });
    channelGroup.append(createElement("legend", {
      text: "Panel channels"
    }));
    const channelRows = createElement("div", {
      className: "spectrogram-panel-channel-list"
    });
    channelGroup.append(channelRows);
    sourcePanel.append(channelGroup);

    let configuredChannels = Array.isArray(settings.channelList)
      ? settings.channelList.map(Number)
      : [0];
    let channelControls = [];
    const availableChannels = () => {
      const bitmap =
        sourceChannelBitmap(displaySourceReference(sourceSelect));
      const result = [];
      for (let channel = 0; channel < 32; channel++) {
        if (Math.floor(bitmap / (2 ** channel)) % 2 === 1) {
          result.push(channel);
        }
      }
      return result;
    };
    const preserveChannels = () => {
      if (channelControls.length) {
        configuredChannels =
          channelControls.map((control) => Number(control.value));
      }
    };
    const renderPanelChannels = () => {
      preserveChannels();
      const rawCount = Number(panelCount.value);
      const count = Number.isInteger(rawCount)
        ? Math.max(1, Math.min(32, rawCount))
        : 1;
      const available = availableChannels();
      const choices = available.length
        ? available
        : Array.from({ length: 32 }, (_, channel) => channel);
      channelRows.replaceChildren();
      channelControls = [];
      for (let panel = 0; panel < count; panel++) {
        const select = createElement("select", {
          attributes: {
            "data-setting-pointer": `/channelList/${panel}`,
            "data-spectrogram-panel-channel": panel
          }
        });
        const configured = Number(
          configuredChannels[panel] ??
          configuredChannels.at(-1) ??
          choices[Math.min(panel, choices.length - 1)] ??
          0);
        const selected = choices.includes(configured)
          ? configured
          : choices[0];
        for (const channel of choices) {
          const option = createElement("option", {
            text: `Channel ${channel}`,
            attributes: { value: channel }
          });
          option.selected = channel === selected;
          select.append(option);
        }
        channelControls.push(select);
        channelRows.append(labelledControl(
          `Panel ${panel + 1}`,
          select));
      }
      const source = displaySourceReference(sourceSelect);
      const sourceUnit = source ? unitById(source.unitId) : null;
      const fft = sourceUnit?.settings?.fft || {};
      const sampleRate = sourceSampleRate(source);
      sourceSummary.textContent = source
        ? `${sourceName(source)} · ` +
          `${sampleRate > 0
            ? `${sampleRate.toLocaleString()} Hz`
            : "sample rate unavailable"} · ` +
          `${Number(fft.fftLength) > 0
            ? `${fft.fftLength}-point FFT`
            : "FFT length unavailable"} · ` +
          `${available.length || 0} available channels`
        : "Unbound display · choose an FFT source to stream data.";
      sourceSummary.dataset.state = source ? "bound" : "unbound";
      onePerChannel.disabled = available.length === 0;
    };
    panelCount.addEventListener("input", renderPanelChannels);
    sourceSelect.addEventListener("change", renderPanelChannels);
    onePerChannel.addEventListener("click", () => {
      const available = availableChannels();
      if (!available.length) return;
      configuredChannels = available;
      panelCount.value = String(Math.min(32, available.length));
      renderPanelChannels();
    });
    renderPanelChannels();
    collectors.push(
      collectNumber(
        panelCount,
        ["nPanels"],
        "Number of panels",
        { integer: true, min: 1, max: 32 }),
      () => {
        const channels =
          channelControls.map((control) => Number(control.value));
        if (channels.length !== Number(panelCount.value) ||
            channels.some((channel) =>
              !Number.isInteger(channel) ||
              channel < 0 ||
              channel > 31)) {
          throw new Error("Every Spectrogram panel needs a valid channel");
        }
        return { path: ["channelList"], value: channels };
      });

    const scalesPanel = panels.get("spectrogram-scales");
    const rangeSection = (
      title,
      pointer,
      values,
      options = {}) => {
      const fieldset = createElement("fieldset", {
        className: "settings-choice-group spectrogram-range"
      });
      fieldset.append(createElement("legend", { text: title }));
      const low = settingNumberControl(
        `${pointer}/0`,
        values?.[0] ?? options.fallback?.[0],
        { min: options.min, step: "any" });
      const high = settingNumberControl(
        `${pointer}/1`,
        values?.[1] ?? options.fallback?.[1],
        { min: options.min, step: "any" });
      fieldset.append(
        labelledControl("Minimum", low, { unit: options.unit }),
        labelledControl("Maximum", high, { unit: options.unit }));
      collectors.push(
        collectNumber(
          low,
          [options.path, 0],
          `${title} minimum`,
          { min: options.min }),
        () => {
          const lowValue = Number(low.value);
          const highValue = Number(high.value);
          if (!Number.isFinite(highValue) ||
              highValue < lowValue ||
              (options.min !== undefined && highValue < options.min)) {
            throw new Error(`${title} must be an ordered pair`);
          }
          return {
            path: [options.path, 1],
            value: highValue
          };
        });
      return fieldset;
    };
    scalesPanel.append(
      rangeSection(
        "Frequency Range",
        "/frequencyLimits",
        settings.frequencyLimits,
        {
          path: "frequencyLimits",
          fallback: [0, 0],
          min: 0,
          unit: "Hz"
        }),
      rangeSection(
        "Amplitude Range",
        "/amplitudeLimits",
        settings.amplitudeLimits,
        {
          path: "amplitudeLimits",
          fallback: [50, 120],
          unit: "dB"
        }));

    const colourMap = createElement("select", {
      attributes: { "data-setting-pointer": "/colourMap" }
    });
    const colourNames = new Map([
      ["GREY", "Grey (black to white)"],
      ["REVERSEGREY", "Grey (white to black)"],
      ["BLUE", "Blue"],
      ["GREEN", "Green"],
      ["RED", "Red"],
      ["HOT", "Rainbow (multicoloured)"],
      ["HSV", "HSV (multicoloured)"],
      ["FIRE", "Fire (multicoloured)"],
      ["PATRIOTIC", "Red-White-Blue"]
    ]);
    for (const [value, label] of colourNames) {
      const option = createElement("option", {
        text: label,
        attributes: { value }
      });
      option.selected = value === (settings.colourMap || "GREY");
      colourMap.append(option);
    }
    collectors.push(() => ({
      path: ["colourMap"],
      value: colourMap.value
    }));
    scalesPanel.append(labelledControl("Colour model", colourMap));

    const timeGroup = createElement("fieldset", {
      className: "settings-choice-group"
    });
    timeGroup.append(createElement("legend", { text: "Time Range" }));
    const timeName = `spectrogram-time-${platform.identifiers.uuidV4()}`;
    const pixelMode = createElement("input", {
      type: "radio",
      attributes: {
        name: timeName,
        value: "pixels",
        "data-spectrogram-time-mode": "pixels"
      }
    });
    const fixedMode = createElement("input", {
      type: "radio",
      attributes: {
        name: timeName,
        value: "seconds",
        "data-setting-pointer": "/timeScaleFixed",
        "data-spectrogram-time-mode": "seconds"
      }
    });
    pixelMode.checked = !settings.timeScaleFixed;
    fixedMode.checked = Boolean(settings.timeScaleFixed);
    const pixelsPerSlice = settingNumberControl(
      "/pixelsPerSlics",
      settings.pixelsPerSlics ?? 1,
      { min: 1, step: 1 });
    const displayLength = settingNumberControl(
      "/displayLength",
      settings.displayLength ?? 20,
      { min: 0.001, step: "any" });
    const pixelChoice = createElement("label", {
      className: "settings-choice spectrogram-time-choice"
    });
    pixelChoice.append(
      pixelMode,
      createElement("span", { text: "Pixels per FFT" }),
      pixelsPerSlice);
    const fixedChoice = createElement("label", {
      className: "settings-choice spectrogram-time-choice"
    });
    fixedChoice.append(
      fixedMode,
      createElement("span", { text: "Window length" }),
      displayLength,
      createElement("span", { text: "seconds" }));
    timeGroup.append(pixelChoice, fixedChoice);
    scalesPanel.append(timeGroup);
    collectors.push(
      () => ({
        path: ["timeScaleFixed"],
        value: fixedMode.checked
      }),
      collectNumber(
        pixelsPerSlice,
        ["pixelsPerSlics"],
        "Pixels per FFT",
        { integer: true, min: 1 }),
      collectNumber(
        displayLength,
        ["displayLength"],
        "Window length",
        { min: Number.EPSILON }));

    const scrolling = createElement("fieldset", {
      className: "settings-choice-group"
    });
    scrolling.append(createElement("legend", { text: "Scrolling" }));
    const scrollName =
      `spectrogram-scroll-${platform.identifiers.uuidV4()}`;
    const wrap = createElement("input", {
      type: "radio",
      attributes: {
        name: scrollName,
        value: "wrap",
        "data-setting-pointer": "/wrapDisplay",
        "data-spectrogram-scroll-mode": "wrap"
      }
    });
    const scroll = createElement("input", {
      type: "radio",
      attributes: {
        name: scrollName,
        value: "scroll",
        "data-spectrogram-scroll-mode": "scroll"
      }
    });
    wrap.checked = settings.wrapDisplay !== false;
    scroll.checked = !wrap.checked;
    for (const [control, label] of [
      [wrap, "Wrap Display"],
      [scroll, "Scroll Display"]
    ]) {
      const choice = createElement("label", {
        className: "settings-choice"
      });
      choice.append(control, createElement("span", { text: label }));
      scrolling.append(choice);
    }
    scalesPanel.append(scrolling);
    collectors.push(() => ({
      path: ["wrapDisplay"],
      value: wrap.checked
    }));

    const showScale = createElement("input", {
      type: "checkbox",
      attributes: { "data-setting-pointer": "/showScale" }
    });
    showScale.checked = settings.showScale !== false;
    scalesPanel.append(labelledControl("Show axes and scales", showScale, {
      className: "settings-field-checkbox",
      help: "Controls the frequency, time, channel, and amplitude axes."
    }));
    collectors.push(() => ({
      path: ["showScale"],
      value: showScale.checked
    }));

    panels.get("spectrogram-plugins").append(createElement("div", {
      className: "dialog-callout",
      text: "Spectrogram plug-in overlays are not part of this vertical " +
        "slice yet. The tab is retained in the authoritative PAMGuard order."
    }));
    panels.get("spectrogram-mark-observers").append(createElement("div", {
      className: "dialog-callout",
      text: "Mark Observer routing will be added with the annotation and " +
        "marking data-model slice."
    }));
  }

  function displaySourceOptions(provider, selected) {
    const outputs = state.inspection?.projection?.publicOutputs || [];
    const input = provider.inputs?.[0];
    const options = outputs.filter((output) => {
      if (!input) return false;
      if (output.dataType !== input.dataType) return false;
      return (input.capabilities || []).every(
        (capability) => output.capabilities?.includes(capability));
    }).map((output) => ({
      source: {
        unitId: output.unitId,
        outputRole: output.outputRole
      },
      label: sourceName({
        unitId: output.unitId,
        outputRole: output.outputRole
      }),
      selected: selected?.unitId === output.unitId &&
        selected?.outputRole === output.outputRole
    }));
    if (selected && !options.some((option) => option.selected)) {
      options.push({
        source: deepClone(selected),
        label: `${sourceName(selected)} · current source unavailable`,
        selected: true
      });
    }
    return options;
  }

  async function addDisplay(ownerUnitId, providerTypeId) {
    const owner = unitById(ownerUnitId);
    const provider = state.providerByType.get(providerTypeId);
    const tab = state.active?.project?.displayTabs
      ?.find((candidate) => candidate.owner.unitId === ownerUnitId);
    if (!owner || !provider || !tab || !canEditStructure()) return;
    if (displayProviderAtMaximum(ownerUnitId, provider)) {
      showToast(
        `${provider.name} has reached its PAMGuard maximum for ${owner.name}.`,
        "warning");
      return;
    }

    const body = createElement("div", {
      className: "settings-sections settings-stack"
    });
    const sourceSection = createElement("section", {
      className: "settings-section"
    });
    sourceSection.append(createElement("h3", { text: "Data Source" }));
    const sourceLabel = createElement("label", {
      className: "settings-field"
    });
    sourceLabel.append(createElement("span", {
      className: "settings-label",
      text: provider.inputs?.[0]?.name || "Source"
    }));
    const select = createElement("select");
    select.setAttribute(
      "data-input-role",
      provider.inputs?.[0]?.id || "fft");
    select.append(createElement("option", {
      text: provider.instanceRules?.canCreateWithoutSource
        ? "Create unbound display"
        : "Choose a source…",
      attributes: { value: "" }
    }));
    for (const option of displaySourceOptions(provider, null)) {
      select.append(createElement("option", {
        text: option.label,
        attributes: {
          value: `${option.source.unitId}|${option.source.outputRole}`
        }
      }));
    }
    sourceLabel.append(select);
    const collectors = [];
    if (provider.providerTypeId === "pamguard.spectrogram-display") {
      appendSpectrogramSettingsEditor(
        body,
        sourceLabel,
        select,
        provider.settings.defaults,
        collectors);
    }
    else {
      sourceSection.append(sourceLabel);
      body.append(sourceSection);
      const settingsSection = createElement("section", {
        className: "settings-section"
      });
      settingsSection.append(createElement("h3", {
        text: "Display Settings"
      }));
      appendSettingsFields(
        settingsSection,
        provider.settings.schema,
        provider.settings.defaults,
        collectors);
      body.append(settingsSection);
    }
    const accepted = await showFormDialog({
      eyebrow: owner.name,
      title: `Add ${provider.name}`,
      body,
      acceptLabel: "Add display",
      note: "The display and its placement are owned by this project."
    });
    if (!accepted) return;

    let settings;
    try {
      settings = collectSettings(provider.settings.defaults, collectors);
    }
    catch (error) {
      showToast(normalizedError(error), "error", 0);
      return;
    }
    const displayId = platform.identifiers.uuidV4();
    const displayTabs = deepClone(state.active.project.displayTabs);
    const nextTab = displayTabs.find(
      (candidate) => candidate.id === tab.id);
    const source = displaySourceReference(select);
    nextTab.displays.push({
      id: displayId,
      providerTypeId: provider.providerTypeId,
      providerVersion: provider.descriptorVersion,
      owner: { unitId: owner.id, role: "provider" },
      source,
      settingsVersion: provider.settings.version,
      settings
    });
    const nextRow = nextTab.layout.items.reduce(
      (maximum, item) => Math.max(maximum, item.row + item.height),
      0);
    nextTab.layout.items.push({
      displayId,
      column: 0,
      row: nextRow,
      width: nextTab.layout.columns,
      height: 6
    });
    nextTab.layout.selectedDisplayId = displayId;
    const result = await applyMutation([
      {
        op: "replaceDisplayHierarchy",
        displayTabs
      }
    ], `${provider.name} added to ${tab.name}`);
    if (result) {
      state.activeTabId = tab.id;
      renderDisplays();
    }
  }

  async function configureDisplay(tabId, displayId) {
    const tab = state.active?.project?.displayTabs
      ?.find((candidate) => candidate.id === tabId);
    const display = tab?.displays
      ?.find((candidate) => candidate.id === displayId);
    const provider = display &&
      state.providerByType.get(display.providerTypeId);
    if (!tab || !display || !provider || !canEditStructure()) return;

    const body = createElement("div", {
      className: "settings-sections settings-stack"
    });
    const sourceSection = createElement("section", {
      className: "settings-section"
    });
    sourceSection.append(createElement("h3", { text: "Data Source" }));
    const sourceLabel = createElement("label", {
      className: "settings-field"
    });
    sourceLabel.append(createElement("span", {
      className: "settings-label",
      text: provider.inputs?.[0]?.name || "Source"
    }));
    const select = createElement("select");
    select.setAttribute(
      "data-input-role",
      provider.inputs?.[0]?.id || "fft");
    select.append(createElement("option", {
      text: "No source",
      attributes: { value: "" }
    }));
    for (const option of displaySourceOptions(provider, display.source)) {
      const element = createElement("option", {
        text: option.label,
        attributes: {
          value: `${option.source.unitId}|${option.source.outputRole}`
        }
      });
      element.selected = option.selected;
      select.append(element);
    }
    sourceLabel.append(select);
    const collectors = [];
    if (provider.providerTypeId === "pamguard.spectrogram-display") {
      appendSpectrogramSettingsEditor(
        body,
        sourceLabel,
        select,
        display.settings,
        collectors);
    }
    else {
      sourceSection.append(sourceLabel);
      body.append(sourceSection);
      const settingsSection = createElement("section", {
        className: "settings-section"
      });
      settingsSection.append(createElement("h3", {
        text: "Display Settings"
      }));
      appendSettingsFields(
        settingsSection,
        provider.settings.schema,
        display.settings,
        collectors);
      body.append(settingsSection);
    }
    const accepted = await showFormDialog({
      eyebrow: tab.name,
      title: provider.name,
      body,
      acceptLabel: "OK",
      note: "OK updates this display instance immediately."
    });
    if (!accepted) return;
    let settings;
    try {
      settings = collectSettings(display.settings, collectors);
    }
    catch (error) {
      showToast(normalizedError(error), "error", 0);
      return;
    }
    const displayTabs = deepClone(state.active.project.displayTabs);
    const nextDisplay = displayTabs
      .find((candidate) => candidate.id === tabId).displays
      .find((candidate) => candidate.id === displayId);
    nextDisplay.settings = settings;
    nextDisplay.source = displaySourceReference(select);
    await applyMutation([
      { op: "replaceDisplayHierarchy", displayTabs }
    ], `${provider.name} updated`);
  }

  async function removeDisplay(tabId, displayId) {
    if (!canEditStructure()) return;
    const tab = state.active.project.displayTabs
      .find((candidate) => candidate.id === tabId);
    const display = tab?.displays
      ?.find((candidate) => candidate.id === displayId);
    if (!tab || !display) return;
    const provider =
      state.providerByType.get(display.providerTypeId);
    const body = createElement("div", { className: "dialog-stack" });
    body.append(createElement("p", {
      className: "dialog-intro",
      text: `Remove this ${provider?.name || "display"} from ${tab.name}?`
    }));
    const accepted = await showFormDialog({
      eyebrow: tab.name,
      title: "Remove display",
      body,
      acceptLabel: "Remove",
      dangerous: true
    });
    if (!accepted) return;
    const displayTabs = deepClone(state.active.project.displayTabs);
    const nextTab = displayTabs.find(
      (candidate) => candidate.id === tabId);
    nextTab.displays = nextTab.displays.filter(
      (candidate) => candidate.id !== displayId);
    nextTab.layout.items = nextTab.layout.items.filter(
      (item) => item.displayId !== displayId);
    nextTab.layout.selectedDisplayId =
      nextTab.displays[0]?.id || null;
    await applyMutation([
      { op: "replaceDisplayHierarchy", displayTabs }
    ], "Display removed");
  }

  async function newProject() {
    if (!state.active ||
        state.commandBusy ||
        state.runtimeBusy ||
        state.conflicted) return;
    const body = createElement("div", { className: "dialog-stack" });
    const nameLabel = createElement("label", {
      className: "dialog-field"
    });
    nameLabel.append(createElement("span", { text: "Project name" }));
    const name = createElement("input", {
      attributes: {
        required: "required",
        maxlength: "128",
        autocomplete: "off"
      }
    });
    name.value = "Untitled Project";
    nameLabel.append(name);
    const descriptionLabel = createElement("label", {
      className: "dialog-field"
    });
    descriptionLabel.append(createElement("span", {
      text: "Description"
    }));
    const description = createElement("textarea", {
      attributes: { rows: "3" }
    });
    descriptionLabel.append(description);
    body.append(nameLabel, descriptionLabel);
    let discard = null;
    if (state.active.dirty || state.layoutDirty) {
      const discardLabel = createElement("label", {
        className: "check-field dialog-callout"
      });
      discard = createElement("input", {
        type: "checkbox",
        attributes: { required: "required" }
      });
      discardLabel.append(
        discard,
        createElement("span", {
          text: "Discard the unsaved changes in the current project"
        }));
      body.append(discardLabel);
    }
    const accepted = await showFormDialog({
      eyebrow: "File",
      title: "New project",
      body,
      acceptLabel: "Create project",
      focus: name
    });
    if (!accepted) return;
    state.commandBusy = true;
    renderControls();
    try {
      const active = await projectClient.newProject({
        name: name.value,
        description: description.value,
        discardDirty: Boolean(discard?.checked)
      });
      adoptActive(active);
      state.activeTabId = "data-model";
      renderAll();
      await refreshAfterCommit("New project");
      showToast(`${active.project.metadata.name} created`, "success");
    }
    catch (error) {
      handleCommandError(error, "Could not create project");
    }
    finally {
      state.commandBusy = false;
      renderControls();
    }
  }

  async function openProject() {
    if (!state.active ||
        state.commandBusy ||
        state.runtimeBusy ||
        state.conflicted) return;
    let projects;
    try {
      projects = await projectClient.listProjects();
    }
    catch (error) {
      handleCommandError(error, "Could not list saved projects");
      return;
    }
    const available = (projects.projects || [])
      .filter((project) => project.status === "available");
    const body = createElement("div", { className: "dialog-stack" });
    if (!available.length) {
      body.append(createElement("p", {
        className: "dialog-intro",
        text: "There are no saved projects in the configured project store."
      }));
      await showFormDialog({
        eyebrow: "File",
        title: "Open project",
        body,
        acceptLabel: "Close",
        cancelHidden: true
      });
      return;
    }
    const label = createElement("label", {
      className: "dialog-field"
    });
    label.append(createElement("span", { text: "Saved project" }));
    const select = createElement("select");
    for (const project of available) {
      select.append(createElement("option", {
        text: `${project.name} · revision ${project.savedRevision}`,
        attributes: { value: project.projectId }
      }));
    }
    label.append(select);
    body.append(label);
    let discard = null;
    if (state.active.dirty || state.layoutDirty) {
      const discardLabel = createElement("label", {
        className: "check-field dialog-callout"
      });
      discard = createElement("input", {
        type: "checkbox",
        attributes: { required: "required" }
      });
      discardLabel.append(
        discard,
        createElement("span", {
          text: "Discard the unsaved changes in the current project"
        }));
      body.append(discardLabel);
    }
    const accepted = await showFormDialog({
      eyebrow: "File",
      title: "Open project",
      body,
      acceptLabel: "Open project",
      focus: select
    });
    if (!accepted) return;
    state.commandBusy = true;
    renderControls();
    try {
      const active = await projectClient.openProject({
        projectId: select.value,
        discardDirty: Boolean(discard?.checked)
      });
      adoptActive(active);
      state.activeTabId = "data-model";
      renderAll();
      await refreshAfterCommit("Open project");
      showToast(`${active.project.metadata.name} opened`, "success");
    }
    catch (error) {
      handleCommandError(error, "Could not open project");
    }
    finally {
      state.commandBusy = false;
      renderControls();
    }
  }

  async function saveProject() {
    if (!state.active || state.commandBusy || state.conflicted) return;
    if (state.active.savedRevision === null) {
      await saveProjectAs();
      return;
    }
    state.commandBusy = true;
    renderControls();
    try {
      const active = await projectClient.save();
      adoptActive(active);
      renderAll();
      showToast(`${active.project.metadata.name} saved`, "success");
    }
    catch (error) {
      handleCommandError(error, "Could not save project");
    }
    finally {
      state.commandBusy = false;
      renderControls();
    }
  }

  async function saveProjectAs() {
    if (!state.active || state.commandBusy || state.conflicted) return;
    const body = createElement("div", { className: "dialog-stack" });
    const label = createElement("label", {
      className: "dialog-field"
    });
    label.append(createElement("span", { text: "Saved project name" }));
    const input = createElement("input", {
      attributes: {
        required: "required",
        maxlength: "128",
        autocomplete: "off"
      }
    });
    input.value = state.active.project.metadata.name;
    label.append(input);
    body.append(
      label,
      createElement("p", {
        className: "section-help",
        text: "Save As creates a new durable project identity while " +
          "preserving controlled-unit, data-block, display and layout IDs."
      }));
    const accepted = await showFormDialog({
      eyebrow: "File",
      title: "Save project as",
      body,
      acceptLabel: "Save As",
      focus: input
    });
    if (!accepted) return;
    state.commandBusy = true;
    renderControls();
    try {
      const active = await projectClient.saveAs({ name: input.value });
      adoptActive(active);
      renderAll();
      await refreshAfterCommit("Save As");
      showToast(`${active.project.metadata.name} saved`, "success");
    }
    catch (error) {
      handleCommandError(error, "Could not save project as");
    }
    finally {
      state.commandBusy = false;
      renderControls();
    }
  }

  async function startConfiguredAcquisitionCaptures() {
    const inventory =
      await serviceJson("/v1/projects/active/acquisitions");
    const acquisitions = inventory.acquisitions || [];
    const configured = acquisitions.filter(
      (acquisition) =>
        acquisition.configurationStatus === "configured");
    const needsConfiguration = acquisitions.filter(
      (acquisition) =>
        acquisition.configurationStatus !== "configured");
    if (!inventory.captureEnabled) {
      return {
        started: [],
        needsConfiguration,
        captureDisabled: configured
      };
    }

    const started = [];
    for (const acquisition of configured) {
      if (acquisition.captureRunning) continue;
      await serviceJson(
        `/v1/projects/active/acquisitions/${encodeURIComponent(
          acquisition.unitId)}/capture:start`,
        {
          method: "POST",
          body: {
            expectedWorkingRevision:
              state.active.workingRevision
          }
        });
      started.push(acquisition);
    }
    return {
      started,
      needsConfiguration,
      captureDisabled: []
    };
  }

  async function runtimeControl(action) {
    if (state.runtimeBusy ||
        state.commandBusy ||
        state.layoutDirty ||
        state.conflicted ||
        !state.active) return;
    state.runtimeBusy = true;
    state.runtimeGeneration += 1;
    let lifecycleSucceeded = false;
    renderControls();
    try {
      if (action === "stop") {
        await stopAllSoundOutputMonitors();
      }
      await serviceJson("/module-runtime/control", {
        method: "POST",
        body: { action }
      });
      let acquisitionStart = null;
      if (action === "start") {
        try {
          acquisitionStart =
            await startConfiguredAcquisitionCaptures();
        }
        catch (error) {
          try {
            await serviceJson("/module-runtime/control", {
              method: "POST",
              body: { action: "stop" }
            });
          }
          catch {
            // Preserve the actionable capture-start failure below.
          }
          throw new Error(
            "A configured Sound Acquisition could not start, so " +
            `processing was stopped: ${normalizedError(error)}`);
        }
      }
      state.runtime = await serviceJson("/module-runtime/status");
      try {
        state.ready = await loadReadiness();
      }
      catch (error) {
        showToast(
          `Processing changed state, but readiness could not be ` +
            `refreshed: ${normalizedError(error)}`,
          "warning",
          0);
      }
      lifecycleSucceeded = true;
      renderAll();
      showToast(
        action === "start"
          ? acquisitionStart?.started?.length
            ? `PAMGuard processing and ${
                acquisitionStart.started.length
              } host input${
                acquisitionStart.started.length === 1 ? "" : "s"
              } started`
            : "PAMGuard processing started"
          : "PAMGuard processing stopped cleanly",
        "success");
      if (action === "start" &&
          acquisitionStart?.needsConfiguration?.length) {
        showToast(
          `${
            acquisitionStart.needsConfiguration
              .map((item) => item.name)
              .join(", ")
          } has no host input. Configure it from the Acquisition node.`,
          "warning",
          0);
      }
      if (action === "start" &&
          acquisitionStart?.captureDisabled?.length) {
        showToast(
          "Host capture is disabled on this engine; processing is " +
            "waiting for supervised PCM ingest.",
          "warning",
          0);
      }
    }
    catch (error) {
      showToast(
        `${humanize(action)} failed: ${normalizedError(error)}`,
        "error",
        0);
      try {
        state.runtime = await serviceJson("/module-runtime/status");
        try {
          state.ready = await loadReadiness();
        }
        catch {
          // Preserve the original actionable lifecycle error.
        }
      }
      catch {
        // Preserve the original actionable lifecycle error.
      }
      renderAll();
    }
    finally {
      state.runtimeBusy = false;
      renderControls();
      if (lifecycleSucceeded &&
          action === "stop" &&
          !state.runtime?.running) {
        void persistLocalDisplaySelections();
      }
    }
  }

  async function persistLocalDisplaySelections() {
    if (!state.localDisplaySelections.size ||
        !canEditStructure()) return;
    const displayTabs = deepClone(
      state.active.project.displayTabs);
    let changed = false;
    for (const tab of displayTabs) {
      const selected =
        state.localDisplaySelections.get(tab.id);
      if (!selected ||
          !tab.displays.some(
            (display) => display.id === selected) ||
          tab.layout.selectedDisplayId === selected) continue;
      tab.layout.selectedDisplayId = selected;
      changed = true;
    }
    if (!changed) {
      state.localDisplaySelections.clear();
      renderDisplays();
      return;
    }
    await applyMutation([
      { op: "replaceDisplayHierarchy", displayTabs }
    ], "Display selection saved");
  }

  async function configureConnection() {
    if (state.commandBusy ||
        state.runtimeBusy ||
        state.runtime?.running) return;
    const body = createElement("div", { className: "dialog-stack" });
    const baseLabel = createElement("label", {
      className: "dialog-field"
    });
    baseLabel.append(createElement("span", { text: "Engine base URL" }));
    const base = createElement("input", {
      type: "url",
      attributes: { required: "required" }
    });
    base.value = state.baseUrl;
    baseLabel.append(base);
    const keyLabel = createElement("label", {
      className: "dialog-field"
    });
    keyLabel.append(createElement("span", { text: "API key" }));
    const key = createElement("input", {
      type: "password",
      attributes: { autocomplete: "off" }
    });
    key.value = state.apiKey;
    keyLabel.append(key);
    body.append(
      baseLabel,
      keyLabel,
      createElement("p", {
        className: "section-help",
        text: "Connection details are held only in this page. They are " +
          "not written into the PAMGuard project."
      }));
    const accepted = await showFormDialog({
      eyebrow: "Diagnostics / Developer",
      title: "Engine connection",
      body,
      acceptLabel: "Connect",
      focus: base
    });
    if (!accepted) return;
    state.baseUrl = base.value.replace(/\/$/, "");
    state.apiKey = key.value;
    state.catalogue = null;
    state.active = null;
    state.inspection = null;
    state.runtime = null;
    clearConflict();
    createClients();
    await initializeProject();
  }

  async function showDiagnostics() {
    const body = $("inspectionBody");
    $("inspectionTitle").textContent = "Diagnostics / Developer";
    body.replaceChildren();
    const refresh = createElement("button", {
      type: "button",
      text: "Refresh diagnostics"
    });
    const output = createElement("pre", {
      className: "diagnostics-output",
      text: "Loading…"
    });
    const load = async () => {
      refresh.disabled = true;
      try {
        const [health, ready, runtime] = await Promise.all([
          serviceJson("/health"),
          serviceJson("/ready").catch((error) => ({
            ok: false,
            error: normalizedError(error),
            body: error.body || null
          })),
          serviceJson("/module-runtime/status")
        ]);
        output.textContent = JSON.stringify({
          connection: {
            baseUrl: state.baseUrl,
            projectEtag: projectClient.activeEtag
          },
          health,
          ready,
          runtime,
          activeProject: state.active,
          inspection: state.inspection
        }, null, 2);
      }
      catch (error) {
        output.textContent = normalizedError(error);
      }
      finally {
        refresh.disabled = false;
      }
    };
    refresh.addEventListener("click", () => void load());
    body.append(
      createElement("p", {
        className: "section-help",
        text: "This read-only developer surface reports the active project " +
          "authority and generated runtime. It is not another editor."
      }),
      refresh,
      output);
    $("inspectionDialog").showModal();
    await load();
  }

  async function showAbout() {
    const body = $("inspectionBody");
    $("inspectionTitle").textContent = "About PAMGuard Web";
    body.replaceChildren(
      createElement("p", {
        className: "dialog-intro",
        text: "A web-native C++ port built against PAMGuard Java 2.02.18e " +
          "as the behavioral authority."
      }),
      createElement("div", {
        className: "dialog-callout",
        text: "The Data Model is the single configuration authority. " +
          "Modules own their settings, data blocks, actions and displays."
      }));
    $("inspectionDialog").showModal();
  }

  async function showFormDialog({
    eyebrow,
    title,
    body,
    acceptLabel = "OK",
    note = "",
    dangerous = false,
    focus = null,
    cancelHidden = false
  }) {
    const dialog = $("formDialog");
    if (dialog.open) {
      showToast(
        "Finish or cancel the open dialog before starting another action.",
        "warning");
      return false;
    }
    // HTMLDialogElement queues its `close` event after removing the `open`
    // attribute. A fast follow-up action can therefore observe open=false
    // while the previous dialog lifecycle is still pending. Wait for that
    // event before attaching the next close listener, otherwise the stale
    // event can resolve and dispose the newly mounted settings editor.
    await formDialogCloseBarrier;
    if (dialog.open) {
      showToast(
        "Finish or cancel the open dialog before starting another action.",
        "warning");
      return false;
    }
    const form = $("dialogForm");
    $("dialogEyebrow").textContent = eyebrow || "PAMGuard";
    $("dialogTitle").textContent = title;
    $("dialogBody").replaceChildren(body);
    $("dialogNote").textContent = note;
    const accept = $("dialogAccept");
    accept.textContent = acceptLabel;
    accept.classList.toggle("danger", dangerous);
    const cancel = form.querySelector(
      ".dialog-actions button[value='cancel']");
    cancel.hidden = cancelHidden;
    dialog.returnValue = "";

    const resetActions = () => {
      accept.classList.remove("danger");
      cancel.hidden = false;
    };
    const closed = new Promise((resolve, reject) => {
      const onClose = () => {
        resetActions();
        resolve(dialog.returnValue === "default");
      };
      dialog.addEventListener("close", onClose, { once: true });
      try {
        dialog.showModal();
      }
      catch (error) {
        dialog.removeEventListener("close", onClose);
        resetActions();
        reject(error);
        return;
      }
      requestAnimationFrame(() => {
        (focus || dialog.querySelector(
          "input:not([type='hidden']), select, textarea, button"))
          ?.focus();
        if (focus?.select) focus.select();
      });
    });
    formDialogCloseBarrier = closed.then(
      () => undefined,
      () => undefined);
    return closed;
  }

  async function pollStatus() {
    if (state.disposed ||
        !state.active ||
        state.commandBusy ||
        state.runtimeBusy ||
        state.statusPollBusy) return;
    state.statusPollBusy = true;
    const generation = state.runtimeGeneration;
    const clientGeneration = state.clientGeneration;
    const runtimeWasRunning = Boolean(state.runtime?.running);
    try {
      const runtime = await serviceJson("/module-runtime/status");
      if (state.disposed ||
          state.commandBusy ||
          state.runtimeBusy ||
          generation !== state.runtimeGeneration ||
          clientGeneration !== state.clientGeneration) return;
      state.runtime = runtime;
      if (runtimeWasRunning && !runtime.running) {
        await stopAllSoundOutputMonitors();
      }
      state.statusPoll += 1;
      if (state.statusPoll % 5 === 0) {
        try {
          state.ready = await loadReadiness();
        }
        catch (error) {
          state.ready = error.body || {
            ok: false,
            error: normalizedError(error)
          };
        }
      }
      if (state.disposed ||
          state.commandBusy ||
          state.runtimeBusy ||
          generation !== state.runtimeGeneration ||
          clientGeneration !== state.clientGeneration) return;
      renderRuntimeStatus();
      renderControls();
    }
    catch (error) {
      if (state.disposed ||
          state.commandBusy ||
          state.runtimeBusy ||
          generation !== state.runtimeGeneration ||
          clientGeneration !== state.clientGeneration) return;
      $("serviceState").textContent =
        `Engine: ${normalizedError(error)}`;
    }
    finally {
      state.statusPollBusy = false;
    }
  }

  async function initializeProject() {
    state.commandBusy = true;
    renderControls();
    try {
      const [catalogue, active] = await Promise.all([
        projectClient.loadCatalogue(),
        projectClient.loadActive()
      ]);
      if (state.disposed) return;
      state.catalogue = catalogue;
      state.descriptorByType = new Map(
        catalogue.controlledUnitTypes.map(
          (descriptor) => [descriptor.typeId, descriptor]));
      state.providerByType = new Map(
        catalogue.displayProviderTypes.map(
          (provider) => [provider.providerTypeId, provider]));
      adoptActive(active);
      renderAll();
      await refreshAfterCommit("Project load");
    }
    catch (error) {
      handleCommandError(error, "PAMGuard initialization failed");
      renderDisconnected(error);
    }
    finally {
      state.commandBusy = false;
      renderControls();
    }
  }

  function mountListeners() {
    for (const trigger of document.querySelectorAll(".menu-trigger")) {
      trigger.addEventListener("click", (event) => {
        event.stopPropagation();
        const menu = trigger.closest(".menu");
        const open = !menu.classList.contains("open");
        closeMenus();
        if (open) {
          menu.classList.add("open");
          trigger.setAttribute("aria-expanded", "true");
        }
      });
    }
    document.addEventListener("click", (event) => {
      if (!event.target.closest(".menu")) closeMenus();
      if (!event.target.closest("#unitContextMenu") &&
          !event.target.closest(".controlled-unit-node")) {
        hideContextMenu();
      }
    });
    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape") {
        closeMenus();
        hideContextMenu();
        if (state.connectionDraft) {
          cancelGraphConnection("Connection selection cancelled.");
        }
      }
    });
    $("fileNew").addEventListener("click", () => {
      closeMenus();
      void newProject();
    });
    $("fileOpen").addEventListener("click", () => {
      closeMenus();
      void openProject();
    });
    $("fileSave").addEventListener("click", () => {
      closeMenus();
      void saveProject();
    });
    $("fileSaveAs").addEventListener("click", () => {
      closeMenus();
      void saveProjectAs();
    });
    $("connectionSettings").addEventListener("click", () => {
      closeMenus();
      void configureConnection();
    });
    $("showDiagnostics").addEventListener("click", () => {
      closeMenus();
      void showDiagnostics();
    });
    $("showAbout").addEventListener("click", () => {
      closeMenus();
      void showAbout();
    });
    $("runtimeStart").addEventListener("click", () =>
      void runtimeControl("start"));
    $("runtimeStop").addEventListener("click", () =>
      void runtimeControl("stop"));
    $("reloadAfterConflict").addEventListener("click", () =>
      void reloadProject({ announce: true }));
    $("openPalette").addEventListener("click", () =>
      $("modulePalette").classList.add("open"));
    $("emptyAddModule").addEventListener("click", () =>
      $("modulePalette").classList.add("open"));
    $("emptyClickMonitoringTemplate").addEventListener(
      "click",
      () => void addClickMonitoringConfiguration());
    $("closePalette").addEventListener("click", () =>
      $("modulePalette").classList.remove("open"));
    $("moduleSearch").addEventListener("input", renderPalette);
    $("zoomIn").addEventListener("click", () => {
      const viewport = state.active ? state.viewport : null;
      if (viewport) setViewport({
        ...viewport,
        zoom: Math.min(8, viewport.zoom * 1.15)
      });
    });
    $("zoomOut").addEventListener("click", () => {
      const viewport = state.active ? state.viewport : null;
      if (viewport) setViewport({
        ...viewport,
        zoom: Math.max(0.1, viewport.zoom / 1.15)
      });
    });
    $("fitModel").addEventListener("click", fitGraph);
    $("arrangeModel").addEventListener("click", arrangeGraph);
    $("dataModelTab").addEventListener("click", () =>
      activateTab("data-model"));
    $("unitContextMenu").addEventListener("click", (event) => {
      const action = event.target.closest("[data-unit-action]")
        ?.dataset.unitAction;
      const unitId = state.contextUnitId;
      if (!action || !unitId) return;
      hideContextMenu();
      if (action === "configure") void configureUnit(unitId);
      if (action === "inspect") void inspectUnit(unitId);
      if (action === "rename") void renameUnit(unitId);
      if (action === "remove") void removeUnit(unitId);
      if (action === "help") {
        const descriptor = descriptorForUnit(unitById(unitId));
        if (descriptor?.help?.point) {
          showToast(
            `Authoritative help target: ${descriptor.help.point}`,
            "info",
            8000);
        }
        else {
          showToast("No authoritative help target is registered.");
        }
      }
    });
    window.addEventListener("resize", () => {
      hideContextMenu();
      drawWires();
    });
    window.addEventListener("beforeunload", (event) => {
      if (state.active?.dirty || state.layoutDirty) {
        event.preventDefault();
        event.returnValue = "";
      }
    });
  }

  function mountApplication() {
    if (state.disposed) {
      throw new Error(
        "PAMGuard project shell disposal is terminal; reload the page " +
          "to mount a fresh application");
    }
    if (activeApplication) {
      throw new Error("PAMGuard project shell can mount only once");
    }
    createClients();
    mountListeners();
    state.statusTimer = window.setInterval(
      () => void pollStatus(),
      STATUS_INTERVAL_MS);
    const application = Object.freeze({
      async dispose() {
        if (state.disposed) return;
        state.disposed = true;
        state.runtimeGeneration += 1;
        state.drag?.cancel?.();
        if (state.statusTimer !== null) {
          clearInterval(state.statusTimer);
          state.statusTimer = null;
        }
        disposeDisplayRuntimes();
        await disposeAllSoundOutputMonitors();
        projectClient?.dispose();
        httpClient?.dispose();
        closeMenus();
        hideContextMenu();
        for (const dialog of document.querySelectorAll("dialog[open]")) {
          dialog.close();
        }
        $("modelNodes").replaceChildren();
        $("modelWires").replaceChildren();
        $("displayPanels").replaceChildren();
        for (const tab of $("tabStrip").querySelectorAll(
          "[data-pamguard-tab-kind='display']")) {
          tab.remove();
        }
        platform.lifecycle.dispose();
        activeApplication = null;
      }
    });
    activeApplication = application;
    platform.lifecycle.seal();
    void initializeProject();
    return application;
  }

  globalThis.PamguardApplication = Object.freeze({
    mount: mountApplication,
    get active() {
      return activeApplication;
    }
  });

  activeApplication = mountApplication();
})();
