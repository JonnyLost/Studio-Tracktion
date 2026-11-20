# Native acoustic engine

The comparison baseline is M4 commit `ca4818e`. It keeps the working opt-in Tracktion send, solo-safe native return, mixer fader, and temporary Room Check.

## Baseline diagnosis

Four fixed delay paths at 29.7, 37.1, 41.1, and 53.3 ms feed a stable, energy-preserving Householder matrix with damped feedback. Stability alone does little to hide the recurring echoes. Sparse injection and pickup expose a few paths on each side. Each early channel has only three taps, and the late tank receives the undiffused source. This produces identifiable clusters of arrivals during release. Fixed path lengths reinforce stationary spectral peaks. Left and right use different output sums, but both draw from the same small set of modes.

The six-band feedback coefficients use first-order crossover approximations and a conservative stability bound. Geometry changes the early taps while the late path lengths stay fixed. The corrected M4 wet reference makes the tail audible. It also exposes the coloration that the previous low return level masked.

The comparison tool measures normalized echo density in local 30 ms windows, peak short-lag impulse autocorrelation at 21–85 ms, local late-spectrum peak prominence, left/right correlation, octave-filtered Schroeder T20, and wet impulse energy. These describe measurable changes in the response. Musical quality still needs listening evaluation.

`tools/compare-acoustics.sh /path/to/output` builds the pinned baseline and the current engine using the same compiler. It writes CSV measurements and paired 48 kHz stereo WAVs of wet impulses, dry piano-like phrases plus the shared wet return, and sustained tones. Both mixes use the same dry source, rear send, and WAV scale. Timing is the median of five runs as a percentage of one core's callback budget. The test runs DSP directly, so actual costs in Studio will also include Tracktion, plug-ins, and device I/O.

## Eight-line refinement

The new tank uses staggered, room-scaled delay lengths and a normalized Hadamard mixer. Three short all-pass diffusers per channel spread transients before the tank. Slow, independently phased fractional-delay modulation breaks up repeating paths. Separate input and output sign patterns retain stereo information. Twelve early taps use dimensions and broad wall, floor, ceiling, and treatment losses. Their timing overlaps the start of the diffused tail. The existing six-band targets shape the feedback with approximate crossovers.

At 48 kHz, the Chamber Hall impulse reaches a normalized echo density of 1.01 at 150 ms, up from 0.037. The 21–85 ms repeat measure falls from 0.672 to 0.161, and the local late-spectrum peak prominence falls from 8.12 dB to 7.27 dB. The eight-line candidate approaches the density of diffuse noise by 150 ms, so the shared return uses eight paths. These comparisons use the deterministic test signal, and they do not establish listening acceptance.

The preset measurements show about 1.76 s T20 at 500 Hz for Scoring Stage, 3.37 s for Wood Concert Hall, and 6.90 s for Stone Nave at 48 kHz. The chart in Rooms displays estimated decay targets. It does not measure the impulse response. The preset dimensions and materials drive the approximation while saved custom and active room values remain project data. Mic geometry stays in the project for placement. The shared stereo return does not simulate individual microphone signals.

Room geometry changes crossfade over 160 ms. Coefficients and levels ramp separately. Plan calculations and delay allocation run outside audio processing. The render thread uses a bounded atomic snapshot and preallocated delay rings. The tank sleeps after its response becomes quiet and can keep metering input while bypassed. A wet-only soft ceiling provides headroom for high send levels and Room Check. The return and master faders still determine overall mix headroom.

Pre-delay is a 0–100 ms room parameter. Older rooms open at 0 ms. New presets provide different starting values. Room Check remains a temporary +12 dB wet boost. Stage sends, depth, pan, mixer metering, bus assignments, and the project format are otherwise carried through the existing routing.

## MacBook listening check

1. Open a project copy with a dry ARIA piano. Set Stage pan to centre and leave the room return and send trims fixed. Confirm the rear seat has a more audible tail than the front.
2. Compare Wood Concert Hall, Scoring Stage, and Stone Nave with short staccato notes, note-release tails, and a sustained chord. Listen for repeated pitches or flutter after the source stops. Compare the paired offline WAVs at the same monitoring level.
3. During playback, adjust pre-delay, apply a different room, and toggle bypass. Use Room Check separately, then let it expire. Check transport stop, seek, replay, solo, and return metering at 44.1 and 48 kHz. If the device supports 96 kHz, repeat and check Resource Inspector in a larger edit.
4. Save As and reopen the copy. Verify the chosen room, pre-delay, ARIA state, and solo-safe return. Check that Room Check opens off.
