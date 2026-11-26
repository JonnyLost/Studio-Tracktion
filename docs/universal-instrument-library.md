# Studio M25 — Universal Instrument Library

In the Instrument Browser collection menu select **Sample Library Manager...**.
Add specific folders (including external SSD libraries), scan, search by name,
format or path, and select one or multiple catalog rows. The catalog includes
native presets, SFZ, EXS, Decent Sampler, readable legacy NKI, SF2/SF3/DLS,
basic GIG and extracted Akai files. Sound banks are indexed by actual
program; unsupported or unreadable banks display their catalog error.
Indexed does not mean playable: the format-specific converter must succeed.

Convert selected creates native presets without changing the current
project. Up to 128 rows per job, processed sequentially on the worker.
Unavailable samples and invalid mappings fail the item. Compatibility
warnings and omitted regions reported by the existing format converters
remain in each conversion report; inspect these before loading. Completed
items remain after cancellation; no live tracks are added during conversion.
Repeated conversion reuses an existing native output. Rescan invalidates
the catalog conversion link if the source definition's size/time changes;
older converted presets/media are not deleted. Native files can be
cataloged directly. Load as new track prepares the native preset on the
worker, checks for unavailable samples, and inserts a native sampler on
a new track through the existing browser insertion path.

The default root is ~/Music/Studio/Instrument Library/ (OS Music folder):
Catalog.xml, Presets/<UUID>/ and Media/<UUID>/ for extracted audio.
External sample formats reference original recordings. Save and back up
these paths with projects; no automatic deletion of accepted media.
Catalog XML is saved atomically. Disconnected or removed library sources
remain in the catalog; a converted preset can still load if its recordings
are available. Rescan is manual. No continuous background scan.
64 roots, 100,000 scanned file entries, 50,000 catalog entries, 64 MiB
catalog file limit; existing format/zone/cache limits remain.

**Repair sample paths:** choose a native catalog entry and the folder
containing relocated recordings. Search is bounded to 20,000 file entries.
Only unique case-insensitive filenames, with no duplicate original
basenames, are considered; native decoder and sample boundary validation
must pass. A new repaired native preset is written, retaining the original.
The report lists candidate replacements. Filename/boundary matching is
not proof of identical audio; compare before using. For unsupported
or not-yet-converted definitions use format-specific import and manual
Locate Sample in the sampler. No automatic whole-drive search.

**Assign artwork:** choose a PNG/JPEG file for one catalog entry; the
manager remembers the external path and displays a preview. No guessed
instrument artwork assignments; the artwork catalog is not yet mapped
to the Atelier Inspector or embedded in projects. No image generation
or renumbering of artwork assets.

## Tests and build status
StudioSamplerIntegration includes a real SFZ/WAV conversion, overlapping
root deduplication, catalog reopen, artwork path persistence, repeated
conversion reuse, missing-source repair and native streamed preset
restore, offline root retention and cancellation.
Linux and Intel macOS CI enabled. Tests added, not yet confirmed.
M24 Linux compilation exposed the JUCE MessageBoxOptions withButton
signature and unqualified Array<File> in EXS resolution; M25 corrects
these inherited blockers. Full build and Mac playback not yet verified.

## Remaining work
Large library indexing and conversion throughput require actual MacBook
benchmarks. The catalog is a local XML index, not a database service.
No global sample deduplication across independent conversions, automatic
artwork matching, bulk relinking across an entire catalog, or full
Kontakt/GIG/legacy format fidelity. M26 should prioritize acceptance,
performance, limits and cache stress rather than introducing new formats.
No branches merged.
