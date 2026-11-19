# ARIA automatic restore acceptance test

## Original state preservation
- A known-good 673-byte state with Intimate D is included in `RECOVERED_TEST_PROJECT`.
- Existing Documents versions were previously observed containing a 454-byte *empty* state; do not use them as the recovery benchmark.
- The original AU state is extracted before Tracktion loads the edit.
- A sibling `.pre-aria-restore.tracktionedit` backup is never overwritten if it already exists.

## Expected behavior
- Open recovered project. After AU instantiation and at least a 1-second settle interval, Studio makes **one** automatic retry from the exact saved data, with a 10-second readiness deadline.
- Open ARIA; Intimate D should be in slot 1 and should play.
- If missing, manual Retry ARIA is still available.
- Save remains locked until you personally confirm that instrument slots and playback are correct.

## Diagnostics
Log: `~/Library/Studio/aria-restore.log`.
When the user opens the project, look for the original 673-byte state, automatic retry and original bytes submitted. Success must be visually and audibly confirmed; a log line indicating a submitted state does not prove ARIA accepted it.

## Regression after success
On a separately named project copy, verify MIDI monitor, record/stop retaining a MIDI take, ARIA playback, Save, quit, reopen. Test two ARIA instances before declaring the milestone complete.

## Limitations
No Mac compilation or AU runtime test was possible in the build environment. No other repository modifications or merges have been made.
