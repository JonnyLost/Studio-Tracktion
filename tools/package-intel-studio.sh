#!/bin/bash
set -euo pipefail

if [[ $# != 2 ]]; then
    echo "Usage: $0 /path/to/Studio.app /path/to/Studio.zip" >&2
    exit 2
fi

app="$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")"
archive="$2"
executable="$app/Contents/MacOS/Studio"
plist="$app/Contents/Info.plist"

test "$(basename "$app")" = Studio.app
test -x "$executable"
plutil -lint "$plist"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")" = org.studioproject.tracktion
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")" = Studio
test "$(lipo -archs "$executable")" = x86_64
test "$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$plist")" = 14.0
otool -L "$executable"
if ! codesign --verify --deep --strict "$app"; then
    codesign --force --sign - "$app"
    codesign --verify --deep --strict "$app"
fi

# Test the archive's actual bundle through Launch Services, as Finder does.
ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
scratch="$(mktemp -d)"
pid=
cleanup() {
    if [[ -n "$pid" ]]; then
        kill "$pid" 2>/dev/null || true
    fi
    rm -rf "$scratch"
}
trap cleanup EXIT
ditto -x -k "$archive" "$scratch"
scratch="$(cd "$scratch" && pwd -P)"
executable="$scratch/Studio.app/Contents/MacOS/Studio"
codesign --verify --deep --strict "$scratch/Studio.app"
open -n "$scratch/Studio.app"
for (( attempt = 0; attempt < 15; ++attempt )); do
    pid="$(pgrep -fx "$executable" || true)"
    [[ -n "$pid" ]] && break
    sleep 1
done
if [[ -z "$pid" ]]; then
    echo "Launch Services did not start the archived Studio.app." >&2
    exit 1
fi
sleep 3
kill -0 "$pid"
echo "Archived Studio.app launched through macOS Launch Services."
