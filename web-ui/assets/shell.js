    // ================= UI chrome: menus, tabs, dialogs, status bar =========

    const shellCallbacks = {
      loadDataModel: async () => {},
      refreshDisplaySources: async () => {}
    };

    document.querySelectorAll(".menu > button").forEach((button) => {
      button.addEventListener("click", (event) => {
        event.stopPropagation();
        const menu = button.parentElement;
        const wasOpen = menu.classList.contains("open");
        document.querySelectorAll(".menu.open").forEach((m) => m.classList.remove("open"));
        if (!wasOpen) {
          menu.classList.add("open");
        }
      });
    });
    document.addEventListener("click", () => {
      document.querySelectorAll(".menu.open").forEach((m) => m.classList.remove("open"));
    });

    document.querySelectorAll("[data-dialog]").forEach((item) => {
      item.addEventListener("click", () => {
        document.querySelectorAll(".menu.open").forEach((m) => m.classList.remove("open"));
        const dialog = $(item.dataset.dialog);
        if (dialog) {
          dialog.showModal();
        }
      });
    });

    document.querySelectorAll("[data-click]").forEach((item) => {
      item.addEventListener("click", () => {
        document.querySelectorAll(".menu.open").forEach((m) => m.classList.remove("open"));
        const target = $(item.dataset.click);
        if (target) {
          target.click();
        }
      });
    });

    function activateTab(name) {
      document.querySelector(".workspace").classList.toggle(
        "graph-mode",
        name === "graph");
      document.querySelectorAll(".tabbar button").forEach((b) => {
        b.classList.toggle("active", b.dataset.tab === name);
      });
      document.querySelectorAll(".tab-page").forEach((page) => {
        page.classList.toggle("active", page.id === "tab-" + name);
      });
      if (name === "clicks") {
        requestAnimationFrame(() => {
          drawClickScatters();
          drawSelectedClick();
        });
      }
      if (name === "workspace") {
        shellCallbacks.refreshDisplaySources(false);
        requestAnimationFrame(() => {
          for (const display of workspaceDisplays.values()) {
            display.scheduleRender();
          }
        });
      }
      if (name === "graph") {
        shellCallbacks.loadDataModel();
      }
    }
    document.querySelectorAll(".tabbar button").forEach((b) => {
      b.addEventListener("click", () => activateTab(b.dataset.tab));
    });
    document.querySelectorAll("[data-tab-jump]").forEach((item) => {
      item.addEventListener("click", () => {
        document.querySelectorAll(".menu.open").forEach((m) => m.classList.remove("open"));
        activateTab(item.dataset.tabJump);
      });
    });

    let shellStatusTimer = null;
    const shellController = Object.freeze({
      configure({
        loadDataModel,
        refreshDisplaySources
      } = {}) {
        if (shellStatusTimer !== null) {
          throw new Error(
            "Shell callbacks cannot change while mounted");
        }
        if (loadDataModel !== undefined) {
          if (typeof loadDataModel !== "function") {
            throw new TypeError(
              "loadDataModel must be a function");
          }
          shellCallbacks.loadDataModel = loadDataModel;
        }
        if (refreshDisplaySources !== undefined) {
          if (typeof refreshDisplaySources !== "function") {
            throw new TypeError(
              "refreshDisplaySources must be a function");
          }
          shellCallbacks.refreshDisplaySources =
            refreshDisplaySources;
        }
      },
      mount() {
        if (shellStatusTimer !== null) {
          throw new Error("Shell controller is already mounted");
        }
        shellStatusTimer = setInterval(() => {
          $("sbSession").textContent =
            $("sessionId").value || "—";
          $("sbContinuity").textContent =
            $("mContinuity").textContent;
          const capture = $("captureMeta").textContent;
          $("sbCapture").textContent = capture.length > 96
            ? capture.slice(0, 93) + "…"
            : capture;
          $("sbRate").textContent =
            `${Number($("sampleRate").value || 0) / 1000} kHz × ` +
            `${$("channels").value} ch`;
        }, 1000);
        const requestedTab =
          new URLSearchParams(location.search).get("tab");
        if (requestedTab && document.querySelector(
          `.tabbar button[data-tab="${CSS.escape(requestedTab)}"]`)) {
          activateTab(requestedTab);
        }
        activateTab(
          requestedTab && document.querySelector(
            `.tabbar button[data-tab="${CSS.escape(requestedTab)}"]`)
            ? requestedTab
            : "spectrogram");
      },
      dispose() {
        if (shellStatusTimer !== null) {
          clearInterval(shellStatusTimer);
          shellStatusTimer = null;
        }
        document.querySelectorAll(".menu.open").forEach(
          (menu) => menu.classList.remove("open"));
        for (const dialog of document.querySelectorAll(
          "dialog[open]")) {
          dialog.close();
        }
      },
      activateTab
    });

    globalThis.PamguardModules = Object.freeze({
      ...(globalThis.PamguardModules || {}),
      shell: shellController
    });
