#!/bin/bash
set -euo pipefail

app="${1:?Usage: verify-intel-studio.sh /path/to/Studio.app [approved-icon.png]}"
approved_icon="${2:-}"
executable="$app/Contents/MacOS/Studio"
plist="$app/Contents/Info.plist"

test "$(basename "$app")" = Studio.app
test -x "$executable"
plutil -lint "$plist"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")" = org.studioproject.tracktion
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")" = Studio
test -n "$(/usr/libexec/PlistBuddy -c 'Print :NSMicrophoneUsageDescription' "$plist")"
test "$(lipo -archs "$executable")" = x86_64
xcrun vtool -show-build "$executable" | grep -Eq '^[[:space:]]+minos 14\.0(\.0)?$'
otool -L "$executable"

if [[ -n "$approved_icon" ]]; then
    [[ "$(sips -g pixelWidth -g pixelHeight "$approved_icon")" == *'pixelWidth: 1024'*'pixelHeight: 1024'* ]]
    icon=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$plist")
    test "$icon" = AppIcon.icns
    test -s "$app/Contents/Resources/$icon"
fi

codesign --verify --deep --strict "$app"
