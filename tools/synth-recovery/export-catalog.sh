#!/usr/bin/env bash
set -euo pipefail

source_repository=${1:?Pass the path to a read-only Garritan-studio checkout}
swift_compiler=${SWIFTC:-swiftc}
revision=3787b60263b0f8d0188f82e626d12c40d17dc86f
script_dir="$(cd "$(dirname "$0")" && pwd)"
working="$(mktemp -d)"
trap 'rm -rf "$working"' EXIT
for file in Studio{Analog,FM,Wavetable,Additive,Granular,Physical}{Patch,Presets} StudioAnalogRouting StudioSynth{Patch,MIDIControls,Modulation,HybridPresets}; do
    git -C "$source_repository" show "$revision:DAWCore/Sources/DAWCore/Audio/$file.swift" > "$working/$file.swift"
done
git -C "$source_repository" show "$revision:DAWUI/Sources/DAWUI/Views/StudioSynthFactoryCatalog.swift" |
    sed '/^import DAWCore$/d' > "$working/StudioSynthFactoryCatalog.swift"
"$swift_compiler" -module-cache-path "$working/modules" "$working"/*.swift "$script_dir/main.swift" -o "$working/export"
"$working/export"
