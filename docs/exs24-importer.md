# Studio M22 — Logic EXS24 instrument conversion

## User workflow

Choose a single .exs file from Studio Sampler's Load button, or drop
one onto the sample display. Choose automatic resolution or select a
relocated recording folder (including an external drive). The import
worker reads and validates the binary mappings, resolves the external
recordings, and prepares a native instrument without changing the
current one. Review the playable, streamed, unavailable and omitted
zone counts and compatibility notes. Confirm replacement or cancel.
Save the result as a native .studiosampler preset. Original EXS and
audio files are not copied, changed or deleted.

The resolver tries explicitly selected/stored paths, then local sample
folders. It performs a bounded recursive search of at most 20,000
file entries, without following symlinks, only in the selected folder
or the definition folder and nearby Samples/Sampler Files/EXSamples
directories. A duplicate basename is not automatically assigned.
The importer checks nonzero source frame count, sample rate and channel
metadata against the decoded recording. Missing or incompatible files
are represented as unavailable zones; use Locate Sample or reimport
with the correct root. No automatic search of all installed libraries,
volumes or the home directory is performed.

## Conversion coverage

- Recognized TBOS/JBOS and legacy SOBT/SOBJ binary block markers,
  little/big endian fields, extended-size markers, legacy and longer
  filenames. Bounds, object references, duplicate IDs and region
  counts are checked before conversion.
- Root key, key and velocity limits, note tracking, coarse and fine
  tuning, zone and group gain/pan, sample boundaries, forward loop
  endpoints and crossfade times; one-shot and play-to-end-on-release
  behavior use the native sampler's corresponding controls.
- Group ranges, mute, release trigger, single MIDI CC range, one MIDI
  channel, and note enable mapped to native latched keyswitch (no
  assumed default). Complete round-robin groups with matching ranges
  are sorted by sequence index. Complex overlapping sequences, missing
  sequence members and unsupported conditions are not silently merged.
- WAV/AIFF and supported macOS CAF decoding through the existing JUCE
  audio-format layer. CAF uses Apple CoreAudioFormat on macOS, including
  the background waveform thumbnail. Long recordings use M19 streaming.
- Native v4 preset persistence with M20 per-region controls. No new
  dependency or preset schema migration in M22.

## Limitations

EXS is proprietary and not completely documented. This is a mapping
converter based on reverse-engineering references, not an emulation
of Logic's sampler. Real user libraries must still be tested. Logic's
global filter, synth parameter blocks, modulation matrix, Flex playback,
exclusive groups, group polyphony, velocity/key crossfades, output
routing and loop-specific tuning are not fully implemented. Equal-power
loop crossfade is approximated by the existing linear crossfade.
Note-enable groups use native latched switch semantics; round-robin
counters and release behavior can differ. Native default envelopes,
velocity response and -6 dB instrument headroom are used.

Reverse, non-forward loops, one-shot loops, random/fixed sample selection,
and unsupported tempo/bend/articulation-ID conditions are omitted with
warnings. Unsupported binary layouts (including plist-only mappings),
protected samples and embedded-audio-only instruments are not decoded.
No guarantee of full Logic Sampler/EXS24 compatibility. Limits remain
128 zones, 16 groups, 16 sequence groups, and 64 voices; definitions
over the limits fail rather than being silently truncated.

## Verification

- Exact GitHub blob IDs verified locally with git hash-object:
  StudioExs.h 2b55160ea39b5d4df87ddc19c144dfc6bb700e07,
  tests/ExsFixtures.h b5622263f0b2b71187ea6b51bcc5c37208dd9b87,
  tests/ExsParserTests.cpp e3d1c783bb9433fca14bd3829673364357782f68.
  All standalone synthetic parser tests passed using C++20 optimized
  compilation and AddressSanitizer/UndefinedBehaviorSanitizer on Linux.
  Covers endian and extended marker variants, both filename layouts,
  group constraints, forward loops, unsupported reverse flag,
  malformed lengths, links and zone limits.
- StudioSamplerIntegration adds a real WAV import with deep streamed
  loop, source matching, group CC and key/velocity constraints, native
  preset reopen, unique relocated source and ambiguous filename checks.
  Full application tests and CAF playback are not yet verified.
- Linux and Intel macOS workflows enabled; M22 is based on unmerged M21.

## Format references

Original C++ implementation based on publicly described EXS binary
field layouts, cross-checked against:
- https://github.com/asatamax/tonverk-elmulti-converter/blob/main/docs/EXS24_FORMAT_SPEC.md
- https://github.com/git-moss/ConvertWithMoss/tree/main/src/main/java/de/mossgrabers/convertwithmoss/format/exs
- https://github.com/esaruoho/paketti/blob/master/PakettiEXS24Parser.lua

On Mac: choose a small, accessible Logic EXS library, verify mappings,
play notes and chords, compare against Logic, save/reopen and bounce
to WAV. Repeat with a relocated library, CAF samples, release triggers
and round robins. Check resource inspector and disk streaming counters.
Do not merge until earlier milestone acceptance gates pass.
