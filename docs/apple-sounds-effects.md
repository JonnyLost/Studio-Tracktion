# Apple Sounds & Effects in Studio 0.2

This is a native Studio browser over **installed Apple Audio Units**. It does
not assume that all Logic/GarageBand sample-library folders are AU programs.

## Find Apple sounds

1. Open the **Instruments** workspace.
2. In the library view selector, switch from **Instruments** to **Apple Sounds**.
3. Choose an installed Apple Audio Unit instrument. Use **Load on Selected Track**
   (the destination must be an empty, unfrozen instrument track) or **Add as
   New Track**.
4. Keep that track selected and choose **Apple Sound Bank...**. Studio lists
   factory programs returned by that specific running AU, grouped into pages
   of 24 for usability. Changing a program uses Tracktion's AU program API,
   synchronizes its saved state, and does not create an extra player.
5. If the AU reports no programs, choose **Open Original Apple Interface**.
   Some Apple instruments use a bank selector inside their own interface
   rather than publishing host-accessible programs.
6. Save the Studio project after you select a patch. Test save, close and
   reopen with a small MIDI phrase before relying on a bank for a large
   arrangement.

Only programs reported by the actual AU are shown. We do not invent 128 GM
program entries or claim that all Apple/Logic EXS samples are available through
a particular instrument.

## Find Apple effects

1. Choose an existing instrument or audio track.
2. In the Instruments workspace choose **Apple Effects**, or use the
   selected track's **FX → Browse Apple Effects...** action.
3. Search/select a scanned Apple effect and click **Insert Apple Effect**.
   Studio inserts it in the existing track's effect chain before its volume
   fader, using the same validated AU loading path as other plug-ins.
4. To edit/remove that inserted effect, use the track **FX** controls or
   selected-track Inspector. **Open editor** retains the real Apple AU
   editor and its manufacturer-provided parameters.

The Apple Effects view intentionally cannot create a new instrument track.

## When Apple sounds or effects are missing

Open **Plug-ins...** and rescan Audio Units. This browser only lists Apple
Audio Units that JUCE/Tracktion has actually discovered and marked with
Apple as the manufacturer. Uninstalled instruments and protected Logic
libraries are not mislabeled as available.

Apple Logic Sampler .exs instruments, GarageBand .patch packages, and loose
.dls/.sf2 sound banks need separate format-specific discovery/conversion.
They are not silently converted or loaded into the Apple AU by this browser.

## Design and safety

- Blue-gray Studio UI with searchable collection/family/type filters
- Uses the existing project, instrument selection, undo and AU restore path
- No new sample copies, library modification, proprietary bank decoding or DRM bypass
- Program changes only while playback and recording are stopped
- Menus re-resolve the original project/track/plug-in before editing
- Mac acceptance: instrument load/play/save/reopen; program list and fallback
  against the installed DLS or Apple instrument; insert/edit/remove at least
  one Apple effect; repeat with an audio track; test at 1024 and 1280 widths

Cloud Linux builds can verify compilation and generic UI behavior but cannot
validate the owner's installed Apple Audio Units or factory sound banks.
