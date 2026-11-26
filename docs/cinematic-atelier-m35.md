# M35 — Cinematic Atelier Instruments

Cumulative child of M34 c6f7922. Native sampler exposes four supported controllers; hosted instruments show four essential MIDI controls with opt-in extra four; Studio Synth shows its actual preset name, editor access and channel controls rather than unverified MIDI/articulation controls. Direct Track Settings / Instrument switch and FX access retain existing channel functionality. Artwork remains cosmetic, sourced only from existing managed files; unavailable filenames are retained in project state. No generated art collection bundled. Stage & Mics opens existing workspace.

Responsive portrait, two/four-column layout, actual articulation row count and shared height calculation replace fixed empty space. CC initialization reloads default values on track change even when saved state is empty; articulation cache is keyed to plugin instance. Switching views does not send MIDI. Send shown sends visible controls only. Full articulation list remains accessible in existing editor.

Portable geometry and profile tests added across all scales. Existing inspector binding review accommodates synth profile. Full GUI, real instrument playback, controller defaults, track switching, artwork, FX and Stage verification remain required. M32 sampler integration and Unified Stage failures are not waived. No merges.
