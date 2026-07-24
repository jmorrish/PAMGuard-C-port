# Click detector control sections

Date: 2026-07-24

This checkpoint makes the browser Click Detector configuration usable as the
implemented Java-parity surface grows.

## Implemented

The Click Detector dialog is split into eight in-dialog sections:

- Detection
- Filters
- Channels
- Classification
- Echo & vetoes
- Delay & localisation
- Noise & output
- Click trains

Only the selected section is displayed. Switching sections does not rebuild the
form, so values entered in another section remain in the same DOM controls and
are included when the session is created.

The browser now also exposes the click module and delay/localisation enable
switches. These values previously existed in the HTTP session contract but were
hard-coded to `true` by the browser.

The Click trains section exposes both ICI and MHT formation, the simple ICI
limits, the train-classifier enable switch, and JSON object editors for the
complete implemented MHT and classifier settings. The JSON is type-checked
before a create request is sent and is placed under `click.train.mht` and
`click.train.classifier`, respectively. The OpenAPI description now includes
the already-implemented bearing-domain train classifier alongside the
pre-classifier, IDI, and spectrum-template classifiers.

## Validation

- The inline JavaScript parses successfully in Node.
- All 222 HTML element IDs are unique.
- All 248 literal JavaScript element lookups resolve to an element.
- A headless Chrome interaction opened the dialog, selected Classification,
  and observed exactly its eight controls as visible.
- The same browser interaction switched to Filters and observed all 18 filter
  controls as visible.

## Claim boundary

This is navigation and contract wiring for controls already supported by the
engine. It does not add detector maths.

The advanced train settings are intentionally exposed as JSON objects rather
than hundreds of individual inputs. The browser exposes every implemented
field without attempting to reproduce the legacy Swing layout or preferences.
