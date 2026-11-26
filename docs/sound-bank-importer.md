# Studio M21 — Native sound-bank conversion

## Workflow

In Studio Sampler use Load, or drop a single .sf2, .sf3 or .dls bank onto
the sample display. Studio reads a catalog on its import worker. Search
the program name, zero-based bank/program number, or Percussion and
select one instrument. Studio then resolves and extracts only the chosen
program's source samples into a unique staging directory. Review the
mapping and compatibility report before replacing the current instrument.
Cancel or failure removes that conversion's uncommitted directory;
confirmation retains its recordings for native presets, undo and project
reopen. The original bank is not modified.

Accepted media lives by default in ~/Music/Studio/Imported Sound Banks/
(or the OS Music folder) with a unique child directory per conversion.
The native preset refers to those extracted WAV recordings, not to a
live external SoundFont player. Keep the extracted media with your
projects/backups. No global cross-import deduplication or automatic
garbage collection of accepted media is performed, because another
project or undo history may still reference it. Identical audio slices
within one conversion share extracted files and native Sample storage.

## Format coverage

- SF2: RIFF hydra preset/instrument/sample tables; preset and instrument
  global/local inheritance; intersected key/velocity ranges; PCM16 and
  optional sm24 low bytes; sample addresses and forward loops; root,
  tuning, attenuation, balance, ADSR and key tracking. Native envelope
  timing and velocity behavior are approximations.
- SF3: SoundFont tables and compression flag, compressed byte slice and
  sample-relative loop indices; JUCE's built-in Ogg Vorbis decoder
  converts mono compressed samples to 24-bit WAV when enabled. No
  separate FluidSynth installation required. Compressed sample address
  offsets and non-mono compressed streams are rejected.
- DLS: RIFF DLS instrument and region lists, MIDI bank/program and
  percussion flag, pool-table cue offsets, PCM 8/16/24-bit mono/stereo
  waves, region/wave wsmp unity note, tuning, gain and forward loops.
  This is a PCM-region subset, not a complete DLS Level 1 or 2 synthesizer.
- Native sample mappings use the M20 per-region parameters and the M19
  resident attack / bounded page cache for long extracted WAVs.

Unsupported/custom modulators, filters, effects, complex linked stereo
semantics, exclusive/choke groups, articulation graphs and some envelope
generators are reported as compatibility notes. Unsupported/corrupt
audio references, ROM samples, malformed tables, unsupported DLS loop
descriptors and oversized mappings fail without replacing the live
instrument. Linked SoundFont left/right regions remain separate mono
regions, with native balance; automatic pairing is not implemented.
Conversion starts at -6 dB instrument gain for layered playback.
No guarantee of identical sound to a dedicated SoundFont/DLS synthesizer.

Bank catalog and metadata read limits, 128-region / 64-voice native
limits, 512 MiB decoded WAV per sample, 2 GiB per instrument conversion,
and existing native memory budgets are enforced. No bank-sized PCM
allocation; WAV conversion is chunked on the import worker. Source
identity is checked between catalog and conversion and after extraction.
The bank folder has no relation to Apple's DLSMusicDevice Audio Unit;
this feature does not certify or fix M18R's Apple GM playback.

## References

- SoundFont 2.04 technical specification:
  https://musescore.org/sites/musescore.org/files/2023-01/sfspec24.pdf
- FluidSynth SF3 format notes:
  https://www.fluidsynth.org/wiki/SoundFont3Format/
- MIDI Association DLS Level 1:
  https://midi.org/dls-level-1

## Verification and acceptance

StudioSoundBanks covers synthetic SF2 catalog/global inheritance/range
intersection, 16/24-bit sample references, SF3 compressed metadata, DLS
FourCC/pool links/loop bounds, invalid links and truncated RIFF. The
StudioSamplerIntegration test adds actual PCM24 and DLS extraction,
Vorbis fixture creation/decoding when enabled, streamed sample checks,
native preset reload and staged-media cleanup. Linux and Intel macOS
CI are configured; results were not verified at the implementation
checkpoint. Local compiler could not retrieve repository source.

On Mac: use a new sampler track and known legally accessible test
banks. Search/select one instrument, review conversion, cancel once
and verify no replacement, then accept and play sustained and short
notes across key/velocity boundaries. Save/reopen and export audio.
Compare with a reference player, check Resource Inspector underruns,
and test each format independently. Do not merge until M18R through
M21 acceptance gates pass.
