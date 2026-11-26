# M27 — consolidated sampler acceptance candidate

M27 is a descendant of M26, which includes M18R through M25. It is a
single checkout for testing, not a merge or a release. Existing branches
and source sample libraries are unchanged. No new sample formats added.

## Verified baseline and M27 changes

At the checkpoint, M26 Linux run 45846214336 completed the standalone
streaming, SFZ parser/playback, SoundFont/GIG/Akai parser, EXS parser and
M26 performance steps successfully (including ASan/UBSan where configured).
Its full application build was still in progress; Intel macOS run
45846214327 was queued. These are baseline results, not M27 pass claims.

M27 preserves completed library batch results when closing the manager
(after cancellation and worker join), defers new-track insertion beyond
dialog dismissal, revalidates existing converted presets before returning
success, and rejects changed source definitions until a rescan. Regression
fixtures cover missing audio and changed definitions. Existing engine,
articulation and streaming tests exercise indexed dispatch; the guarded
allocation test builds its index outside the guarded render operations.
The workflows run these tests before the full application build and
retain complete CTest reports. Same-ref superseded runs are cancelled.
No claims that every importer works on real libraries.

## Acceptance gates

1. Both Linux and Intel macOS application builds compile. Standalone tests
   and full CTest pass; inspect any skipped tests rather than treating them
   as successful playback. Apple DLSMusicDevice playback is macOS-specific;
   a skip for unavailable Audio Unit is not a pass for M18R.
2. Native sampler: import WAV/AIFF, load/save/reopen a native preset,
   use key/velocity layers, CC, keyswitch, sustain, releases and round robin.
   Render a long sample and compare live/offline behavior. Move a sample
   and confirm a missing source is reported; relink without changing original.
3. Import small accessible known-good SFZ, SF2, SF3, DLS, EXS, GIG, Akai,
   Decent and legacy NKI fixtures independently. Compare audible mapping,
   pitch, loops and envelopes against a reference player where available.
   Unsupported variants must fail/report limitations, not silently claim
   faithful conversion. Protected audio and modern Kontakt are not supported;
   GIG multidimensions and Akai disk images remain future work.
4. Library: scan a specific directory, search, batch convert, cancel and
   close/reopen, confirm completed outputs retained. Load on a new track.
   Remove/reconnect an external drive; repair a preset and verify original
   unchanged. Reject ambiguous replacements and changed source definitions.
5. Apple GM: select two distinct instruments on separate tracks, verify
   patch changes and pitch independence, including project reopen. Recheck
   original user's detuning report on Mac hardware.
6. Regression: recording, MIDI editing, mixer, stage/rooms, synth, film
   scoring, project reopen and WAV bounce. Use a copy of a known-good
   project, not the only copy of a composition.
7. Performance: at actual device rate/buffer, gradually add 4/8/16/32
   sampler tracks, starting with repeated recordings then distinct files
   and divergent loop positions. Log callback load, process CPU/RAM, stream
   misses/read errors, voice counts and audible dropouts on internal SSD
   and external sample drive. 768 MiB shared tracked PCM/cache and 256 MiB
   per-instrument limits are not total application RAM. Synthetic engine
   tests do not certify real-time DAW throughput.

## Local Intel Mac verification

From the existing checkout, preserve local edits and close any running
Studio instance before changing branches. Fetch the M27 branch, switch
to it, and ensure a fast-forward-only pull. Reconfigure the existing build
directory (retains the configured Tracktion root). Run:
```sh
/Applications/CMake.app/Contents/bin/cmake -S . -B build -DBUILD_TESTING=ON
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug --parallel 4
/Applications/CMake.app/Contents/bin/ctest --test-dir build -C Debug --output-on-failure --no-tests=error
open build/Studio_artefacts/Debug/Studio.app
```

No release promotion, merge, or deletion of earlier branches until CI
and user acceptance have been explicitly reviewed. M27 is a test
candidate until those gates pass.
