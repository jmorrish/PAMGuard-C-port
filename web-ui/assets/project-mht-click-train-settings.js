(() => {
  "use strict";

  const UINT32_MAX = 0xffffffff;
  const INT32_MIN = -2147483648;
  const INT32_MAX = 2147483647;
  const JUMP_DIRECTIONS = Object.freeze([
    ["both", "Both directions"],
    ["positive", "Positive only"],
    ["negative", "Negative only"]
  ]);
  const capturedScriptSource =
    typeof document !== "undefined" && document.currentScript
      ? document.currentScript.src
      : "";

  const DEFAULT_SPECTRUM = Object.freeze([
    0.0207928796815748,
    0.0306907634391936,
    0.0542618013334441,
    0.0927715736291923,
    0.160880226335102,
    0.296684784810738,
    0.597646428735672,
    1.30240513409102,
    2.89418728104064,
    5.90182387336775,
    9.56798776063848,
    10.8497298549224,
    10.6268357383588,
    7.67719642764775,
    4.25588468454799,
    2.03543953486809,
    0.944338665649875,
    0.464770071613377,
    0.254353569529111,
    0.155756953724082,
    0.105040575926229,
    0.0764551025180798,
    0.0590657823674759,
    0.0478494061986734,
    0.040305203133092,
    0.0350966305067761,
    0.0314672978023124,
    0.0289713012337297,
    0.0273407573040125,
    0.0264177207215999
  ]);

  function createElement(tag, options = {}) {
    const element = document.createElement(tag);
    if (options.className) element.className = options.className;
    if (options.text !== undefined) {
      element.textContent = String(options.text);
    }
    if (options.type) element.type = options.type;
    if (options.attributes) {
      for (const [name, value] of Object.entries(options.attributes)) {
        if (value !== null && value !== undefined) {
          element.setAttribute(name, String(value));
        }
      }
    }
    return element;
  }

  function ensureStylesheet() {
    if (document.querySelector(
      "link[data-pamguard-project-mht-click-train-settings]")) {
      return;
    }
    const link = createElement("link", {
      attributes: {
        rel: "stylesheet",
        "data-pamguard-project-mht-click-train-settings": "true"
      }
    });
    link.href = capturedScriptSource
      ? new URL(
          "project-mht-click-train-settings.css",
          capturedScriptSource).href
      : "/assets/project-mht-click-train-settings.css";
    document.head.append(link);
  }

  function clone(value) {
    if (Array.isArray(value)) return value.map(clone);
    if (value && typeof value === "object") {
      return Object.fromEntries(
        Object.entries(value).map(
          ([name, child]) => [name, clone(child)]));
    }
    return value;
  }

  function defaultSettings() {
    return {
      algorithm: "mht",
      channelGroups: [1],
      dataSelector: {
        enabled: false,
        useEchoes: true,
        minimumAmplitudeDb: 0,
        includedClickTypes: []
      },
      kernel: {
        nHold: 20,
        nPruneback: 4,
        nPrunebackStart: 5,
        maxCoast: 3
      },
      chi2: {
        maximumIciSeconds: 0.4,
        coastPenalty: 10,
        newTrackPenalty: 50,
        newTrackClicks: 3,
        longTrackExponent: 0.1,
        lowIciExponent: 0.1,
        electricalNoiseFilter: {
          enabled: false,
          minimumChi2: 0.00001,
          dataUnits: 30
        },
        variables: {
          idi: {
            enabled: true,
            error: 0.2,
            minimumError: 0.0005,
            minimumIdiSeconds: 0.0005
          },
          amplitude: {
            enabled: true,
            error: 30,
            minimumError: 1,
            jumpEnabled: true,
            maximumJumpDb: 10
          },
          bearing: {
            enabled: true,
            errorRadians: 0.06981317007977318,
            minimumErrorRadians: 0.03490658503988659,
            jumpEnabled: false,
            maximumJumpRadians: 0.3490658503988659,
            jumpDirection: "positive"
          },
          correlation: {
            enabled: true,
            error: 1,
            minimumError: 0.01
          },
          timeDelay: {
            enabled: true,
            error: 0.000001,
            minimumError: 0.000000001
          },
          length: {
            enabled: true,
            error: 0.2,
            minimumError: 0.002
          },
          peakFrequency: {
            enabled: true,
            error: 30,
            minimumError: 1
          }
        }
      },
      classifier: {
        runClassifier: false,
        preClassifier: {
          chi2Threshold: 1500,
          minimumClicks: 5,
          minimumSelectedPercentage: 0,
          minimumTimeSeconds: 0,
          speciesFlag: 1
        },
        idi: {
          enabled: false,
          useMedianIdi: true,
          minimumMedianIdi: 0,
          maximumMedianIdi: 2,
          useMeanIdi: false,
          minimumMeanIdi: 0,
          maximumMeanIdi: 2,
          useStdIdi: false,
          minimumStdIdi: 0,
          maximumStdIdi: 100,
          speciesFlag: 1
        },
        bearing: {
          enabled: false,
          minimumBearingRadians: 1.4835298641951802,
          maximumBearingRadians: 1.6580627893946132,
          useMean: false,
          minimumMeanDerivative: -0.00008726646259971648,
          maximumMeanDerivative: 0.00008726646259971648,
          useMedian: true,
          minimumMedianDerivative: -0.00008726646259971648,
          maximumMedianDerivative: 0.00008726646259971648,
          useStd: true,
          minimumStdDerivative: 0,
          maximumStdDerivative: 0.026179938779914945,
          speciesFlag: -1
        },
        spectrumTemplate: {
          enabled: false,
          name: "Beaked Whale",
          sampleRateHz: 192000,
          spectrum: Array.from(DEFAULT_SPECTRUM),
          correlationThreshold: 0.5,
          speciesFlag: 1
        }
      },
      localisation: {
        enabled: false,
        minimumDataUnits: 20,
        minimumAngleRangeRadians: 0.5235987755982988
      }
    };
  }

  function plainObject(value, label) {
    if (!value || typeof value !== "object" ||
        Array.isArray(value)) {
      throw new Error(`${label} must be an object`);
    }
    return value;
  }

  function exactObject(value, fields, label) {
    plainObject(value, label);
    const actual = Object.keys(value);
    const missing = fields.filter(
      (fieldName) =>
        !Object.prototype.hasOwnProperty.call(value, fieldName));
    const extra = actual.filter(
      (fieldName) => !fields.includes(fieldName));
    if (missing.length || extra.length) {
      throw new Error(
        `${label} must contain exactly: ${fields.join(", ")}`);
    }
    return value;
  }

  function finiteNumber(value, label, options = {}) {
    const result = Number(value);
    if (!Number.isFinite(result) ||
        (options.integer && !Number.isInteger(result)) ||
        (options.min !== undefined && result < options.min) ||
        (options.exclusiveMin !== undefined &&
          result <= options.exclusiveMin) ||
        (options.max !== undefined && result > options.max)) {
      throw new Error(`${label} has an invalid value`);
    }
    return result;
  }

  function booleanValue(value, label) {
    if (typeof value !== "boolean") {
      throw new Error(`${label} must be true or false`);
    }
    return value;
  }

  function integer(value, label, min = INT32_MIN, max = INT32_MAX) {
    return finiteNumber(value, label, {
      integer: true,
      min,
      max
    });
  }

  function positive(value, label) {
    return finiteNumber(value, label, { exclusiveMin: 0 });
  }

  function nonNegative(value, label) {
    return finiteNumber(value, label, { min: 0 });
  }

  function canonicalChannelGroups(value) {
    if (!Array.isArray(value) || value.length > 32) {
      throw new Error(
        "Channel groups must be an array with at most 32 entries");
    }
    const usedChannels = new Set();
    return value.map((entry, index) => {
      const bitmap = integer(
        entry,
        `Channel group ${index}`,
        1,
        UINT32_MAX);
      for (let channel = 0; channel < 32; channel++) {
        if (Math.floor(bitmap / (2 ** channel)) % 2 !== 1) {
          continue;
        }
        if (usedChannels.has(channel)) {
          throw new Error("Channel groups cannot overlap");
        }
        usedChannels.add(channel);
      }
      return bitmap;
    });
  }

  function canonicalClickTypes(value) {
    if (!Array.isArray(value) || value.length > 256) {
      throw new Error(
        "Included click types must contain at most 256 entries");
    }
    const seen = new Set();
    return value.map((entry, index) => {
      const type = integer(
        entry,
        `Included click type ${index}`,
        0,
        255);
      if (seen.has(type)) {
        throw new Error("Included click types must not contain duplicates");
      }
      seen.add(type);
      return type;
    });
  }

  function canonicalCommonVariable(value, label) {
    exactObject(
      value,
      ["enabled", "error", "minimumError"],
      label);
    return {
      enabled: booleanValue(value.enabled, `${label} enabled`),
      error: positive(value.error, `${label} error`),
      minimumError: positive(
        value.minimumError,
        `${label} minimum error`)
    };
  }

  function canonicalSettings(value) {
    exactObject(
      value,
      [
        "algorithm",
        "channelGroups",
        "dataSelector",
        "kernel",
        "chi2",
        "classifier",
        "localisation"
      ],
      "Click Train Detector settings");
    if (value.algorithm !== "mht") {
      throw new Error("Click Train Detector algorithm must be mht");
    }

    const selector = exactObject(
      value.dataSelector,
      [
        "enabled",
        "useEchoes",
        "minimumAmplitudeDb",
        "includedClickTypes"
      ],
      "Detection Selector");
    const minimumAmplitudeDb = finiteNumber(
      selector.minimumAmplitudeDb,
      "Minimum amplitude");
    if (minimumAmplitudeDb !== 0) {
      throw new Error(
        "Minimum amplitude must remain zero until calibrated click dB " +
          "is available");
    }

    const kernel = exactObject(
      value.kernel,
      ["nHold", "nPruneback", "nPrunebackStart", "maxCoast"],
      "MHT Kernel settings");
    const chi2 = exactObject(
      value.chi2,
      [
        "maximumIciSeconds",
        "coastPenalty",
        "newTrackPenalty",
        "newTrackClicks",
        "longTrackExponent",
        "lowIciExponent",
        "electricalNoiseFilter",
        "variables"
      ],
      "MHT Chi-squared settings");
    const electrical = exactObject(
      chi2.electricalNoiseFilter,
      ["enabled", "minimumChi2", "dataUnits"],
      "Electrical Noise Filter");
    const variables = exactObject(
      chi2.variables,
      [
        "idi",
        "amplitude",
        "bearing",
        "correlation",
        "timeDelay",
        "length",
        "peakFrequency"
      ],
      "MHT Chi-squared variables");

    const idiVariable = exactObject(
      variables.idi,
      ["enabled", "error", "minimumError", "minimumIdiSeconds"],
      "IDI Chi-squared variable");
    const amplitudeVariable = exactObject(
      variables.amplitude,
      [
        "enabled",
        "error",
        "minimumError",
        "jumpEnabled",
        "maximumJumpDb"
      ],
      "Amplitude Chi-squared variable");
    const bearingVariable = exactObject(
      variables.bearing,
      [
        "enabled",
        "errorRadians",
        "minimumErrorRadians",
        "jumpEnabled",
        "maximumJumpRadians",
        "jumpDirection"
      ],
      "Bearing Chi-squared variable");
    if (!JUMP_DIRECTIONS.some(
      ([valueName]) =>
        valueName === bearingVariable.jumpDirection)) {
      throw new Error(
        "Bearing jump direction must be both, positive, or negative");
    }

    const classifier = exactObject(
      value.classifier,
      [
        "runClassifier",
        "preClassifier",
        "idi",
        "bearing",
        "spectrumTemplate"
      ],
      "Click Train classifier");
    const pre = exactObject(
      classifier.preClassifier,
      [
        "chi2Threshold",
        "minimumClicks",
        "minimumSelectedPercentage",
        "minimumTimeSeconds",
        "speciesFlag"
      ],
      "Pre Classifier");
    const minimumSelectedPercentage = nonNegative(
      pre.minimumSelectedPercentage,
      "Pre Classifier minimum selected percentage");
    if (minimumSelectedPercentage !== 0) {
      throw new Error(
        "Pre Classifier minimum selected percentage must remain zero");
    }

    const idiClassifier = exactObject(
      classifier.idi,
      [
        "enabled",
        "useMedianIdi",
        "minimumMedianIdi",
        "maximumMedianIdi",
        "useMeanIdi",
        "minimumMeanIdi",
        "maximumMeanIdi",
        "useStdIdi",
        "minimumStdIdi",
        "maximumStdIdi",
        "speciesFlag"
      ],
      "IDI Classifier");
    const canonicalIdiClassifier = {
      enabled: booleanValue(
        idiClassifier.enabled,
        "IDI Classifier enabled"),
      useMedianIdi: booleanValue(
        idiClassifier.useMedianIdi,
        "IDI median enabled"),
      minimumMedianIdi: nonNegative(
        idiClassifier.minimumMedianIdi,
        "IDI median minimum"),
      maximumMedianIdi: nonNegative(
        idiClassifier.maximumMedianIdi,
        "IDI median maximum"),
      useMeanIdi: booleanValue(
        idiClassifier.useMeanIdi,
        "IDI mean enabled"),
      minimumMeanIdi: nonNegative(
        idiClassifier.minimumMeanIdi,
        "IDI mean minimum"),
      maximumMeanIdi: nonNegative(
        idiClassifier.maximumMeanIdi,
        "IDI mean maximum"),
      useStdIdi: booleanValue(
        idiClassifier.useStdIdi,
        "IDI standard deviation enabled"),
      minimumStdIdi: nonNegative(
        idiClassifier.minimumStdIdi,
        "IDI standard deviation minimum"),
      maximumStdIdi: nonNegative(
        idiClassifier.maximumStdIdi,
        "IDI standard deviation maximum"),
      speciesFlag: integer(
        idiClassifier.speciesFlag,
        "IDI Classifier species ID")
    };
    for (const [minimum, maximum, label] of [
      ["minimumMedianIdi", "maximumMedianIdi", "median IDI"],
      ["minimumMeanIdi", "maximumMeanIdi", "mean IDI"],
      ["minimumStdIdi", "maximumStdIdi", "standard deviation IDI"]
    ]) {
      if (canonicalIdiClassifier[minimum] >
          canonicalIdiClassifier[maximum]) {
        throw new Error(`${label} limits must be ordered`);
      }
    }

    const bearingClassifier = exactObject(
      classifier.bearing,
      [
        "enabled",
        "minimumBearingRadians",
        "maximumBearingRadians",
        "useMean",
        "minimumMeanDerivative",
        "maximumMeanDerivative",
        "useMedian",
        "minimumMedianDerivative",
        "maximumMedianDerivative",
        "useStd",
        "minimumStdDerivative",
        "maximumStdDerivative",
        "speciesFlag"
      ],
      "Bearing Classifier");
    const canonicalBearingClassifier = {
      enabled: booleanValue(
        bearingClassifier.enabled,
        "Bearing Classifier enabled"),
      minimumBearingRadians: finiteNumber(
        bearingClassifier.minimumBearingRadians,
        "Bearing minimum"),
      maximumBearingRadians: finiteNumber(
        bearingClassifier.maximumBearingRadians,
        "Bearing maximum"),
      useMean: booleanValue(
        bearingClassifier.useMean,
        "Mean bearing derivative enabled"),
      minimumMeanDerivative: finiteNumber(
        bearingClassifier.minimumMeanDerivative,
        "Mean bearing derivative minimum"),
      maximumMeanDerivative: finiteNumber(
        bearingClassifier.maximumMeanDerivative,
        "Mean bearing derivative maximum"),
      useMedian: booleanValue(
        bearingClassifier.useMedian,
        "Median bearing derivative enabled"),
      minimumMedianDerivative: finiteNumber(
        bearingClassifier.minimumMedianDerivative,
        "Median bearing derivative minimum"),
      maximumMedianDerivative: finiteNumber(
        bearingClassifier.maximumMedianDerivative,
        "Median bearing derivative maximum"),
      useStd: booleanValue(
        bearingClassifier.useStd,
        "Bearing derivative standard deviation enabled"),
      minimumStdDerivative: finiteNumber(
        bearingClassifier.minimumStdDerivative,
        "Bearing derivative standard deviation minimum"),
      maximumStdDerivative: finiteNumber(
        bearingClassifier.maximumStdDerivative,
        "Bearing derivative standard deviation maximum"),
      speciesFlag: integer(
        bearingClassifier.speciesFlag,
        "Bearing Classifier species ID")
    };
    for (const [minimum, maximum, label] of [
      [
        "minimumBearingRadians",
        "maximumBearingRadians",
        "bearing"
      ],
      [
        "minimumMeanDerivative",
        "maximumMeanDerivative",
        "mean bearing derivative"
      ],
      [
        "minimumMedianDerivative",
        "maximumMedianDerivative",
        "median bearing derivative"
      ],
      [
        "minimumStdDerivative",
        "maximumStdDerivative",
        "bearing derivative standard deviation"
      ]
    ]) {
      if (canonicalBearingClassifier[minimum] >
          canonicalBearingClassifier[maximum]) {
        throw new Error(`${label} limits must be ordered`);
      }
    }

    const spectrum = exactObject(
      classifier.spectrumTemplate,
      [
        "enabled",
        "name",
        "sampleRateHz",
        "spectrum",
        "correlationThreshold",
        "speciesFlag"
      ],
      "Spectrum Template Classifier");
    if (typeof spectrum.name !== "string" ||
        spectrum.name.length === 0) {
      throw new Error("Spectrum template name must not be empty");
    }
    if (!Array.isArray(spectrum.spectrum) ||
        spectrum.spectrum.length < 2) {
      throw new Error(
        "Spectrum template must contain at least 2 bins");
    }
    const correlationThreshold = finiteNumber(
      spectrum.correlationThreshold,
      "Spectrum correlation threshold",
      { min: -1, max: 1 });

    const localisation = exactObject(
      value.localisation,
      ["enabled", "minimumDataUnits", "minimumAngleRangeRadians"],
      "Click Train localisation");
    const localisationEnabled = booleanValue(
      localisation.enabled,
      "Click Train localisation enabled");
    if (localisationEnabled) {
      throw new Error(
        "Target-motion click-train localisation is not implemented");
    }

    return {
      algorithm: "mht",
      channelGroups: canonicalChannelGroups(value.channelGroups),
      dataSelector: {
        enabled: booleanValue(
          selector.enabled,
          "Detection Selector enabled"),
        useEchoes: booleanValue(
          selector.useEchoes,
          "Detection Selector use echoes"),
        minimumAmplitudeDb,
        includedClickTypes: canonicalClickTypes(
          selector.includedClickTypes)
      },
      kernel: {
        nHold: integer(
          kernel.nHold,
          "Maximum number of trains",
          1,
          UINT32_MAX),
        nPruneback: integer(
          kernel.nPruneback,
          "Prune-back",
          0,
          UINT32_MAX),
        nPrunebackStart: integer(
          kernel.nPrunebackStart,
          "Prune-start",
          0,
          UINT32_MAX),
        maxCoast: integer(
          kernel.maxCoast,
          "Maximum number of coasts",
          0,
          INT32_MAX)
      },
      chi2: {
        maximumIciSeconds: positive(
          chi2.maximumIciSeconds,
          "Maximum ICI"),
        coastPenalty: nonNegative(
          chi2.coastPenalty,
          "Coast penalty"),
        newTrackPenalty: nonNegative(
          chi2.newTrackPenalty,
          "New track penalty"),
        newTrackClicks: integer(
          chi2.newTrackClicks,
          "Number of new track clicks",
          0,
          UINT32_MAX),
        longTrackExponent: nonNegative(
          chi2.longTrackExponent,
          "Long track exponent"),
        lowIciExponent: nonNegative(
          chi2.lowIciExponent,
          "Low ICI exponent"),
        electricalNoiseFilter: {
          enabled: booleanValue(
            electrical.enabled,
            "Electrical Noise Filter enabled"),
          minimumChi2: nonNegative(
            electrical.minimumChi2,
            "Electrical Noise Filter minimum chi-squared"),
          dataUnits: integer(
            electrical.dataUnits,
            "Electrical Noise Filter data units",
            1,
            UINT32_MAX)
        },
        variables: {
          idi: {
            enabled: booleanValue(
              idiVariable.enabled,
              "IDI variable enabled"),
            error: positive(idiVariable.error, "IDI error"),
            minimumError: positive(
              idiVariable.minimumError,
              "IDI minimum error"),
            minimumIdiSeconds: nonNegative(
              idiVariable.minimumIdiSeconds,
              "Minimum IDI")
          },
          amplitude: {
            enabled: booleanValue(
              amplitudeVariable.enabled,
              "Amplitude variable enabled"),
            error: positive(
              amplitudeVariable.error,
              "Amplitude error"),
            minimumError: positive(
              amplitudeVariable.minimumError,
              "Amplitude minimum error"),
            jumpEnabled: booleanValue(
              amplitudeVariable.jumpEnabled,
              "Amplitude jump enabled"),
            maximumJumpDb: nonNegative(
              amplitudeVariable.maximumJumpDb,
              "Maximum amplitude jump")
          },
          bearing: {
            enabled: booleanValue(
              bearingVariable.enabled,
              "Bearing variable enabled"),
            errorRadians: positive(
              bearingVariable.errorRadians,
              "Bearing error"),
            minimumErrorRadians: positive(
              bearingVariable.minimumErrorRadians,
              "Bearing minimum error"),
            jumpEnabled: booleanValue(
              bearingVariable.jumpEnabled,
              "Bearing jump enabled"),
            maximumJumpRadians: nonNegative(
              bearingVariable.maximumJumpRadians,
              "Maximum bearing jump"),
            jumpDirection: bearingVariable.jumpDirection
          },
          correlation: canonicalCommonVariable(
            variables.correlation,
            "Correlation variable"),
          timeDelay: canonicalCommonVariable(
            variables.timeDelay,
            "Time Delay variable"),
          length: canonicalCommonVariable(
            variables.length,
            "Length variable"),
          peakFrequency: canonicalCommonVariable(
            variables.peakFrequency,
            "Peak Frequency variable")
        }
      },
      classifier: {
        runClassifier: booleanValue(
          classifier.runClassifier,
          "Click Train classification enabled"),
        preClassifier: {
          chi2Threshold: nonNegative(
            pre.chi2Threshold,
            "Pre Classifier chi-squared threshold"),
          minimumClicks: integer(
            pre.minimumClicks,
            "Pre Classifier minimum clicks",
            0,
            UINT32_MAX),
          minimumSelectedPercentage,
          minimumTimeSeconds: nonNegative(
            pre.minimumTimeSeconds,
            "Pre Classifier minimum time"),
          speciesFlag: integer(
            pre.speciesFlag,
            "Pre Classifier species ID")
        },
        idi: canonicalIdiClassifier,
        bearing: canonicalBearingClassifier,
        spectrumTemplate: {
          enabled: booleanValue(
            spectrum.enabled,
            "Spectrum Template Classifier enabled"),
          name: spectrum.name,
          sampleRateHz: positive(
            spectrum.sampleRateHz,
            "Spectrum template sample rate"),
          spectrum: spectrum.spectrum.map((entry, index) =>
            finiteNumber(entry, `Spectrum bin ${index}`)),
          correlationThreshold,
          speciesFlag: integer(
            spectrum.speciesFlag,
            "Spectrum Template Classifier species ID")
        }
      },
      localisation: {
        enabled: false,
        minimumDataUnits: integer(
          localisation.minimumDataUnits,
          "Localisation minimum data units",
          0,
          UINT32_MAX),
        minimumAngleRangeRadians: nonNegative(
          localisation.minimumAngleRangeRadians,
          "Localisation minimum angle range")
      }
    };
  }

  function pointerControl(tag, pointer, options = {}) {
    return createElement(tag, {
      type: options.type,
      className: options.className,
      attributes: {
        "data-setting-pointer": pointer,
        ...options.attributes
      }
    });
  }

  function numberControl(value, pointer, attributes = {}) {
    const control = pointerControl("input", pointer, {
      type: "number",
      attributes: {
        step: "any",
        ...attributes
      }
    });
    control.value = String(value);
    return control;
  }

  function textControl(value, pointer, attributes = {}) {
    const control = pointerControl("input", pointer, {
      type: "text",
      attributes
    });
    control.value = String(value);
    return control;
  }

  function checkboxControl(value, pointer) {
    const control = pointerControl("input", pointer, {
      type: "checkbox"
    });
    control.checked = value;
    return control;
  }

  function selectControl(value, pointer, choices) {
    const control = pointerControl("select", pointer);
    for (const [stored, label] of choices) {
      const option = createElement("option", {
        text: label,
        attributes: { value: stored }
      });
      option.selected = stored === String(value);
      control.append(option);
    }
    control.value = String(value);
    return control;
  }

  function degreeControl(radians, pointer, attributes = {}) {
    const displayed = String(radians * 180 / Math.PI);
    const control = numberControl(
      displayed,
      pointer,
      {
        ...attributes,
        "data-stored-unit": "radians"
      });
    control.__pamguardOriginalRadians = radians;
    control.__pamguardOriginalDegrees = displayed;
    return control;
  }

  function radiansFromControl(control, label, options = {}) {
    if (control.value === control.__pamguardOriginalDegrees) {
      const exact = control.__pamguardOriginalRadians;
      if ((options.min !== undefined && exact < options.min) ||
          (options.exclusiveMin !== undefined &&
            exact <= options.exclusiveMin)) {
        throw new Error(`${label} has an invalid value`);
      }
      return exact;
    }
    const degrees = finiteNumber(control.value, label, options);
    return degrees * Math.PI / 180;
  }

  function field(label, control, options = {}) {
    const row = createElement("label", {
      className: "mht-click-train-settings-field"
    });
    row.append(
      createElement("span", {
        className: "mht-click-train-settings-label",
        text: label
      }),
      control);
    if (options.unit) {
      row.append(createElement("span", {
        className: "mht-click-train-settings-unit",
        text: options.unit
      }));
    }
    if (options.help) {
      row.append(createElement("small", {
        className: "mht-click-train-settings-help",
        text: options.help
      }));
    }
    return row;
  }

  function checkLabel(label, control, help = "") {
    const row = createElement("label", {
      className: "mht-click-train-settings-check"
    });
    row.append(control, createElement("span", { text: label }));
    if (help) {
      row.append(createElement("small", {
        className: "mht-click-train-settings-help",
        text: help
      }));
    }
    return row;
  }

  function actionButton(label, action) {
    return createElement("button", {
      type: "button",
      className: "secondary",
      text: label,
      attributes: {
        "data-mht-click-train-action": action
      }
    });
  }

  function titledSection(title, options = {}) {
    const section = createElement(options.tag || "fieldset", {
      className:
        "mht-click-train-settings-section" +
        (options.className ? ` ${options.className}` : ""),
      attributes: options.attributes
    });
    if ((options.tag || "fieldset") === "fieldset") {
      section.append(createElement("legend", { text: title }));
    }
    else {
      section.append(createElement("h4", { text: title }));
    }
    if (options.help) {
      section.append(createElement("p", {
        className: "section-help",
        text: options.help
      }));
    }
    return section;
  }

  function bindVisibility(toggle, target, options = {}) {
    const update = () => {
      const visible = options.invert
        ? !toggle.checked
        : toggle.checked;
      target.hidden = !visible;
      target.setAttribute(
        "data-state",
        visible ? "enabled" : "disabled");
    };
    toggle.addEventListener("change", update);
    update();
    return update;
  }

  function statRange(
      root,
      label,
      enabled,
      minimum,
      maximum,
      pointers,
      unit) {
    const row = createElement("div", {
      className: "mht-click-train-settings-range"
    });
    const toggle = checkboxControl(enabled, pointers.enabled);
    const minimumControl = numberControl(
      minimum,
      pointers.minimum,
      { min: 0 });
    const maximumControl = numberControl(
      maximum,
      pointers.maximum,
      { min: 0 });
    const controls = createElement("div", {
      className: "mht-click-train-settings-range-controls"
    });
    controls.append(
      field("Minimum", minimumControl, { unit }),
      field("Maximum", maximumControl, { unit }));
    row.append(checkLabel(label, toggle), controls);
    root.append(row);
    bindVisibility(toggle, controls);
    return { toggle, minimumControl, maximumControl };
  }

  function angularRange(
      root,
      label,
      enabled,
      minimum,
      maximum,
      pointers,
      unit) {
    const row = createElement("div", {
      className: "mht-click-train-settings-range"
    });
    const toggle = checkboxControl(enabled, pointers.enabled);
    const minimumControl = degreeControl(
      minimum,
      pointers.minimum);
    const maximumControl = degreeControl(
      maximum,
      pointers.maximum);
    const controls = createElement("div", {
      className: "mht-click-train-settings-range-controls"
    });
    controls.append(
      field("Minimum", minimumControl, { unit }),
      field("Maximum", maximumControl, { unit }));
    row.append(checkLabel(label, toggle), controls);
    root.append(row);
    bindVisibility(toggle, controls);
    return { toggle, minimumControl, maximumControl };
  }

  function mountEditor(options) {
    ensureStylesheet();
    const {
      container,
      settings = defaultSettings(),
      sourceSelect = null,
      getAvailableChannelGroups = () => [],
      getAvailableChannelBitmap = () => 0,
      reportError = () => {}
    } = options;
    const draft = canonicalSettings(settings);
    const selectedGroups = new Set(draft.channelGroups);
    let groupControls = [];
    let clickTypeControls = [];
    let spectrumControls = [];

    const root = createElement("div", {
      className: "mht-click-train-settings-editor",
      attributes: {
        "data-pamguard-mht-click-train-settings-editor": "true"
      }
    });
    const tabs = createElement("div", {
      className: "mht-click-train-settings-tabs",
      attributes: {
        role: "tablist",
        "aria-label": "Click Train Settings"
      }
    });
    const panels = new Map();
    const tabControls = new Map();
    for (const [id, label] of [
      ["detector", "Detector"],
      ["pre-classifier", "Pre Classifier"],
      ["species-classifiers", "Species Classifiers"]
    ]) {
      const tabId = `mht-click-train-tab-${id}`;
      const panelId = `mht-click-train-panel-${id}`;
      const tab = createElement("button", {
        type: "button",
        text: label,
        attributes: {
          id: tabId,
          role: "tab",
          "aria-controls": panelId,
          "data-mht-click-train-tab": id
        }
      });
      const panel = createElement("section", {
        className: "mht-click-train-settings-panel",
        attributes: {
          id: panelId,
          role: "tabpanel",
          "aria-labelledby": tabId,
          "data-mht-click-train-panel": id
        }
      });
      tabs.append(tab);
      tabControls.set(id, tab);
      panels.set(id, panel);
      root.append(panel);
    }
    root.prepend?.(tabs);
    if (!root.prepend) {
      root.children.unshift(tabs);
      tabs.parentNode = root;
    }
    const activateTab = (activeId) => {
      for (const [id, tab] of tabControls) {
        tab.setAttribute(
          "aria-selected",
          id === activeId ? "true" : "false");
        panels.get(id).hidden = id !== activeId;
      }
    };
    tabControls.forEach((tab, id) => {
      tab.addEventListener("click", () => activateTab(id));
    });
    activateTab("detector");

    const detectorPanel = panels.get("detector");
    const sourceSection = titledSection("Click Data Source", {
      help:
        "The graph binding above selects the detected-click data block. " +
        "Choose which source channel groups the MHT detector will process."
    });
    const sourceSummary = createElement("p", {
      className: "mht-click-train-settings-summary",
      attributes: {
        "data-mht-click-train-source-summary": ""
      }
    });
    const groupHost = createElement("div", {
      className: "mht-click-train-settings-group-host"
    });
    sourceSection.append(sourceSummary, groupHost);
    detectorPanel.append(sourceSection);

    const normalizeAvailableGroups = () => {
      let groups = getAvailableChannelGroups();
      if (!Array.isArray(groups)) groups = [];
      groups = groups.filter((entry) =>
        Number.isInteger(Number(entry)) &&
        Number(entry) > 0 &&
        Number(entry) <= UINT32_MAX).map(Number);
      if (!groups.length) {
        const bitmap = Number(getAvailableChannelBitmap());
        if (Number.isInteger(bitmap) &&
            bitmap > 0 &&
            bitmap <= UINT32_MAX) {
          for (let channel = 0; channel < 32; channel++) {
            if (Math.floor(bitmap / (2 ** channel)) % 2 === 1) {
              groups.push(2 ** channel);
            }
          }
        }
      }
      return Array.from(new Set(groups));
    };

    const syncSelectedGroups = () => {
      for (const { bitmap, control } of groupControls) {
        if (control.checked) selectedGroups.add(bitmap);
        else selectedGroups.delete(bitmap);
      }
    };

    const renderGroups = () => {
      syncSelectedGroups();
      groupHost.replaceChildren();
      groupControls = [];
      const available = normalizeAvailableGroups();
      const choices = [
        ...Array.from(selectedGroups),
        ...available.filter((bitmap) => !selectedGroups.has(bitmap))
      ];
      if (!choices.length) {
        sourceSummary.textContent =
          "No channel groups are advertised by the selected click source.";
        groupHost.append(createElement("p", {
          className: "section-help",
          text:
            "The portable contract allows no selected groups, but the " +
            "project will not be runnable until a group is selected."
        }));
        return;
      }
      sourceSummary.textContent =
        `${available.length || choices.length} source channel ` +
        `group${(available.length || choices.length) === 1 ? "" : "s"} ` +
        "available.";
      for (const bitmap of choices) {
        const channels = [];
        for (let channel = 0; channel < 32; channel++) {
          if (Math.floor(bitmap / (2 ** channel)) % 2 === 1) {
            channels.push(channel);
          }
        }
        const control = checkboxControl(
          selectedGroups.has(bitmap),
          `/channelGroups/${bitmap}`);
        const unavailable = !available.includes(bitmap);
        const label =
          `${channels.length === 1 ? "Channel" : "Channels"} ` +
          `[${channels.join(", ")}] · bitmap ${bitmap}` +
          (unavailable ? " (saved; not currently advertised)" : "");
        groupHost.append(checkLabel(label, control));
        groupControls.push({ bitmap, control });
      }
    };
    const sourceChanged = () => renderGroups();
    sourceSelect?.addEventListener("change", sourceChanged);
    renderGroups();

    const selectorSection = titledSection("Detection Selector");
    const selectorEnabled = checkboxControl(
      draft.dataSelector.enabled,
      "/dataSelector/enabled");
    const selectorDetails = createElement("div", {
      className: "mht-click-train-settings-conditional",
      attributes: {
        "data-mht-click-train-conditional": "data-selector"
      }
    });
    const useEchoes = checkboxControl(
      draft.dataSelector.useEchoes,
      "/dataSelector/useEchoes");
    const minimumAmplitude = numberControl(
      draft.dataSelector.minimumAmplitudeDb,
      "/dataSelector/minimumAmplitudeDb");
    minimumAmplitude.disabled = true;
    const clickTypesHost = createElement("div", {
      className: "mht-click-train-settings-list",
      attributes: {
        "data-mht-click-train-list": "included-click-types"
      }
    });
    const addClickType = actionButton("Add click type", "add-click-type");
    const clickTypes = Array.from(
      draft.dataSelector.includedClickTypes);
    const syncClickTypes = () => {
      const result = clickTypeControls.map((control, index) =>
        integer(
          control.value,
          `Included click type ${index}`,
          0,
          255));
      canonicalClickTypes(result);
      clickTypes.splice(0, clickTypes.length, ...result);
    };
    const renderClickTypes = () => {
      clickTypesHost.replaceChildren();
      clickTypeControls = [];
      if (!clickTypes.length) {
        clickTypesHost.append(createElement("p", {
          className: "section-help",
          text: "No type filter: every click type is included."
        }));
      }
      clickTypes.forEach((type, index) => {
        const row = createElement("div", {
          className: "mht-click-train-settings-list-row"
        });
        const control = numberControl(
          type,
          `/dataSelector/includedClickTypes/${index}`,
          { min: 0, max: 255, step: 1 });
        const remove = actionButton(
          "Remove",
          `remove-click-type-${index}`);
        remove.addEventListener("click", () => {
          try {
            syncClickTypes();
            clickTypes.splice(index, 1);
            renderClickTypes();
          }
          catch (error) {
            reportError(error);
          }
        });
        row.append(
          createElement("span", { text: `Click type ${index + 1}` }),
          control,
          remove);
        clickTypesHost.append(row);
        clickTypeControls.push(control);
      });
      addClickType.disabled = clickTypes.length >= 256;
    };
    addClickType.addEventListener("click", () => {
      try {
        syncClickTypes();
        const used = new Set(clickTypes);
        let candidate = 0;
        while (used.has(candidate) && candidate < 255) candidate++;
        if (used.has(candidate)) return;
        clickTypes.push(candidate);
        renderClickTypes();
      }
      catch (error) {
        reportError(error);
      }
    });
    renderClickTypes();
    selectorDetails.append(
      checkLabel(
        "Use echoes",
        useEchoes,
        "Matches PAMGuard ClickAlarmParameters.useEchoes."),
      field("Minimum amplitude", minimumAmplitude, {
        unit: "dB",
        help:
          "Held at Java's zero default until calibrated click amplitude " +
          "is available."
      }),
      createElement("h5", { text: "Included click types" }),
      clickTypesHost,
      addClickType);
    selectorSection.append(
      checkLabel(
        "Detection Selector",
        selectorEnabled,
        "Apply the click-source selector before MHT formation."),
      selectorDetails);
    bindVisibility(selectorEnabled, selectorDetails);
    detectorPanel.append(selectorSection);

    const algorithmSection = titledSection(
      "Click Train Detector Algorithm");
    const algorithm = selectControl(
      "mht",
      "/algorithm",
      [["mht", "MHT (Multiple Hypothesis Tracking)"]]);
    algorithm.disabled = true;
    algorithmSection.append(field("Algorithm", algorithm));
    detectorPanel.append(algorithmSection);

    const kernelSection = titledSection("MHT Kernel Settings");
    const nPruneback = numberControl(
      draft.kernel.nPruneback,
      "/kernel/nPruneback",
      { min: 0, max: UINT32_MAX, step: 1 });
    const nPrunebackStart = numberControl(
      draft.kernel.nPrunebackStart,
      "/kernel/nPrunebackStart",
      { min: 0, max: UINT32_MAX, step: 1 });
    const maxCoast = numberControl(
      draft.kernel.maxCoast,
      "/kernel/maxCoast",
      { min: 0, max: INT32_MAX, step: 1 });
    const nHold = numberControl(
      draft.kernel.nHold,
      "/kernel/nHold",
      { min: 1, max: UINT32_MAX, step: 1 });
    kernelSection.append(
      field("Prune-back", nPruneback, {
        help: "Detections retained before bad hypotheses are pruned."
      }),
      field("Prune-start", nPrunebackStart, {
        help: "Minimum detections before pruning begins."
      }),
      field("Max no. coasts", maxCoast, {
        help: "Missing detections allowed before a track closes."
      }),
      field("Max no. trains", nHold, {
        help: "Maximum hypotheses retained by the MHT kernel."
      }));
    detectorPanel.append(kernelSection);

    const chi2Section = titledSection("χ² Calculation Settings");
    const maximumIci = numberControl(
      draft.chi2.maximumIciSeconds,
      "/chi2/maximumIciSeconds",
      { min: Number.MIN_VALUE });
    chi2Section.append(
      field("Max. ICI", maximumIci, { unit: "s" }));

    const variableHost = createElement("div", {
      className: "mht-click-train-settings-variable-grid"
    });
    const variableViews = {};
    const mountCommonVariable = (
        key,
        label,
        value,
        options = {}) => {
      const card = titledSection(label, {
        className: "mht-click-train-settings-variable"
      });
      const enabled = checkboxControl(
        value.enabled,
        `/chi2/variables/${key}/enabled`);
      const details = createElement("div", {
        className: "mht-click-train-settings-conditional",
        attributes: {
          "data-mht-click-train-conditional": `chi2-${key}`
        }
      });
      const error = options.angular
        ? degreeControl(
            value[options.errorKey],
            `/chi2/variables/${key}/${options.errorKey}`)
        : numberControl(
            value.error,
            `/chi2/variables/${key}/error`,
            { min: Number.MIN_VALUE });
      const minimumError = options.angular
        ? degreeControl(
            value[options.minimumErrorKey],
            `/chi2/variables/${key}/${options.minimumErrorKey}`)
        : numberControl(
            value.minimumError,
            `/chi2/variables/${key}/minimumError`,
            { min: Number.MIN_VALUE });
      details.append(
        field("Error", error, { unit: options.unit || "" }),
        field("Minimum error", minimumError, {
          unit: options.unit || ""
        }));
      card.append(checkLabel(`Use ${label}`, enabled), details);
      bindVisibility(enabled, details);
      variableHost.append(card);
      variableViews[key] = {
        enabled,
        details,
        error,
        minimumError
      };
      return { card, details };
    };

    const idiVariableView = mountCommonVariable(
      "idi",
      "IDI",
      draft.chi2.variables.idi);
    const minimumIdi = numberControl(
      draft.chi2.variables.idi.minimumIdiSeconds,
      "/chi2/variables/idi/minimumIdiSeconds",
      { min: 0 });
    idiVariableView.details.append(
      field("Minimum IDI", minimumIdi, { unit: "s" }));
    variableViews.idi.minimumIdi = minimumIdi;

    const amplitudeVariableView = mountCommonVariable(
      "amplitude",
      "Amplitude",
      draft.chi2.variables.amplitude,
      { unit: "dB" });
    const amplitudeJump = checkboxControl(
      draft.chi2.variables.amplitude.jumpEnabled,
      "/chi2/variables/amplitude/jumpEnabled");
    const amplitudeJumpDetails = createElement("div", {
      className: "mht-click-train-settings-conditional",
      attributes: {
        "data-mht-click-train-conditional": "amplitude-jump"
      }
    });
    const maximumAmplitudeJump = numberControl(
      draft.chi2.variables.amplitude.maximumJumpDb,
      "/chi2/variables/amplitude/maximumJumpDb",
      { min: 0 });
    amplitudeJumpDetails.append(
      field("Maximum jump", maximumAmplitudeJump, { unit: "dB" }));
    amplitudeVariableView.details.append(
      checkLabel("Limit amplitude jumps", amplitudeJump),
      amplitudeJumpDetails);
    bindVisibility(amplitudeJump, amplitudeJumpDetails);
    Object.assign(variableViews.amplitude, {
      jumpEnabled: amplitudeJump,
      maximumJump: maximumAmplitudeJump
    });

    const bearingVariableView = mountCommonVariable(
      "bearing",
      "Bearing",
      draft.chi2.variables.bearing,
      {
        angular: true,
        errorKey: "errorRadians",
        minimumErrorKey: "minimumErrorRadians",
        unit: "°"
      });
    const bearingJump = checkboxControl(
      draft.chi2.variables.bearing.jumpEnabled,
      "/chi2/variables/bearing/jumpEnabled");
    const bearingJumpDetails = createElement("div", {
      className: "mht-click-train-settings-conditional",
      attributes: {
        "data-mht-click-train-conditional": "bearing-jump"
      }
    });
    const maximumBearingJump = degreeControl(
      draft.chi2.variables.bearing.maximumJumpRadians,
      "/chi2/variables/bearing/maximumJumpRadians",
      { min: 0 });
    const bearingJumpDirection = selectControl(
      draft.chi2.variables.bearing.jumpDirection,
      "/chi2/variables/bearing/jumpDirection",
      JUMP_DIRECTIONS);
    bearingJumpDetails.append(
      field("Maximum jump", maximumBearingJump, { unit: "°" }),
      field("Jump direction", bearingJumpDirection));
    bearingVariableView.details.append(
      checkLabel("Limit bearing jumps", bearingJump),
      bearingJumpDetails);
    bindVisibility(bearingJump, bearingJumpDetails);
    Object.assign(variableViews.bearing, {
      jumpEnabled: bearingJump,
      maximumJump: maximumBearingJump,
      jumpDirection: bearingJumpDirection
    });

    for (const [key, label, unit] of [
      ["correlation", "Correlation", ""],
      ["timeDelay", "Time Delay", "s"],
      ["length", "Length", "s"],
      ["peakFrequency", "Peak Frequency", "Hz"]
    ]) {
      mountCommonVariable(
        key,
        label,
        draft.chi2.variables[key],
        { unit });
    }
    chi2Section.append(variableHost);

    const advancedChi2 = titledSection("Advanced χ² Settings", {
      className: "mht-click-train-settings-subsection"
    });
    const lowIciExponent = numberControl(
      draft.chi2.lowIciExponent,
      "/chi2/lowIciExponent",
      { min: 0 });
    const longTrackExponent = numberControl(
      draft.chi2.longTrackExponent,
      "/chi2/longTrackExponent",
      { min: 0 });
    const coastPenalty = numberControl(
      draft.chi2.coastPenalty,
      "/chi2/coastPenalty",
      { min: 0 });
    const newTrackPenalty = numberControl(
      draft.chi2.newTrackPenalty,
      "/chi2/newTrackPenalty",
      { min: 0 });
    const newTrackClicks = numberControl(
      draft.chi2.newTrackClicks,
      "/chi2/newTrackClicks",
      { min: 0, max: UINT32_MAX, step: 1 });
    advancedChi2.append(
      field("Low ICI Bonus", lowIciExponent),
      field("Long Track Bonus", longTrackExponent),
      field("Coast Penalty", coastPenalty),
      field("New Track Penalty", newTrackPenalty),
      field("No. New Track Clicks", newTrackClicks));
    chi2Section.append(advancedChi2);

    const electricalSection = titledSection(
      "χ² Electrical Noise Filter",
      { className: "mht-click-train-settings-subsection" });
    const electricalEnabled = checkboxControl(
      draft.chi2.electricalNoiseFilter.enabled,
      "/chi2/electricalNoiseFilter/enabled");
    const electricalDetails = createElement("div", {
      className: "mht-click-train-settings-conditional",
      attributes: {
        "data-mht-click-train-conditional": "electrical-noise"
      }
    });
    const electricalDataUnits = numberControl(
      draft.chi2.electricalNoiseFilter.dataUnits,
      "/chi2/electricalNoiseFilter/dataUnits",
      { min: 1, max: UINT32_MAX, step: 1 });
    const electricalMinimumChi2 = numberControl(
      draft.chi2.electricalNoiseFilter.minimumChi2,
      "/chi2/electricalNoiseFilter/minimumChi2",
      { min: 0 });
    electricalDetails.append(
      field("Minimum no. detections", electricalDataUnits),
      field("Minimum χ² for track", electricalMinimumChi2));
    electricalSection.append(
      checkLabel(
        "Run electrical noise filter",
        electricalEnabled,
        "Reject near-constant tracks characteristic of electrical noise."),
      electricalDetails);
    bindVisibility(electricalEnabled, electricalDetails);
    chi2Section.append(electricalSection);
    detectorPanel.append(chi2Section);

    const localisationSection = titledSection(
      "Click Train Localisation",
      {
        help:
          "PAMGuard CTLocParams are retained in the project. Target-motion " +
          "click-train localisation is not yet available in this runtime."
      });
    const localisationEnabled = checkboxControl(
      false,
      "/localisation/enabled");
    localisationEnabled.disabled = true;
    const localisationMinimumDataUnits = numberControl(
      draft.localisation.minimumDataUnits,
      "/localisation/minimumDataUnits",
      { min: 0, max: UINT32_MAX, step: 1 });
    const localisationMinimumAngle = degreeControl(
      draft.localisation.minimumAngleRangeRadians,
      "/localisation/minimumAngleRangeRadians",
      { min: 0 });
    localisationSection.append(
      checkLabel(
        "Run target-motion localisation (unavailable)",
        localisationEnabled),
      field(
        "Minimum no. detections",
        localisationMinimumDataUnits),
      field(
        "Minimum angle range",
        localisationMinimumAngle,
        { unit: "°" }));
    detectorPanel.append(localisationSection);

    const prePanel = panels.get("pre-classifier");
    const preSection = titledSection("Pre Classifier", {
      help:
        "The Java χ² threshold pre-classifier rejects unsuitable trains " +
        "before the species classifiers run."
    });
    const preChi2Threshold = numberControl(
      draft.classifier.preClassifier.chi2Threshold,
      "/classifier/preClassifier/chi2Threshold",
      { min: 0 });
    const preMinimumClicks = numberControl(
      draft.classifier.preClassifier.minimumClicks,
      "/classifier/preClassifier/minimumClicks",
      { min: 0, max: UINT32_MAX, step: 1 });
    const preMinimumSelectedPercentage = numberControl(
      draft.classifier.preClassifier.minimumSelectedPercentage,
      "/classifier/preClassifier/minimumSelectedPercentage");
    preMinimumSelectedPercentage.disabled = true;
    const preMinimumTime = numberControl(
      draft.classifier.preClassifier.minimumTimeSeconds,
      "/classifier/preClassifier/minimumTimeSeconds",
      { min: 0 });
    const preSpeciesFlag = numberControl(
      draft.classifier.preClassifier.speciesFlag,
      "/classifier/preClassifier/speciesFlag",
      { min: INT32_MIN, max: INT32_MAX, step: 1 });
    preSection.append(
      field("χ² Threshold", preChi2Threshold),
      field("Min. clicks", preMinimumClicks),
      field("Min. % clicks", preMinimumSelectedPercentage, {
        unit: "%",
        help:
          "Held at zero until per-classifier data selectors are modeled."
      }),
      field("Min. time", preMinimumTime, { unit: "s" }),
      field("Species ID", preSpeciesFlag));
    prePanel.append(preSection);

    const speciesPanel = panels.get("species-classifiers");
    const classifierEnabled = checkboxControl(
      draft.classifier.runClassifier,
      "/classifier/runClassifier");
    const classifierIntro = titledSection("Species Classifiers", {
      help:
        "The current portable Java-authoritative slice exposes one IDI, " +
        "one bearing, and one spectrum-template classifier."
    });
    classifierIntro.append(checkLabel(
      "Enable Click Train Classification",
      classifierEnabled));
    speciesPanel.append(classifierIntro);
    const classifierDetails = createElement("div", {
      className: "mht-click-train-settings-classifier-grid",
      attributes: {
        "data-mht-click-train-conditional": "species-classifiers"
      }
    });
    speciesPanel.append(classifierDetails);
    bindVisibility(classifierEnabled, classifierDetails);

    const idiClassifierSection = titledSection("IDI Classifier");
    const idiClassifierEnabled = checkboxControl(
      draft.classifier.idi.enabled,
      "/classifier/idi/enabled");
    const idiClassifierDetails = createElement("div", {
      className: "mht-click-train-settings-conditional",
      attributes: {
        "data-mht-click-train-conditional": "idi-classifier"
      }
    });
    const idiSpeciesFlag = numberControl(
      draft.classifier.idi.speciesFlag,
      "/classifier/idi/speciesFlag",
      { min: INT32_MIN, max: INT32_MAX, step: 1 });
    idiClassifierDetails.append(field("Species ID", idiSpeciesFlag));
    const medianIdi = statRange(
      idiClassifierDetails,
      "Median IDI",
      draft.classifier.idi.useMedianIdi,
      draft.classifier.idi.minimumMedianIdi,
      draft.classifier.idi.maximumMedianIdi,
      {
        enabled: "/classifier/idi/useMedianIdi",
        minimum: "/classifier/idi/minimumMedianIdi",
        maximum: "/classifier/idi/maximumMedianIdi"
      },
      "s");
    const meanIdi = statRange(
      idiClassifierDetails,
      "Mean IDI",
      draft.classifier.idi.useMeanIdi,
      draft.classifier.idi.minimumMeanIdi,
      draft.classifier.idi.maximumMeanIdi,
      {
        enabled: "/classifier/idi/useMeanIdi",
        minimum: "/classifier/idi/minimumMeanIdi",
        maximum: "/classifier/idi/maximumMeanIdi"
      },
      "s");
    const stdIdi = statRange(
      idiClassifierDetails,
      "Std IDI",
      draft.classifier.idi.useStdIdi,
      draft.classifier.idi.minimumStdIdi,
      draft.classifier.idi.maximumStdIdi,
      {
        enabled: "/classifier/idi/useStdIdi",
        minimum: "/classifier/idi/minimumStdIdi",
        maximum: "/classifier/idi/maximumStdIdi"
      },
      "s");
    idiClassifierSection.append(
      checkLabel("Run IDI Classifier", idiClassifierEnabled),
      idiClassifierDetails);
    bindVisibility(idiClassifierEnabled, idiClassifierDetails);
    classifierDetails.append(idiClassifierSection);

    const bearingClassifierSection = titledSection(
      "Bearing Classifier");
    const bearingClassifierEnabled = checkboxControl(
      draft.classifier.bearing.enabled,
      "/classifier/bearing/enabled");
    const bearingClassifierDetails = createElement("div", {
      className: "mht-click-train-settings-conditional",
      attributes: {
        "data-mht-click-train-conditional": "bearing-classifier"
      }
    });
    const bearingSpeciesFlag = numberControl(
      draft.classifier.bearing.speciesFlag,
      "/classifier/bearing/speciesFlag",
      { min: INT32_MIN, max: INT32_MAX, step: 1 });
    const bearingMinimum = degreeControl(
      draft.classifier.bearing.minimumBearingRadians,
      "/classifier/bearing/minimumBearingRadians");
    const bearingMaximum = degreeControl(
      draft.classifier.bearing.maximumBearingRadians,
      "/classifier/bearing/maximumBearingRadians");
    bearingClassifierDetails.append(
      field("Species ID", bearingSpeciesFlag),
      createElement("h5", { text: "Bearing Limits" }),
      field("Minimum", bearingMinimum, { unit: "°" }),
      field("Maximum", bearingMaximum, { unit: "°" }));
    const meanBearing = angularRange(
      bearingClassifierDetails,
      "Δ Bearing Mean",
      draft.classifier.bearing.useMean,
      draft.classifier.bearing.minimumMeanDerivative,
      draft.classifier.bearing.maximumMeanDerivative,
      {
        enabled: "/classifier/bearing/useMean",
        minimum: "/classifier/bearing/minimumMeanDerivative",
        maximum: "/classifier/bearing/maximumMeanDerivative"
      },
      "°/s");
    const medianBearing = angularRange(
      bearingClassifierDetails,
      "Δ Bearing Median",
      draft.classifier.bearing.useMedian,
      draft.classifier.bearing.minimumMedianDerivative,
      draft.classifier.bearing.maximumMedianDerivative,
      {
        enabled: "/classifier/bearing/useMedian",
        minimum: "/classifier/bearing/minimumMedianDerivative",
        maximum: "/classifier/bearing/maximumMedianDerivative"
      },
      "°/s");
    const stdBearing = angularRange(
      bearingClassifierDetails,
      "Δ Bearing Std",
      draft.classifier.bearing.useStd,
      draft.classifier.bearing.minimumStdDerivative,
      draft.classifier.bearing.maximumStdDerivative,
      {
        enabled: "/classifier/bearing/useStd",
        minimum: "/classifier/bearing/minimumStdDerivative",
        maximum: "/classifier/bearing/maximumStdDerivative"
      },
      "°/s");
    bearingClassifierSection.append(
      checkLabel("Run Bearing Classifier", bearingClassifierEnabled),
      bearingClassifierDetails);
    bindVisibility(
      bearingClassifierEnabled,
      bearingClassifierDetails);
    classifierDetails.append(bearingClassifierSection);

    const templateSection = titledSection(
      "Spectrum Template Classifier");
    const templateEnabled = checkboxControl(
      draft.classifier.spectrumTemplate.enabled,
      "/classifier/spectrumTemplate/enabled");
    const templateDetails = createElement("div", {
      className: "mht-click-train-settings-conditional",
      attributes: {
        "data-mht-click-train-conditional": "spectrum-template"
      }
    });
    const templateName = textControl(
      draft.classifier.spectrumTemplate.name,
      "/classifier/spectrumTemplate/name");
    const templateSampleRate = numberControl(
      draft.classifier.spectrumTemplate.sampleRateHz,
      "/classifier/spectrumTemplate/sampleRateHz",
      { min: Number.MIN_VALUE });
    const templateThreshold = numberControl(
      draft.classifier.spectrumTemplate.correlationThreshold,
      "/classifier/spectrumTemplate/correlationThreshold",
      { min: -1, max: 1 });
    const templateSpeciesFlag = numberControl(
      draft.classifier.spectrumTemplate.speciesFlag,
      "/classifier/spectrumTemplate/speciesFlag",
      { min: INT32_MIN, max: INT32_MAX, step: 1 });
    const spectrumValues = Array.from(
      draft.classifier.spectrumTemplate.spectrum);
    const spectrumHost = createElement("div", {
      className: "mht-click-train-settings-spectrum",
      attributes: {
        "data-mht-click-train-list": "spectrum"
      }
    });
    const spectrumActions = createElement("div", {
      className: "mht-click-train-settings-actions"
    });
    const addSpectrumBin = actionButton(
      "Add spectrum bin",
      "add-spectrum-bin");
    const removeSpectrumBin = actionButton(
      "Remove last bin",
      "remove-spectrum-bin");
    const syncSpectrum = () => {
      const values = spectrumControls.map((control, index) =>
        finiteNumber(control.value, `Spectrum bin ${index}`));
      if (values.length < 2) {
        throw new Error(
          "Spectrum template must contain at least 2 bins");
      }
      spectrumValues.splice(0, spectrumValues.length, ...values);
    };
    const renderSpectrum = () => {
      spectrumHost.replaceChildren();
      spectrumControls = [];
      spectrumValues.forEach((value, index) => {
        const row = createElement("label", {
          className: "mht-click-train-settings-spectrum-bin"
        });
        const control = numberControl(
          value,
          `/classifier/spectrumTemplate/spectrum/${index}`);
        row.append(
          createElement("span", { text: String(index) }),
          control);
        spectrumHost.append(row);
        spectrumControls.push(control);
      });
      addSpectrumBin.disabled = false;
      removeSpectrumBin.disabled = spectrumValues.length <= 2;
    };
    addSpectrumBin.addEventListener("click", () => {
      try {
        syncSpectrum();
        spectrumValues.push(
          spectrumValues[spectrumValues.length - 1] || 0);
        renderSpectrum();
      }
      catch (error) {
        reportError(error);
      }
    });
    removeSpectrumBin.addEventListener("click", () => {
      try {
        syncSpectrum();
        if (spectrumValues.length <= 2) return;
        spectrumValues.pop();
        renderSpectrum();
      }
      catch (error) {
        reportError(error);
      }
    });
    renderSpectrum();
    spectrumActions.append(addSpectrumBin, removeSpectrumBin);
    templateDetails.append(
      field("Name", templateName),
      field("Sample rate", templateSampleRate, { unit: "Hz" }),
      field("Spectrum Correlation Threshold", templateThreshold),
      field("Species ID", templateSpeciesFlag),
      createElement("h5", { text: "Spectrum Template" }),
      createElement("p", {
        className: "section-help",
        text:
          "Each numbered value is one Java SpectrumTemplateParams " +
          "spectrum bin. Values are committed only when OK is accepted."
      }),
      spectrumHost,
      spectrumActions);
    templateSection.append(
      checkLabel(
        "Run Spectrum Template Classifier",
        templateEnabled),
      templateDetails);
    bindVisibility(templateEnabled, templateDetails);
    classifierDetails.append(templateSection);

    container.append(root);

    return {
      collect() {
        syncSelectedGroups();
        syncClickTypes();
        syncSpectrum();
        const commonVariable = (key, label) => ({
          enabled: variableViews[key].enabled.checked,
          error: positive(
            variableViews[key].error.value,
            `${label} error`),
          minimumError: positive(
            variableViews[key].minimumError.value,
            `${label} minimum error`)
        });
        const result = {
          algorithm: "mht",
          channelGroups: Array.from(selectedGroups),
          dataSelector: {
            enabled: selectorEnabled.checked,
            useEchoes: useEchoes.checked,
            minimumAmplitudeDb: finiteNumber(
              minimumAmplitude.value,
              "Minimum amplitude"),
            includedClickTypes: Array.from(clickTypes)
          },
          kernel: {
            nHold: integer(
              nHold.value,
              "Maximum number of trains",
              1,
              UINT32_MAX),
            nPruneback: integer(
              nPruneback.value,
              "Prune-back",
              0,
              UINT32_MAX),
            nPrunebackStart: integer(
              nPrunebackStart.value,
              "Prune-start",
              0,
              UINT32_MAX),
            maxCoast: integer(
              maxCoast.value,
              "Maximum number of coasts",
              0,
              INT32_MAX)
          },
          chi2: {
            maximumIciSeconds: positive(
              maximumIci.value,
              "Maximum ICI"),
            coastPenalty: nonNegative(
              coastPenalty.value,
              "Coast penalty"),
            newTrackPenalty: nonNegative(
              newTrackPenalty.value,
              "New track penalty"),
            newTrackClicks: integer(
              newTrackClicks.value,
              "Number of new track clicks",
              0,
              UINT32_MAX),
            longTrackExponent: nonNegative(
              longTrackExponent.value,
              "Long track exponent"),
            lowIciExponent: nonNegative(
              lowIciExponent.value,
              "Low ICI exponent"),
            electricalNoiseFilter: {
              enabled: electricalEnabled.checked,
              minimumChi2: nonNegative(
                electricalMinimumChi2.value,
                "Electrical Noise Filter minimum chi-squared"),
              dataUnits: integer(
                electricalDataUnits.value,
                "Electrical Noise Filter data units",
                1,
                UINT32_MAX)
            },
            variables: {
              idi: {
                ...commonVariable("idi", "IDI"),
                minimumIdiSeconds: nonNegative(
                  variableViews.idi.minimumIdi.value,
                  "Minimum IDI")
              },
              amplitude: {
                ...commonVariable("amplitude", "Amplitude"),
                jumpEnabled:
                  variableViews.amplitude.jumpEnabled.checked,
                maximumJumpDb: nonNegative(
                  variableViews.amplitude.maximumJump.value,
                  "Maximum amplitude jump")
              },
              bearing: {
                enabled: variableViews.bearing.enabled.checked,
                errorRadians: radiansFromControl(
                  variableViews.bearing.error,
                  "Bearing error",
                  { exclusiveMin: 0 }),
                minimumErrorRadians: radiansFromControl(
                  variableViews.bearing.minimumError,
                  "Bearing minimum error",
                  { exclusiveMin: 0 }),
                jumpEnabled:
                  variableViews.bearing.jumpEnabled.checked,
                maximumJumpRadians: radiansFromControl(
                  variableViews.bearing.maximumJump,
                  "Maximum bearing jump",
                  { min: 0 }),
                jumpDirection:
                  variableViews.bearing.jumpDirection.value
              },
              correlation: commonVariable(
                "correlation",
                "Correlation"),
              timeDelay: commonVariable(
                "timeDelay",
                "Time Delay"),
              length: commonVariable("length", "Length"),
              peakFrequency: commonVariable(
                "peakFrequency",
                "Peak Frequency")
            }
          },
          classifier: {
            runClassifier: classifierEnabled.checked,
            preClassifier: {
              chi2Threshold: nonNegative(
                preChi2Threshold.value,
                "Pre Classifier chi-squared threshold"),
              minimumClicks: integer(
                preMinimumClicks.value,
                "Pre Classifier minimum clicks",
                0,
                UINT32_MAX),
              minimumSelectedPercentage: finiteNumber(
                preMinimumSelectedPercentage.value,
                "Pre Classifier minimum selected percentage"),
              minimumTimeSeconds: nonNegative(
                preMinimumTime.value,
                "Pre Classifier minimum time"),
              speciesFlag: integer(
                preSpeciesFlag.value,
                "Pre Classifier species ID")
            },
            idi: {
              enabled: idiClassifierEnabled.checked,
              useMedianIdi: medianIdi.toggle.checked,
              minimumMedianIdi: nonNegative(
                medianIdi.minimumControl.value,
                "IDI median minimum"),
              maximumMedianIdi: nonNegative(
                medianIdi.maximumControl.value,
                "IDI median maximum"),
              useMeanIdi: meanIdi.toggle.checked,
              minimumMeanIdi: nonNegative(
                meanIdi.minimumControl.value,
                "IDI mean minimum"),
              maximumMeanIdi: nonNegative(
                meanIdi.maximumControl.value,
                "IDI mean maximum"),
              useStdIdi: stdIdi.toggle.checked,
              minimumStdIdi: nonNegative(
                stdIdi.minimumControl.value,
                "IDI standard deviation minimum"),
              maximumStdIdi: nonNegative(
                stdIdi.maximumControl.value,
                "IDI standard deviation maximum"),
              speciesFlag: integer(
                idiSpeciesFlag.value,
                "IDI Classifier species ID")
            },
            bearing: {
              enabled: bearingClassifierEnabled.checked,
              minimumBearingRadians: radiansFromControl(
                bearingMinimum,
                "Bearing minimum"),
              maximumBearingRadians: radiansFromControl(
                bearingMaximum,
                "Bearing maximum"),
              useMean: meanBearing.toggle.checked,
              minimumMeanDerivative: radiansFromControl(
                meanBearing.minimumControl,
                "Mean bearing derivative minimum"),
              maximumMeanDerivative: radiansFromControl(
                meanBearing.maximumControl,
                "Mean bearing derivative maximum"),
              useMedian: medianBearing.toggle.checked,
              minimumMedianDerivative: radiansFromControl(
                medianBearing.minimumControl,
                "Median bearing derivative minimum"),
              maximumMedianDerivative: radiansFromControl(
                medianBearing.maximumControl,
                "Median bearing derivative maximum"),
              useStd: stdBearing.toggle.checked,
              minimumStdDerivative: radiansFromControl(
                stdBearing.minimumControl,
                "Bearing derivative standard deviation minimum"),
              maximumStdDerivative: radiansFromControl(
                stdBearing.maximumControl,
                "Bearing derivative standard deviation maximum"),
              speciesFlag: integer(
                bearingSpeciesFlag.value,
                "Bearing Classifier species ID")
            },
            spectrumTemplate: {
              enabled: templateEnabled.checked,
              name: templateName.value,
              sampleRateHz: positive(
                templateSampleRate.value,
                "Spectrum template sample rate"),
              spectrum: Array.from(spectrumValues),
              correlationThreshold: finiteNumber(
                templateThreshold.value,
                "Spectrum correlation threshold",
                { min: -1, max: 1 }),
              speciesFlag: integer(
                templateSpeciesFlag.value,
                "Spectrum Template Classifier species ID")
            }
          },
          localisation: {
            enabled: false,
            minimumDataUnits: integer(
              localisationMinimumDataUnits.value,
              "Localisation minimum data units",
              0,
              UINT32_MAX),
            minimumAngleRangeRadians: radiansFromControl(
              localisationMinimumAngle,
              "Localisation minimum angle range",
              { min: 0 })
          }
        };
        return clone(canonicalSettings(result));
      },
      focus() {
        groupControls[0]?.control?.focus?.();
      },
      cleanup() {
        sourceSelect?.removeEventListener?.(
          "change",
          sourceChanged);
      }
    };
  }

  globalThis.PamguardProjectMhtClickTrainSettings =
    Object.freeze({
      mountEditor,
      canonicalSettings,
      defaultSettings
    });
})();
