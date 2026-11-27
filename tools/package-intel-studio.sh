#!/bin/bash
set -euo pipefail

if [[ $# != 2 ]]; then
    echo "Usage: $0 /path/to/Studio.app /path/to/Studio.zip" >&2
    exit 2
fi

app="$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")"
archive="$2"
script_dir="$(cd "$(dirname "$0")" && pwd -P)"
if ! codesign --verify --deep --strict "$app"; then
    codesign --force --sign - "$app"
fi
"$script_dir/verify-intel-studio.sh" "$app" "${STUDIO_ICON_FILE:-}"

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
    pid="$(pgrep -f "^${executable}([[:space:]]|$)" || true)"
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
