# Studio M19: native sampler disk streaming

## Architecture

Short WAV/AIFF files remain resident. Files longer than 131,072 source
frames (or beyond the resident budget) are candidates for streaming.
The engine uses source-frame addressing: 32,768 immutable preload frames
and six 16,384-frame decoded cache pages per streamed recording, with
one or two channels according to the source. This is 512 KiB for mono
and 1 MiB for stereo audio payload, excluding small object and UI overhead.
The existing 256 MiB per-instrument budget includes cache and preload.
Source length is independent of resident vector length. The old 64 MiB
individual decoded-sample cap is no longer a limit for streamed sources;
the current frame index representation supports fewer than INT_MAX frames.
The 128-zone / 64-voice / 16-group limits remain unchanged in M19.

A single shared worker processes bounded atomic page requests. A
24-entry seekable reader pool avoids retaining a file descriptor for every
recording. Sample pages are preallocated; real-time cursors pin cache slots
with bounded atomic operations and never call a disk reader, allocate, lock
a mutex or wait. Playback requests two pages of lookahead, including loop
heads near a wrap. A cache miss fades over 32 output frames and increments
the underrun counter; read failures are separately counted. Source length,
MIDI note pitch, ADSR, velocity, interpolation, stereo, loop crossfades
and note-release playback use the same voice engine as resident samples.
Trim/loop edits and preview ranges prime their start pages outside live
audio processing. Files are never rewritten by the sampler.

Offline rendering can synchronously fill a missing page. The regular WAV
and film cue/stem exporters check stream source identity and recorded offline
failures before declaring outputs complete. Film cue delivery removes
invalid output files in the new delivery directory and reports failure.
These safeguards are deliberately conservative.

The waveform editor uses JUCE's asynchronous thumbnail cache for streamed
recordings; it does not mistake the preloaded attack for the full sample.
The Resource Inspector distinguishes decoded resident audio, allocated cache,
logical decoded stream size, stream underruns and decoder errors. The sample
payload numbers are not a measurement of the entire process RSS.

## Verification

- `StudioSamplerStreaming`: compare live and offline streamed sample
  output against resident playback; exercise stereo, pitch transposition,
  loop crossfades, page boundaries, pin safety, worker disk I/O and failures.
- `StudioSamplerIntegration`: write and import a stereo WAV exceeding 64 MiB
  decoded, a streamed AIFF, seek near EOF, verify cache memory, preset reload,
  deep loop mapping and source disappearance.
- `StudioComposerBounce`: render a long note from a deep offset through a
  real Tracktion offline snapshot; check every window for audio and refuse
  export when a stream source disappears.
- Local Linux smoke harness using the exact `SamplerStream.h` blob:
  real file read on worker, bounded cache, pinned page consistency, offline
  seeks including final partial page, and failed-decoder handling passed
  normally and with AddressSanitizer/UndefinedBehaviorSanitizer.
  This is not a substitute for the full Studio build or the CTest suite.

## MacBook acceptance

Use a saved copy of a test project. Import a long WAV or AIFF file into Studio
Sampler (same import workflow); check the Disk streaming label, waveform,
and Resource Inspector. Play a long note beyond the preload and several
page boundaries. Repeat with chords, pitch wheel, deep trim and a loop
across page boundaries. Save/reopen, then export WAV and compare. Test
multiple native sampler tracks and external SSDs at the usual audio buffer
size. Check underruns, CPU callback load, memory, and read errors.

## Remaining limits

M19 does not import new instrument formats. There is no global deduplication
of identical files across independent sampler plug-ins, no adjustable cache
size UI, and no proven 64-voice/many-track SSD throughput target yet.
Extreme transposition, unusually diverse offsets into one recording,
slow/removable drives and high concurrent demand may exhaust a six-page
cache. The Resource Inspector exposes this rather than promising dropout-free
playback. Full macOS/Intel compilation, audio latency, export and stress
testing are acceptance gates. M19 branches from the unmerged M18 checkpoint;
it does not merge or certify the Apple GM repair.
