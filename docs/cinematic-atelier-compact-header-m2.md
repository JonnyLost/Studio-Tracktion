# Cinematic Atelier M2 — compact header and full-portrait fitting

This is a presentation-only follow-up to the fixed Instrument View height. It is stacked on `feature/studio-atelier-fixed-dock-sizing` / draft PR #38; do not merge without owner approval.

## MacBook design contract

- **Split:** at 100% appearance, the lower Instrument Editor targets **425 logical points** including one 42-point Studio dock header. The divider is locked for instrument editing. Shorter windows retain some arrangement through the existing dock bounds.
- **One unified dock header:** Studio's existing outer bar holds the **actual active instrument selector** and **Float** on the left. Browser/Inspector, Compact/Split/Expanded and Hide remain on the right (or the existing condensed Layout menu at narrow widths). The second selector row and redundant **Expand** action no longer occupy the editor area.
- **Float:** the same selector and **Dock** action move back into the top-level floating window, preserving the same plug-in instance, MIDI/audio state, and previously saved floating window geometry (initially 960×510 logical points at default appearance).
- **Portraits:** Studio scales *the entire picture* to fit its allocated image rectangle (contain, centered; no source crop). Source illustrations remain in `~/Library/Application Support/Studio/AtelierArtwork/`, and the bundled study remains the fallback when no valid portrait is present. The caption has its own lower strip outside the image. Images are decoded/scaled on selection on the GUI thread rather than in audio rendering.
- **Other workspaces:** Piano Roll, Stage, Mixer and Browser do not inherit the instrument header or fixed Split height.

## Review checklist

1. Open an ARIA instrument's Cinematic Atelier at a 1536×960 Studio window with standard (100%) appearance. Verify the whole selected Violin image—scroll, body and bottom—is visible without clipping or a vertical scroll bar. Repeat with Cello, Flute, Clarinet, Horn and Oboe, and with one missing artwork file.
2. Confirm there is only one selector / Float pair, in the Studio dock header; **Expanded** appears only in the regular layout controls. Check that Browser/Inspector and Hide still work.
3. Float, resize, Dock, reopen and switch instruments. Confirm one audio/MIDI instrument instance and unchanged playback, automation, mixer levels and saved artwork choices. Floating should show a Dock action *inside its own window*, not leave a stray selector over Studio.
4. Switch to the Piano Roll and Mixer. Confirm the normal headers and resizable Split behavior return; switch back and check Instrument View is fixed.
5. Repeat at 1280×800 and at 110%/125%/150%/175% interface scale. On physically constrained screens scrolling is acceptable only when genuinely required; buttons must not overlap.

## Automated coverage

`ctest --test-dir build -C Debug --output-on-failure -R StudioAdaptiveWorkspace` exercises one-row header vs Piano Roll, locked instrument divider, floating/docked instrument ownership and the existing no-scroll check. The `--studio-adaptive-workspace-review` screenshots include the fixed dock and floating views.

Studio's audio graph, Tracktion Edit persistence, ARIA state safety, original Audio Unit editor and native Synth/Sampler processing are not modified by this UI pass.
