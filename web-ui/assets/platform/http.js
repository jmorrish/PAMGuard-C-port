(() => {
  "use strict";

  class HttpError extends Error {
    constructor(message, {
      status,
      statusText,
      url,
      body
    }) {
      super(message);
      this.status = status;
      this.statusText = statusText;
      this.url = url;
      this.body = body;
    }
  }

  function createHttpClient({
    getBaseUrl,
    getApiKey,
    fetchImpl = globalThis.fetch.bind(globalThis)
  }) {
    if (typeof getBaseUrl !== "function" ||
        typeof getApiKey !== "function" ||
        typeof fetchImpl !== "function") {
      throw new TypeError(
        "HTTP client requires base URL, API-key, and fetch providers");
    }
    const requestControllers = new Set();
    let disposed = false;

    function api(path) {
      return String(getBaseUrl()).replace(/\/$/, "") + path;
    }

    function authorizedOptions(options = {}) {
      const apiKey = String(getApiKey() || "").trim();
      if (apiKey) {
        options.headers = {
          ...(options.headers || {}),
          "X-API-Key": apiKey
        };
      }
      return options;
    }

    async function fetchAuthorized(url, options) {
      if (disposed) {
        throw new DOMException(
          "HTTP client is disposed",
          "AbortError");
      }
      options = authorizedOptions(options);
      const externalSignal = options.signal;
      const controller = new AbortController();
      const abortFromExternal = () =>
        controller.abort(externalSignal?.reason);
      if (externalSignal?.aborted) {
        abortFromExternal();
      }
      else {
        externalSignal?.addEventListener(
          "abort",
          abortFromExternal,
          { once: true });
      }
      requestControllers.add(controller);
      try {
        return await fetchImpl(url, {
          ...options,
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

    async function requestJson(url, options = {}) {
      const response = await fetchAuthorized(url, options);
      const text = await response.text();
      const body = text ? JSON.parse(text) : {};
      if (!response.ok) {
        throw new HttpError(
          body.error || response.statusText,
          {
            status: response.status,
            statusText: response.statusText,
            url,
            body
          });
      }
      return body;
    }

    async function requestText(url, options = {}) {
      const response = await fetchAuthorized(url, options);
      const text = await response.text();
      if (!response.ok) {
        throw new HttpError(
          text || response.statusText,
          {
            status: response.status,
            statusText: response.statusText,
            url,
            body: text
          });
      }
      return text;
    }

    return Object.freeze({
      api,
      requestJson,
      requestText,
      dispose() {
        if (disposed) return;
        disposed = true;
        for (const controller of requestControllers) {
          controller.abort();
        }
        requestControllers.clear();
      }
    });
  }

  const platform = globalThis.PamguardPlatform || {};
  globalThis.PamguardPlatform = Object.freeze({
    ...platform,
    http: Object.freeze({
      HttpError,
      createHttpClient
    })
  });
})();
