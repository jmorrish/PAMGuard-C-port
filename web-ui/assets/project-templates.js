(() => {
  "use strict";

  function createElement(tag, options = {}) {
    const element = document.createElement(tag);
    if (options.className) element.className = options.className;
    if (options.text !== undefined) {
      element.textContent = String(options.text);
    }
    if (options.attributes) {
      for (const [name, value] of Object.entries(options.attributes)) {
        if (value !== null && value !== undefined) {
          element.setAttribute(name, String(value));
        }
      }
    }
    return element;
  }

  function moduleCard(label, detail, attributes = {}) {
    const card = createElement("div", {
      className: "template-module-card",
      attributes
    });
    card.append(
      createElement("strong", { text: label }),
      createElement("small", { text: detail }));
    return card;
  }

  function branch(label, children) {
    const row = createElement("div", {
      className: "template-branch",
      attributes: { "data-template-branch": label }
    });
    row.append(
      createElement("span", {
        className: "template-branch-line",
        attributes: { "aria-hidden": "true" }
      }),
      ...children);
    return row;
  }

  function createClickMonitoringPreview(options = {}) {
    const acquisitionCount = Number(options.acquisitionCount || 0);
    const body = createElement("div", {
      className: "dialog-stack configuration-template-preview",
      attributes: {
        "data-configuration-template-preview": "pamguard.click-monitoring"
      }
    });
    body.append(createElement("p", {
      className: "dialog-intro",
      text: "Create the standard independent PAMGuard monitoring branches " +
        "as one atomic project change. This is a configuration template, " +
        "not a combined processing module."
    }));

    const diagram = createElement("div", {
      className: "template-branch-diagram",
      attributes: {
        role: "img",
        "aria-label": "Sound Acquisition branches independently to FFT and " +
          "Spectrogram, Click Detector and Click display, and Sound Output"
      }
    });
    diagram.append(moduleCard(
      "Sound Acquisition",
      acquisitionCount === 1
        ? "Reuse the existing Acquisition unit"
        : "Create a new Acquisition unit",
      { "data-template-module": "pamguard.acquisition" }));
    diagram.append(
      branch("spectrogram", [
        moduleCard(
          "FFT (Spectrogram) Engine",
          "Raw audio to FFT data",
          { "data-template-module": "pamguard.fft" }),
        moduleCard(
          "User Display / Spectrogram",
          "Provider instance bound to the FFT block",
          { "data-template-module": "pamguard.user-display" })
      ]),
      branch("clicks", [
        moduleCard(
          "Click Detector",
          "Raw audio to detected clicks",
          { "data-template-module": "pamguard.click-detector" }),
        moduleCard(
          "Click display",
          "Static display owned by Click Detector",
          { "data-template-display": "pamguard.click-display" })
      ]),
      branch("playback", [
        moduleCard(
          "Sound Output",
          "Listen to the same selected raw-audio block",
          { "data-template-module": "pamguard.sound-output" })
      ]));
    body.append(diagram);

    let canCreate = true;
    let status;
    if (acquisitionCount === 0) {
      status = "The new Sound Acquisition remains visibly incomplete until " +
        "you choose a real host device, file, or HTTP(S) stream.";
      body.append(createElement("div", {
        className: "dialog-callout template-warning",
        text: status,
        attributes: { "data-template-acquisition-state": "create-incomplete" }
      }));
    }
    else if (acquisitionCount === 1) {
      status = "The existing Sound Acquisition will be reused; its current " +
        "portable and host configuration is left unchanged.";
      body.append(createElement("div", {
        className: "dialog-callout",
        text: status,
        attributes: { "data-template-acquisition-state": "reuse" }
      }));
    }
    else {
      canCreate = false;
      status = "This project has more than one Sound Acquisition. Connect " +
        "branches manually so the intended source is unambiguous.";
      body.append(createElement("div", {
        className: "dialog-callout template-error",
        text: status,
        attributes: { "data-template-acquisition-state": "ambiguous" }
      }));
    }

    body.append(createElement("p", {
      className: "section-help",
      text: "All units stay independently configurable and reconnectable " +
        "after creation. Repeating the template adds another independent " +
        "FFT, User Display, Click Detector, and Sound Output branch."
    }));

    return Object.freeze({ body, canCreate, status });
  }

  globalThis.PamguardProjectTemplates = Object.freeze({
    clickMonitoringTemplateId: "pamguard.click-monitoring",
    createClickMonitoringPreview
  });
})();
