# Studio M29 — Visual instrument library and native Atelier

## Everyday browsing
Open Instrument Browser > Sample Library Manager. The browsing view
shows instrument names and families, favorites, name search, readiness
and a selected portrait. No thumbnails are decoded for every catalog
row. Native presets can be loaded directly; an available unconverted
source offers Prepare instrument, followed by the compatibility report
and an explicit Load as new track action. Double-click performs the
same enabled action. Open Manage library for folders, rescan, format
filtering, batch conversion, sample path repair and artwork assignment.
An empty catalog starts with management visible. Existing format
limits and source validation still apply.

Families: Strings, Woodwinds, Brass, Percussion, Keyboards, Guitars &
plucked, Voices, Electronic, Other and Unclassified. Whole-word name
suggestions are provisional; conflicting or unknown names stay
Unclassified. Explicit family edits and favorites persist through
catalog reopen and rescans. A favorite is a catalog entry, not a
duplicate instrument or duplicate sample allocation.

## Artwork
Assign a selected PNG/JPEG under 8 MiB. A managed copy with a unique
filename lives in ~/Music/Studio/Instrument Library/Artwork/ (OS Music
folder); original artwork path and copy token remain in Catalog.xml.
No original file or production catalog is modified. Existing M25
external artwork associations are retained; when loading a native
instrument an accessible legacy portrait can be copied to the managed
folder and its token persisted. No automatic artwork matching, new
artwork production IDs or claim that the earlier artwork collection
has been installed. Managed images are referenced by filename, not
embedded; include the Artwork folder in backups. Missing art has a
placeholder, never an invented instrument portrait. The Inspector
does not resolve arbitrary paths from projects.

## Native sampler Atelier
Native sampler tracks now use the same selected-track Atelier drawer
as hosted instruments. Library load writes name, family and managed
portrait token on that sampler's plugin state, before insertion.
The Inspector uses the sampler model's actual name and its mapped
articulations (up to eight buttons; all mappings available in Play).
Only mapped playable, unmuted groups are displayed. Keyswitch meter
indicates actual channel selection; CC mappings are sent without
claiming a persistent selected state. Mapping lists cache by sampler
instance and model revision.

Native controls send only supported live CC1 (vibrato/modulation),
CC7 (MIDI volume), CC11 (expression), CC64 (sustain) through the
existing selected track, subject to playback context, recording and
freeze restrictions. Channel volume, pan, room send and Stage links
reuse existing mixer parameters. Open Sampler opens M28's editor;
hosted AU original-editor and controller behavior retained. The
native Atelier uses a shorter controller section; the existing
drawer scrolls on small screens. This does not add new articulation
sounds, synthesis, microphone emulation, or artwork for every patch.

## Verification
Added fixtures for whole-word family suggestions, manual override,
favorite/family/format filters, managed portrait copy and unsafe
filename rejection, rescan and catalog persistence. Existing sampler
integration saves/reopens an Edit with presentation metadata on one
sampler and checks that another sampler has no portrait token.
Linux/Intel macOS CI enabled. Tests added; no successful M29 build,
actual project reopen or visual result confirmed at checkpoint.
M28 Linux and macOS workflows were still in progress when checked.
No branches merged; no new dependencies.
