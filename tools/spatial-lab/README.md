# Studio Spatial Laboratory L1

Standalone native macOS AUv2 offline reference measurement tool. Requires Xcode Command Line Tools or Xcode, and the locally installed, validated Panagement 2.8.0 AU (aufx / @s22 / Aubn). No CMake, JUCE, Studio build, sound card, or live DAW required. No binary or proprietary source code from the reference plug-in is included. The program creates fresh local instances of an installed effect, uses its public parameter interface, and records its response to original synthetic stimuli. It neither saves plug-in state nor alters the installed component or Studio projects.

## Run
From a Studio repository on the laboratory branch:
```sh
bash tools/spatial-lab/run.sh
```
Or fetch just the lab from the development branch without changing your current checkout:
```sh
cd "$HOME/Developer/Studio-FilmScoring-M1" || exit
git fetch origin feature/studio-spatial-lab-l1 || exit
LAB="$(mktemp -d)" || exit
git archive FETCH_HEAD tools/spatial-lab | tar -x -C "$LAB" || exit
bash "$LAB/tools/spatial-lab/run.sh"
```
The launcher creates a unique folder and matching ZIP on the Desktop, then reveals the ZIP in Finder. Upload the ZIP, even if a test or build failed. No sudo required. No automation of external network activity other than the explicit git fetch above. The folder may contain tens of megabytes of float WAV files; ZIP compression varies. All generated audio is synthetic and not an orchestral library recording. The report contains macOS version/architecture, AU metadata, parameter values, output metrics, WAVs and diagnostic messages; compiler errors may contain local filesystem paths. No serial numbers, full preset state blobs, audio from a microphone, or personal music projects are collected.

## Experiment design
Strictly validate the 44 ID/name/range entries and the 2.8.0 component version against the supplied AU validation report. Capture all available formatted display values at 11 normalized points per parameter and read every exposed parameter for each factory preset. Preset enumeration expects 14, with errors reported rather than pretending success. Do not infer engineering units from normalized values. The test program does not require or infer the edition of the installed plug-in.

76 planned cases:
- Default impulse and sweep; global amount zero reference.
- Nine pan positions; independently vary three panning coupling controls at left/right endpoints.
- Five distance values, stereo width endpoints/intermediate values with independent left/right inputs, and five tilt values with coherent multitone stimuli.
- Pan endpoint tonal response, mono-check, center and endpoint sweeps.
- Distance/reverb interactions, decay settings and distance coupling extremes; one delay-enabled case with default sync.
- Center/left/right impulses at 44.1 and 96 kHz; primary rate 48 kHz.
- Native mono-to-stereo and stereo-to-stereo routes. Signals begin after 250 ms of silence. Each measurement creates a new AU instance; setting requests and actual readbacks before/after the render are exported.

All audio output is IEEE Float32 WAV with no normalization/clipping, and the matching source WAV is preserved. Use the data to design original DSP, not to extract the reference algorithm. Stimuli do not determine the proprietary topology. Onset/peak differences are descriptive metrics, not proof of interaural timing or an HRTF. Early/late energy uses a provisional 50 ms boundary and finite tails; it is not a certified RT60 or absolute early reflection separation. Tone output is measured in a 1 s coherent window; no linearity assumed. Sweeps are exported as audio, not automatically deconvolved. Repeatability across modulated/randomized instances is not established. No host tempo callbacks: synchronized delay and LFO measurement are deferred. Offline wall time is not real-time CPU load. Explicitly controlled normalized settings are not asserted to isolate individual effects perfectly; compare actual readbacks and results.

## Output
- manifest.txt: conditions, completion count and limitations.
- parameters.csv: exposed ID, name, range, default and initial readback.
- display_values.csv: optional host-provided strings at normalized steps.
- presets.csv: actual values following selection of each factory preset, or failure row.
- case_parameters.csv: requested values and actual before/after readbacks.
- measurements.csv: per-channel onset, peak, RMS, energy, impulse early/late energy, correlation, reported AU latency/tail and offline elapsed time.
- tones.csv: multitone input/output amplitudes at 125, 250, 500, 1000, 4000 and 8000 Hz.
- input/, audio/: matched raw float WAVs.
- errors.csv, run.log, exit-code.txt: diagnostic evidence. A nonzero exit is not an acceptance pass. Incomplete runs can be analyzed, but failed cases must not be treated as valid measurements.

## Verification
Native Intel macOS workflow compiles the same source, checks launcher syntax, runs built-in synthetic self-tests and sanitizer tests, and attempts an offline render with an Apple system low-pass AU if available. Third-party plug-in tests require the user's own installation. S1 is the base; no live Studio code, routing, project schema, branches or commercial plugin binaries modified. Future L2: repeatability, time-frequency analysis, deconvolution, controlled tempo/transport callbacks, envelope/decay analysis and Studio Spatial comparison adapter.
