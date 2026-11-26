# M20 — Native SFZ import

## User workflow

Open a Studio Sampler, choose its existing Load button, and select a
single .sfz file (or drag one onto the sample display). The source is
parsed and prepared on the existing import worker. A read-only report
lists the playable, unavailable, omitted, and streamed regions and
compatibility notes. Confirm "Replace instrument" to commit the new
mapping; cancel to leave the original instrument unchanged. Save as a
native .studiosampler file to retain the converted mapping, groups and
per-region playback settings. The source SFZ and original recordings
are never overwritten. The existing undo transaction is retained.

## Supported conversion

- global/master/group/region inheritance, control default_path, comments,
  bounded #include and #define, quoted and unquoted paths with spaces,
  Windows-style path separators, MIDI numbers and C4=60 note names.
- key/lokey/hikey/pitch_keycenter, velocity bounds, volume, tune,
  transpose, key tracking, static stereo balance, amplitude ADSR,
  velocity tracking and velocity crossfade endpoints (gain/power).
- offset and inclusive SFZ end/loop_end translated to native exclusive
  endpoints; explicit forward continuous/sustain/one-shot/no-loop
  playback; embedded forward WAV loop points when available;
  loop_crossfade translated from seconds to frames.
- attack/release regions, sw_last and sw_default, single-CC gating,
  unrestricted or single-channel gating; articulation names from
  sw_label/group_label. Complete identical-mapping sequence groups
  with seq_length/seq_position are sorted by position, then use the
  native round-robin group counter.
- WAV and AIFF decoding, repeated-source sharing within the instrument,
  M19 streaming, missing-source relinking, v4 native preset persistence
  (versions 1–3 still accepted).

## Limits and compatibility

This is a conversion importer, not a complete SFZ virtual machine.
Studio uses linear native ADSR, native pan/balance, and its own channel
volume/expression and release-trigger behavior. SFZ-specific dynamic
modulation, filters, custom curves, alternate loop directions, legato,
random/timer conditions, generated sources, protected audio, and complex
combined triggers are not implemented. Unsupported opcodes are listed;
regions with unsupported known gating or choke semantics are omitted
and reported rather than converted into uncontrolled layers. Other
unsupported opcodes can affect the sound even when a region is imported.
WAV forward-loop metadata is supported; AIFF implicit marker loops and
non-forward/fractional WAV loops are not converted. Native group sequence
counters differ from complex SFZ region counters. Imported ADSR controls
edit the selected region; the native velocity-step fade controls are
disabled on SFZ regions to preserve the imported endpoints.

There is a 128-region, 16 articulation-group, 16 sequence-group limit.
Larger definitions or incomplete round-robin groups are rejected without
silently truncating them. Text expansion is bounded to 8 MiB, 64 includes,
16 nested levels and 1024 macros. One instrument is imported at a time.
Only available WAV/AIFF samples are decoded; FLAC, OGG, encrypted
Garritan audio, and proprietary extensions are not supported. Missing
explicitly mapped samples are represented as unavailable zones; missing
round-robin members require source repair before conversion.
No claim of complete Garritan library compatibility.

SFZ format reference: https://sfzformat.com/ (headers and opcode definitions).

## Verification

- StudioSfzParser: inheritance, includes, macros, names, paths, bounds,
  aliases and malformed input. Exact GitHub parser and test blobs were
  reconstructed and SHA-1 verified in local Linux, then the tests passed
  with C++20 optimized compilation and AddressSanitizer/UBSan.
- StudioSfzPlayback: native voice velocity, pan, keyswitch and sustain-loop
  checks, registered in CTest and early Linux/Intel macOS CI steps.
- StudioSamplerIntegration: a real streamed WAV/ SFZ/include fixture,
  ordered sequences, missing samples and preset reload. Added but not
  yet verified on the full Studio application.

M20 is based on the unmerged M19 branch; both M18R and M19 are still
subject to their own acceptance gates. No branches were merged.
