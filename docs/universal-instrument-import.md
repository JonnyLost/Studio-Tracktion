# Universal instrument import in Studio 0.2

Open **Instruments → Import Instruments…**. This window extends the Garritan
workflow from PR #48, which was verified on top of the integrated Studio 0.2
baseline. The inspected repository and branch history contained an SFZ reader
and the Studio Sampler. Kontakt, Giga and Akai appeared as recognizable file
types, with no separate reader implementation to retain or replace.

## Import and reconstruct

- **Import Instrument** inspects one definition or bank. **Import Library Folder**
  scans a user-selected folder recursively. Select an entry to inspect its
  mapping and limitations, then use **Convert Selected** or **Batch Convert**.
  Converted presets appear under **Instruments → Imported Libraries** and open
  in the existing Studio Sampler editor.
- **Build from Samples** proposes note ranges and velocity layers from filename
  tokens such as `Violin_C4_v1-63.wav`, embedded root-note and forward-loop
  metadata, relative folder paths and sample lengths. It uses C4 for MIDI 60.
  Unknown pitches receive provisional roots, and overlapping layers are
  called out. Recognized articulation and microphone labels create editable
  organizational groups, with no inferred trigger or routing rules. Approve
  the proposal explicitly before converting. Adjust roots,
  velocity ranges, layer organization and playback in the Sampler editor.
- **Locate Missing Samples** rescans a selected definition with the folder you
  choose as its sample root. EXS can also locate a unique filename anywhere in
  that folder. Ambiguous matches require a narrower selection. **Relink Studio
  Preset** makes a new preset for an existing converted instrument after a move,
  using its saved root and exact relative paths. Open the new preset and
  resave a copy of your project. Existing project files and the old catalog
  preset stay available.
- **View Compatibility Report** reveals schema v2 JSON. It includes the selected
  library, relative source paths, variants, source opcodes where available,
  dependencies, mapping, envelope, compatibility, memory estimates and unresolved
  requirements. The report can include private filenames or metadata.
- **Cancel** stops scanning or conversion cooperatively. **Rescan / Resume**
  enumerates the folder again and reuses completed cached mappings when their
  definition, dependency size/time and the audio file listing still match.
  The application retains that checkpoint across restarts for the same
  selection. Resuming a batch reuses matching catalog presets and keeps
  previously completed conversions. Duplicate sample filenames are reported
  without assuming their audio is identical.

## Format compatibility

| Format | Implementation and limitations |
| --- | --- |
| Studio `.studiosampler` | Readable native presets can be inventoried, checked and registered. Existing versions 1–3 load as before. Version 4 carries SoundFont PCM references when needed. For native presets referencing a different tree, use **Locate Missing Samples** to choose the relevant root. |
| SFZ and readable Garritan SFZ | Existing SFZ handling covers scoped key/velocity ranges, keyswitches and a single CC gate per group, complete sequence sets, release samples, sample boundaries, forward loops and shared ADSR. PCM/FLAC/macOS CAF sources use the same decoders as the sampler. Multi-condition mappings, includes, macros and protected sample content still require the original player. |
| SoundFont 2 `.sf2` | Native PCM reader enumerates presets and reconstructs key/velocity zones, mono samples, transposition within the native range, attenuation, shared volume ADSR and forward loops. The sampler reads referenced PCM slices directly from the source bank. ROM and stereo-linked samples are currently unsupported. Custom modulators, unsupported generators, unequal envelopes and 24-bit precision reduction are reported. SF3 compressed audio is outside this reader. |
| EXS24 and Logic Sampler `.exs` | Experimental reader for readable big- and little-endian EXS records with referenced samples. Translates note/velocity ranges, tuning/gain within native limits, slice boundaries, one-shots and forward loops/crossfades. Group processing, Flex, per-zone routing, additional tails and some playback modes require review. Reverse and fixed-pitch zones stay in Logic. Logic patches/packages with other layouts are catalogued when identifiable. |
| WAV, AIFF, FLAC, CAF | Standard mono/stereo files open via the existing native sample import and folder builder. CAF uses macOS Core Audio. Embedded forward WAV loop markers and WAV/AIFF root-note metadata can inform proposals. Recognized extensions can still contain unreadable or unsupported encodings. |
| Decent Sampler `.dspreset` | Experimental standalone XML subset for sample paths, inherited note/velocity ranges, start/end and forward loops. Bindings, filters, effects, group conditions and other player behaviors require review. Archives stay in their original player. |
| Kontakt `.nki`, `.nkm`, NCW and containers | Inventory identifies known extensions. Native conversion is unavailable for these containers. Use exported PCM and a readable mapping where your license and player allow it. |
| GigaSampler/GigaStudio `.gig` | Recognized in the inventory. Giga dimensions and sample-container reconstruction need a validated reader and real fixtures. |
| Akai S1000/S3000 | Recognized loose program/image extensions are listed when present. Disk filesystem traversal and native program conversion need validated disk revisions and fixtures. |
| Garritan ARIA banks | Readable SFZ plus accessible samples can use the SFZ path. Opaque banks and protected sample containers stay with licensed ARIA. |
| Ableton `.adv`, Reason `.sxt`, HALion, SampleTank, UVI | Recognized extensions are catalogued. These formats require validated native readers or accessible exports. Generic plug-in presets and disk images cannot be reliably attributed to a vendor by extension alone. |

The EXS layout was investigated using the published field descriptions in
[exstools4’s EXS format notes](https://github.com/jonkubis/exstools4/blob/main/export_as_exs24_implementation.md).
The SoundFont subset follows the RIFF sfbk table and sample-chunk arrangement.
Reader behavior has synthetic regression coverage. Compatibility by commercial
product or revision requires instruments on your Mac.

## Performance and source handling

Scanning, conversion validation and catalog preset loads run outside the audio
callback. Scans inspect headers and mappings without decoding full sample banks.
The index has a 48 MiB on-disk limit and scans cover at most 50,000 entries
and 512 instrument definitions. Native playback keeps the established RAM bank
limits of 128 zones, 16 playback groups, 64 MiB per decoded sample and 256 MiB
per instrument. Identical references within a bank share decoded sample memory.
Disk streaming and memory-mapped realtime playback are deferred. Very large
multi-articulation banks can exceed the native limits and should be tested with
smaller source presets or the original player.

Conversion writes only to Studio's catalog. Source audio stays in its library
or bank, and presets store source provenance. Studio does not circumvent source
player protections. Use source instruments and exports your license permits.

## Validate on your Mac

1. Download the Intel preview linked from the draft PR, extract it and open
   **Studio.app**. Use a fresh test project and a backed-up or read-only source
   library. For Apple Silicon native testing, build Studio and its pinned
   dependencies for arm64 using the existing CMake setup.
2. Scan an SFZ library, a SoundFont and a legacy EXS instrument. Export the
   compatibility report, then convert one preset from each appropriate class.
   A patch that needs another player should remain in that category.
3. Compare playable key ranges, velocity layers, loops, switches and note
   releases against the source player. Save and reopen the project. For a
   relocated test library, use **Relink Studio Preset**, then check that the new
   instrument still plays from the external drive.
4. Cancel and resume a larger scan during playback. Compare the report and
   sample-memory estimate with Activity Monitor, and note the product, format
   variant, architecture, and any playback or offline render differences.
