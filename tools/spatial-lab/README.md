# Studio Spatial Laboratory L2

Standalone native macOS AUv2 offline reference measurement tool. Requires Xcode or Command Line Tools and locally installed Panagement 2.8.0 (aufx / @s22 / Aubn). L1's user run completed 76/76 cases with zero reported failures. L2 builds on that validated host, but its new host callbacks and measurement matrix require a fresh native run. No CMake, JUCE, DAW rebuild, sound card or project required. Only public plug-in parameter interfaces and synthetic stimulus recordings are used; no proprietary source, executable, license data or private preset state is included.

## Run without switching the current Studio branch
```sh
(
  set -o pipefail
  cd "$HOME/Developer/Studio-FilmScoring-M1" || exit
  git fetch origin feature/studio-spatial-lab-l2 || exit
  LAB="$(mktemp -d)" || exit
  git archive FETCH_HEAD tools/spatial-lab | tar -x -C "$LAB" || exit
  bash "$LAB/tools/spatial-lab/run.sh"
)
```
The launcher compiles, self-tests and measures the installed AU. A unique Desktop folder and matching ZIP are produced even for build/test failures, and the ZIP is revealed in Finder. Upload it. Expect more data than L1: roughly hundreds of MB uncompressed, depending on samples and duplicate source recordings; compression depends on the actual output. Reserve at least 1 GB free on the Desktop volume. No audio is played through speakers. The launcher collects macOS version/architecture, public AU metadata, synthetic audio and diagnostics (compiler messages may include local paths). No personal music or microphone recordings.

## 132 planned experiments
L1 formatted values established that Far ranges from -24 to +6 (not metres), with .8 displaying zero. L2 uses .8 as the dry reference and sweeps the entire range without assuming the direction or level law. Pan coupling 0 / .5 / 1 displays 0 / 100 / 200 percent. Room algorithm selector values 0 / .25 / .5 / .75 / 1 correspond to Dark / Concrete / Wood / Heaven / Cave; verify actual formatted readback in every case. The delay and modulation sync labels below are requested from L1's observed display values, not guaranteed audible modes.

- 1 overall amount-zero reference.
- 32 distance cases: 21 multitone steps and 11 impulses.
- 45 isolated timing cases: 5 pan positions, 3 timing coupling values, 3 sample rates (44.1, 48, 96 kHz), level and spectral coupling requested zero.
- 12 level-only and spectral-only multitone cases.
- 6 fresh-instance timing repeats and 3 short-settle comparisons against 2-second-settle references.
- 15 late-room responses: all five room algorithms twice at normal decay (14-second post-excitation), once each at maximum decay (36-second post-excitation).
- 3 manual predelay requests.
- 12 cases at 60, 120, 180 BPM: free and quarter-note delay, eighth-note reverb predelay, and quarter-note sine pan LFO.
- 3 free-running LFO and non-modulated carrier cases.

A fresh AU instance per case, 512-frame processing blocks, 2-second silent settling except the short-settle controls. Per-case exact settings, readbacks, signal, sample rate, timing and files are recorded. Tone carrier envelope uses 440 Hz, 6-second excitation; distance/filter uses 125/250/500/1000/4000/8000 Hz multitone. L2 does not replace the L1 sweep/width corpus.

## Host timing
Use public AUv2 HostCallbackInfo beat/tempo, musical time and transport callbacks; 4/4, playing, no recording/cycle, sample counter advanced with each render block. Registration status and invocation counts are logged. Apple API reference: https://developer.apple.com/documentation/audiotoolbox/hostcallbackinfo
The reference effect can reject or ignore these callbacks. Registration failure does not stop unrelated tests, but a silent echo or absent tempo query cannot establish tempo sync. Compare free-delay and synced-delay at all BPMs, against raw impulses and readbacks; a selected sync mode does not prove a feature is enabled in the installed edition. Callback queries are counted cumulatively including initialization, not just rendering.

## Output
L1 CSVs and original Float32 WAV format retained, with per-case source onset added in conditions.csv. File paths refer to unique input WAVs including excitation offset and to the measured stereo output. No normalization. Additional tables:
- conditions.csv: excitation offset, BPM, host callback registration OSStatus and call counts.
- timing.csv: signed right-minus-left normalized cross-correlation lag in ms (bounded +/-3 ms search), peak correlation score. Parabolic interpolation is an estimate, not independently verified physical ITD. Silent, ambiguous or boundary cases need inspection; filtering can bias correlation even with coupling controls requested zero.
- decay.csv: stereo reverse-energy T20 extrapolation from -5 to -25 dB, fit R-squared and energy remaining in the final second relative to total. Not certified RT60; no noise floor compensation. Long tails may still be truncated.
- envelope.csv: 10 ms RMS envelopes for modulation and delay/predelay cases; inspect raw samples for exact echo times.
- parameters.csv, display_values.csv, presets.csv, case_parameters.csv, measurements.csv, tones.csv, manifest.txt and errors.csv remain. Onset and peak indices are absolute file samples; subtract settle_s * sample rate for excitation-relative timing. Read errors and actual readbacks before using measurements. NaN means unavailable/undefined, not zero. Offline wall time is not DAW CPU utilization.

## Verification
Intel macOS CI compiles the same program, tests synthetic timing/decay/envelope/clock behavior and WAV output, runs sanitizer self-tests, and attempts a system Apple AU smoke test if present. Native CI does not have Panagement. Actual plug-in measurement results are available only after the user's local run. L1 branch and live Studio source remain unchanged. No merge. The lab supports original, independent DSP design; it is not an attempt to reconstruct proprietary implementation or bypass paid features.
