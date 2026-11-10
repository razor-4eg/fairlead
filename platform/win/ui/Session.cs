//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// Session.cs -- The window's state. Tunnels on disk, the helper pipe, and the route list.
//
//===============================================================

using Microsoft.UI.Xaml.Media;
using Windows.UI;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO.Pipes;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;

namespace Fairlead;

public class Bindable : INotifyPropertyChanged {
    public event PropertyChangedEventHandler? PropertyChanged;

    /*
    ===============
    Set
    Write the field and notify when the value changed.
    ===============
    */
    protected bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null) {
        if (EqualityComparer<T>.Default.Equals(field, value)) {
            return false;
        }

        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        return true;
    }

    /*
    ===============
    Raise
    Notify one bound property.
    ===============
    */
    public void Raise(string name) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public sealed class RouteEntry {
    public string Id { get; set; } = "";
    public string Kind { get; set; } = "";
    public string Value { get; set; } = "";
    public List<string> Addresses { get; set; } = new();
    public string Status { get; set; } = "";
}

public sealed class RouteList {
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public List<RouteEntry> Entries { get; } = new();
}

public sealed class RouteProblem {
    public string Line { get; set; } = "";
    public string Message { get; set; } = "";
}

public sealed class AppRule {
    public string Id { get; set; } = "";
    public string Path { get; set; } = "";
    public string Name { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public bool Learn { get; set; } = true;
    public List<string> Learned { get; set; } = new();
}

public sealed class Tunnel : Bindable {
    public string Id { get; set; } = "";
    public string Slug { get; set; } = "";
    public string ConfigText { get; set; } = "";
    string _name = "";
    public string Name { get => _name; set => Set(ref _name, value); }
    bool _excludeLan = true;
    public bool ExcludeLan { get => _excludeLan; set => Set(ref _excludeLan, value); }
    bool _selected;
    public bool SelectedPolicy { get => _selected; set => Set(ref _selected, value); }
    public List<RouteList> Lists { get; } = new();
    public List<AppRule> Apps { get; } = new();
    public string Endpoint { get; set; } = "no endpoint";
    public string Address { get; set; } = "";
    public bool Amnezia { get; set; }
    public string Phase { get; set; } = "Off";
    public string Detail { get; set; } = "Traffic is bypassing WireGuard";
    public string IfaceLine { get; set; } = "";
    public string Down { get; set; } = "0 B/s";
    public string Up { get; set; } = "0 B/s";
    public string Rx { get; set; } = "0 B";
    public string Tx { get; set; } = "0 B";
    public string Lan { get; set; } = "The current Wi-Fi or Ethernet network. It appears after you connect.";
    bool _connected;
    public bool Connected {
        get => _connected;
        set {
            if (!Set(ref _connected, value)) {
                return;
            }

            Raise(nameof(StatusBrush));
        }
    }

    public SolidColorBrush StatusBrush => new(Connected
        ? Color.FromArgb(255, 124, 219, 182)
        : Color.FromArgb(255, 94, 107, 98));
    public ulong SampleRx, SampleTx;
    public long SampleTick;
    public int Handshake;
}

public sealed class Session : Bindable {
    public ObservableCollection<Tunnel> Tunnels { get; } = new();
    public ObservableCollection<Tunnel> Filtered { get; } = new();
    Tunnel? _selected;
    public Tunnel? Selected { get => _selected; set { Set(ref _selected, value); Touch(); } }
    string _query = "";
    public string Query { get => _query; set { Set(ref _query, value); Refilter(); } }
    public string Notice { get; private set; } = "";
    public string Banner { get; private set; } = "";
    public bool HelperOnline { get; private set; }
    public string HelperText => HelperOnline ? "Helper is running" : "Helper is not running";
    public string AwgText { get; private set; } = "amneziawg-go  not found";
    public bool AwgOk { get; private set; }
    public bool LaunchAtLogin { get; set; }
    public bool StartHidden { get; set; }
    /// Empty = none; "last" = last active; otherwise a tunnel id.
    public string DefaultId { get; set; } = "";
    public const string LastActiveKey = "last";
    public string LastActiveId { get; set; } = "";
    public bool HasTunnel => Selected != null;
    public bool HasNotice => Notice.Length > 0;
    public string Title => Selected?.Name ?? "";
    public string Endpoint => Selected?.Endpoint ?? "";
    public string Address => Selected?.Address ?? "";
    public string Phase => Selected?.Phase ?? "";
    public string Detail => Selected?.Detail ?? "";
    public string IfaceLine => Selected?.IfaceLine ?? "";
    public string Down => Selected?.Down ?? "0 B/s";
    public string Up => Selected?.Up ?? "0 B/s";
    public string Rx => Selected?.Rx ?? "0 B";
    public string Tx => Selected?.Tx ?? "0 B";
    public string PolicyTitle => Selected?.SelectedPolicy == true ? "Through the tunnel" : "Direct";
    public string PolicyBody => Selected?.SelectedPolicy == true
        ? "Only these addresses use the tunnel. Everything else stays on the regular network."
        : "These addresses stay out of the tunnel and use the regular network.";
    public string LanText => Selected?.Lan ?? "";
    public bool ExcludeLan => Selected?.ExcludeLan ?? true;
    public bool Amnezia => Selected?.Amnezia ?? false;
    public bool Connected => Selected?.Connected ?? false;
    public bool DirectOn => Selected?.SelectedPolicy != true;
    public bool RulesEmpty => Selected != null && Selected.Lists.Count == 0 && Selected.Apps.Count == 0;

	readonly string _dir;
	readonly string _token;
	readonly string _exeDir;
	readonly HashSet<string> _resolving = new();
	readonly SemaphoreSlim _pipe = new(1, 1);
	readonly SemaphoreSlim _helperStart = new(1, 1);

    /*
    ===============
    Session
    Load the tunnels, the token, and which tools are installed.
    ===============
    */
    public Session() {
        _dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Fairlead");
        Directory.CreateDirectory(_dir);
        _exeDir = ToolDir();
        _token = LoadToken();
        Load();
        RefreshTools();
        Refilter();
        Selected = ResolveDefaultTunnel() ?? (Tunnels.Count > 0 ? Tunnels[0] : null);
    }

    /*
    ===============
    ResolveDefaultTunnel
    None, last active, or a fixed tunnel from settings.
    ===============
    */
    public Tunnel? ResolveDefaultTunnel() {
        string key = (DefaultId ?? "").Trim();
        if (key.Length == 0) {
            return null;
        }

        string id = key == LastActiveKey ? LastActiveId : key;
        if (id.Length == 0) {
            return null;
        }

        return Tunnels.FirstOrDefault(t => t.Id == id);
    }

    public event Action? RulesChanged;

    /*
    ===============
    ToolDir
    The folder that holds the helper and the userspace binaries.
    ===============
    */
    static string ToolDir() {
        var dir = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        var parent = Path.GetDirectoryName(dir);
        if (parent != null && File.Exists(Path.Combine(parent, "fairlead-helper.exe"))) {
            return parent;
        }

        return dir;
    }

    /*
    ===============
    Touch
    Raise every property the window binds.
    ===============
    */
    public void Touch() {
        foreach (var name in new[] { nameof(HasTunnel), nameof(HasNotice), nameof(Title), nameof(Endpoint), nameof(Address), nameof(Phase), nameof(Detail), nameof(IfaceLine), nameof(Down), nameof(Up), nameof(Rx), nameof(Tx), nameof(PolicyTitle), nameof(PolicyBody), nameof(LanText), nameof(ExcludeLan), nameof(Amnezia), nameof(Connected), nameof(DirectOn), nameof(RulesEmpty), nameof(Notice), nameof(HelperText), nameof(Banner) })
            Raise(name);
    }

    /*
    ===============
    RefreshTools
    Note whether amneziawg-go is next to the app. It carries both protocols.
    ===============
    */
    void RefreshTools() {
        string awg = Path.Combine(_exeDir, "amneziawg-go.exe");
        AwgOk = File.Exists(awg);
        AwgText = AwgOk ? awg : "amneziawg-go  not found";
        Notice = !HelperOnline
            ? "The system helper is not running. A tunnel cannot start without it."
            : Selected != null && !AwgOk
                ? "amneziawg-go is not included in this copy of Fairlead."
                : "";
        Raise(nameof(AwgText));
        Touch();
    }

    /*
    ===============
    Refilter
    Rebuild the tunnel list from the search box.
    ===============
    */
    public void Refilter() {
        string q = _query.Trim();
        Filtered.Clear();
        foreach (var t in Tunnels)
            if (q.Length == 0 || t.Name.Contains(q, StringComparison.OrdinalIgnoreCase) || t.Endpoint.Contains(q, StringComparison.OrdinalIgnoreCase))
                Filtered.Add(t);
    }

    /*
    ===============
    RefreshLabels
    Endpoint, address, and the Amnezia flag from the config.
    ===============
    */
    public void RefreshLabels(Tunnel t) {
        try {
            using var doc = JsonDocument.Parse(Core.Inspect(t.ConfigText));
            var root = doc.RootElement;
            t.Endpoint = root.GetProperty("endpoint").GetString() ?? "no endpoint";
            t.Address = root.GetProperty("address").GetString() ?? "";
            t.Amnezia = root.GetProperty("amnezia").GetBoolean();
            t.Raise(nameof(Tunnel.Name));
        } catch { }
        if (Selected == t) {
            Touch();
        }

        Refilter();
    }

    /*
    ===============
    Tick
    Poll the helper and update the live rates.
    ===============
    */
    public async Task Tick() {
        RefreshTools();
        bool busy = Tunnels.Any(t => t.Phase == "Connecting" || t.Phase == "Disconnecting");
        // When the helper is down, fail fast — a 5s Connect blocks StartHelper and Quit.
        int wait = HelperOnline ? 3000 : 200;
        string? reply = await Task.Run(() => Call("{\"token\":" + JsonQuote(_token) + ",\"op\":\"status\"}", wait));
        if (reply != null && reply.Contains("\"ok\":true")) {
            HelperOnline = true;
        } else if (!busy) {
            HelperOnline = false;
        }
        Raise(nameof(HelperText));
        if (reply != null) {
            ApplyStatus(reply);
        }

        if (HelperOnline) {
            await Learn();
        }

        RefreshTools();
    }

    /*
    ===============
    ApplyStatus
    Copy one helper reply onto the running tunnel.
    ===============
    */
    void ApplyStatus(string reply) {
        var live = Tunnels.FirstOrDefault(t => t.Connected || t.Phase == "Connecting");
        if (live == null) {
            return;
        }

        try {
            using var doc = JsonDocument.Parse(reply);
            var root = doc.RootElement;
            if (root.TryGetProperty("devices", out var devices) && devices.GetArrayLength() > 0) {
                var peer = devices[0].GetProperty("peers")[0];
                ulong rx = peer.GetProperty("rx").GetUInt64();
                ulong tx = peer.GetProperty("tx").GetUInt64();
                long now = Environment.TickCount64;
                if (live.SampleTick != 0 && now > live.SampleTick) {
                    double dt = (now - live.SampleTick) / 1000.0;
                    live.Down = Rate((rx - live.SampleRx) / dt);
                    live.Up = Rate((tx - live.SampleTx) / dt);
                }

                live.SampleRx = rx;
                live.SampleTx = tx;
                live.SampleTick = now;
                live.Rx = Bytes(rx);
                live.Tx = Bytes(tx);
                live.Handshake = peer.GetProperty("handshake").GetInt32();
                string ep = peer.GetProperty("endpoint").GetString() ?? "";
                if (ep.Length > 0) {
                    live.Endpoint = ep;
                }

                // Interface can be up before WireGuard has shaken hands.
                if (live.Handshake > 0) {
                    live.Connected = true;
                    live.Phase = "Connected";
                    live.Detail = "Handshake " + Ago(live.Handshake);
                } else if (live.Phase == "Connecting" || live.Connected) {
                    live.Connected = false;
                    live.Phase = "Connecting";
                    live.Detail = "Waiting for the handshake";
                }
            }

            if (root.TryGetProperty("managed", out var managed) && managed.GetArrayLength() > 0) {
                var item = managed[0];
                string iface = item.GetProperty("realInterface").GetString() ?? "";
                string gw = item.GetProperty("gatewayV4").GetString() ?? "";
                live.IfaceLine = iface.Length > 0 ? iface + "  ·  gateway " + (gw.Length > 0 ? gw : "—") : "";
                if (item.TryGetProperty("lanCIDRs", out var lans) && lans.GetArrayLength() > 0) {
                    var parts = new List<string>();
                    foreach (var lan in lans.EnumerateArray()) parts.Add(lan.GetString() ?? "");
                    live.Lan = string.Join(", ", parts);
                }
            }
        } catch { }
        if (Selected == live) {
            Touch();
        }
    }

    /*
    ===============
    MakeDraft
    An unsaved tunnel with a fresh config.
    ===============
    */
    public Tunnel MakeDraft() {
        var t = NewTunnel("Tunnel " + (Tunnels.Count + 1), Core.Blank());
        RefreshLabels(t);
        return t;
    }

    /*
    ===============
    Adopt
    Keep a tunnel and select it.
    ===============
    */
    public void Adopt(Tunnel t) {
        if (Tunnels.Contains(t)) {
            return;
        }

        Tunnels.Add(t);
        Selected = t;
        Refilter();
    }

    /*
    ===============
    ImportFile
    Read a dropped or chosen file into a tunnel.
    ===============
    */
    public void ImportFile(string path) {
        string text;

        try {
            text = File.ReadAllText(path);
        } catch {
            Banner = "Could not read that file.";
            Raise(nameof(Banner));
            return;
        }
        var (code, config, name) = Core.Import(text);
        if (code != 0 || config.Length == 0) {
            Banner = code == 1 ? "That file is not a WireGuard or AmneziaWG config." : "Could not read that share.";
            Raise(nameof(Banner));
            return;
        }

        if (name.Length == 0) {
            name = Path.GetFileNameWithoutExtension(path);
        }

        var t = NewTunnel(name, config);
        Tunnels.Add(t);
        RefreshLabels(t);
        Selected = t;
        Save();
    }

    /*
    ===============
    Delete
    Remove a tunnel from disk and from the list.
    ===============
    */
    public void Delete(Tunnel t) {
        if (t.Connected) {
            Disconnect(t);
        }

        Tunnels.Remove(t);
        if (Selected == t) {
            Selected = Tunnels.FirstOrDefault();
        }

        if (DefaultId == t.Id) {
            DefaultId = "";
        }

        if (LastActiveId == t.Id) {
            LastActiveId = "";
        }

        Refilter();
        Save();
    }

    /*
    ===============
    Toggle
    Connect, or disconnect, the tunnel.
    ===============
    */
    public async Task Toggle(Tunnel t) {
        if (t.Connected || t.Phase == "Connecting") {
            await Disconnect(t);
        } else {
            await Connect(t);
        }
    }

    /*
    ===============
    Connect
    Ask the helper to bring the tunnel up.
    ===============
    */
    public async Task Connect(Tunnel t) {
        foreach (var other in Tunnels.Where(x => x != t && (x.Connected || x.Phase == "Connecting")).ToList())
            await Disconnect(other);
        Banner = "";
        Raise(nameof(Banner));
        t.Phase = "Connecting";
        t.Detail = "Bringing up the interface and routes";
        if (Selected == t) {
            Touch();
        }

        if (!HelperOnline) {
            await StartHelper();
        }

        await ResolveDomains(t);
        string body = Rpc("up", t, true);
        string? reply = await Task.Run(() => Call(body));
        if (reply == null || !reply.Contains("\"ok\":true")) {
            t.Phase = "Off";
            t.Connected = false;
            t.Detail = "Traffic is bypassing WireGuard";
            Banner = ErrorOf(reply) ?? "The tunnel did not start.";
            Raise(nameof(Banner));
        } else {
            // Stay Connecting until ApplyStatus sees a real handshake.
            t.Connected = false;
            t.Phase = "Connecting";
            t.Detail = "Waiting for the handshake";
            ApplyStatus(reply);
            RememberLastActive(t.Id);
        }

        if (Selected == t) {
            Touch();
        }
    }

    /*
    ===============
    RememberLastActive
    ===============
    */
    void RememberLastActive(string id) {
        if (LastActiveId == id) {
            return;
        }

        LastActiveId = id;
        Save();
    }

    /*
    ===============
    ConnectDefaultIfNeeded
    After launch, bring up None / Last active / a fixed tunnel.
    ===============
    */
    public async Task ConnectDefaultIfNeeded() {
        var t = ResolveDefaultTunnel();
        if (t == null || t.Connected || t.Phase == "Connecting") {
            return;
        }

        Selected = t;
        await Connect(t);
    }

    /*
    ===============
    Shutdown
    Disconnect, stop the helper, then leave.
    ===============
    */
    public async Task Shutdown() {
        foreach (var t in Tunnels.Where(x => x.Connected || x.Phase == "Connecting").ToList())
            await Disconnect(t);
        if (HelperOnline) {
            await Task.Run(() => Call("{\"token\":" + JsonQuote(_token) + ",\"op\":\"quit\"}", 800));
        }

        HelperOnline = false;
    }

    /*
    ===============
    Disconnect
    Ask the helper to bring the tunnel down.
    ===============
    */
    public async Task Disconnect(Tunnel t) {
        t.Phase = "Disconnecting";
        t.Detail = "Restoring the previous routes";
        if (Selected == t) {
            Touch();
        }

        await Task.Run(() => Call(Rpc("down", t, false)));
        t.Connected = false;
        t.Phase = "Off";
        t.Detail = "Traffic is bypassing WireGuard";
        t.IfaceLine = "";
        t.Down = "0 B/s";
        t.Up = "0 B/s";
        if (Selected == t) {
            Touch();
        }
    }

    /*
    ===============
    SetPolicy
    Direct, or through the tunnel.
    ===============
    */
    public async Task SetPolicy(bool through) {
        if (Selected == null) {
            return;
        }

        Selected.SelectedPolicy = through;
        Save();
        Touch();
        await PushBypass(Selected);
    }

    /*
    ===============
    SetLan
    Keep the current network off the tunnel, or let it in.
    ===============
    */
    public async Task SetLan(bool on) {
        if (Selected == null) {
            return;
        }

        Selected.ExcludeLan = on;
        Save();
        Touch();
        await PushBypass(Selected);
    }

    /*
    ===============
    ChangedRules
    Save the route list and push it to a live tunnel.
    ===============
    */
    public async Task ChangedRules() {
        Save();
        Touch();
        await PushBypass(Selected);
    }

    /*
    ===============
    PushBypass
    Replace routes on a live tunnel. The helper pipe is single-client; Call serializes.
    ===============
    */
    async Task PushBypass(Tunnel? t) {
        if (t == null || !t.Connected) {
            return;
        }

        string body = Rpc("bypass", t, true);
        string? reply = await Task.Run(() => Call(body));
        if (reply != null && reply.Contains("\"ok\":true")) {
            ApplyStatus(reply);
            if (Banner.Length > 0 && Banner.StartsWith("Could not update routes", StringComparison.Ordinal)) {
                Banner = "";
                Raise(nameof(Banner));
            }

            return;
        }

        // One retry: a status tick may have owned the pipe for the first attempt.
        reply = await Task.Run(() => Call(body));
        if (reply != null && reply.Contains("\"ok\":true")) {
            ApplyStatus(reply);
            return;
        }

        Banner = ErrorOf(reply) ?? "Could not update routes on the live tunnel.";
        Raise(nameof(Banner));
    }

    /*
    ===============
    AddRouteList
    Read one named list. A line is an address or a domain. Resolves domains before the list is stored.
    The box stores a return as \r. Pasted text may use \n or \r\n.
    ===============
    */
    public async Task<List<RouteProblem>> AddRouteList(string name, string text, Action<string> progress, CancellationToken cancel) {
        var problems = new List<RouteProblem>();
        if (Selected == null || name.Trim().Length == 0) {
            return problems;
        }

        var list = new RouteList { Id = Id(), Name = name.Trim(), Enabled = true };
        var seen = new HashSet<string>();
        foreach (var existing in Selected.Lists) {
            foreach (var entry in existing.Entries) {
                seen.Add(entry.Kind + " " + entry.Value);
            }
        }

        bool reportedCap = false;
        foreach (var raw in text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n')) {
            cancel.ThrowIfCancellationRequested();
            string line = raw.Trim();
            if (line.Length == 0) {
                continue;
            }

            if (list.Entries.Count >= 4096) {
                problems.Add(new RouteProblem { Line = line, Message = reportedCap ? "" : "A list can hold 4096 entries." });
                reportedCap = true;
                continue;
            }

            var (kind, value, note, err) = Core.Classify(line);
            if (note.Length > 0 || kind == "error" || kind.Length == 0) {
                problems.Add(new RouteProblem { Line = line, Message = err.Length > 0 ? err : "Enter an IP, a network, or a domain." });
                continue;
            }

            if (!seen.Add(kind + " " + value)) {
                string message = kind == "domain" ? "That domain is already listed." : "That address is already listed.";
                problems.Add(new RouteProblem { Line = line, Message = message });
                continue;
            }

            list.Entries.Add(new RouteEntry { Id = Id(), Kind = kind, Value = value });
        }

        if (list.Entries.Count == 0) {
            if (problems.Count == 0) {
                problems.Add(new RouteProblem { Message = "Enter an IP, a network, or a domain." });
            }

            return problems;
        }

        var domains = list.Entries.Where(entry => entry.Kind == "domain").ToList();
        if (domains.Count == 0) {
            progress("Adding the list");
            await Task.Yield();
        } else {
            for (int i = 0; i < domains.Count; i++) {
                cancel.ThrowIfCancellationRequested();
                progress(domains[i].Value + "  " + (i + 1) + "/" + domains.Count);
                await Task.Yield();
                cancel.ThrowIfCancellationRequested();
                await ResolveEntry(domains[i]);
            }
        }

        cancel.ThrowIfCancellationRequested();
        progress("Adding the list");
        Selected.Lists.Add(list);
        await ChangedRules();
        RulesChanged?.Invoke();
        return problems;
    }

    /*
    ===============
    UpdateRouteList
    Replace one list's name and entries. Keeps resolved addresses for unchanged domains.
    ===============
    */
    public async Task<List<RouteProblem>> UpdateRouteList(RouteList list, string name, string text, Action<string> progress, CancellationToken cancel) {
        var problems = new List<RouteProblem>();
        if (Selected == null || name.Trim().Length == 0) {
            return problems;
        }

        var keptAddresses = list.Entries
            .Where(entry => entry.Kind == "domain")
            .GroupBy(entry => entry.Value, StringComparer.OrdinalIgnoreCase)
            .ToDictionary(group => group.Key, group => (group.First().Addresses.ToList(), group.First().Status), StringComparer.OrdinalIgnoreCase);

        var next = new List<RouteEntry>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var existing in Selected.Lists) {
            if (existing.Id == list.Id) {
                continue;
            }

            foreach (var entry in existing.Entries) {
                seen.Add(entry.Kind + " " + entry.Value);
            }
        }

        bool reportedCap = false;
        foreach (var raw in text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n')) {
            cancel.ThrowIfCancellationRequested();
            string line = raw.Trim();
            if (line.Length == 0) {
                continue;
            }

            if (next.Count >= 4096) {
                problems.Add(new RouteProblem { Line = line, Message = reportedCap ? "" : "A list can hold 4096 entries." });
                reportedCap = true;
                continue;
            }

            var (kind, value, note, err) = Core.Classify(line);
            if (note.Length > 0 || kind == "error" || kind.Length == 0) {
                problems.Add(new RouteProblem { Line = line, Message = err.Length > 0 ? err : "Enter an IP, a network, or a domain." });
                continue;
            }

            if (!seen.Add(kind + " " + value)) {
                string message = kind == "domain" ? "That domain is already listed." : "That address is already listed.";
                problems.Add(new RouteProblem { Line = line, Message = message });
                continue;
            }

            var entry = new RouteEntry { Id = Id(), Kind = kind, Value = value };
            if (kind == "domain" && keptAddresses.TryGetValue(value, out var prior)) {
                entry.Addresses = prior.Item1;
                entry.Status = prior.Item2;
            }

            next.Add(entry);
        }

        if (next.Count == 0) {
            if (problems.Count == 0) {
                problems.Add(new RouteProblem { Message = "Enter an IP, a network, or a domain." });
            }

            return problems;
        }

        var domains = next.Where(entry => entry.Kind == "domain" && entry.Addresses.Count == 0 && entry.Status.Length == 0).ToList();
        if (domains.Count == 0) {
            progress("Saving the list");
            await Task.Yield();
        } else {
            for (int i = 0; i < domains.Count; i++) {
                cancel.ThrowIfCancellationRequested();
                progress(domains[i].Value + "  " + (i + 1) + "/" + domains.Count);
                await Task.Yield();
                cancel.ThrowIfCancellationRequested();
                await ResolveEntry(domains[i]);
            }
        }

        cancel.ThrowIfCancellationRequested();
        progress("Saving the list");
        list.Name = name.Trim();
        list.Entries.Clear();
        list.Entries.AddRange(next);
        await ChangedRules();
        RulesChanged?.Invoke();
        return problems;
    }

    /*
    ===============
    AddProgram
    Remember an application and learn the addresses it uses.
    ===============
    */
    public void AddProgram(string path, string? name = null) {
        if (Selected == null || Selected.Apps.Count >= 12) {
            return;
        }

        if (Selected.Apps.Any(a => string.Equals(a.Path, path, StringComparison.OrdinalIgnoreCase))) {
            return;
        }

        string label = (name ?? "").Trim();
        if (label.Length == 0) {
            label = Path.GetFileName(path);
        }

        Selected.Apps.Add(new AppRule { Id = Id(), Path = path, Name = label, Enabled = true, Learn = true });
        _ = ChangedRules();
        RulesChanged?.Invoke();
    }

    /*
    ===============
    SetLaunch
    Start with Windows, or stop doing that.
    ===============
    */
    public void SetLaunch(bool on) {
        LaunchAtLogin = on;
        using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run", true);
        if (key != null) {
            string exe = Environment.ProcessPath ?? "";
            if (on) {
                key.SetValue("Fairlead", "\"" + exe + "\"");
            } else {
                key.DeleteValue("Fairlead", false);
            }
        }

        Save();
    }

    /*
    ===============
    SetStartHidden
    First launch opens in the tray only.
    ===============
    */
    public void SetStartHidden(bool on) {
        StartHidden = on;
        Save();
    }

    /*
    ===============
    SetDefault
    None, last active, or a fixed tunnel when the app opens.
    ===============
    */
    public void SetDefault(string id) {
        string key = (id ?? "").Trim();
        if (key.Length == 0) {
            DefaultId = "";
        } else if (key == LastActiveKey) {
            DefaultId = LastActiveKey;
        } else if (Tunnels.Any(t => t.Id == key)) {
            DefaultId = key;
        } else {
            DefaultId = "";
        }

        Save();
    }

    /*
    ===============
    OpenFolder
    Open the config folder.
    ===============
    */
    public void OpenFolder() => Process.Start(new ProcessStartInfo(_dir) { UseShellExecute = true });

    /*
    ===============
    EnsureHelper
    Start the helper when it is not already up.
    ===============
    */
    public async Task EnsureHelper() {
        if (await PingHelper()) {
            MarkHelperOnline();
            return;
        }

        // Auto-start: scheduled task only. No UAC prompt behind the window.
        await StartHelper(interactive: false);
    }

    /*
    ===============
    StartHelper
    Settings button: allow the elevation prompt.
    ===============
    */
    public Task StartHelper() => StartHelper(interactive: true);

    /*
    ===============
    StartHelper
    Bring the helper up. interactive=true may show UAC.
    ===============
    */
    async Task StartHelper(bool interactive) {
        string exe = Path.Combine(_exeDir, "fairlead-helper.exe");
        if (!File.Exists(exe)) {
            Banner = "fairlead-helper.exe is missing from " + _exeDir;
            Raise(nameof(Banner));
            return;
        }

        await _helperStart.WaitAsync();
        try {
            if (await PingHelper()) {
                MarkHelperOnline();
                return;
            }

            if (await Task.Run(RunScheduledHelper) && await WaitHelper(16)) {
                return;
            }

            if (!interactive) {
                return;
            }

            if (!await LaunchHelper(exe)) {
                return;
            }

            if (await WaitHelper(20)) {
                return;
            }

            await Task.Run(StopHelpers);
            if (!await LaunchHelper(exe)) {
                return;
            }

            if (!await WaitHelper(20)) {
                Banner = "The helper did not start.";
                Raise(nameof(Banner));
            }
        } finally {
            _helperStart.Release();
        }
    }

    /*
    ===============
    LaunchHelper
    Start the helper next to this app, elevating when needed.
    ===============
    */
    async Task<bool> LaunchHelper(string exe) {
        var info = new ProcessStartInfo(exe, "--helper " + _token) {
            UseShellExecute = true,
            WorkingDirectory = _exeDir,
            WindowStyle = ProcessWindowStyle.Hidden
        };
        if (!IsAdmin()) {
            info.Verb = "runas";
        }

        try {
            Process? proc = Process.Start(info);
            if (proc == null) {
                return false;
            }

            await Task.Delay(300);
            return true;
        } catch {
            Banner = "The helper start was cancelled.";
            Raise(nameof(Banner));
            return false;
        }
    }

    /*
    ===============
    PingHelper
    One short ping against the live pipe.
    ===============
    */
    async Task<bool> PingHelper() {
        string? reply = await Task.Run(() => Call("{\"token\":" + JsonQuote(_token) + ",\"op\":\"ping\"}", 400));
        return reply != null && reply.Contains("\"ok\":true");
    }

    /*
    ===============
    WaitHelper
    Poll until the helper answers, or give up (~tries * 0.35s).
    ===============
    */
    async Task<bool> WaitHelper(int tries) {
        for (int i = 0; i < tries; i++) {
            await Task.Delay(150);
            if (await PingHelper()) {
                MarkHelperOnline();
                return true;
            }
        }

        return false;
    }

    void MarkHelperOnline() {
        HelperOnline = true;
        Raise(nameof(HelperText));
        RefreshTools();
    }

    /*
    ===============
    StopHelpers
    End every fairlead-helper process we can. Elevated peers need the new helper to claim the pipe.
    ===============
    */
    static void StopHelpers() {
        Process[] list;
        try {
            list = Process.GetProcessesByName("fairlead-helper");
        } catch {
            return;
        }

        foreach (var proc in list) {
            try {
                if (!proc.HasExited) {
                    proc.Kill(entireProcessTree: true);
                    proc.WaitForExit(4000);
                }
            } catch {
            } finally {
                proc.Dispose();
            }
        }
    }

    /*
    ===============
    RunScheduledHelper
    Start the helper from the saved task.
    ===============
    */
    static bool RunScheduledHelper() {
        try {
            var info = new ProcessStartInfo("schtasks.exe", "/Run /TN FairleadHelper") {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            using var proc = Process.Start(info);
            if (proc == null || !proc.WaitForExit(8000)) {
                return false;
            }

            return proc.ExitCode == 0;
        } catch {
            return false;
        }
    }

    public void ClearBanner() {
        Banner = "";
        Raise(nameof(Banner));
    }

    /*
    ===============
    SaveConfig
    Replace a tunnel's config and refresh its labels.
    ===============
    */
    public void SaveConfig(Tunnel t, string config) {
        t.ConfigText = config;
        RefreshLabels(t);
        Save();
        if (t.Connected) {
            _ = Disconnect(t);
        }
    }

    /*
    ===============
    ResolveDomains
    Resolve enabled domains on the regular network.
    ===============
    */
    public async Task ResolveDomains(Tunnel t) {
        var pending = new List<RouteEntry>();
        foreach (var list in t.Lists) {
            if (!list.Enabled) {
                continue;
            }

            foreach (var entry in list.Entries) {
                if (entry.Kind == "domain" && entry.Addresses.Count == 0 && entry.Status.Length == 0 && _resolving.Add(entry.Id)) {
                    pending.Add(entry);
                }
            }
        }

        if (pending.Count == 0) {
            return;
        }

        bool changed = false;
        foreach (var entry in pending) {
            try {
                await ResolveEntry(entry);
                changed = true;
            } finally {
                _resolving.Remove(entry.Id);
            }
        }

        if (!changed) {
            return;
        }

        Save();
        RulesChanged?.Invoke();
        await PushBypass(t);
    }

    /*
    ===============
    Learn
    Remember addresses the enabled apps have open.
    ===============
    */
    async Task Learn() {
        var t = Tunnels.FirstOrDefault(x => x.Connected);
        if (t == null || t.Apps.All(a => !a.Learn || !a.Enabled)) {
            return;
        }

        var found = await Task.Run(SnapshotTcp);
        bool changed = false;
        foreach (var app in t.Apps.Where(a => a.Enabled && a.Learn)) {
            foreach (var pair in found) {
                if (!BelongsTo(pair.Key, app.Path)) {
                    continue;
                }

                foreach (var ip in pair.Value) {
                    string cidr = ip + "/32";
                    if (app.Learned.Contains(cidr) || app.Learned.Count >= 400) {
                        continue;
                    }

                    app.Learned.Add(cidr);
                    changed = true;
                }
            }
        }

        if (changed) {
            Save();
            Touch();
            RulesChanged?.Invoke();
            await PushBypass(t);
        }
    }

    /*
    ===============
    ResolveEntry
    Fill one domain from the regular network. At most eight IPv4 addresses.
    ===============
    */
    async Task ResolveEntry(RouteEntry entry) {
        try {
            var ips = await Task.Run(() => Dns.GetHostAddresses(entry.Value));
            entry.Addresses = ips.Where(a => a.AddressFamily == AddressFamily.InterNetwork).Select(a => a.ToString()).Distinct().Take(8).ToList();
            entry.Status = entry.Addresses.Count == 0 ? "Could not resolve" : "";
        } catch {
            entry.Status = "Could not resolve";
        }
    }

    /*
    ===============
    ReadLists
    routeLists, or one list per rule from a tunnel saved before lists.
    ===============
    */
    void ReadLists(JsonElement item, Tunnel t) {
        if (item.TryGetProperty("routeLists", out var lists)) {
            foreach (var one in lists.EnumerateArray()) {
                var list = new RouteList {
                    Id = Str(one, "id"),
                    Name = Str(one, "name"),
                    Enabled = !one.TryGetProperty("enabled", out var en) || en.GetBoolean()
                };
                if (list.Id.Length == 0) {
                    list.Id = Id();
                }

                if (one.TryGetProperty("entries", out var entries)) {
                    foreach (var entry in entries.EnumerateArray()) {
                        var row = new RouteEntry { Id = Str(entry, "id"), Kind = Str(entry, "kind"), Value = Str(entry, "value") };
                        if (row.Id.Length == 0) {
                            row.Id = Id();
                        }

                        if (entry.TryGetProperty("addresses", out var addrs)) {
                            foreach (var a in addrs.EnumerateArray()) {
                                row.Addresses.Add(a.GetString() ?? "");
                            }
                        }

                        list.Entries.Add(row);
                    }
                }

                t.Lists.Add(list);
            }

            return;
        }

        if (item.TryGetProperty("ipRules", out var ips)) {
            foreach (var one in ips.EnumerateArray()) {
                string cidr = Str(one, "cidr");
                string note = Str(one, "note").Trim();
                var list = new RouteList {
                    Id = Str(one, "id"),
                    Name = note.Length > 0 ? note : cidr,
                    Enabled = !one.TryGetProperty("enabled", out var en) || en.GetBoolean()
                };
                if (list.Id.Length == 0) {
                    list.Id = Id();
                }

                list.Entries.Add(new RouteEntry { Id = Id(), Kind = "ip", Value = cidr });
                t.Lists.Add(list);
            }
        }

        if (item.TryGetProperty("domainRules", out var doms)) {
            foreach (var one in doms.EnumerateArray()) {
                string domain = Str(one, "name");
                string note = Str(one, "note").Trim();
                var list = new RouteList {
                    Id = Str(one, "id"),
                    Name = note.Length > 0 ? note : domain,
                    Enabled = !one.TryGetProperty("enabled", out var en) || en.GetBoolean()
                };
                if (list.Id.Length == 0) {
                    list.Id = Id();
                }

                var row = new RouteEntry { Id = Id(), Kind = "domain", Value = domain };
                if (one.TryGetProperty("addresses", out var addrs)) {
                    foreach (var a in addrs.EnumerateArray()) {
                        row.Addresses.Add(a.GetString() ?? "");
                    }
                }

                list.Entries.Add(row);
                t.Lists.Add(list);
            }
        }
    }

    /*
    ===============
    Load
    Read tunnels and settings from disk.
    ===============
    */
    void Load() {
        string path = Path.Combine(_dir, "tunnels.json");
        if (!File.Exists(path)) {
            return;
        }

        try {
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var root = doc.RootElement;
            if (root.TryGetProperty("launchAtLogin", out var launch)) {
                LaunchAtLogin = launch.GetBoolean();
            }

            if (root.TryGetProperty("startHidden", out var hidden)) {
                StartHidden = hidden.GetBoolean();
            }

            if (root.TryGetProperty("defaultTunnel", out var def)) {
                DefaultId = def.GetString() ?? "";
            }

            if (root.TryGetProperty("lastActiveTunnel", out var last)) {
                LastActiveId = last.GetString() ?? "";
            }

            if (!root.TryGetProperty("tunnels", out var list)) {
                return;
            }

            foreach (var item in list.EnumerateArray()) {
                if (Tunnels.Count >= 24) {
                    break;
                }

                var t = new Tunnel {
                    Id = Str(item, "id"),
                    Name = Str(item, "name"),
                    Slug = Str(item, "slug"),
                    ConfigText = Str(item, "configText"),
                    ExcludeLan = !item.TryGetProperty("excludeLAN", out var lan) || lan.GetBoolean(),
                    SelectedPolicy = Str(item, "routePolicy") == "selected"
                };
                if (t.Id.Length == 0) {
                    t.Id = Id();
                }

                if (t.Slug.Length == 0) {
                    t.Slug = Slug();
                }

                ReadLists(item, t);

                if (item.TryGetProperty("appRules", out var apps))
                    foreach (var one in apps.EnumerateArray()) {
                        var a = new AppRule { Id = Str(one, "id"), Path = Str(one, "path"), Name = Str(one, "name"), Enabled = !one.TryGetProperty("enabled", out var en) || en.GetBoolean(), Learn = !one.TryGetProperty("learn", out var learn) || learn.GetBoolean() };
                        if (one.TryGetProperty("learned", out var learned))
                            foreach (var ip in learned.EnumerateArray()) a.Learned.Add(ip.GetString() ?? "");
                        t.Apps.Add(a);
                    }

                RefreshLabels(t);
                Tunnels.Add(t);
            }
        } catch { }
    }

    /*
    ===============
    Save
    Write tunnels and settings to disk.
    ===============
    */
    public void Save() {
        var sb = new StringBuilder();
        sb.Append("{\n  \"launchAtLogin\": ").Append(LaunchAtLogin ? "true" : "false");
        sb.Append(",\n  \"startHidden\": ").Append(StartHidden ? "true" : "false");
        sb.Append(",\n  \"defaultTunnel\": ").Append(JsonQuote(DefaultId));
        sb.Append(",\n  \"lastActiveTunnel\": ").Append(JsonQuote(LastActiveId));
        sb.Append(",\n  \"tunnels\": [\n");
        for (int i = 0; i < Tunnels.Count; i++) {
            var t = Tunnels[i];
            sb.Append("    {\"id\":").Append(JsonQuote(t.Id));
            sb.Append(",\"name\":").Append(JsonQuote(t.Name));
            sb.Append(",\"slug\":").Append(JsonQuote(t.Slug));
            sb.Append(",\"configText\":").Append(JsonQuote(t.ConfigText));
            sb.Append(t.ExcludeLan ? ",\"excludeLAN\":true" : ",\"excludeLAN\":false");
            sb.Append(t.SelectedPolicy ? ",\"routePolicy\":\"selected\"" : ",\"routePolicy\":\"exceptions\"");
            sb.Append(",\"routeLists\":[");
            for (int k = 0; k < t.Lists.Count; k++) {
                if (k > 0) {
                    sb.Append(',');
                }

                var list = t.Lists[k];
                sb.Append("{\"id\":").Append(JsonQuote(list.Id)).Append(",\"name\":").Append(JsonQuote(list.Name));
                sb.Append(list.Enabled ? ",\"enabled\":true" : ",\"enabled\":false").Append(",\"entries\":[");
                for (int n = 0; n < list.Entries.Count; n++) {
                    if (n > 0) {
                        sb.Append(',');
                    }

                    var entry = list.Entries[n];
                    sb.Append("{\"id\":").Append(JsonQuote(entry.Id)).Append(",\"kind\":").Append(JsonQuote(entry.Kind));
                    sb.Append(",\"value\":").Append(JsonQuote(entry.Value)).Append(",\"addresses\":[");
                    for (int a = 0; a < entry.Addresses.Count; a++) {
                        if (a > 0) {
                            sb.Append(',');
                        }

                        sb.Append(JsonQuote(entry.Addresses[a]));
                    }

                    sb.Append("]}");
                }

                sb.Append("]}");
            }

            sb.Append("],\"appRules\":[");
            for (int k = 0; k < t.Apps.Count; k++) {
                if (k > 0) {
                    sb.Append(',');
                }

                var r = t.Apps[k];
                sb.Append("{\"id\":").Append(JsonQuote(r.Id)).Append(",\"path\":").Append(JsonQuote(r.Path)).Append(",\"name\":").Append(JsonQuote(r.Name));
                sb.Append(r.Enabled ? ",\"enabled\":true" : ",\"enabled\":false");
                sb.Append(r.Learn ? ",\"learn\":true" : ",\"learn\":false").Append(",\"learned\":[");
                for (int a = 0; a < r.Learned.Count; a++) {
                    if (a > 0) {
                        sb.Append(',');
                    }

                    sb.Append(JsonQuote(r.Learned[a]));
                }
                sb.Append("]}");
            }

            sb.Append("]}");
            sb.Append(i + 1 < Tunnels.Count ? ",\n" : "\n");
        }

        sb.Append("  ]\n}\n");
        File.WriteAllText(Path.Combine(_dir, "tunnels.json"), sb.ToString());
    }

    /*
    ===============
    Rpc
    One helper call, with the pipe opened for it.
    ===============
    */
    string Rpc(string op, Tunnel t, bool withConfig) {
        var sb = new StringBuilder();
        sb.Append("{\"token\":").Append(JsonQuote(_token)).Append(",\"op\":").Append(JsonQuote(op));
        sb.Append(",\"name\":").Append(JsonQuote(t.Slug));
        if (withConfig && op == "up") {
            sb.Append(",\"config\":").Append(JsonQuote(t.ConfigText));
        }

        if (op == "up" || op == "bypass") {
            sb.Append(",\"policy\":").Append(t.SelectedPolicy ? "\"selected\"" : "\"exceptions\"");
            sb.Append(t.ExcludeLan ? ",\"excludeLAN\":true" : ",\"excludeLAN\":false");
            sb.Append(",\"cidrs\":[");
            bool first = true;
            foreach (var list in t.Lists.Where(x => x.Enabled)) {
                foreach (var entry in list.Entries) {
                    if (entry.Kind == "ip") {
                        if (!first) {
                            sb.Append(',');
                        }

                        first = false;
                        sb.Append(JsonQuote(entry.Value));
                        continue;
                    }

                    foreach (var ip in entry.Addresses) {
                        if (!first) {
                            sb.Append(',');
                        }

                        first = false;
                        sb.Append(JsonQuote(ip));
                    }
                }
            }

            foreach (var a in t.Apps.Where(x => x.Enabled)) {
                foreach (var ip in a.Learned) {
                    if (!first) {
                        sb.Append(',');
                    }

                    first = false;
                    sb.Append(JsonQuote(ip));
                }
            }
            sb.Append(']');
        }

        sb.Append('}');
        return sb.ToString();
    }

    /*
    ===============
    Call
    Send one JSON message and return the reply.
    ===============
    */
    string? Call(string body, int connectMs = 5000) {
        if (!_pipe.Wait(connectMs + 1000)) {
            return null;
        }

        try {
            using var pipe = new NamedPipeClientStream(".", "fairlead", PipeDirection.InOut);
            pipe.Connect(Math.Max(50, connectMs));
            byte[] bytes = Encoding.UTF8.GetBytes(body);
            var hdr = new byte[] { (byte)(bytes.Length >> 24), (byte)(bytes.Length >> 16), (byte)(bytes.Length >> 8), (byte)bytes.Length };
            pipe.Write(hdr);
            pipe.Write(bytes);
            if (pipe.Read(hdr, 0, 4) != 4) {
                return null;
            }

            int n = (hdr[0] << 24) | (hdr[1] << 16) | (hdr[2] << 8) | hdr[3];
            if (n < 0 || n > 2_000_000) {
                return null;
            }

            var buf = new byte[n];
            int got = 0;
            while (got < n) {
                int c = pipe.Read(buf, got, n - got);
                if (c <= 0) {
                    break;
                }

                got += c;
            }

            return Encoding.UTF8.GetString(buf, 0, got);
        } catch {
            return null;
        } finally {
            _pipe.Release();
        }
    }

    /*
    ===============
    ErrorOf
    The error string in a helper reply, or null.
    ===============
    */
    static string? ErrorOf(string? reply) {
        if (reply == null) {
            return null;
        }

        try {
            using var doc = JsonDocument.Parse(reply);
            if (doc.RootElement.TryGetProperty("error", out var err)) {
                return err.GetString();
            }
        } catch { }
        return null;
    }

    /*
    ===============
    LoadToken
    The pipe token, created once and kept beside the configs.
    ===============
    */
    string LoadToken() {
        string path = Path.Combine(_dir, "helper.token");
        if (File.Exists(path)) {
            string t = File.ReadAllText(path).Trim();
            if (t.Length > 8) {
                return t;
            }
        }

        string made = Convert.ToHexString(Guid.NewGuid().ToByteArray())[..32].ToLowerInvariant();
        File.WriteAllText(path, made);
        return made;
    }

    /*
    ===============
    NewTunnel
    A blank tunnel in the editor.
    ===============
    */
    static Tunnel NewTunnel(string name, string config) => new() { Id = Id(), Name = name, Slug = Slug(), ConfigText = config, ExcludeLan = true };

    /*
    ===============
    Id
    A short tunnel id.
    ===============
    */
    static string Id() => Guid.NewGuid().ToString("N")[..16];

    /*
    ===============
    Slug
    The interface name handed to the userspace daemon.
    ===============
    */
    static string Slug() => "fld" + Convert.ToHexString(Guid.NewGuid().ToByteArray())[..8].ToLowerInvariant();

    /*
    ===============
    Str
    A JSON string field, or empty.
    ===============
    */
    static string Str(JsonElement e, string key) => e.TryGetProperty(key, out var v) ? v.GetString() ?? "" : "";

    /*
    ===============
    JsonQuote
    A JSON string.
    ===============
    */
    static string JsonQuote(string? text) {
        var sb = new StringBuilder("\"");
        foreach (char c in text ?? "") {
            if (c == '"' || c == '\\') {
                sb.Append('\\').Append(c);
            } else if (c == '\n') {
                sb.Append("\\n");
            } else if (c == '\r') {
                sb.Append("\\r");
            } else {
                sb.Append(c);
            }
        }

        return sb.Append('"').ToString();
    }

    /*
    ===============
    Bytes
    UTF-8 up to the first zero.
    ===============
    */
    static string Bytes(double n) {
        string[] u = { "B", "KB", "MB", "GB" };
        int i = 0;
        while (n >= 1024 && i < 3) {
            n /= 1024;
            i++;
        }
        return i == 0 ? ((ulong)n) + " B" : (n >= 10 ? n.ToString("0") : n.ToString("0.0")) + " " + u[i];
    }

    /*
    ===============
    Rate
    A size per second.
    ===============
    */
    static string Rate(double n) => Bytes(Math.Max(0, n)) + "/s";

    /*
    ===============
    Ago
    How long ago a handshake was, or empty.
    ===============
    */
    static string Ago(int stamp) {
        if (stamp <= 0) {
            return "none";
        }

        int s = (int)DateTimeOffset.Now.ToUnixTimeSeconds() - stamp;
        if (s < 5) {
            return "just now";
        }

        if (s < 60) {
            return s + " s ago";
        }

        if (s < 3600) {
            return (s / 60) + " min ago";
        }

        if (s < 86400) {
            return (s / 3600) + " h ago";
        }

        return "a while ago";
    }

    /*
    ===============
    IsAdmin
    True when this process is elevated.
    ===============
    */
    static bool IsAdmin() {
        using var id = WindowsIdentity.GetCurrent();
        return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);
    }

    /*
    ===============
    BelongsTo
    Same exe, or any process under the install folder (launcher → updater → app).
    ===============
    */
    static bool BelongsTo(string processPath, string appPath) {
        if (processPath.Length == 0 || appPath.Length == 0) {
            return false;
        }

        if (string.Equals(processPath, appPath, StringComparison.OrdinalIgnoreCase)) {
            return true;
        }

        string? root = Path.GetDirectoryName(appPath);
        if (string.IsNullOrEmpty(root)) {
            return false;
        }

        root = root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            + Path.DirectorySeparatorChar;
        return processPath.StartsWith(root, StringComparison.OrdinalIgnoreCase);
    }

/*
===============
SnapshotTcp
===============
*/
    static Dictionary<string, List<string>> SnapshotTcp() {
        var map = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        int size = 0;
        GetExtendedTcpTable(IntPtr.Zero, ref size, false, 2, 5, 0);
        IntPtr buf = Marshal.AllocHGlobal(size);
        try {
            if (GetExtendedTcpTable(buf, ref size, false, 2, 5, 0) != 0) {
                return map;
            }

            int count = Marshal.ReadInt32(buf);
            int row = 24;
            for (int i = 0; i < count; i++) {
                IntPtr p = buf + 4 + i * row;
                uint remote = (uint)Marshal.ReadInt32(p, 8);
                uint pid = (uint)Marshal.ReadInt32(p, 20);
                if (remote == 0 || pid == 0) {
                    continue;
                }

                string ip = new IPAddress(remote).ToString();
                string image = ImagePath(pid);
                if (image.Length == 0) {
                    continue;
                }

                if (!map.TryGetValue(image, out var list)) {
                    map[image] = list = new List<string>();
                }

                if (!list.Contains(ip)) {
                    list.Add(ip);
                }
            }
        } finally {
            Marshal.FreeHGlobal(buf);
        }
        return map;
    }

    /*
    ===============
    ImagePath
    The executable path of a process.
    ===============
    */
    static string ImagePath(uint pid) {
        IntPtr h = OpenProcess(0x1000, false, pid);
        if (h == IntPtr.Zero) {
            return "";
        }

        var sb = new StringBuilder(520);
        int n = sb.Capacity;
        bool ok = QueryFullProcessImageName(h, 0, sb, ref n);
        CloseHandle(h);
        return ok ? sb.ToString() : "";
    }

/*
===============
GetExtendedTcpTable
===============
*/
    [DllImport("iphlpapi.dll")]
    static extern uint GetExtendedTcpTable(IntPtr table, ref int size, bool order, uint family, int tableClass, uint reserved);

/*
===============
OpenProcess
===============
*/
    [DllImport("kernel32.dll")]
    static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);

/*
===============
CloseHandle
===============
*/
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);

/*
===============
QueryFullProcessImageName
===============
*/
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern bool QueryFullProcessImageName(IntPtr handle, int flags, StringBuilder name, ref int size);
}
