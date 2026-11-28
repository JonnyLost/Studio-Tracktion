# L3 — Universal acoustic research laboratory

Independent companion to L1/L2 Audio Unit measurement host. Base P4 commit 35776eb401e1459106f04622c1784a9a95ae4b7e; P3 and P4 Intel/Linux full builds and P4 standalone DSP CI passed before L3 development. No production routing or reverb changes. No merge. No commercial presets, IRs, plugin code or extracted audio committed.

## Setup on Intel Mac

Python 3.9+ standard library only, C++20 compiler (Xcode command-line tools) for optional Studio baseline. No JUCE build, dependencies or plugin installs. Run these from the L3 checkout:

    bash tools/acoustic-lab/run.sh generate
    bash tools/acoustic-lab/run.sh baseline
    bash tools/acoustic-lab/run.sh analyze

Default directory Desktop/Studio-Acoustic-Lab-L3. Optional second argument selects a different folder. Generate refuses nonempty destination to protect existing measurements. Baseline rewrites its own studio_* WAVs and performance CSV; analysis rewrites report CSVs. Use separate folders for successive baselines. Native baseline directly exercises current SpatialReverb.cpp with all factory Room presets, M/L/R excitation at 44.1 kHz, 18 seconds, block 512, original factory gain, no host/mixer. CPU time excludes file writing but is offline wall time, not a real-time deadline or DAW benchmark. No P5 changes yet.

## Start with two Logic bounces only

1. New empty project, 44.1 kHz, one stereo audio track, import inputs/impulse_M.wav at zero. First second silent, one frame 0.25 amplitude in both channels at 1 s, then silence.
2. Insert ChromaVerb. Select Concert Hall algorithm. Dry 0%, Wet 100%, predelay 0, tempo sync disabled. Record remaining settings with screenshots; room type alone is not a complete preset. Track/master gain unity, no other processing.
3. Bounce exactly 18 seconds including leading silence and tail. Stereo WAV 24-bit PCM or 32-bit float, normalization off, no dither. Save as renders/chroma_concert_hall_M1.wav.
4. Repeat same bounce separately, without changing settings, as renders/chroma_concert_hall_M2.wav. Save screenshots/settings separately. Long decays may need extended range <=60 s. Offline bounce may reset modulation or behave differently from realtime; document which mode was used.
5. Analyze. Upload report folder ZIP plus settings screenshots. For further tests use cases.csv, which lists first five algorithm names, L/R isolated probes and repeat M takes. Any subset works, missing cases are listed and not counted as failures. Stereo dual-mono excitation is not the same as a mono source gain reference. No automatic Logic hosting because ChromaVerb is internal.

Generated noise_burst.wav is a deterministic one-second noise signal for listening and future noise-tail experiments, not an impulse response. Do not place noise renders in the IR analyzer. Sweep deconvolution, frequency-response/resonance FFT and instrument listening corpus are later extensions, not implemented in L3.

## ARIA research

Original generated stereo SFZ fixture plus L/R/M one-frame pulse WAVs. Notes MIDI 60/62/64, sample has 100 ms lead-in. This is a candidate fixture, not yet verified in installed ARIA. Load via normal documented SFZ import if available. Capture dry first (all reverbs off), verify calibrated signal, velocity, note duration, instrument processing, stereo routing and onset. Hold note >=2 seconds. Capture one room/reverb at a time, same MIDI and gain, record settings. If wet-only not available, retain dry reference and mark mixed capture; do not label as pure IR. No automatic subtraction/deconvolution or true stereo validation. Record actual dry excitation onset in cases.csv, not assumed note timing. Nine priority room cases plus dry calibration are included. No automated ARIA host or guaranteed SFZ compatibility.

## Reference library

Copy authorized local WAV/AIFF/SDIR files or ZIP archives into references. Input audio parsed with bounded RIFF WAVE (PCM 8/16/24/32, float 32/64, standard extensible subtype) and AIFF/AIFF-C (PCM, sowt, float) parsing. 1/2/4 channels, 8–192 kHz, <=60 s, <=96 MiB per file. SDIR accepted only if actual supported container. ZIP members read in memory, no extraction. Skip unsupported proprietary container; PWAV and WIR are inventoried but not decoded unless independently recognized as a supported standard audio container. No rights/access bypass. 4-channel measurements aggregate energy; channel mapping must be established separately. Input audio never copied into reports. Original uploaded archives were inspected: ChromaVerb 87 PST + 1 plist, Space Designer 20 SDIR, ARIA 40 ARIAP, 27 PWAV and 28 PNG; metadata inventory only, no commercial assets in repository. Existing L1/L2 standard rendered WAVs can be copied selectively into references, but only impulse outputs should be used for decay measurements.

## Measurements and limits

- Absolute onset (-80 dB relative amplitude, minimum 1e-8), relative onset when excitation supplied, sample format, duration, peak and integrated energy.
- 10 ms energy bins, Schroeder backwards integration, least-squares fit -5 to -25 dB extrapolated to 60 dB, fit R2. Estimates only, no noise correction; near-silent or short responses may have no fit. Last 100 ms energy fraction warning detects some truncation, not proof of complete tail.
- Approximate octave-band RBJ bandpass decay at 125/250/500/1k/2k/4k/8k where below 0.4 sample rate. Not IEC-certified filters. Early/late energy split at 80 ms from detected onset, not excitation. Stereo correlation for stereo only. Channel-one 20 ms Gaussian-normalized echo-density proxy sampled at 20/100/200/500 ms after onset.
- Repeatability CSV compares features and SHA-256 file equality; file equality includes container metadata and is not sample equality. No claim of recovered algorithm, IR equivalence, modulation rate or spectral resonance diagnosis.
- Inventory, measurements, envelopes, errors, missing case summary, native performance CSV. Analyze local files offline; output only CSV/JSON.
- Test fixtures validate impulse calibration, standard containers, finite decay, malformed/unsupported input, silence and report generation. CI checks actual native core render on Intel macOS and Linux. External products require user capture and verification.

## Next

L4 reference analysis and P5 native reverb architecture. Convolution development remains separate; authorized user-local imports, true stereo channel matrix, efficient partitioned FFT and shared late tail to be implemented later. No measured ChromaVerb/ARIA data claimed by L3 alone.
