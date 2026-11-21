# Orchestral Seating Designer

On macOS, open **Stage → Seating Designer** or find **Seating Designer** under **More**. Open or create a project first. Search by instrument name, family, or volume, such as `muted trumpet v2`. Drag a result onto the stage or use its **+** button for a suggested position. Choose **Modern Symphony** to load a 24-placement modern orchestra, with first and second violins together on the conductor’s left.

Click a placement to select it and drag it to move it. The inspector edits the label, section, left/right position, and depth. Use **Use Existing Section** to group a seat with other placements. Use **Remove Seat** in the inspector or the seat’s context menu. On narrow windows, **Inspector** and **Library** switch the side panel. Use Studio’s workspace divider or **Expanded** for more room. Studio’s project Undo and Redo include seating actions.

The conductor is at the bottom of the stage. Left/right coordinates run from −1 to 1, and increasing depth moves a seat away from the conductor. The stage constrains seat centers to a slightly inset semicircle. Coordinates are independent of window size, and the seat labels may be changed independently of library entries.

## Inventory sources

Studio uses VSL’s current **Studio Special Editions** documentation, which identifies the instruments as the former **SYNCHRON-ized Special Editions**. The browser describes physical instruments and ensembles. Each row corresponds to a seating choice. Performance articulations and individual player presets within a group share the same entry. Instrument and ensemble names were checked against the official manuals and product listings. The catalog includes variant choices where they identify distinct documented setups, including different transpositions and ensemble sizes.

| Volume | Official source |
| --- | --- |
| 1 · Essential Orchestra | [Manual](https://www.vsl.co.at/instruments/special-editions/special-edition-1) · [Product](https://www.vsl.co.at/products/studio/special-edition-vol-1) |
| 2 · Extended Orchestra | [Manual](https://www.vsl.co.at/instruments/special-editions/special-edition-2) · [Product](https://www.vsl.co.at/products/studio/special-edition-vol-2) |
| 3 · Appassionata & Muted Strings | [Manual](https://www.vsl.co.at/instruments/special-editions/special-edition-3) · [Product](https://www.vsl.co.at/products/studio/special-edition-vol-3) |
| 4 · Special Winds, Choir & Voices | [Manual](https://www.vsl.co.at/instruments/special-editions/special-edition-4) · [Product](https://www.vsl.co.at/products/studio/special-edition-vol-4) |
| 5 · Dimension Strings | [Manual](https://www.vsl.co.at/instruments/special-editions/special-edition-5) · [Product](https://www.vsl.co.at/products/studio/special-edition-vol-5) |
| 6 · Dimension Brass | [Manual](https://www.vsl.co.at/instruments/special-editions/special-edition-6) · [Product](https://www.vsl.co.at/products/studio/special-edition-vol-6) |
| 7 · Historic Instruments | [Manual](https://www.vsl.co.at/instruments/special-editions/special-edition-7) · [Product](https://www.vsl.co.at/products/studio/special-edition-vol-7) |

The [official Special Editions legacy manual](https://odl.vsl.co.at/cms-vsl/legacy-manuals/starter/vi_special_editions_manual_v3.pdf) also lists the original named instrument and ensemble variants. The seating browser works independently of sample library ownership and installation.

## Integration and persistence

Studio’s current app uses JUCE and Tracktion, with an AppKit bridge hosting a native SwiftUI view inside the existing lower workspace. The editor is created on demand on the main thread and uses Studio’s navy/slate colors, section colors, appearance settings, and workspace navigation. It uses SF Symbols for recognized instrument types and a note or initial where an instrument-specific symbol is unavailable.

Studio saves a versioned seating layout in the edit’s `studioPhysicalSeating` property. It stores independent seat IDs, catalog references, display names, labels, sections, and normalized positions. Legacy projects open with an empty seating design until the first edit. An unrecognized layout version is preserved on disk. A drag commits once to the existing project undo history. The physical layout has no track, audio graph, microphone, panner, or reverb bindings. Use **Audio Stage** to return to Studio’s existing acoustic placement.

## Verification

Build Studio with the [Intel macOS commands](../README.md#build-and-launch-on-intel-macos). The seating editor uses SwiftUI on macOS 14 or later. The Intel workflow tests the Swift model, builds the app, exercises project undo and restoration, and publishes native screenshots. For a local macOS test run, build both `Studio` and `StudioSeatingModelTests`, then run the following command.

```bash
STUDIO_SEATING_SCREENSHOTS="$PWD/build/seating-screenshots" \
  ctest --test-dir build -C Debug -R 'StudioSeating' --output-on-failure
```
