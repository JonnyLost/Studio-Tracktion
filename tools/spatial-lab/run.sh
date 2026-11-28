#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# No installation, no changes to Studio or the installed AU.
set -u
set -o pipefail
if [ "$(uname -s)" != Darwin ]; then
  echo "This laboratory requires macOS and an installed Panagement 2.8.0 Audio Unit."
  exit 1
fi
HERE="$(cd "$(dirname "$0")" && pwd)" || exit 1
WORK="$(mktemp -d "$HOME/Desktop/Studio-Spatial-Lab-XXXXXX")" || exit 1
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/studio-spatial-lab-build.XXXXXX")" || exit 1
trap 'rm -rf "$BUILD"' EXIT
(
  set -e
  echo "Studio Spatial Laboratory L1"
  sw_vers
  uname -m
  echo "Compiling standalone native AU host..."
  xcrun clang++ -std=c++17 -O2 -Wall -Wextra "$HERE/SpatialLab.cpp" \
    -framework AudioToolbox -framework CoreFoundation -o "$BUILD/spatial-lab"
  "$BUILD/spatial-lab" --self-test
  echo "Measuring Panagement 2.8.0; no audio device is used."
  "$BUILD/spatial-lab" "$WORK/report"
) 2>&1 | tee "$WORK/run.log"
RESULT=${PIPESTATUS[0]}
printf '%s\n' "$RESULT" > "$WORK/exit-code.txt"
echo "Packaging output, including any diagnostic failures..."
if /usr/bin/ditto -c -k --keepParent "$WORK" "$WORK.zip"; then
  echo "UPLOAD THIS ZIP: $WORK.zip"
  open -R "$WORK.zip"
else
  echo "ZIP creation failed; the uncompressed report remains at $WORK"
  exit 1
fi
if [ "$RESULT" -ne 0 ]; then
  echo "Some or all tests failed; upload the ZIP so we can diagnose them."
else
  echo "Laboratory measurement run completed. Upload the ZIP."
fi
exit "$RESULT"
