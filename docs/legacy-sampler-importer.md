# M23 — Legacy sampler conversion, first supported subset

Open one .gig, .p1, .p3, .s1 or .s3 in Studio Sampler's Load dialog or drop
it on the sample display. The existing bank catalog and review workflow
extracts the selected instrument to native WAV media, with cancellation
cleanup and M19 streaming for long samples. Save the accepted result as
.studiosampler. The originals are never modified.

## Akai

Supports extracted S1000 .P1/.S1 and S3000 .P3/.S3 files, not SysEx dumps,
AKP, raw floppy, SCSI or CD-ROM images. Keep each extracted program and
its associated samples in the same directory. The program and sample
header generation is selected by extension, with exact length checks
(150 or 192 bytes). Program name, program number, keygroup key and
velocity ranges, signed 16-bit mono PCM, sample rate, root, tuning,
play bounds, one integer hold loop, one-shot mode and static pan are
converted. Tuning uses 1/256 semitone units. Only enabled velocity zones
are converted. Sample names use the internal Akai name encoding;
duplicate or unavailable references fail the entire proposal rather
than guessing or substituting. External sample size and modification
time are checked for each extraction read. Timed/fractional/multiple
loops, hardware-specific envelopes, filters, modulation, group/zone
crossfades, level curves, output routing and loop overrides are not
emulated. Left/right sample pairs are not automatically combined.

## GigaStudio

Supports the DLS-derived RIFF container, program catalog and **single
subregion** dimension tables (172/1092 byte layouts), reading the actual
3lnk wave cue and 3prg/3ewl wsmp rather than the fallback DLS wave link.
Uncompressed PCM mono/stereo with supported forward-loop metadata only.
Multi-dimension instruments (including typical velocity, round robin,
keyswitch or controller dimensions), compressed or split-bank audio,
GIG-specific synthesis/effects and scripts remain unsupported. This is
a first subset, not general GIG compatibility. Unsupported dimension
counts and missing dimension metadata fail without replacing the
current instrument. A .dls with GIG dimension markers follows the
same guarded path rather than silently flattening the dimension.

The 128-zone limit, existing cache limits and extraction disk budgets
remain. Accepted WAV media lives in the Studio imported sound bank
directory; back it up with native presets. No new external dependency.

## Verification

Synthetic parser fixtures exercise S1000/S3000 program and sample
headers, key/velocity ranges, tuning, loop bounds, PCM access, duplicate
names and malformed sample lengths, as well as both GIG table layouts
and rejection of unsupported multidimensional instruments. The GIG
fixture intentionally supplies a bogus DLS fallback cue. Integration
tests exercise native extracted WAV playback data, streaming and
preset reopening for a GIG and Akai program. Tests are registered
through existing StudioSoundBanks and StudioSamplerIntegration
targets. Linux and Intel macOS workflow triggers enabled. No test
execution or full application build has been verified at this checkpoint.

## References

- https://github.com/lentferj/mpc2emu/blob/main/docs/AKAI_S3000_FORMAT.md
- https://github.com/lentferj/mpc2emu/blob/main/parsers/akai_s3000_parser.py
- https://sources.debian.org/src/libinstpatch/1.0.0-7/libinstpatch/IpatchDLSReader.c
- https://github.com/Midi-In/akaiutil

## Follow-up

M23 needs real-library acceptance and a dedicated multidimension GIG
and Akai image filesystem extension. Do not claim support for all
GigaStudio or Akai libraries. Keep M18R–M23 unmerged until their
acceptance gates are met.
