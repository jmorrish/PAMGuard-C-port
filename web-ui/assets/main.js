(() => {
  "use strict";

  let activeApplication = null;
  let mountConsumed = false;

  function createTestAdapter() {
    return Object.freeze({
      get graph() {
        return structuredClone(graphDraft);
      },
      get graphDirty() {
        return graphDirty;
      },
      get graphLayout() {
        return structuredClone(graphLayout);
      },
      get settingsSectionId() {
        return graphSettingsSectionId;
      },
      get workspaceDisplays() {
        return workspaceDisplays;
      },
      get workspaceBlocks() {
        return workspaceBlocks;
      },
      addGraphModuleAt: graphAddModuleAt,
      applyGraph: applyGraphEditor,
      validateGraph: validateGraphEditor,
      loadGraph: loadGraphEditor,
      openGraphSettings,
      refreshWorkspaceSources,
      setGraphViewport(viewport) {
        graphLayout.viewport = structuredClone(viewport);
      },
      saveGraphLayout
    });
  }

  function mountApplication() {
    if (activeApplication !== null || mountConsumed) {
      throw new Error(
        "PAMGuard web application can mount only once per page");
    }
    mountConsumed = true;

    $("apiBase").value = window.location.origin;
    const modules = globalThis.PamguardModules;
    const settingsController = modules.settings.create();
    modules.dataModel.configure({
      onApplied: () =>
        modules.displays.refreshSources(false),
      onAcquisitionListChanged: (acquisitions) =>
        modules.legacyCompatibility.updateAcquisitionList(
          acquisitions)
    });
    modules.legacyCompatibility.configure({
      refreshWorkspaceSources:
        modules.displays.refreshSources,
      activateTab: modules.shell.activateTab
    });
    modules.displays.configure({
      activateTab: modules.shell.activateTab,
      focusControlledUnit:
        modules.dataModel.focusControlledUnit
    });
    modules.shell.configure({
      loadDataModel: modules.dataModel.load,
      refreshDisplaySources:
        modules.displays.refreshSources
    });
    const controllers = [
      modules.legacyCompatibility,
      settingsController,
      modules.dataModel,
      modules.displays,
      modules.diagnostics,
      modules.shell
    ];
    const mountedControllers = [];
    try {
      for (const controller of controllers) {
        controller.mount();
        mountedControllers.push(controller);
      }
    }
    catch (error) {
      for (const controller of mountedControllers.reverse()) {
        Promise.resolve(controller.dispose()).catch(() => {});
      }
      disposeHttpClient();
      globalThis.PamguardPlatform.lifecycle.dispose();
      throw error;
    }

    let disposed = false;
    const application = Object.freeze({
      test: createTestAdapter(),
      async dispose() {
        if (disposed) return;
        disposed = true;
        for (const controller of mountedControllers.reverse()) {
          await controller.dispose();
        }
        disposeHttpClient();
        globalThis.PamguardPlatform.lifecycle.dispose();
        activeApplication = null;
      }
    });
    globalThis.PamguardPlatform.lifecycle.seal();
    activeApplication = application;
    return application;
  }

  globalThis.PamguardApplication = Object.freeze({
    mount: mountApplication,
    get active() {
      return activeApplication;
    }
  });
  activeApplication = mountApplication();
  globalThis.__pamguardTest = activeApplication.test;
})();
