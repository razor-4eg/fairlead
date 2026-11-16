#!/bin/bash
# Builds the mihomo (MetaCubeX) binary Fairlead ships. mihomo owns the TUN, routes, DNS,
# process rules, and carries WireGuard and AmneziaWG. Go is only needed to rebuild this
# binary; the app build does not download it.
set -euo pipefail
cd "$(dirname "$0")/.."

GO_VERSION="${GO_VERSION:-1.27.3}"
GO_SHA256="${GO_SHA256:-dd3908cf20959d798c1e06ebe5b72213f0505ce01373b40ce3110975a6cd99c2}"
MIHOMO_REF="${MIHOMO_REF:-v1.19.34}"
MIHOMO_ARCH="${MIHOMO_ARCH:-arm64}"

ROOT="$(pwd)"
CACHE="$ROOT/.build/mihomo"
GO_ROOT="$CACHE/go"
SRC="$CACHE/src"
mkdir -p "$CACHE"

if [[ ! -x "$GO_ROOT/bin/go" ]]; then
  archive="$CACHE/go.tgz"
  curl -fsSL "https://go.dev/dl/go${GO_VERSION}.darwin-arm64.tar.gz" -o "$archive"
  echo "$GO_SHA256  $archive" | shasum -a 256 -c -
  rm -rf "$GO_ROOT"
  tar -C "$CACHE" -xzf "$archive"
fi

rm -rf "$SRC"
git clone --depth 1 --branch "$MIHOMO_REF" https://github.com/MetaCubeX/mihomo.git "$SRC"

export PATH="$GO_ROOT/bin:$PATH"
export GOTOOLCHAIN=local
export CGO_ENABLED=0
export GOOS=darwin
export GOARCH="$MIHOMO_ARCH"
export GOSUMDB=off
export GOPROXY="${GOPROXY:-https://proxy.golang.org,direct}"

(
  cd "$SRC"
  go build -trimpath -ldflags "-s -w -X github.com/metacubex/mihomo/constant.Version=${MIHOMO_REF}" -o "$CACHE/mihomo" .
)

cp "$CACHE/mihomo" "$ROOT/platform/mac/Support/mihomo"
cp "$SRC/LICENSE" "$ROOT/platform/mac/Support/mihomo.LICENSE"
chmod 755 "$ROOT/platform/mac/Support/mihomo"
echo "Built platform/mac/Support/mihomo from $MIHOMO_REF ($MIHOMO_ARCH)"
