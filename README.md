# Studio 0.1 — Automatic ARIA Restoration Milestone

This is a **separate experimental copy** of the working Studio 0.1 application, based on the successful original-state/manual-retry source. It does not change the previous working Studio build or any GitHub repository.

## Change in this milestone

- Captures immutable ARIA state from `.tracktionedit` XML **before** Tracktion loads plugins.
- Schedules a single automatic restore attempt on JUCE's message thread after at least 1 second, once *all* matched ARIA plugin instances are present and no longer reporting asynchronous initialization. No restoration is performed inside the real-time audio callback.
- Waits up to 10 seconds for AU instances; leaves **Retry ARIA** available if not ready. Stops retrying automatically after one submission, with no repeated state writes.
- Submits the saved original AU state using Tracktion's existing `restorePluginStateFromValueTree` method, the same route that worked with the manual button.
- Keeps **Confirm ARIA** and save protection until the user verifies both the loaded instrument names and that they make sound. Automatic submission **does not** automatically unlock saving.
- Keeps MIDI recording's `transport.stop (false, false)` fix, MIDI Monitor, mixer, and Tracktion editor unchanged.
- Keeps original project recovery backup behavior. The supplied recovery file contains Intimate D in saved slot 1 (673-byte state).

## Build and launch on your Intel MacBook

1. Quit other Studio instances.
2. Extract ZIP in `~/Downloads`; folder must be `Studio-ARIA-Auto-Restore`.
3. Paste into Terminal:

```bash
cd "$HOME/Downloads/Studio-ARIA-Auto-Restore" || exit 1
/Applications/CMake.app/Contents/bin/cmake -S . -B build -G Xcode \
  -DTRACKTION_ROOT="$HOME/Developer/Tracktion-Studio" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 && \
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug --target Studio --parallel 4 && \
open "$HOME/Downloads/Studio-ARIA-Auto-Restore/build/Studio_artefacts/Debug/Studio.app"
```

## Test

1. Choose **Open Project** and open `RECOVERED_TEST_PROJECT/Aria State Test - Intimate D RECOVERY.tracktionedit`, *not* the 454-byte empty-state project under Documents.
2. Wait briefly, then open ARIA. Check whether **Intimate D** appears in slot 1 **without clicking Retry ARIA**. If it does, play notes and verify sound.
3. If empty, stop playback, click **Retry ARIA** once, reopen ARIA, and test again.
4. **Do not click Confirm ARIA or Save unless the instrument loaded and played.** Keep the recovered project unchanged during the test.
5. For diagnostics, use: `tail -n 35 "$HOME/Library/Studio/aria-restore.log"`.

Expected logged phrases: `Captured original on-disk ARIA state ... bytes=673`, `Automatic ARIA retry`, `Retry original ARIA ... disk bytes=673`.

**Limitations**: static checks pass here, but this environment cannot compile or run an Intel macOS Audio Unit. Actual automatic restore and multi-instance reliability require testing on your Mac. Once verified, further work should include multiple ARIA instances and save/reopen regression checks. GPL-3.0-or-later; see LICENSE.
