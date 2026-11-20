#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

repo=$(git rev-parse --show-toplevel)
baseline=ca4818e9f0ede69264232cedd5dc8c18a69793d7
output=${1:-"$PWD/acoustic-comparison"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$output"

for source in SpatialModel.h SpatialReverb.h SpatialReverb.cpp; do
    git -C "$repo" show "$baseline:$source" > "$work/$source"
done

for engine in baseline current; do
    sources=$repo
    if [ "$engine" = baseline ]; then sources=$work; fi
    "${CXX:-c++}" -std=c++20 -O3 -DNDEBUG -I"$sources" \
        "$repo/tools/AcousticComparison.cpp" "$sources/SpatialReverb.cpp" -o "$work/compare"
    "$work/compare" "$output/$engine" | tee "$output/$engine.csv"
done
