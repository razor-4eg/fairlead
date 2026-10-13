<p align="center">
  <img src="docs/icon.png" width="128" alt="Harbor">
</p>

<h1 align="center">Harbor</h1>

Harbor is a native macOS client for WireGuard. It keeps your configs on this Mac, brings a tunnel up, and lets chosen addresses skip the tunnel and use the regular network.

WireGuard is a trademark of Jason A. Donenfeld. Harbor is an independent client.

## Tunnel

Import a `.conf` file, drop it on the window, or create a tunnel and edit it in fields or as text. One tunnel is active at a time. Connecting another one brings the current one down.

The window shows the endpoint, address, handshake, and live rates for traffic down and up. The same controls are in the menu bar. Closing the window leaves Harbor running there and hides the Dock icon. Quitting Harbor brings the tunnel down and restores the routes it added.

![Tunnel overview, with addresses that stay off the tunnel](docs/screenshots/overview.png)

## Direct addresses

Traffic to the tunnel follows the AllowedIPs in the config. Harbor can send some of that traffic out through the normal gateway instead.

- An IP or network, with an optional note. Each rule can be switched off without deleting it.
- A domain, such as `example.com`. Harbor looks the name up on the regular network and keeps the resulting addresses out of the tunnel. The list refreshes when the DNS record expires. A shared address, such as a CDN, is direct for every app. Wildcards are not included.
- The current Wi-Fi or Ethernet network. Harbor adds those routes when you connect.
- An app. Harbor watches where that app connects and keeps those addresses out of the tunnel. New addresses are remembered. Already open connections move to the direct path after they reconnect.

On macOS the route is for the whole system. An address marked direct for one app is direct for every app. That is how Harbor can do this without a network extension.

![An app whose learned address stays off the tunnel](docs/screenshots/apps.png)

## Configs

The editor covers the interface and each peer: keys, address, DNS, MTU, listen port, endpoint, allowed IPs, and keepalive. The private key stays hidden until you show it. You can also edit the config as text, copy it, or export a `.conf`.

PreUp, PostUp, PreDown, PostDown, and SaveConfig are removed before the tunnel starts. They would otherwise run as root.

![Config editor](docs/screenshots/config.png)

## Settings

Open Settings from the gear, the menu bar, or Command-Comma.

**Open at login.** Harbor starts when you sign in. If macOS asks you to allow it, the switch stays on and Settings tells you where to approve it: System Settings, General, Login Items.

**Default tunnel.** Harbor selects this tunnel when it opens and connects it. Together with Open at login, that tunnel comes up after you sign in. Choose None to open Harbor without connecting.

**Components.** `wireguard-go` is included in Harbor. The helper is listed in the same window and starts from here. It asks for an administrator password once, then brings tunnels and routes up itself. If the installed copy is older than the app, Harbor asks once and replaces it. Configs and keys stay in `~/Library/Application Support/Harbor`, readable only by you. That folder opens from Settings.

![Settings, including the components Harbor needs](docs/screenshots/settings.png)

## Language

Harbor follows the system language. English, Russian, German, Spanish, French, Portuguese, and Simplified Chinese are included.

## Build

You need Xcode Command Line Tools. Harbor includes `wireguard-go`, so running it does not need Homebrew, `wg`, or `wg-quick`.

```bash
scripts/build-app.sh
open dist/Harbor.app
```

The script builds `dist/Harbor.app`. `wireguard-go` is the MIT-licensed userspace implementation from WireGuard; its license is `Support/wireguard-go.LICENSE`.
