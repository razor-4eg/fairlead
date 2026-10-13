<p align="center">
  <img src="docs/icon.png" width="128" alt="Harbor">
</p>

<h1 align="center">Harbor</h1>

Harbor is a native macOS client for WireGuard. It keeps your configs on this Mac, brings a tunnel up, and lets chosen addresses skip the tunnel and use the regular network.

WireGuard is a trademark of Jason A. Donenfeld. Harbor is an independent client.

## Run

Build the app, or start from a `Harbor.app` you were given. Put it in `/Applications`, then open it.

```bash
scripts/build-app.sh
ditto dist/Harbor.app /Applications/Harbor.app
open /Applications/Harbor.app
```

`wireguard-go` is inside the app. Harbor does not need Homebrew, `wg`, or `wg-quick`.

A copy you built on this Mac is ad-hoc signed and has no quarantine attribute, so Gatekeeper lets it open. The first time you connect, Harbor asks for an administrator password once and installs a helper. After that the helper starts on its own. Configs and keys stay in `~/Library/Application Support/Harbor`, readable only by you.

Closing the window hides the Dock icon and leaves Harbor in the menu bar. Open it again from there. Quit from the menu bar when you want the tunnel down. Quitting restores the routes and DNS Harbor changed.

### Gatekeeper

Harbor is not notarized. macOS blocks the first launch only when the app is quarantined. A browser, AirDrop, Messages, or Mail sets that on a copy that arrived from somewhere else. A local build does not.

Move `Harbor.app` into `/Applications` before the first open. If you open a quarantined app from Downloads or from a disk image, macOS can run a translocated copy from a random read-only folder. The helper would then be installed from that copy.

On macOS Sequoia and later, Control-click and Open does not bypass Gatekeeper.

1. Double-click Harbor. If macOS says it cannot check the app for malicious software, click Done. Move to Trash deletes the app.
2. Open System Settings, then Privacy & Security, and scroll to the bottom. macOS names the app it just blocked.
3. Click Open Anyway, confirm, and authenticate when asked.

That exception is for this copy of the app. Replacing the app can make Gatekeeper ask again.

To clear quarantine in Terminal, before the first open:

```bash
xattr -dr com.apple.quarantine /Applications/Harbor.app
```

`xattr -d` removes the attribute, and `-r` does that for the bundle and everything inside it. Run this only on a copy whose source you trust. It skips Gatekeeper's first-launch check. If macOS says the attribute is missing, the app was not quarantined and you can open it as it is. A copy owned by root needs `sudo` in front of the same command.

`spctl --assess` still says this build is rejected, because it is not notarized. Clearing quarantine is what lets the ad-hoc app launch. Leave the system-wide Gatekeeper setting as it is.

The administrator password Harbor asks for later is the helper, not Gatekeeper. Approve it when you want tunnels to start.

## Tunnel

Import a `.conf` file, drop it on the window, or create a tunnel and edit it in fields or as text. One tunnel is active at a time. Connecting another one brings the current one down.

Harbor starts the bundled `wireguard-go` and sets the address, routes, and DNS itself. The window shows the endpoint, address, handshake, and live rates for traffic down and up. The same controls are in the menu bar.

![Tunnel overview, with addresses that stay off the tunnel](docs/screenshots/overview.png)

## Direct addresses

Traffic to the tunnel follows the AllowedIPs in the config. Harbor can send some of that traffic out through the normal gateway instead.

- An IP or network, with an optional note. Each rule can be switched off without deleting it.
- A domain, such as `example.com`. Harbor looks the name up on the regular network and keeps the resulting addresses out of the tunnel. The list refreshes when the DNS record expires. A shared address, such as a CDN, is direct for every app. Wildcards are not included.
- The current Wi-Fi or Ethernet network. Harbor adds those routes when you connect.
- An app. Harbor watches where that app connects and keeps those addresses out of the tunnel. New addresses are remembered. Already open connections move to the direct path after they reconnect.

On macOS the route is for the whole system. An address marked direct for one app or one domain is direct for every app. That is how Harbor can do this without a network extension.

![A domain resolved on the regular network](docs/screenshots/domains.png)

![An app whose learned address stays off the tunnel](docs/screenshots/apps.png)

## Configs

The editor covers the interface and each peer: keys, address, DNS, MTU, listen port, endpoint, allowed IPs, and keepalive. The private key stays hidden until you show it. You can also edit the config as text, copy it, or export a `.conf`.

PreUp, PostUp, PreDown, PostDown, and SaveConfig are removed before the tunnel starts. They would otherwise run as root.

![Config editor](docs/screenshots/config.png)

## Settings

Open Settings from the gear, the menu bar, or Command-Comma.

**Open at login.** Harbor starts when you sign in. If macOS asks you to allow it, the switch stays on and Settings tells you where to approve it: System Settings, General, Login Items.

**Default tunnel.** Harbor selects this tunnel when it opens and connects it. Together with Open at login, that tunnel comes up after you sign in. Choose None to open Harbor without connecting.

**Components.** `wireguard-go` is included. The helper is listed in the same window. Start helper asks for an administrator password once, then brings tunnels and direct routes up itself. If the installed helper is older than the app, Harbor asks once and replaces it. The config folder opens from Settings.

![Settings, including the components Harbor needs](docs/screenshots/settings.png)

## Language

Harbor follows the system language. English, Russian, German, Spanish, French, Portuguese, and Simplified Chinese are included.

## Build

You need Xcode Command Line Tools. The script builds `dist/Harbor.app` and ad-hoc signs it. It does not download `wireguard-go`. The binary already in `Support/wireguard-go` is the MIT-licensed userspace implementation from WireGuard. Its license is `Support/wireguard-go.LICENSE`.

```bash
scripts/build-app.sh
open dist/Harbor.app
```
