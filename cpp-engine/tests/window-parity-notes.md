# Window Parity Notes

The C++ implementation in `WindowFunction.cpp` follows `src/Spectrogram/WindowFunction.java`.

Important behaviours:

- Rectangular: all `1`
- Hamming: denominator `length - 1`
- Hann: denominator `length`
- Bartlett: Java loop boundaries preserved
- Blackman: denominator `length - 1`
- Blackman-Harris: denominator `length - 1`
- Window gain: RMS gain, `sqrt(sum(w*w) / n)`

Planned first parity fixtures:

```text
type=0 length=8
type=1 length=8
type=2 length=8
type=3 length=8
type=4 length=8
type=5 length=8
type=2 length=1024
```

