# M24 — Decent Sampler and readable legacy Kontakt

Open .dspreset, .dslibrary or .nki from the native sampler Load button or
drop a single definition on the sample display. The existing worker and
read-only review prepare a replacement instrument. Confirm or cancel;
source files remain unchanged. Save accepted mappings as .studiosampler.

## Decent
Supports XML group/sample inheritance, numeric MIDI root and ranges,
velocity bounds, gain (multiplicative global/group/sample, linear or dB),
pan offsets, tuning/key tracking, ADSR aliases, forward loops and trims,
one-shot envelopes, attack/release groups, single CC condition or single
channel, disabled groups, and complete matching-range round robins.
Native ADSR, velocity, pan and loop crossfade behavior approximate the
original. Unsupported trigger/selection/stretch/transpose rules are
omitted; incomplete RR fails. UI bindings, scripts, effects, generated
oscillators, tag/choke rules, dynamic modulation and advanced envelope
stages are not reproduced. FLAC reader and thumbnail registered when
JUCE_USE_FLAC is enabled, in addition to previous WAV/AIFF/CAF support.

Single-preset .dslibrary archives: bounded ZIP extraction to a unique
Studio imported media directory, allowed audio/preset extensions only,
path traversal, symlinks and duplicate paths rejected, size checks,
cancellation cleanup. 8192 entries, 512 MiB per file, 2 GiB total.
Multi-preset archives require manual extraction and .dspreset selection.
Keep accepted media for presets and undo. No automatic cleanup of
accepted media. No full ZIP integrity/CRC test claim.

## Kontakt
Initial subset for readable single-program K2_Container XML with
external WAV/AIFF/FLAC/CAF recordings. Recognizes plain XML and legacy
zlib (header within first 256 bytes) or gzip payload; bounded decoded
XML. Reads direct V attributes, key and velocity ranges, group/zone
gain, key tracking, simple release and loop modes, and sample paths.
Legacy XML variants with other layouts may fail. Pan/tuning and
endpoint semantics vary by writer, so tuning/pan adjustments are not
converted. Explicit known group-start/reverse conditions are omitted.
Unknown scripting, selection and other semantics may affect playback;
not a general Kontakt emulator. Modern NKI/NKM, embedded monoliths,
encrypted content and NCW are unsupported. No decryption or external
Kontakt dependency.

All paths preserve the 128-zone/16-group/64-voice limits, streaming
memory budgets, and transactional review. Definitions capped at 32 MiB,
decoded XML 8 MiB, XML depth 64 and tag count 50000; DTD/entity
declarations rejected. Missing sources reported. Source and staged
media must be kept available.

## Tests and build
Integration fixtures added for plain Decent preset inheritance, gain,
RR, CC, loops, streamed audio and preset restoration; single-preset
ZIP extraction/cancellation/traversal; compressed synthetic K2 XML
mapping and invalid NKI; cancellation. M24 application tests have
not yet been executed or verified. Linux/Intel macOS CI enabled.

M23 Linux job 139983274691 passed standalone streaming, SFZ parser
and playback, sound bank/legacy parser and EXS parser checks before
failing the full Studio build at five ambiguous StringPairArray
remove calls in inherited Apple GM code. M24 disambiguates those calls
using juce::StringRef; the correction is not yet verified in CI.
No earlier branch changed and no merges.

References:
- https://decentsampler-developers-guide.readthedocs.io/en/latest/the-groups-element.html
- https://github.com/JefroB/Multisample-format-docs/blob/main/nki.md
- https://docs.native-instruments.com/ni-tech-manuals/kontakt-manual/en/file-formats
