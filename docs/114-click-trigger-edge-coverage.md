# Click Trigger Edge Coverage

Date: 2026-07-01

This checkpoint expands the focused click trigger detector test.

## Added cases

- empty channel bitmap is rejected;
- invalid filter alpha is rejected;
- zero max length is rejected;
- invalid PCM chunk shape is rejected;
- missing configured detector channels are rejected;
- `minTriggerChannels` suppresses detections when too few trigger channels fire;
- detector reset is reproducible for the synthetic fixture chunk.

## Test

```text
click_trigger_basic_2ch_parity
```
