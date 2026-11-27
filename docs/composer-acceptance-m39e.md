# M39E — Composer acceptance and cumulative stabilization

Base: M39D, 9d1edb1, all M39A–D changes and 242 vector assets. No new features; transport instrument-building idea deferred until after M40.

## Repairs and evidence
- M39B Linux compiled and passed 38/40. Floating instrument restoration passed. Two failures: StudioSamplerIntegration (preset persistence), StudioAdaptiveWorkspace (staccato phrase).
- Source review confirmed the sampler editor test changed its original two-zone fixture into a one-zone guided-builder instrument and returned without restoring it. Restore the original instrument after validating the builder and isolated catalog, keep exact parameter checks and extend to loop/zone boundaries. No production preset serialization changes without evidence.
- Adaptive workspace uses a real mouse draw test. The shorter dock can put the test's centre row on a pre-existing note; choose a free visible lane at the same beat, refresh after undo/redo, and separately diagnose selection and draw failures. Preserve keyswitch lead-time assertion. Hypothesis pending CI.
- Correct contrabass clarinet matching precedence; add assertion.
- Exercise all seven actual theme palettes and restore original without writing user preferences.
- M39D latest completed Linux run 45924048566 failed in apt update due to Microsoft package feed HTTP 403, before source compilation. Remove only known unrelated runner source and retain apt signature verification and retry. No weakening of checks.

## Physical acceptance gate
Build only from clean M39E branch. Use a disposable project first. No production project until acceptance.
1. Open Piano Roll, Stage, Mixer and Score; check default height and docking, resize and reopen.
2. Create instrument, sampler, synth and audio tracks. Browse a family and change sound on same track; retain MIDI, volume, pan, EQ, undo and redo. Confirm GM patch and independent sampler tuning on two tracks.
3. Inspector essential controls at normal MacBook resolution; compare keyboard and Inspector CC1/7/11/74/91; hardware MIDI and playback; distinguish CC7 from channel fader, CC91 from Stage send. Check no unexpected sustained notes on track switching.
4. FX two commands; add native EQ and existing AU; open/remove; retain protected Stage routing.
5. Switch seven themes including Light Studio; check legibility, all icons fit, preferences survive relaunch. Full catalog reconciliation/anatomical review is outstanding.
6. Metronome short and long clicks, downbeat 3/4 and 4/4, cycle hides/reappears preserving endpoints.
7. Create two-track sketch with MIDI and audio, edit, add articulation, set Stage room, mix, save/reopen, export WAV, listen. Film scoring sync and picture dock smoke test.
8. Check CPU/RAM and headphones on actual Intel MacBook. No quantified performance claims without measurements.

Acceptance requires green cumulative CI and real playback/user review. No release or merge. If a test fails, retain strict assertions, capture exact failure and repair before M40.