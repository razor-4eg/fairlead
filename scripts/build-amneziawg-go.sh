#!/bin/bash
# Builds the userspace AmneziaWG daemon Fairlead ships. It also carries plain WireGuard.
# The socket directory is pinned to /var/run/wireguard so the helper can
# configure it the same way as wireguard-go. Go is only needed to rebuild
# this binary; the app build does not download it.
set -euo pipefail
cd "$(dirname "$0")/.."

GO_VERSION="${GO_VERSION:-1.27.3}"
GO_SHA256="${GO_SHA256:-dd3908cf20959d798c1e06ebe5b72213f0505ce01373b40ce3110975a6cd99c2}"
AWG_REF="${AWG_REF:-v3.1.20260828}"
MODULE="github.com/amnezia-vpn/amneziawg-go/v3"

ROOT="$(pwd)"
CACHE="$ROOT/.build/amneziawg"
GO_ROOT="$CACHE/go"
SRC="$CACHE/src"
mkdir -p "$CACHE"

if [[ ! -x "$GO_ROOT/bin/go" ]]; then
  archive="$CACHE/go.tgz"
  curl -fsSL "https://go.dev/dl/go${GO_VERSION}.darwin-arm64.tar.gz" -o "$archive"
  echo "$GO_SHA256  $archive" | shasum -a 256 -c -
  rm -rf "$GO_ROOT"
  mkdir -p "$GO_ROOT"
  tar -C "$CACHE" -xzf "$archive"
fi

rm -rf "$SRC"
git clone --depth 1 --branch "$AWG_REF" https://github.com/amnezia-vpn/amneziawg-go.git "$SRC"

export PATH="$GO_ROOT/bin:$PATH"
export GOTOOLCHAIN=local
export CGO_ENABLED=0
export GOOS=darwin
export GOARCH=arm64
export GOSUMDB=off

# Go's own client prefers IPv6, which fails on some networks. Mirror the
# modules with curl -4 and build from that directory.
PROXY="$CACHE/proxy"
python3 - "$SRC/go.sum" "$PROXY" <<'PY'
import os, pathlib, sys
root, proxy = sys.argv[1:]
proxy = pathlib.Path(proxy)
mods = []
for line in pathlib.Path(root).read_text().splitlines():
    parts = line.split()
    if len(parts) < 2 or parts[1].endswith("/go.mod"):
        continue
    mods.append((parts[0], parts[1]))

def enc(mod):
    return "".join("!" + ch.lower() if ch.isupper() else ch for ch in mod)

for mod, ver in mods:
    base = proxy / enc(mod) / "@v"
    base.mkdir(parents=True, exist_ok=True)
    for suffix in ("info", "mod", "zip"):
        dest = base / f"{ver}.{suffix}"
        if dest.exists() and dest.stat().st_size > 0:
            continue
        url = f"https://proxy.golang.org/{mod}/@v/{ver}.{suffix}"
        if os.system(f"curl -4 -fsSL --retry 3 -o {str(dest)!r} {url!r}") != 0:
            raise SystemExit(f"failed to download {url}")
PY
export GOPROXY="file://$PROXY"

(
  cd "$SRC"
  go build -trimpath -ldflags "-s -w -X ${MODULE}/ipc.socketDirectory=/var/run/wireguard" -o "$CACHE/amneziawg-go" .
)

cp "$CACHE/amneziawg-go" "$ROOT/platform/mac/Support/amneziawg-go"
cp "$SRC/LICENSE" "$ROOT/platform/mac/Support/amneziawg-go.LICENSE"
chmod 755 "$ROOT/platform/mac/Support/amneziawg-go"
echo "Built platform/mac/Support/amneziawg-go from $AWG_REF"
