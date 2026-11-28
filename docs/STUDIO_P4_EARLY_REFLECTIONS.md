# P4 — Source-specific early reflections

Base P3 (full native acceptance pending at development start); P2 standalone Linux and Intel macOS checks passed. P4 is an original lightweight image-source approximation, not a MIR venue clone, Panagement algorithm extraction, measured IR or convolution engine. No merge.

## Signal path and geometry

Six first-order image sources: left wall, right wall, front wall, rear wall, floor and ceiling. Each gets its own fractional-sample path delay, stereo direction, amplitude coefficient and one-pole material/path low-pass. Speed of sound 343 m/s. Reflection delay = excess path over direct source-listener path / speed (minimum 1 sample, bounded at 250 ms); direct path stays time zero. Listener at x=0, y=23% of length, z=1.4 m; source at same height. P3 direction controls azimuth in front hemisphere. Perceptual distance -1..1 maps to radial 1.2..14 m (neutral 4 m), constrained to room boundaries with 0.5 m margin. This illustrative local geometry is not a physical seating coordinate or venue measurement. It does not change P1 direct distance attenuation.

Image-source amplitude = 0.2 × amount × surface amplitude coefficient × direct path / reflected path; constant-power stereo weighting by reflected direction. No individualized binaural reflection HRTF, diffusion, higher-order reflections, occlusion, scattering, or late reverberation. Each material has an original approximate reflection coefficient and damping cutoff; longer paths damp more. Reflection feed is the mono average of the distance/width/tilt-processed stereo signal before P2 ear processing; anti-correlated stereo content can cancel in the reflection feed. Direct signal remains stereo. Reflected stereo adds after P2, before common dry/wet Amount, output trim and Mono Check. Early reflections are part of the wet path, not a second copy of the dry sound.

| Local room | Width × length × height (m) | Walls / floor / ceiling |
|---|---|---|
| Treated studio | 8 × 10 × 3.5 | Treated / carpet / treated |
| Chamber room | 12 × 17 × 6 | Wood / wood / plaster |
| Scoring stage | 22 × 27 × 10 | Wood / wood / treated |
| Stone hall | 18 × 32 × 13 | Stone / stone / plaster |

These four original local geometries are deliberately separate from the existing shared Stage Room Return. P5 and P7 will address shared late reverberation and common venue geometry, avoiding double early reflections and duplicated dry signal. For isolated evaluation leave legacy Stage routing off and mixer pan centered. Do not install a spatial insert on the Room Return.

## User-facing controls and compatibility

Add Effect > Studio Spatial. Existing position pad changes both direct cues and reflection geometry. Early reflections slider 0–100%, Reflection Room selector, true bypass, output and mono check. Default amount 0; all P3 factory presets remain dry unless reflection level is enabled. Reset restores neutral (room scoring stage, early amount zero). State string version 2 extends the original 13 scalars with reflection amount and room index; the stable V1 property and plugin type remain for backwards loading into P4. Version-one strings retain every original parameter and reflections off. Invalid or unknown version falls back to neutral. Future versions of Studio must preserve v2 decoding. Do not save the only copy of an important project on an unverified development branch; P3 cannot decode P4's v2 state.

## Performance

One mono delay ring, allocated only during prepare, power-of-two capacity covering 250 ms plus guard samples. At 48 kHz, 16,384 floats = 64 KiB; at 192 kHz maximum 256 KiB. Reprepare can retain earlier larger vector capacity. At 48 kHz, 128 freshly prepared insert rings are 8 MiB, excluding direct DSP, host, graph and overhead. Zero tap processing when off after transition; history stays warm. Six taps normally, at most twelve during 40 ms geometry crossfade. Latest requested geometry retained during crossfade, no unbounded queue. Source controls remain P1/P2 smoothed. Crossfades may briefly mix two locations and produce coloration; no measured claim of artifact-free automation. No callback allocations, locks, or per-sample transcendental math; rate-specific geometry computed once on the processing owner on coherent changed snapshot, consistent with P3. Allocation failure disables local reflection engine while direct processing stays active, status reports not prepared. Transport reset clears bounded ring/filter state. Conservative host tail estimate 0.4 s. No dynamic resource allocation in core P1/P2 Processor; early Engine owns separate history. Actual laptop load, offline export, seek and bypass tail behavior require acceptance.

## Tests

Standalone StudioSpatialEarly regression: geometry/mirrored gain and delay, rate consistency at 8/44.1/48/96/192 kHz, independent floor path, material and distance differences, no pre-arrival/direct copy, energy bounds, default off identity, P1 amount zero, mono monitoring, reset, invalid plans/rates, memory bounds, deterministic rapid geometry changes and disabling. Prints plan CSV (analytical path targets, not listening measurements). Retains P1/P2 checks. CI standalone optimized + ASan/UBSan on Linux/Intel macOS; full app CI enabled by P3. Hosted test covers migration, UI controls, actual output change, save/reopen and no shared Room Return creation. Do not claim native pass until results are observed.

P5 — shared late reverb improvements, damping and diffusion. P6 — spatial delay/modulation. P7 — calibrated physical seating, common room and routing with explicit migration. P8 — listening and optimization. M45–M51 still separate; no implicit integration.
