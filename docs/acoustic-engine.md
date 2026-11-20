# Native acoustic engine

The comparison baseline is M4 commit `ca4818e`. It keeps the working opt-in Tracktion send, solo-safe native return, mixer fader, and temporary Room Check.

## Baseline diagnosis

Four fixed delay paths at 29.7, 37.1, 41.1, and 53.3 ms feed a stable, energy-preserving Householder matrix with damped feedback. Stability alone does little to hide the recurring echoes. Sparse injection and pickup expose a few paths on each side. Each early channel has only three taps, and the late tank receives the undiffused source. This produces identifiable clusters of arrivals during release. Fixed path lengths reinforce stationary spectral peaks. Left and right use different output sums, but both draw from the same small set of modes.

The six-band feedback coefficients use first-order crossover approximations and a conservative stability bound. Geometry changes the early taps while the late path lengths stay fixed. The corrected M4 wet reference makes the tail audible. It also exposes the coloration that the previous low return level masked.

The comparison tool measures normalized echo density in local 30 ms windows, peak short-lag impulse autocorrelation at 21–85 ms, local late-spectrum peak prominence, left/right correlation, octave-filtered Schroeder T20, and wet impulse energy. These describe measurable changes in the response. Musical quality still needs listening evaluation.

`tools/compare-acoustics.sh /path/to/output` builds the pinned baseline and the current engine using the same compiler. It writes CSV measurements and paired 48 kHz stereo WAVs of wet impulses, dry piano-like phrases plus the shared wet return, and sustained tones. Both mixes use the same dry source, rear send, and WAV scale. Timing is the median of five runs as a percentage of one core's callback budget. The test runs DSP directly, so actual costs in Studio will also include Tracktion, plug-ins, and device I/O.
