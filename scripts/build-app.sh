#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Command Line Tools for this OS don't ship SwiftUIMacros. The 26.5 SDK
# still exposes SwiftUI state as a normal property wrapper, so the app builds.
export SDKROOT="${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk}"

if [[ ! -f Support/AppIcon.icns ]]; then
  python3 scripts/make-icon.py
fi

swift build -c release

APP="dist/Harbor.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp .build/release/Harbor "$APP/Contents/MacOS/Harbor"
cp .build/release/HarborHelper "$APP/Contents/MacOS/HarborHelper"
cp Support/Info.plist "$APP/Contents/Info.plist"
cp Support/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
cp -R Support/*.lproj "$APP/Contents/Resources/"
chmod 755 "$APP/Contents/MacOS/Harbor" "$APP/Contents/MacOS/HarborHelper"
codesign --force --deep --sign - "$APP"
echo "Built $(pwd)/$APP"
