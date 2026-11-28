# P2 — Native Binaural Engine

Based on the passing P1 native spatial source branch. Independent original DSP, no third-party plug-in dependencies. SpatialBinaural.h is integrated into SpatialSource.h's processed path, not into Studio's live Tracktion graph. No project format, mixer, UI, stage routing, existing StagePan or RoomReverb changes. No merge.

## Design

Angle is listener/conductor perspective, -90 degrees left, 0 center, +90 right. Front hemisphere only. Both ears receive a centered mono source; for stereo, original channel content is preserved unless the independent focus control is enabled. At nonzero angle a wide stereo patch receives per-channel ear cues, not a physically correct sum of independently placed emitters. Focus progressively narrows the pair with lateral angle, reaching the common mid signal at maximum focus and +/-90 degrees. Default focus is 0 to preserve stereo library content. Use mono or width 0 for a point source. Full focus can cancel anti-correlated input as ordinary mono fold-down does.

Three separately adjustable cues, 0..2 (0–200%, default 100%):
- Timing: rigid-sphere front-hemisphere approximation using a nominal 8.75 cm head radius, speed of sound 343 m/s, far-ear delay r/c * (abs(theta) + sin(abs(theta))) with angle in radians. Near ear stays at current input sample. At +/-90 degrees nominal normal relative delay approximately 0.656 ms, exaggerated maximum approximately 1.312 ms. Values in physical time, independent of sample rate; no reproduction of L2's inconsistent timing results. Two 512-sample inline rings, linear fractional interpolation, causal. No fixed buffering latency; intentional ear delay is not host processing latency. Interpolation can color near-Nyquist response, moving delay can impart Doppler-like pitch change, and mixed dry/wet signals can comb-filter.
- Level: far ear attenuation 4 dB * sin(abs(theta)) * level strength, near ear unity. Normal max 4 dB, exaggerated max 8 dB. No automatic loudness compensation. This is a perceptual approximation informed by L2's approximately 4.1 dB isolated level difference, not a reproduction of proprietary code or a claim of exact head acoustics.
- Head shadow: two cascaded first-order shelves (TPT/bilinear low-pass plus attenuated high component) around 2.5 kHz. Each stage high-frequency gain 10^(-14 * sin(abs(theta)) * shadow strength / 40), with unity DC and near-ear identity. Normal total asymptotic high-frequency attenuation 14 dB, exaggerated 28 dB. Sample rate-dependent Nyquist limit and interpolation behavior; measured response differs from the asymptotes. Inspired by L2's frequency-dependent far-ear attenuation, not a fitted HRTF or exact reference response.

Center is transparent at all cue/focus strengths. Angle alone does not change the signal if all three cues and focus are zero. Width and distance parameters from P1 remain independent. No measured HRTF datasets, individualized anatomy, elevation or rear cues, head tracking, loudspeaker crosstalk cancellation or surround rendering. Headphone listening is needed to assess perceived externalization. For loudspeakers, independently reduce timing/shadow as desired; P3 may expose named presets, not another hidden mode.

## Processing

P1 width -> P1 air/proximity/tilt/distance gain -> optional binaural focus -> per-ear delay, shelf and level -> existing P1 amount blend with original dry -> output trim -> mono monitor. Dry amount zero remains unaffected by ear cues with unity trim and mono off. Monitor combines after ear processing and is exact dual mono when fully enabled. Both ear histories are kept warm at center and amount zero; reset clears them. Control plans built off the audio thread; setPlan is serialized between processing blocks, not a thread-safe publication API. Invalid rate, nonfinite or out-of-range coefficient/delay rejects entire combined plan. Coefficients and ear delay/gain/shadow/focus smoothed with 20 ms time constant. Per-sample fixed-cost math, no allocation, locks or transcendental functions in process, 4 KiB inline delay history, tested combined object bound <5 KiB. No claims of live audio deadline performance until P3 integration and hardware testing.

## Verification

Standalone tests/SpatialBinauralTests.cpp:
- Relative delay via impulse absolute-amplitude centroid; sign, zero, 100%, 200%, +/-45 and +/-90, five sample rates (8/44.1/48/96/192 kHz). Tolerance 0.0001 ms versus the analytic model, not a psychophysical accuracy claim.
- Independent level and spectral tone gain at 125/1000/4000/8000 Hz across 44.1/48/96 kHz, left/right mirror symmetry, near ear unity, low band preservation, high-band attenuation.
- Center identity, disabled-cue stereo identity, full focus, independent dry mix, mono monitor and alias-safe mono input.
- Automated position reversals with distance, block partition invariance, rejection of invalid plans, finite continuous signal, cleared history on reset, bounded footprint.
- Offline aggregate render throughput for 16/32/64/128 sources, 48 kHz, 256-frame blocks, 2 seconds of input per source. Results include wall elapsed ms and wall time / source session duration ratio, not real-time CPU utilization. Synthetic input, no AU, Tracktion, mixer, disk, audio callback deadline or sample streaming. Do not interpret CI hardware as the user's MacBook.
- All existing P1 regressions retained except object size limit adjusted for the two inline delay rings.

CTest StudioSpatialBinaural and standalone spatial-source workflow run both suites on Linux and Intel macOS with optimized checks and ASan/UBSan. Response text artifacts include delay and frequency tables and offline benchmark results. Native live listening and actual laptop resource use remain pending.

Optional standalone test on Mac (does not switch checkout or rebuild Studio):

```sh
(
  set -o pipefail
  cd "$HOME/Developer/Studio-FilmScoring-M1" || exit
  git fetch origin feature/studio-native-spatial-p2 || exit
  P2="$(mktemp -d)" || exit
  git archive FETCH_HEAD SpatialSource.h SpatialBinaural.h tests/SpatialSourceTests.cpp tests/SpatialBinauralTests.cpp | tar -x -C "$P2" || exit
  xcrun clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror "$P2/tests/SpatialBinauralTests.cpp" -o "$P2/binaural" || exit
  "$P2/binaural" | tee "$HOME/Desktop/Studio-Spatial-P2-response.txt"
)
```

P3: production Tracktion adapter and stable saved parameters, UI/presets and listening acceptance. Keep stage opt-in, existing project compatibility and independent shared reverb. Future P4/P5: source-specific early reflections and room refinement. M45–M51 still needs explicit consolidation; no branch merge in P2.
