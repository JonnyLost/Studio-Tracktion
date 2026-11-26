# Scan and register Garritan instruments on your Mac

Use the Studio build from `feature/studio-aria-catalog-60`. If you build locally, follow the [Intel macOS build instructions](../README.md#build-and-launch-on-intel-macos). Substitute `feature/studio-aria-catalog-60` for the example branch and use a fresh checkout folder. Install and authorize ARIA Player and your Garritan libraries using their original installers.

1. In Studio, open **Instruments → Plug-ins…** and scan for Audio Units. Make sure **ARIA Player** appears in the Instrument Browser.
2. Choose **ARIA… → Discover Garritan/ARIA instruments in standard locations**. Studio checks the system and user Garritan support folders, the Plogue ARIA Presets folder and `/Users/Shared`. Choose **Scan a library folder…** to add each custom library location or external volume. Select the library folder containing its instrument definitions and samples where possible.
3. Use **Cancel** to checkpoint completed inventory work and **Resume interrupted scan** to continue. **Rescan all known roots** refreshes the source and dependency observations. Missing or inaccessible roots stay pending. Grant access in the macOS folder chooser when prompted.
4. Search for a patch name or narrow by the Family and Type columns. ARIA entries use the library name in the Type column. Use **Favorite** to find entries quickly later. **ARIA… → Show local catalog and reports** opens `~/Library/Application Support/Studio/ARIA Catalog` in Finder. That folder contains `catalog.json`, `compatibility.txt` and the individual local state manifests under `Presets`.

## Register a single playable patch

1. Select the discovered patch in the Instrument Browser and click **Add as New Track**. If there is no registered capture yet, choose **Channel → Open Instrument** to open the original ARIA editor for manual selection.
2. Load that exact patch into **slot 1 on MIDI channel 1**. Leave the other slots empty. Play it and check for sound and missing sample warnings. Some Garritan patches require raising CC1 before they sound.
3. Stop the transport and leave the ARIA track and browser entry selected. Choose **ARIA… → Register auditioned patch from selected track…** and confirm. Studio saves the actual state returned by the installed Audio Unit. To replace a capture, move its manifest from `Presets` first. The manifest contains the source identity and the Audio Unit version used for capture.
4. Select the same browser entry and click **Add as New Track** again. Studio applies the saved state to a fresh matching ARIA instance with its editor closed. Audition the recalled patch on channel 1. Open the editor separately if you want to inspect slot 1. Record a short phrase, save a test project, close and reopen it, then follow Studio's existing **Retry ARIA / Confirm ARIA** workflow. Repeat with at least one patch from each installed library.

## Reading the results

- SFZ entries are candidates. The scanner inventories literal sample paths, includes and some explicit metadata. Macros, encrypted banks and vendor-specific identifiers may require selection inside ARIA.
- `.aria` and `.ariax` entries are snapshots with unverified slot layout. Open the generic ARIA Player on a fresh track to load one of these files. You can register its browser entry after confirming it contains a single working patch in slot 1. Other player formats remain in the report with their original-player or importer route.
- Audition confirmation applies to the loaded source instance. Playback on a fresh track and after reopening a project should be checked on your Mac. An updated Audio Unit version may require a new capture. A changed or disconnected source can invalidate the previous capture.
- Library definitions, sample files and artwork stay in their original locations. Keep the local catalog and state manifests private because they can contain installation paths and Audio Unit state.
