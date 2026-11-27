#!/bin/bash
set -euo pipefail

die() { printf '%s\n' "$*" >&2; exit 1; }

branch=integration/studio-0.2-m44-consolidated
tracktion_revision=c6293127cdc0a8488aea2ae0907f4709ee89c228
[[ $# == 1 ]] || die "Usage: TRACKTION_ROOT=/path/to/tracktion_engine $0 <expected-commit>"
[[ "$(uname -s)" == Darwin && "$(uname -m)" == x86_64 ]] || die "Run on the Intel Mac that will use Studio."
[[ "$(sw_vers -productVersion | cut -d. -f1)" -ge 14 ]] || die "Studio requires macOS 14 or later."
for tool in git cmake xcodebuild xcrun codesign ditto lipo otool plutil sips open pgrep; do
    command -v "$tool" >/dev/null || die "Missing required tool $tool."
done
xcodebuild -version
xcrun --find swiftc >/dev/null
xcrun --find vtool >/dev/null

source_dir="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$source_dir"
[[ "$(git branch --show-current)" == "$branch" ]] || die "Check out $branch before installing."
commit="$(git rev-parse HEAD)"
[[ "$commit" == "$(git rev-parse --verify "$1^{commit}")" ]] || die "The checkout differs from the requested commit."
git diff --quiet HEAD -- || die "Use a clean checkout so the executable corresponds to $commit."
printf 'Building Studio at %s (%s)\n' "$commit" "$branch"

tracktion="${TRACKTION_ROOT:-}"
if [[ -z "$tracktion" ]]; then
    for candidate in "$source_dir"/../tracktion_engine "$source_dir"/../tracktion-engine \
                     "$HOME"/Developer/tracktion_engine "$HOME"/Developer/tracktion-engine; do
        if [[ -f "$candidate/modules/juce/CMakeLists.txt" ]]; then
            tracktion="$candidate"
            break
        fi
    done
fi
[[ -n "$tracktion" && -f "$tracktion/modules/juce/CMakeLists.txt" ]] \
    || die "Set TRACKTION_ROOT to the existing Tracktion checkout with JUCE initialized."
tracktion="$(cd "$tracktion" && pwd -P)"
[[ "$(git -C "$tracktion" rev-parse HEAD)" == "$tracktion_revision" ]] \
    || die "Use Tracktion revision $tracktion_revision in a separate checkout if needed."
[[ "$(git -C "$tracktion" ls-tree HEAD modules/juce | cut -d' ' -f3 | cut -f1)" == \
    "$(git -C "$tracktion/modules/juce" rev-parse HEAD)" ]] || die "Initialize the pinned JUCE submodule."

icon="${STUDIO_ICON_FILE:-}"
if [[ -z "$icon" && -f "$source_dir/assets/Studio.png" ]]; then
    icon="$source_dir/assets/Studio.png"
fi
if [[ -n "$icon" ]]; then
    [[ -f "$icon" ]] || die "Approved icon file does not exist: $icon"
    icon="$(cd "$(dirname "$icon")" && pwd -P)/$(basename "$icon")"
else
    printf '%s\n' 'Approved Studio icon is unavailable. Building a preview bundle.' >&2
fi

build="$source_dir/build-m44-intel"
cmake -S "$source_dir" -B "$build" -G Xcode -DTRACKTION_ROOT="$tracktion" \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DBUILD_TESTING=OFF -DSTUDIO_ICON_FILE="$icon"
cmake --build "$build" --config Release --target Studio --parallel "${STUDIO_BUILD_JOBS:-2}"
git diff --quiet HEAD -- && [[ "$(git rev-parse HEAD)" == "$commit" ]] \
    || die "Source changed during the build."

built_app="$build/Studio_artefacts/Release/Studio.app"
# Prefer the directory containing a previous per-user installation when present.
user_apps="$HOME/Applications"
if [[ -e "$user_apps/Studio.app" && ! -e /Applications/Studio.app ]]; then
    destination="$user_apps"
elif [[ -w /Applications && ( ! -e /Applications/Studio.app || -w /Applications/Studio.app ) ]]; then
    destination=/Applications
else
    destination="$user_apps"
fi
mkdir -p "$destination"
target="$destination/Studio.app"
[[ ! -L "$target" && ( ! -e "$target" || -d "$target" ) ]] || die "Unexpected existing path: $target"
if pgrep -x Studio >/dev/null; then
    die "Quit Studio before updating it. The Release build is ready at $built_app."
fi

stage="$(mktemp -d "$destination/.studio-install.XXXXXXXX")"
backup=
cleanup() {
    local result=$?
    if [[ -n "$backup" && -d "$backup/Studio.app" && ! -e "$target" ]]; then
        if ! mv "$backup/Studio.app" "$target"; then
            printf 'Restore Studio.app from %s\n' "$backup" >&2
        fi
    fi
    rm -rf "$stage"
    rmdir "$destination/.studio-install-lock"
    exit "$result"
}
if ! mkdir "$destination/.studio-install-lock"; then
    rmdir "$stage"
    die "Another Studio installation is active in $destination."
fi
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

ditto "$built_app" "$stage/Studio.app"
codesign --force --sign - "$stage/Studio.app"
"$source_dir/tools/verify-intel-studio.sh" "$stage/Studio.app" "$icon"
if pgrep -x Studio >/dev/null; then
    die "Quit Studio before replacing the installed app."
fi
if [[ -d "$target" ]]; then
    backup="$(mktemp -d "$destination/.studio-backup.XXXXXXXX")"
    mv "$target" "$backup/Studio.app"
    printf 'Previous Studio saved at %s/Studio.app\n' "$backup"
fi
mv "$stage/Studio.app" "$target"
printf 'Installed %s\n' "$target"
open "$target"
