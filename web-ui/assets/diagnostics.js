    // ================= diagnostics ======================================

    $("healthButton").addEventListener("click", async () => {
      try {
        log(await requestJson(api("/health")));
      } catch (error) {
        log(String(error));
      }
    });

    $("ingestStatusButton").addEventListener("click", async () => {
      try {
        log(await requestJson(api("/ingest/status")));
      } catch (error) {
        log(String(error));
      }
    });

    async function refreshHealthStatus() {
      const status = $("menubarStatus");
      try {
        const health = await requestJson(api("/health"));
        status.textContent =
          `connected · schema v${health.resultSchemaVersion} · ` +
          `${health.sessions} session${health.sessions === 1 ? "" : "s"} · ` +
          `capture ${health.captureEnabled ? "on" : "off"}`;
        status.classList.add("ok");
        status.classList.remove("bad");
      } catch (error) {
        status.textContent = "engine unreachable";
        status.classList.add("bad");
        status.classList.remove("ok");
      }
    }

    let diagnosticsHealthTimer = null;
    const diagnosticsController = Object.freeze({
      mount() {
        if (diagnosticsHealthTimer !== null) {
          throw new Error(
            "Diagnostics controller is already mounted");
        }
        refreshHealthStatus();
        diagnosticsHealthTimer = setInterval(
          refreshHealthStatus,
          10000);
      },
      dispose() {
        if (diagnosticsHealthTimer !== null) {
          clearInterval(diagnosticsHealthTimer);
          diagnosticsHealthTimer = null;
        }
      }
    });

    globalThis.PamguardModules = Object.freeze({
      ...(globalThis.PamguardModules || {}),
      diagnostics: diagnosticsController
    });
