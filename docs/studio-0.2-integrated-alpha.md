# Studio 0.2 Integrated Alpha

Studio 0.2 combines the Core DAW milestones M8–M14, Cinematic Atelier M1–M5,
and Film Scoring M1–M5. Launch the application on an Intel Mac with macOS 14
or later. Picture lives in the rail and the Navigate menu. It uses the lower
workspace alongside the same right-hand Inspector used by Piano Roll,
Mixer and Stage.

## Install on an Intel Mac

Open the Intel macOS workflow run linked from the integration PR and download
the Intel app artifact. Finder may extract the download automatically. If you
see an inner ZIP, double-click it to extract **Studio.app**. Drag **Studio**
into **Applications**, then double-click it. Keep your existing Garritan AU and
sample library installations in place. Your Studio preferences and existing
project files use their usual locations. If macOS blocks the first launch of the
trusted private build, open **System Settings → Privacy & Security** and use
**Open Anyway** for Studio.

An artifact with **preview** in the name is built for testing while the approved
icon artwork is pending. The final icon input is the previously approved
1024 × 1024 PNG at `assets/Studio.png`. The workflow checks that CMake produced
the corresponding `AppIcon.icns` and that the bundle references it.

## Build and package on an Intel Mac

From a clean checkout on this branch with CMake and Xcode installed:

```bash
git init ../studio-tracktion-engine
git -C ../studio-tracktion-engine fetch --depth 1 https://github.com/Tracktion/tracktion_engine.git c6293127cdc0a8488aea2ae0907f4709ee89c228
git -C ../studio-tracktion-engine checkout --detach FETCH_HEAD
git -C ../studio-tracktion-engine -c url.https://github.com/.insteadOf=git@github.com: submodule update --init --depth 1
cmake -S . -B build -G Xcode \
  -DTRACKTION_ROOT="$(cd ../studio-tracktion-engine && pwd)" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug --output-on-failure
tools/package-intel-studio.sh build/Studio_artefacts/Debug/Studio.app "$PWD/Studio-0.2-Intel.zip"
```

The packaging check confirms the bundle identifier, Intel binary and deployment
target, then exercises the extracted bundle through macOS Launch Services.
The project also accepts an approved local PNG via `-DSTUDIO_ICON_FILE=/path/to/Studio.png`.

## Acceptance and scope

- Use the existing Intel and Linux Actions runs for the automated results.
  The review artifacts include Picture layout images and the interface gallery,
  along with WAV exports from the synthetic audio fixtures.
- Picture references remain external media. Opening a project with moved footage
  requires relinking that footage through **Import Picture**. Native video
  playback uses AVKit on macOS.
- Film delivery supplies a 48 kHz, 24-bit stereo WAV for the chosen cue, one WAV
  per track, or per-family stems using explicit assignments. Track and family
  renders can include routing dependencies. Check a cue's outputs when using
  external Audio Units or projects with shared routing.
- Stage room audio remains opt-in. The Stage microphone tools store placement
  metadata. Independent virtual microphone capture is pending.
- Garritan audio libraries are supplied by the owner's local installation.
  Installed ARIA AU playback and the visual Finder icon require an owner Mac
  acceptance check with their actual installation and the approved icon.
- Ostinato, further Stage audio routing, Garritan library recovery and new
  instrument-building features are deferred.
