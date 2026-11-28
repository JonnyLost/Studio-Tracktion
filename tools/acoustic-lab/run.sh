#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DEST="${2:-$HOME/Desktop/Studio-Acoustic-Lab-L3}"
case "${1:-generate}" in
  generate)
    python3 "$HERE/acoustic_lab.py" generate "$DEST"
    ;;
  baseline)
    if [ ! -d "$DEST/renders" ]; then
      echo "Generate the lab directory first." >&2; exit 1
    fi
    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    "${CXX:-clang++}" -std=c++20 -O2 "$HERE/native_baseline.cpp" \
      "$ROOT/SpatialReverb.cpp" -o "$WORK/native_baseline"
    "$WORK/native_baseline" "$DEST"
    ;;
  analyze)
    python3 "$HERE/acoustic_lab.py" analyze "$DEST"
    ;;
  *)
    echo "Usage: bash run.sh {generate|baseline|analyze} [lab directory]" >&2; exit 1;;
esac
