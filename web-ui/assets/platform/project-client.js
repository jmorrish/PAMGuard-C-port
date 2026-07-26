(() => {
  "use strict";

  const PROJECT_SCHEMA_VERSION = 1;
  const STRONG_PROJECT_ETAG =
    /^"pgp1-[A-Za-z0-9_-]{43}"$/;
  const PROJECT_UUID =
    /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
  const PROJECT_ROLE = /^[a-z][A-Za-z0-9]{0,63}$/;

  class ProjectClientError extends Error {
    constructor(message, options = {}) {
      super(message);
      this.name = new.target.name;
      this.code = options.code || "";
      if (options.cause !== undefined) {
        this.cause = options.cause;
      }
    }
  }

  class ProjectStateError extends ProjectClientError {}

  class ProjectProtocolError extends ProjectClientError {
    constructor(message, options = {}) {
      super(message, options);
      this.status = options.status || 0;
      this.url = options.url || "";
      this.body = options.body ?? null;
    }
  }

  class ProjectRequestError extends ProjectClientError {
    constructor(message, options = {}) {
      super(message, {
        code: options.code,
        cause: options.cause
      });
      this.status = options.status || 0;
      this.statusText = options.statusText || "";
      this.url = options.url || "";
      this.body = options.body ?? null;
      this.currentEtag = options.currentEtag || null;
    }
  }

  class ProjectConflictError extends ProjectRequestError {
    constructor(message, options = {}) {
      super(message, options);
      this.conflict = true;
    }
  }

  function abortError(message) {
    if (typeof DOMException === "function") {
      return new DOMException(message, "AbortError");
    }
    const error = new Error(message);
    error.name = "AbortError";
    return error;
  }

  function isPlainObject(value) {
    if (value === null || typeof value !== "object" ||
        Array.isArray(value)) {
      return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
  }

  function exactObject(value, label, allowedKeys) {
    if (!isPlainObject(value)) {
      throw new TypeError(`${label} must be an object`);
    }
    const allowed = new Set(allowedKeys);
    const unknown = Object.keys(value)
      .filter((key) => !allowed.has(key));
    if (unknown.length) {
      throw new TypeError(
        `${label} contains unknown field '${unknown[0]}'`);
    }
    return value;
  }

  function requiredString(value, label) {
    if (typeof value !== "string" || value.length === 0) {
      throw new TypeError(`${label} must be a non-empty string`);
    }
    return value;
  }

  function optionalBoolean(value, fallback, label) {
    if (value === undefined) return fallback;
    if (typeof value !== "boolean") {
      throw new TypeError(`${label} must be a boolean`);
    }
    return value;
  }

  function requireSignal(signal) {
    if (signal === undefined || signal === null) return null;
    if (typeof signal !== "object" ||
        typeof signal.aborted !== "boolean" ||
        typeof signal.addEventListener !== "function" ||
        typeof signal.removeEventListener !== "function") {
      throw new TypeError("signal must be an AbortSignal");
    }
    return signal;
  }

  function encodeJson(value, label) {
    try {
      const encoded = JSON.stringify(value);
      if (encoded === undefined) {
        throw new TypeError("value is not JSON encodable");
      }
      return encoded;
    }
    catch (error) {
      throw new TypeError(
        `${label} could not be encoded as JSON: ${error.message}`);
    }
  }

  function requireSchemaOne(body, label) {
    if (!isPlainObject(body) ||
        body.schemaVersion !== PROJECT_SCHEMA_VERSION) {
      throw new ProjectProtocolError(
        `${label} is not a project schemaVersion 1 object`,
        { body });
    }
    return body;
  }

  function createProjectClient({
    httpClient = null,
    api = null,
    getBaseUrl = null,
    getApiKey = () => "",
    fetchImpl = globalThis.fetch?.bind(globalThis),
    initialEtag = null
  } = {}) {
    if (typeof fetchImpl !== "function") {
      throw new TypeError(
        "Project client requires a fetch implementation");
    }
    if (api !== null && typeof api !== "function") {
      throw new TypeError("api must be a URL resolver function");
    }
    if (getBaseUrl !== null &&
        typeof getBaseUrl !== "function") {
      throw new TypeError(
        "getBaseUrl must be a function");
    }
    if (typeof getApiKey !== "function") {
      throw new TypeError("getApiKey must be a function");
    }
    if (httpClient !== null &&
        (typeof httpClient !== "object" ||
         typeof httpClient.api !== "function")) {
      throw new TypeError(
        "httpClient must expose api(path)");
    }
    if (initialEtag !== null &&
        !STRONG_PROJECT_ETAG.test(initialEtag)) {
      throw new TypeError(
        "initialEtag must be an exact strong project ETag");
    }

    const requestControllers = new Set();
    let activeEtag = initialEtag;
    let disposed = false;
    let activeRequestTail = Promise.resolve();

    function resolveUrl(path) {
      if (api) return String(api(path));
      if (httpClient) return String(httpClient.api(path));
      if (getBaseUrl) {
        return String(getBaseUrl()).replace(/\/$/, "") + path;
      }
      return path;
    }

    function assertUsable() {
      if (disposed) {
        throw abortError("Project client is disposed");
      }
    }

    function requireActiveEtag() {
      assertUsable();
      if (!activeEtag) {
        throw new ProjectStateError(
          "Load the active project before issuing a project command",
          { code: "active_etag_unavailable" });
      }
      return activeEtag;
    }

    function enqueueActive(operation) {
      const result = activeRequestTail.then(
        operation,
        operation);
      activeRequestTail = result.then(
        () => undefined,
        () => undefined);
      return result;
    }

    async function fetchResponse(
      url,
      requestOptions,
      externalSignal) {
      assertUsable();
      if (externalSignal?.aborted) {
        throw abortError("Project request was aborted");
      }

      const controller = new AbortController();
      const abortFromExternal = () => {
        controller.abort(
          abortError("Project request was aborted"));
      };
      externalSignal?.addEventListener(
        "abort",
        abortFromExternal,
        { once: true });
      requestControllers.add(controller);
      try {
        return await fetchImpl(url, {
          ...requestOptions,
          signal: controller.signal
        });
      }
      finally {
        requestControllers.delete(controller);
        externalSignal?.removeEventListener(
          "abort",
          abortFromExternal);
      }
    }

    function responseHeader(response, name) {
      if (!response?.headers ||
          typeof response.headers.get !== "function") {
        throw new ProjectProtocolError(
          "Project response does not expose HTTP headers");
      }
      const value = response.headers.get(name);
      return value === null ? null : String(value);
    }

    function reconcileEtag(
      headerEtag,
      bodyEtag,
      {
        requireHeader = false,
        requireBody = false,
        adopt = true,
        status = 0,
        url = "",
        body = null
      } = {}) {
      if (requireHeader && headerEtag === null) {
        throw new ProjectProtocolError(
          "Project response omitted its ETag header",
          { status, url, body });
      }
      if (requireBody && bodyEtag === null) {
        throw new ProjectProtocolError(
          "Project response omitted its body ETag",
          { status, url, body });
      }
      if (headerEtag !== null &&
          !STRONG_PROJECT_ETAG.test(headerEtag)) {
        throw new ProjectProtocolError(
          "Project response supplied an invalid strong ETag header",
          { status, url, body });
      }
      if (bodyEtag !== null &&
          !STRONG_PROJECT_ETAG.test(bodyEtag)) {
        throw new ProjectProtocolError(
          "Project response supplied an invalid body ETag",
          { status, url, body });
      }
      if (headerEtag !== null && bodyEtag !== null &&
          headerEtag !== bodyEtag) {
        throw new ProjectProtocolError(
          "Project response header and body ETags disagree",
          { status, url, body });
      }
      const reconciled = headerEtag ?? bodyEtag;
      if (adopt && reconciled !== null) {
        activeEtag = reconciled;
      }
      return reconciled;
    }

    async function requestJson(path, {
      method = "GET",
      bodyText,
      expectedEtag = null,
      signal = null,
      activeError = false,
      expectedStatus = 200
    } = {}) {
      assertUsable();
      signal = requireSignal(signal);
      const url = resolveUrl(path);
      const headers = {
        "Accept": "application/json"
      };
      const apiKey = String(getApiKey() || "").trim();
      if (apiKey) {
        headers["X-API-Key"] = apiKey;
      }
      if (bodyText !== undefined) {
        headers["Content-Type"] = "application/json";
      }
      if (expectedEtag !== null) {
        if (!STRONG_PROJECT_ETAG.test(expectedEtag)) {
          throw new ProjectStateError(
            "The owned active-project ETag is invalid",
            { code: "invalid_active_etag" });
        }
        headers["If-Match"] = expectedEtag;
      }

      const response = await fetchResponse(
        url,
        {
          method,
          headers,
          ...(bodyText === undefined
            ? {}
            : { body: bodyText })
        },
        signal);
      if (!response ||
          typeof response.status !== "number" ||
          typeof response.text !== "function") {
        throw new ProjectProtocolError(
          "Project fetch returned an invalid Response",
          { url });
      }

      const text = await response.text();
      let body;
      try {
        body = text ? JSON.parse(text) : {};
      }
      catch (error) {
        throw new ProjectProtocolError(
          "Project response was not valid JSON",
          {
            status: response.status,
            url,
            body: text,
            cause: error
          });
      }

      if (!response.ok) {
        let currentEtag = null;
        if (activeError) {
          const headerEtag =
            responseHeader(response, "ETag");
          const bodyEtag =
            isPlainObject(body) &&
            typeof body.currentEtag === "string"
              ? body.currentEtag
              : null;
          const isConflict = response.status === 412;
          currentEtag = reconcileEtag(
            headerEtag,
            bodyEtag,
            {
              requireHeader: isConflict,
              requireBody: isConflict,
              adopt: false,
              status: response.status,
              url,
              body
            });
        }
        const ErrorType = response.status === 412
          ? ProjectConflictError
          : ProjectRequestError;
        throw new ErrorType(
          (isPlainObject(body) &&
           typeof body.error === "string" &&
           body.error) ||
            response.statusText ||
            "Project request failed",
          {
            status: response.status,
            statusText: response.statusText,
            url,
            body,
            code: isPlainObject(body) &&
              typeof body.code === "string"
              ? body.code
              : "",
            currentEtag
          });
      }
      if (response.status !== expectedStatus) {
        throw new ProjectProtocolError(
          `Project endpoint returned HTTP ${response.status}; ` +
            `expected ${expectedStatus}`,
          { status: response.status, url, body });
      }
      return { body, response, url };
    }

    function adoptSnapshot(result, label) {
      const body = requireSchemaOne(result.body, label);
      if (!isPlainObject(body.project)) {
        throw new ProjectProtocolError(
          `${label} omitted its project document`,
          {
            status: result.response.status,
            url: result.url,
            body
          });
      }
      reconcileEtag(
        responseHeader(result.response, "ETag"),
        typeof body.etag === "string" ? body.etag : null,
        {
          requireHeader: true,
          requireBody: true,
          status: result.response.status,
          url: result.url,
          body
        });
      return body;
    }

    function adoptMutationResult(result) {
      const body = requireSchemaOne(
        result.body,
        "Project mutation response");
      if (!isPlainObject(body.active)) {
        throw new ProjectProtocolError(
          "Project mutation response omitted its active snapshot",
          {
            status: result.response.status,
            url: result.url,
            body
          });
      }
      reconcileEtag(
        responseHeader(result.response, "ETag"),
        typeof body.active.etag === "string"
          ? body.active.etag
          : null,
        {
          requireHeader: true,
          requireBody: true,
          status: result.response.status,
          url: result.url,
          body
        });
      return body;
    }

    async function loadCatalogue({ signal } = {}) {
      const result = await requestJson(
        "/v1/controlled-unit-types",
        { signal });
      return requireSchemaOne(
        result.body,
        "Controlled-unit catalogue response");
    }

    async function listProjects({ signal } = {}) {
      const result = await requestJson(
        "/v1/projects",
        { signal });
      return requireSchemaOne(
        result.body,
        "Saved-project list response");
    }

    async function loadSavedProject(
      projectId,
      { signal } = {}) {
      if (!PROJECT_UUID.test(String(projectId))) {
        throw new TypeError(
          "projectId must be a lowercase UUIDv4");
      }
      const result = await requestJson(
        `/v1/projects/${encodeURIComponent(projectId)}`,
        { signal });
      if (!isPlainObject(result.body) ||
          !isPlainObject(result.body.project)) {
        throw new ProjectProtocolError(
          "Saved-project response omitted its project envelope",
          {
            status: result.response.status,
            url: result.url,
            body: result.body
          });
      }
      return result.body;
    }

    async function loadActive({ signal } = {}) {
      return enqueueActive(async () => {
        const result = await requestJson(
          "/v1/projects/active",
          {
            signal,
            activeError: true
          });
        return adoptSnapshot(
          result,
          "Active-project response");
      });
    }

    async function loadInspection({ signal } = {}) {
      const expectedEtag = requireActiveEtag();
      return enqueueActive(async () => {
        const result = await requestJson(
          "/v1/projects/active/inspection",
          {
            signal,
            activeError: true
          });
        const body = requireSchemaOne(
          result.body,
          "Project-inspection response");
        const observedEtag = reconcileEtag(
          responseHeader(result.response, "ETag"),
          null,
          {
            requireHeader: true,
            adopt: false,
            status: result.response.status,
            url: result.url,
            body
          });
        if (observedEtag !== expectedEtag) {
          throw new ProjectConflictError(
            "Project inspection no longer matches the loaded active project",
            {
              status: 412,
              statusText: "Precondition Failed",
              url: result.url,
              body,
              code: "active_project_changed",
              currentEtag: observedEtag
            });
        }
        return body;
      });
    }

    async function loadCompatibleSources(
      unitId,
      inputRole,
      { signal } = {}) {
      unitId = String(unitId);
      inputRole = String(inputRole);
      if (!PROJECT_UUID.test(unitId)) {
        throw new TypeError(
          "unitId must be a lowercase UUIDv4");
      }
      if (!PROJECT_ROLE.test(inputRole)) {
        throw new TypeError(
          "inputRole must be a lower-camel project role");
      }
      const query =
        `?unitId=${encodeURIComponent(unitId)}` +
        `&inputRole=${encodeURIComponent(inputRole)}`;
      const expectedEtag = requireActiveEtag();
      return enqueueActive(async () => {
        const result = await requestJson(
          "/v1/projects/active/compatible-sources" + query,
          {
            signal,
            activeError: true
          });
        const body = requireSchemaOne(
          result.body,
          "Compatible-source response");
        const observedEtag = reconcileEtag(
          responseHeader(result.response, "ETag"),
          null,
          {
            requireHeader: true,
            adopt: false,
            status: result.response.status,
            url: result.url,
            body
          });
        if (observedEtag !== expectedEtag) {
          throw new ProjectConflictError(
            "Compatible sources no longer match the loaded active project",
            {
              status: 412,
              statusText: "Precondition Failed",
              url: result.url,
              body,
              code: "active_project_changed",
              currentEtag: observedEtag
            });
        }
        return body;
      });
    }

    async function mutate(
      command,
      { signal } = {}) {
      command = exactObject(
        command,
        "mutation command",
        ["operations", "validateOnly"]);
      if (!Array.isArray(command.operations)) {
        throw new TypeError(
          "mutation command operations must be an array");
      }
      const expectedEtag = requireActiveEtag();
      const bodyText = encodeJson(
        {
          schemaVersion: PROJECT_SCHEMA_VERSION,
          validateOnly: optionalBoolean(
            command.validateOnly,
            false,
            "mutation command validateOnly"),
          operations: command.operations
        },
        "mutation command");
      return enqueueActive(async () => {
        const result = await requestJson(
          "/v1/projects/active/mutations",
          {
            method: "POST",
            bodyText,
            expectedEtag,
            signal,
            activeError: true
          });
        return adoptMutationResult(result);
      });
    }

    async function newProject(
      command,
      { signal } = {}) {
      command = exactObject(
        command,
        "new-project command",
        ["name", "description", "discardDirty"]);
      const expectedEtag = requireActiveEtag();
      const description = command.description ?? "";
      if (typeof description !== "string") {
        throw new TypeError(
          "new-project command description must be a string");
      }
      const bodyText = encodeJson(
        {
          schemaVersion: PROJECT_SCHEMA_VERSION,
          name: requiredString(
            command.name,
            "new-project command name"),
          description,
          discardDirty: optionalBoolean(
            command.discardDirty,
            false,
            "new-project command discardDirty")
        },
        "new-project command");
      return enqueueActive(async () => {
        const result = await requestJson(
          "/v1/projects/active/new",
          {
            method: "POST",
            bodyText,
            expectedEtag,
            signal,
            activeError: true
          });
        return adoptSnapshot(
          result,
          "New-project response");
      });
    }

    async function openProject(
      command,
      { signal } = {}) {
      command = exactObject(
        command,
        "open-project command",
        ["projectId", "discardDirty"]);
      const projectId = String(command.projectId);
      if (!PROJECT_UUID.test(projectId)) {
        throw new TypeError(
          "open-project command projectId must be a lowercase UUIDv4");
      }
      const expectedEtag = requireActiveEtag();
      const bodyText = encodeJson(
        {
          schemaVersion: PROJECT_SCHEMA_VERSION,
          projectId,
          discardDirty: optionalBoolean(
            command.discardDirty,
            false,
            "open-project command discardDirty")
        },
        "open-project command");
      return enqueueActive(async () => {
        const result = await requestJson(
          "/v1/projects/active/open",
          {
            method: "POST",
            bodyText,
            expectedEtag,
            signal,
            activeError: true
          });
        return adoptSnapshot(
          result,
          "Open-project response");
      });
    }

    async function save({ signal } = {}) {
      const expectedEtag = requireActiveEtag();
      return enqueueActive(async () => {
        const result = await requestJson(
          "/v1/projects/active/save",
          {
            method: "POST",
            expectedEtag,
            signal,
            activeError: true
          });
        return adoptSnapshot(
          result,
          "Save-project response");
      });
    }

    async function saveAs(
      command,
      { signal } = {}) {
      command = exactObject(
        command,
        "save-as command",
        ["name"]);
      const expectedEtag = requireActiveEtag();
      const bodyText = encodeJson(
        {
          schemaVersion: PROJECT_SCHEMA_VERSION,
          name: requiredString(
            command.name,
            "save-as command name")
        },
        "save-as command");
      return enqueueActive(async () => {
        const result = await requestJson(
          "/v1/projects/active/save-as",
          {
            method: "POST",
            bodyText,
            expectedEtag,
            signal,
            activeError: true,
            expectedStatus: 201
          });
        return adoptSnapshot(
          result,
          "Save-as response");
      });
    }

    return Object.freeze({
      get activeEtag() {
        return activeEtag;
      },
      get disposed() {
        return disposed;
      },
      loadCatalogue,
      listProjects,
      loadSavedProject,
      loadActive,
      loadInspection,
      loadCompatibleSources,
      mutate,
      newProject,
      openProject,
      save,
      saveAs,
      dispose() {
        if (disposed) return;
        disposed = true;
        activeEtag = null;
        const reason =
          abortError("Project client was disposed");
        for (const controller of requestControllers) {
          controller.abort(reason);
        }
        requestControllers.clear();
      }
    });
  }

  const platform = globalThis.PamguardPlatform || {};
  globalThis.PamguardPlatform = Object.freeze({
    ...platform,
    project: Object.freeze({
      PROJECT_SCHEMA_VERSION,
      STRONG_PROJECT_ETAG,
      ProjectClientError,
      ProjectStateError,
      ProjectProtocolError,
      ProjectRequestError,
      ProjectConflictError,
      createProjectClient
    })
  });
})();
