#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Command Line Tools for this OS don't ship SwiftUIMacros. The 26.5 SDK
# still exposes SwiftUI state as a normal property wrapper, so the app builds.
export SDKROOT="${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk}"

if [[ ! -f platform/mac/Support/AppIcon.icns ]]; then
  if [[ -f scripts/make-icon.py ]]; then
    python3 scripts/make-icon.py
  else
    echo "platform/mac/Support/AppIcon.icns is missing." >&2
    exit 1
  fi
fi

swift build -c release

APP="dist/Fairlead.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
if [[ ! -x platform/mac/Support/wireguard-go ]]; then
  echo "platform/mac/Support/wireguard-go is missing. Fairlead bundles wireguard-go and does not download it." >&2
  exit 1
fi
if [[ ! -x platform/mac/Support/amneziawg-go ]]; then
  echo "platform/mac/Support/amneziawg-go is missing. Build it with scripts/build-amneziawg-go.sh." >&2
  exit 1
fi
cp .build/release/Fairlead "$APP/Contents/MacOS/Fairlead"
cp .build/release/FairleadHelper "$APP/Contents/MacOS/FairleadHelper"
cp platform/mac/Support/wireguard-go "$APP/Contents/MacOS/wireguard-go"
cp platform/mac/Support/amneziawg-go "$APP/Contents/MacOS/amneziawg-go"
cp platform/mac/Support/Info.plist "$APP/Contents/Info.plist"
cp platform/mac/Support/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
cp platform/mac/Support/wireguard-go.LICENSE "$APP/Contents/Resources/wireguard-go.LICENSE"
cp platform/mac/Support/amneziawg-go.LICENSE "$APP/Contents/Resources/amneziawg-go.LICENSE"
cp -R platform/mac/Support/*.lproj "$APP/Contents/Resources/"
chmod 755 "$APP/Contents/MacOS/Fairlead" "$APP/Contents/MacOS/FairleadHelper" "$APP/Contents/MacOS/wireguard-go" "$APP/Contents/MacOS/amneziawg-go"
codesign --force --deep --sign - "$APP"
echo "Built $(pwd)/$APP"
