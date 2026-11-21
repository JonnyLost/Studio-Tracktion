# M10 validation results

Runtime commit: `18ecb8538edcb59d4da46cff46053d20a3c71aa7`. Subsequent review assets and guide do not change application code.

| Validation | Result | Evidence |
| --- | --- | --- |
| Linux release build and complete CTest suite | 23/23 passed, none skipped | [Linux run](https://github.com/JonnyLost/Studio-Tracktion/actions/runs/44947285822) |
| Intel macOS x86_64 debug build and complete CTest suite | 24/24 passed, none skipped | [Intel run](https://github.com/JonnyLost/Studio-Tracktion/actions/runs/44947285800) |
| Native Swift seating checks | Passed | Separate seating-swift job in Intel run |
| Standalone sampler engine/mapping suites | Passed, including AddressSanitizer and UndefinedBehaviorSanitizer | Local Linux; leak sanitizer needed execution outside the ptrace sandbox |
| Changed wrapper/editor/integration translation units | Passed pinned JUCE/Tracktion header syntax checks | Local Linux |
| Native screen capture | Captured and visually inspected | [13-view gallery](README.md) |
| Intel app package | Packaged executable verified as Mach-O x86_64 | [Download app artifact](https://github.com/JonnyLost/Studio-Tracktion/actions/runs/44947285800/artifacts/14724169814) |
| Starter kit | Four valid WAVs, valid relative references, two groups/layers, valid loops; reproducible ZIP | [Kit](../../examples/Studio-First-Instrument.zip), [generator](../../../tools/generate-sampler-demo.py) |

The native integration tests cover generated WAV/AIFF decoding; key/velocity mapping; import proposals and cancellation; group persistence and assignment undo/redo; version 1 preset migration; source overwrite protection; sample trim playback; loops and one-shots; missing-sample relinking; keyboard/hosted MIDI playback and recording; preset/project save and reopening. Existing workspace tests exercise opening, floating, docking, dismissing and switching tracks during playback. The final review also verifies that the compact Performance panel remains visible after the detailed workspace closes.

## Intel performance measurement

The existing optimized benchmark's **Hybrid + Sampler** case combines a four-layer synth with a sampler, with a 32-voice capacity and 192,000 bytes of synthetic sampler PCM. These are combined benchmark timings, not a sampler-only CPU claim or a large-library memory stress test.

| Rate | Block | Mean | 95th percentile | Mean block budget | Deadline misses |
| --- | --- | --- | --- | --- | --- |
| 44.1 kHz | 128 | 401.17 us | 424.78 us | 13.82% | 0 |
| 44.1 kHz | 256 | 779.08 us | 807.12 us | 13.42% | 0 |
| 44.1 kHz | 512 | 1527.12 us | 1564.33 us | 13.15% | 0 |
| 48 kHz | 128 | 410.40 us | 438.30 us | 15.39% | 0 |
| 48 kHz | 256 | 796.68 us | 847.05 us | 14.94% | 0 |
| 48 kHz | 512 | 1646.51 us | 1732.24 us | 15.44% | 0 |

Audio rendering retains fixed voice arrays and immutable bank publication. New file enumeration, decode, model construction and serialization remain outside rendering. Decoded memory accounting counts unique shared sample buffers; duplicating zones does not duplicate decoded PCM. The original per-sample, bank, zone and voice limits remain in force.

## Practical limits

The shipped app is an unsigned Intel macOS 14+ development build. Validation used the Intel CI runner and hosted audio; no physical MacBook thermal, hardware MIDI or loudspeaker test was performed. Owner review on the target laptop is still needed.

Import enumeration/decoding can briefly pause the message thread. Groups organize layers but do not switch articulations. Acoustic pitch detection, disk streaming, portable sample-copy export, velocity crossfading, adjustable loop crossfade and group keyswitch/CC/release triggers remain deferred. The supported import/map/layer/audition/edit/save/reopen workflow has no known remaining blocker within the documented limits.
