# Garritan Library Import

Open **Instruments → Import Garritan Library…** and select the top-level
directory containing the installed library's definitions and samples. Selecting
the entire library lets the scanner resolve relative paths between its
subfolders. Studio reads the selected directory in a background worker.

The window lists the discovered definitions and their compatibility. **Show
JSON Report** reveals a saved inventory in Studio's settings directory. The
inventory contains relative library paths and source mapping text, which may
identify private installations or instruments. Review it before sharing.

- **Fully supported** means the recognized SFZ mapping fits the native sampler
  and all sample headers are readable within its limits.
- **Partially supported** lists the approximations or source opcodes left out of
  native playback. Review those rows first. Select **Include partial
  conversions** to include them in a batch.
- **Unsupported** entries have no importable mapping, a missing file, a
  protected or unreadable sample format, or a playback condition Studio cannot
  represent. Open these through the original licensed ARIA installation when
  available.

Use **Import Supported** to start the batch. The importer validates each
instrument by decoding its samples before writing a native `.studiosampler`
preset. **Cancel** stops at the next cooperative checkpoint. Presets that
finished before cancellation remain available. Repeating a batch uses new
preset filenames where names already exist.

Converted presets appear in **Instruments → Imported Libraries**. Select a
preset and choose **Add as New Track** or load it on an eligible selected track.
Keep the original folders in place. Presets reference the original audio files
and Studio reuses decoded audio for repeated zones in the same instrument.
Deleting an imported preset from the Studio settings directory removes the
browser entry after reopening Studio. Saved projects keep their sampler mapping
and sample references.

## Compatibility

| Source feature | Native handling |
| --- | --- |
| Readable SFZ | Inline control, global, master, group and region sections. The inventory records effective inherited opcodes. |
| Samples | Mono or stereo WAV and AIFF. Local relative references inside the selected root. The PCM is never copied into the catalog. |
| Pitch and velocity | Root key, note and velocity ranges, offset, inclusive end frame, static gain and tuning. Note names use C4 for MIDI 60. |
| Articulations | Latched `sw_last` with a representable default, labels, or one CC range per playback group. Native CC7, CC11, sustain and pitch bend remain available. |
| Release / round robin | `trigger=release`, one-shot playback and complete `seq_position` sets with matching zone ranges and group gates. Each native sequence set has its own counter. |
| Loops | Forward loop points from SFZ or a single infinite forward loop in WAV metadata. Sustain loops continue through the native release envelope and are marked partial. Other embedded loop types need explicit compatible SFZ points. |
| Envelope | Shared ADSR. Differing region ADSRs use the first region's envelope and are marked partial. Arbitrary controller curves, filters and modulation are retained in the report. |
| ARIA banks and legacy presets | Inventory records recognized opaque definitions and encoded sample extensions. Conversion requires readable SFZ mappings and accessible PCM. Includes, macros, alternate switch modes, multiple CC constraints, sample chokes and unsupported selection conditions require the original player. |
| Resource limits | 128 zones, 16 playback groups, 16 sequence sets, 64 MiB decoded per sample and 256 MiB per instrument. Discovery reports its 50,000-entry and 512-definition limits. |

Use only libraries whose license permits the access and reuse you intend.
Studio reads standard files through its existing audio decoders and leaves
original samples, definitions and presets in place. It does not decode
encrypted bank containers or extract content from ARIA.

## Inventory format

Schema version 1 is a JSON object with the selected library's folder name,
`samples`, `instruments` and scan-level `unresolved` messages. Each
instrument has a relative `source`, `name`, source `format`,
compatibility label, unresolved requirements, estimated unique decoded PCM
bytes and its `regions`. Each region includes effective `opcodes` and the
resolved sample path relative to the library, when available. Sample entries
list their format and file size, with a candidate flag for native header
probing. Discovery classifies a format candidate by extension. Definition
checks and conversion validate the actual samples separately.

## Test on your Intel Mac

1. Download the Intel importer artifact from the draft PR's macOS Actions run.
   Extract the ZIP, open **Studio.app** and use a new test project.
2. Open the importer, select the root of one installed Garritan product and let
   the scan finish. Use **Show JSON Report** to review missing dependencies,
   unsupported definitions and partial conversions.
3. Import the fully supported instruments, load one from **Imported
   Libraries**, and compare its low/high velocities, articulation changes,
   expression and note releases with the same instrument in ARIA. Save, quit
   and reopen the project. Check that the native preset still resolves all
   samples.
4. Cancel a fresh scan or batch while transport is playing. Check that playback
   continues and that the report or completed presets match the cancellation
   point. Check Activity Monitor while loading a large eligible instrument.
5. Record which product and edition you used, whether the source contains
   readable SFZ and PCM, and a brief result for each tested patch. Use synthetic
   fixtures when filing public reproductions.

CI exercises generated PCM and synthetic SFZ with no proprietary libraries.
Compatibility with specific installed Garritan editions and audio continuity
under a large real-world scan require the local Mac check.
