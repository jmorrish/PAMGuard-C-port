# Whistles & Moans grouped-source channels

Date: 2026-07-24

## Java behavior

`WhistleToneParameters` inherits `GroupedSourceParameters`. At prepare time,
`WhistleToneConnectProcess` creates one `ShapeConnector` per used group:

- the lowest channel in the group creates the contour;
- every channel in that group supplies FFT data for delay measurement;
- only the group's first channel owns its raw-FFT background smoother/output;
- contours from separate groups may still be associated by the detection
  grouper.

The earlier engine created one contour tracker per FFT channel and measured
delays across every selected channel. That over-produced contours for the
normal one-group case and could combine channels from unrelated groups during
localisation.

## Port

Whistles & Moans now accepts:

- `channelBitmap`;
- `groupingType`: `all`, `singles`, or `user`;
- `channelGroups`: one group number per audio channel.

The default is PAMGuard's one-group mode. Groups are ordered by ascending used
group number, and channels within a group are ordered ascending. One tracker
and background monitor are created for the first channel of each group.
Whistle delays and bearing selection use only that contour's group.

The settings are implemented in the C++ session, HTTP validation/readback,
OpenAPI, browser contour dialog, example sessions, and `.psfx` converter.

## Validation

A pinned Java fixture drives the real
`WhistleToneParameters.countChannelGroups/getGroupChannels` path over:

- two normal channel pairs;
- a bitmap selecting only a subset of assigned channels;
- sparse, out-of-order group IDs;
- sparse selected audio channels.

All eight Java group bitmaps match the shared C++ grouping primitive.

The session-level Whistles & Moans test proves both runtime modes:

- one all-channel group emits contours only on channel 0 while measuring the
  0–1 delay;
- single-channel groups emit independent contour streams that the existing
  cross-group detector can associate, including across PCM chunk boundaries.

The real `.psfx` fixture uses four channels in user groups `[0,0,1,1]` and is
accepted by the HTTP service smoke.

## Claim boundary

This covers ordinary FFT sources whose bitmap identifies audio channels.
PAMGuard can also carry beamformer sequence maps through the same
`GroupedSourceParameters` fields. The engine has no beamformer sequence-map
source model, so sequence-number grouping is not claimed.
