# Milestone 10 native sampler review

Capture status: pending Intel macOS CI verification. No mockups or commercial recordings are used. The integration test generates synthetic WAV and AIFF tones; these demonstrate the builder, not a sampled piano's sound quality.

The sampler integration review produces these genuine native JUCE editor renders on the Intel runner:

| Image | What it demonstrates |
| --- | --- |
| sampler-empty.png | Empty sampler and import prompt |
| sampler-mapping.png | Multiple mapped samples and selected-zone controls |
| sampler-velocity-layers.png | Two discrete velocity layers |
| sampler-groups.png | Named Piano Soft and Piano Loud groups |
| sampler-waveform.png | Non-destructive sample boundaries and enabled loop |
| sampler-builder-basic.png | Envelope, volume, root and audition keyboard |
| sampler-reopened.png | Saved synthetic instrument reopened in the editor |

The existing StudioInstrumentWorkspace test additionally captures sampler-docked.png, sampler-floating.png, sampler-mapping.png and sampler-loops.png in the instrument workspace artifact. It exercises playback during Float/Dock and editor dismissal. These captures are application component renders, not OS desktop screenshots. The complete requested twelve-view, full-resolution gallery remains a delivery gate until captures have been downloaded and inspected.
