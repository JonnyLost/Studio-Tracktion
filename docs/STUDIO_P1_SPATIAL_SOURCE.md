# P1 — Native Spatial Source Processor

First independent DSP milestone of Studio Spatial, on the L2 reference laboratory branch. No third-party binaries, source, preset state, impulse responses or commercial plug-in dependencies. No live insert, stage routing, project schema or UI change. Existing StagePan and shared RoomReverb remain untouched. M45–M51 remains separately pending consolidation; do not merge branches implicitly.

## Reference evidence and original design

User L2 report Studio-Spatial-Lab-L2-VBch2U.zip: 132 completed cases. Reopened tones.csv and case_parameters.csv for P1. Panagement's Far display is a perceptual control, not metres. At normalized .8 (display 0.00), centered mono with room disabled measured essentially unity. At normalized 0 (display -24), measured gain was -31.83 dB at 125 Hz and -49.29 dB at 8 kHz. At normalized 1 (display +6), measured +7.34 dB at 125 Hz and +4.80 dB at 8 kHz. These are observable reference targets, not an asserted filter topology. No measured response arrays or proprietary presets copied into the processor. Studio uses original conservative approximations rather than exact response fitting.

SpatialSource.h is a dependency-free C++17 fixed-size native DSP processor in studio::spatial::source. Signed perceptual distance -1 close / 0 neutral / +1 far. Direct level is -30 dB at far, +4 dB close, linear in dB by side of neutral. Two cascaded one-pole low-pass sections implement optional progressive air damping; cutoff moves from 18 kHz toward 3 kHz, crossfaded in as distance increases. An independent close proximity boost uses a 220 Hz low-pass contribution, up to 3 dB asymptotic low-frequency boost. These are perceptual approximations, not acoustic propagation or physical air absorption over metres. Coefficients calculated for the actual sample rate (8–192 kHz). No claim of matching Panagement.

## Controls and signal chain

| Parameter | Range | Default |
|---|---|---|
| distance | -1..1 | 0 |
| width | 0..2 (0..200%) | 1 |
| tiltDb | -6..+6 dB tonal balance | 0 |
| outputDb | -24..+12 dB | 0 |
| amount | 0..1 dry/processed blend | 1 |
| air | 0..1 distance coloration strength | 1 |
| distanceWidth | 0..1 far-distance side reduction | 0 |
| mono | 0..1 mono monitor | 0 |

Stereo or mono input -> width (mid/side, mono sum preserved, width 2 may have opposite-polarity crossfeed) -> distance color (air, proximity) -> tilt (complementary low/high components) -> distance gain -> dry/processed blend -> output gain -> optional mono monitor. Positive tilt brightens, negative darkens; actual response is not a calibrated two-shelf EQ. DistanceWidth optionally narrows stereo at far positions, disabled by default. Mono is an explicit monitor transform, not part of distance. Output trim and mono monitor remain active when amount=0. True neutral is width 1, distance 0, tilt 0, output 0 dB, amount 1 and mono off. Exact pass-through for ordinary finite normal-range samples at neutral or amount 0 with unity trim/monitor off. Nonfinite and tiny denormal-like inputs become zero; pathological values bounded to +/-1e6 (not a musical limiter). Float headroom above unity is preserved; output can clip at downstream conversion. Zero buffered lookahead, not zero filter phase delay.

## Host contract / performance

prepare(rate, params) and makePlan(rate, params) on a non-render owner thread. makePlan computes rate-specific poles and gains. setPlan validates finite bounded scalars and matching rate; call on the serialized processing owner between blocks, never concurrently with process/reset. Future host must provide a thread-safe bounded handoff of whole plans. Processor owns no heap allocations, locks, file access or per-sample transcendental math. Per-sample 15 ms one-pole smoothing of parameters (time constant, not full settling time), stable coefficient interpolation and warm filter state. reset snaps to target and clears history. In-place stereo requires distinct nonoverlapping buffers; mono-to-stereo accepts alias of exactly one output. Unsupported rates or invalid plans rejected; malformed samples bounded. No automatic latency compensation or dry-path delay yet. C++ core alone does not make a complete real-time Tracktion plug-in. Measure actual Intel MacBook CPU before promising orchestra-scale capacity.

## Tests

tests/SpatialSourceTests.cpp covers exact neutral, mid/side width and sum, amount-zero behavior, output gain, mono monitor, mono input alias, distance response and monotonic low-band gain at 44.1/48/96 kHz, independent air bypass, proximity and tilt, block partition invariance, smoothing, reset, invalid plans/rates, nonfinite and pathological inputs, fixed footprint. Emits response measurements for original DSP. CTest StudioSpatialSource and standalone Linux/Intel macOS optimized and ASan/UBSan workflow; results must be verified, not inferred. Local Mac test (no Studio build):

```sh
(
  set -o pipefail
  cd "$HOME/Developer/Studio-FilmScoring-M1" || exit
  git fetch origin feature/studio-native-spatial-p1 || exit
  P1="$(mktemp -d)" || exit
  git archive FETCH_HEAD SpatialSource.h tests/SpatialSourceTests.cpp | tar -x -C "$P1" || exit
  xcrun clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror "$P1/tests/SpatialSourceTests.cpp" -o "$P1/spatial-source" || exit
  "$P1/spatial-source" | tee "$HOME/Desktop/Studio-Spatial-P1-response.txt"
)
```

P2 adds independent, physically informed binaural relative timing, level and head shadow with sample-rate-consistent delays; do not replicate the inconsistent reference timing estimates. P3 adds host adapter, persisted controls, presets and usable UI. P4–P8 cover early reflections, shared reverb, spatial delay/modulation, stage integration and acoustic acceptance. Full GUI and audible project testing deferred until the production adapter exists.
