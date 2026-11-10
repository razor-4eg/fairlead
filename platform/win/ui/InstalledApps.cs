//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// InstalledApps.cs -- Start Menu shortcuts plus Steam / Epic / GOG /
// Ubisoft / Xbox game installs that resolve to an .exe.
//
//================================================================

using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace Fairlead;

public sealed class InstalledApp {
    public string Name { get; init; } = "";
    public string Path { get; init; } = "";
}

public static class InstalledApps {
    static readonly Regex VdfKey = new(
        @"^\s*""(?<key>[^""]+)""\s*""(?<val>[^""]*)""",
        RegexOptions.Compiled | RegexOptions.Multiline);

    static readonly HashSet<string> SkipExe = new(StringComparer.OrdinalIgnoreCase) {
        "unitycrashhandler64.exe", "unitycrashhandler32.exe", "unitycrashhandler.exe",
        "crashpad_handler.exe", "crashreporter.exe", "vcredist", "vc_redist",
        "dxsetup.exe", "dotnetfx.exe", "uninstall.exe", "unins000.exe",
        "setup.exe", "installer.exe", "notification_helper.exe",
        "steamerrorreporter.exe", "steam.exe", "epicgameslauncher.exe",
        "easervices.exe", "eadesktop.exe", "origin.exe", "upc.exe",
        "ubisoftconnect.exe", "galaxyclient.exe", "battle.net.exe",
        "riotclientservices.exe", "wegame.exe"
    };

    /*
    ===============
    Scan
    Start Menu first, then launcher libraries. Launcher names win on conflict.
    ===============
    */
    public static List<InstalledApp> Scan() {
        var byPath = new Dictionary<string, InstalledApp>(StringComparer.OrdinalIgnoreCase);
        ScanStartMenu(byPath);
        ScanSteam(byPath);
        ScanEpic(byPath);
        ScanGog(byPath);
        ScanUbisoft(byPath);
        ScanXbox(byPath);
        ScanPublisherUninstall(byPath);
        return byPath.Values
            .OrderBy(app => app.Name, StringComparer.CurrentCultureIgnoreCase)
            .ToList();
    }

    /*
    ===============
    ScanStartMenu
    ===============
    */
    static void ScanStartMenu(Dictionary<string, InstalledApp> byPath) {
        foreach (var root in StartRoots()) {
            if (!Directory.Exists(root)) {
                continue;
            }

            string[] links;
            try {
                links = Directory.GetFiles(root, "*.lnk", SearchOption.AllDirectories);
            } catch {
                continue;
            }

            foreach (var link in links) {
                try {
                    string name = System.IO.Path.GetFileNameWithoutExtension(link);
                    if (SkipName(name)) {
                        continue;
                    }

                    string? target = ResolveLink(link);
                    Put(byPath, name, target, preferName: false);
                } catch {
                }
            }
        }
    }

    /*
    ===============
    StartRoots
    ===============
    */
    static IEnumerable<string> StartRoots() {
        yield return Environment.GetFolderPath(Environment.SpecialFolder.Programs);
        yield return Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms);
        string start = Environment.GetFolderPath(Environment.SpecialFolder.StartMenu);
        if (start.Length > 0) {
            yield return System.IO.Path.Combine(start, "Programs");
        }
    }

    /*
    ===============
    ScanSteam
    libraryfolders.vdf + appmanifest_*.acf → common/<installdir>.
    ===============
    */
    static void ScanSteam(Dictionary<string, InstalledApp> byPath) {
        string? steam = SteamRoot();
        if (steam == null) {
            return;
        }

        foreach (var library in SteamLibraries(steam)) {
            string apps = System.IO.Path.Combine(library, "steamapps");
            if (!Directory.Exists(apps)) {
                continue;
            }

            string[] manifests;
            try {
                manifests = Directory.GetFiles(apps, "appmanifest_*.acf");
            } catch {
                continue;
            }

            foreach (var manifest in manifests) {
                try {
                    AddSteamManifest(byPath, apps, manifest);
                } catch {
                }
            }
        }
    }

    /*
    ===============
    SteamRoot
    ===============
    */
    static string? SteamRoot() {
        try {
            using var key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");
            string? path = key?.GetValue("SteamPath") as string;
            if (!string.IsNullOrWhiteSpace(path) && Directory.Exists(path)) {
                return path.Replace('/', '\\');
            }
        } catch {
        }

        foreach (var candidate in new[] {
            System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"),
            System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Steam")
        }) {
            if (Directory.Exists(candidate)) {
                return candidate;
            }
        }

        return null;
    }

    /*
    ===============
    SteamLibraries
    ===============
    */
    static IEnumerable<string> SteamLibraries(string steam) {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { steam };
        yield return steam;

        string vdf = System.IO.Path.Combine(steam, "steamapps", "libraryfolders.vdf");
        if (!File.Exists(vdf)) {
            yield break;
        }

        string text;
        try {
            text = File.ReadAllText(vdf);
        } catch {
            yield break;
        }

        foreach (Match m in VdfKey.Matches(text)) {
            if (!string.Equals(m.Groups["key"].Value, "path", StringComparison.OrdinalIgnoreCase)) {
                continue;
            }

            string path = UnescapeVdf(m.Groups["val"].Value);
            if (path.Length == 0 || !Directory.Exists(path) || !seen.Add(path)) {
                continue;
            }

            yield return path;
        }
    }

    /*
    ===============
    AddSteamManifest
    ===============
    */
    static void AddSteamManifest(Dictionary<string, InstalledApp> byPath, string apps, string manifest) {
        string text = File.ReadAllText(manifest);
        string name = VdfValue(text, "name");
        string installdir = VdfValue(text, "installdir");
        string flags = VdfValue(text, "StateFlags");
        if (name.Length == 0 || installdir.Length == 0) {
            return;
        }

        if (flags.Length > 0 && uint.TryParse(flags, out uint state) && (state & 4) == 0) {
            return;
        }

        if (SkipSteamName(name)) {
            return;
        }

        string dir = System.IO.Path.Combine(apps, "common", installdir);
        string? exe = FindExe(dir, name, installdir);
        Put(byPath, name, exe, preferName: true);
    }

    /*
    ===============
    SkipSteamName
    ===============
    */
    static bool SkipSteamName(string name) {
        string lower = name.ToLowerInvariant();
        return lower.Contains("redistributable")
            || lower.Contains("steamworks")
            || lower.Contains("proton")
            || lower.Contains("steam linux runtime")
            || lower.Contains("dedicated server")
            || lower.Contains("server dedicated")
            || lower.Contains("sdk")
            || lower.Contains("benchmark")
            || lower.EndsWith(" soundtrack")
            || lower.Contains("ost ")
            || lower.StartsWith("soundtrack");
    }

    /*
    ===============
    ScanEpic
    %ProgramData%\Epic\...\Manifests\*.item
    ===============
    */
    static void ScanEpic(Dictionary<string, InstalledApp> byPath) {
        string root = System.IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
            "Epic", "EpicGamesLauncher", "Data", "Manifests");
        if (!Directory.Exists(root)) {
            return;
        }

        string[] items;
        try {
            items = Directory.GetFiles(root, "*.item");
        } catch {
            return;
        }

        foreach (var item in items) {
            try {
                AddEpicItem(byPath, item);
            } catch {
            }
        }
    }

    /*
    ===============
    AddEpicItem
    ===============
    */
    static void AddEpicItem(Dictionary<string, InstalledApp> byPath, string item) {
        using var doc = JsonDocument.Parse(File.ReadAllText(item));
        var root = doc.RootElement;
        string name = JsonString(root, "DisplayName");
        string location = JsonString(root, "InstallLocation");
        string launch = JsonString(root, "LaunchExecutable");
        if (name.Length == 0 || location.Length == 0 || launch.Length == 0) {
            return;
        }

        if (SkipName(name) || launch.EndsWith(".bat", StringComparison.OrdinalIgnoreCase)) {
            return;
        }

        string exe = System.IO.Path.GetFullPath(System.IO.Path.Combine(location, launch));
        Put(byPath, name, exe, preferName: true);
    }

    /*
    ===============
    ScanGog
    HKLM\...\GOG.com\Games\<id>
    ===============
    */
    static void ScanGog(Dictionary<string, InstalledApp> byPath) {
        foreach (var hive in new[] {
            @"SOFTWARE\WOW6432Node\GOG.com\Games",
            @"SOFTWARE\GOG.com\Games"
        }) {
            try {
                using var key = Registry.LocalMachine.OpenSubKey(hive);
                if (key == null) {
                    continue;
                }

                foreach (var id in key.GetSubKeyNames()) {
                    try {
                        using var game = key.OpenSubKey(id);
                        if (game == null) {
                            continue;
                        }

                        string name = (game.GetValue("gameName") as string
                            ?? game.GetValue("DISPLAYNAME") as string
                            ?? "").Trim();
                        string path = (game.GetValue("path") as string
                            ?? game.GetValue("PATH") as string
                            ?? "").Trim();
                        string exeName = (game.GetValue("exe") as string
                            ?? game.GetValue("EXE") as string
                            ?? "").Trim();
                        if (name.Length == 0 || path.Length == 0) {
                            continue;
                        }

                        string? exe = exeName.Length > 0
                            ? System.IO.Path.Combine(path, System.IO.Path.GetFileName(exeName))
                            : FindExe(path, name, System.IO.Path.GetFileName(path));
                        Put(byPath, name, exe, preferName: true);
                    } catch {
                    }
                }
            } catch {
            }
        }
    }

    /*
    ===============
    ScanUbisoft
    HKLM\...\Ubisoft\Launcher\Installs\<id>\InstallDir
    ===============
    */
    static void ScanUbisoft(Dictionary<string, InstalledApp> byPath) {
        foreach (var hive in new[] {
            @"SOFTWARE\WOW6432Node\Ubisoft\Launcher\Installs",
            @"SOFTWARE\Ubisoft\Launcher\Installs"
        }) {
            try {
                using var key = Registry.LocalMachine.OpenSubKey(hive);
                if (key == null) {
                    continue;
                }

                foreach (var id in key.GetSubKeyNames()) {
                    try {
                        using var game = key.OpenSubKey(id);
                        string? dir = game?.GetValue("InstallDir") as string;
                        if (string.IsNullOrWhiteSpace(dir) || !Directory.Exists(dir)) {
                            continue;
                        }

                        string name = System.IO.Path.GetFileName(dir.TrimEnd('\\', '/'));
                        if (SkipName(name)) {
                            continue;
                        }

                        Put(byPath, name, FindExe(dir, name, name), preferName: true);
                    } catch {
                    }
                }
            } catch {
            }
        }
    }

    /*
    ===============
    ScanXbox
    XboxGames\<title>\Content with MicrosoftGame.config when present.
    ===============
    */
    static void ScanXbox(Dictionary<string, InstalledApp> byPath) {
        foreach (var root in XboxRoots()) {
            if (!Directory.Exists(root)) {
                continue;
            }

            string[] titles;
            try {
                titles = Directory.GetDirectories(root);
            } catch {
                continue;
            }

            foreach (var title in titles) {
                try {
                    AddXboxTitle(byPath, title);
                } catch {
                }
            }
        }
    }

    /*
    ===============
    XboxRoots
    ===============
    */
    static IEnumerable<string> XboxRoots() {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var candidate in new[] {
            System.IO.Path.Combine(System.IO.Path.GetPathRoot(Environment.SystemDirectory) ?? "C:\\", "XboxGames"),
            @"D:\XboxGames",
            @"E:\XboxGames",
            @"F:\XboxGames"
        }) {
            if (Directory.Exists(candidate) && seen.Add(candidate)) {
                yield return candidate;
            }
        }
    }

    /*
    ===============
    AddXboxTitle
    ===============
    */
    static void AddXboxTitle(Dictionary<string, InstalledApp> byPath, string titleDir) {
        string content = System.IO.Path.Combine(titleDir, "Content");
        string dir = Directory.Exists(content) ? content : titleDir;
        string name = System.IO.Path.GetFileName(titleDir);
        string config = System.IO.Path.Combine(dir, "MicrosoftGame.config");
        string? exe = null;

        if (File.Exists(config)) {
            try {
                string xml = File.ReadAllText(config);
                var nameMatch = Regex.Match(xml, @"<ShellVisuals[^>]*DefaultDisplayName=""([^""]+)""", RegexOptions.IgnoreCase);
                if (!nameMatch.Success) {
                    nameMatch = Regex.Match(xml, @"<Identity[^>]*Name=""([^""]+)""", RegexOptions.IgnoreCase);
                }

                if (nameMatch.Success && nameMatch.Groups[1].Value.Trim().Length > 0) {
                    name = nameMatch.Groups[1].Value.Trim();
                }

                var exeMatch = Regex.Match(xml, @"<Executable\s+Name=""([^""]+\.exe)""", RegexOptions.IgnoreCase);
                if (exeMatch.Success) {
                    exe = System.IO.Path.Combine(dir, exeMatch.Groups[1].Value.Replace('/', '\\'));
                }
            } catch {
            }
        }

        exe ??= FindExe(dir, name, name);
        Put(byPath, name, exe, preferName: true);
    }

    /*
    ===============
    ScanPublisherUninstall
    Catch EA / Battle.net / Rockstar / Riot installs that register an .exe icon.
    ===============
    */
    static void ScanPublisherUninstall(Dictionary<string, InstalledApp> byPath) {
        foreach (var root in new[] {
            Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall"),
            Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"),
            Registry.LocalMachine.OpenSubKey(@"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall")
        }) {
            if (root == null) {
                continue;
            }

            using (root) {
                foreach (var id in root.GetSubKeyNames()) {
                    try {
                        using var key = root.OpenSubKey(id);
                        if (key == null) {
                            continue;
                        }

                        string publisher = (key.GetValue("Publisher") as string ?? "").Trim();
                        if (!GamePublisher(publisher)) {
                            continue;
                        }

                        string name = (key.GetValue("DisplayName") as string ?? "").Trim();
                        string icon = (key.GetValue("DisplayIcon") as string ?? "").Trim().Trim('"');
                        int comma = icon.IndexOf(',');
                        if (comma >= 0) {
                            icon = icon[..comma].Trim().Trim('"');
                        }

                        if (SkipName(name) || SkipSteamName(name)) {
                            continue;
                        }

                        if (icon.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) {
                            Put(byPath, name, icon, preferName: true);
                            continue;
                        }

                        string loc = (key.GetValue("InstallLocation") as string ?? "").Trim().Trim('"');
                        if (loc.Length > 0) {
                            Put(byPath, name, FindExe(loc, name, System.IO.Path.GetFileName(loc.TrimEnd('\\'))), preferName: true);
                        }
                    } catch {
                    }
                }
            }
        }
    }

    /*
    ===============
    GamePublisher
    ===============
    */
    static bool GamePublisher(string publisher) {
        if (publisher.Length == 0) {
            return false;
        }

        string lower = publisher.ToLowerInvariant();
        return lower.Contains("electronic arts")
            || lower.Contains("ea swiss")
            || lower.Contains("blizzard")
            || lower.Contains("battle.net")
            || lower.Contains("activision")
            || lower.Contains("rockstar")
            || lower.Contains("riot games")
            || lower.Contains("2k ")
            || lower.StartsWith("2k")
            || lower.Contains("ubisoft")
            || lower.Contains("valve")
            || lower.Contains("epic games")
            || lower.Contains("gog.com")
            || lower.Contains("xbox game studios")
            || lower.Contains("bethesda")
            || lower.Contains("cd projekt");
    }

    /*
    ===============
    FindExe
    Prefer a root .exe whose name looks like the game; else the largest non-helper.
    ===============
    */
    static string? FindExe(string dir, string gameName, string installdir) {
        if (!Directory.Exists(dir)) {
            return null;
        }

        var candidates = new List<FileInfo>();
        try {
            candidates.AddRange(new DirectoryInfo(dir).EnumerateFiles("*.exe", SearchOption.TopDirectoryOnly));
        } catch {
            return null;
        }

        if (candidates.Count == 0) {
            try {
                foreach (var sub in Directory.GetDirectories(dir)) {
                    string leaf = System.IO.Path.GetFileName(sub);
                    if (leaf.EndsWith("_Data", StringComparison.OrdinalIgnoreCase)
                        || leaf.Equals("Engine", StringComparison.OrdinalIgnoreCase)
                        || leaf.Equals("Redist", StringComparison.OrdinalIgnoreCase)
                        || leaf.Equals("__Installer", StringComparison.OrdinalIgnoreCase)) {
                        continue;
                    }

                    if (leaf.Equals("Binaries", StringComparison.OrdinalIgnoreCase)
                        || leaf.Equals("bin", StringComparison.OrdinalIgnoreCase)
                        || leaf.Equals("Bin", StringComparison.OrdinalIgnoreCase)) {
                        candidates.AddRange(new DirectoryInfo(sub).EnumerateFiles("*.exe", SearchOption.AllDirectories).Take(40));
                        continue;
                    }

                    candidates.AddRange(new DirectoryInfo(sub).EnumerateFiles("*.exe", SearchOption.TopDirectoryOnly));
                }
            } catch {
            }
        }

        // Unreal-style Binaries/Win64
        foreach (var win in new[] {
            System.IO.Path.Combine(dir, "Binaries", "Win64"),
            System.IO.Path.Combine(dir, "bin", "Win64"),
            System.IO.Path.Combine(dir, "Binaries", "Win32")
        }) {
            if (!Directory.Exists(win)) {
                continue;
            }

            try {
                candidates.AddRange(new DirectoryInfo(win).EnumerateFiles("*.exe", SearchOption.TopDirectoryOnly));
            } catch {
            }
        }

        FileInfo? best = null;
        int bestScore = int.MinValue;
        string needle = Normalize(gameName);
        string folder = Normalize(installdir);

        foreach (var file in candidates) {
            if (SkipExeName(file.Name)) {
                continue;
            }

            int score = 0;
            string stem = Normalize(System.IO.Path.GetFileNameWithoutExtension(file.Name));
            if (stem.Length > 0 && (needle.Contains(stem) || stem.Contains(needle) || folder.Contains(stem) || stem.Contains(folder))) {
                score += 1000;
            }

            if (string.Equals(file.DirectoryName, dir, StringComparison.OrdinalIgnoreCase)) {
                score += 200;
            }

            long size = 0;
            try {
                size = file.Length;
            } catch {
            }

            score += (int)Math.Min(size / (128 * 1024), 500);
            if (score > bestScore) {
                bestScore = score;
                best = file;
            }
        }

        return best?.FullName;
    }

    /*
    ===============
    Put
    ===============
    */
    static void Put(Dictionary<string, InstalledApp> byPath, string name, string? path, bool preferName) {
        if (path == null || name.Length == 0) {
            return;
        }

        path = path.Trim().Trim('"');
        if (!path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) || !File.Exists(path) || SkipPath(path)) {
            return;
        }

        if (byPath.TryGetValue(path, out var existing)) {
            if (!preferName && existing.Name.Length <= name.Length) {
                return;
            }

            if (preferName) {
                byPath[path] = new InstalledApp { Name = name, Path = path };
            } else if (name.Length < existing.Name.Length) {
                byPath[path] = new InstalledApp { Name = name, Path = path };
            }

            return;
        }

        byPath[path] = new InstalledApp { Name = name, Path = path };
    }

    /*
    ===============
    SkipName
    ===============
    */
    static bool SkipName(string name) {
        if (name.Length == 0) {
            return true;
        }

        string lower = name.ToLowerInvariant();
        return lower.Contains("uninstall")
            || lower.Contains("удален")
            || lower.StartsWith("help ")
            || lower.EndsWith(" help")
            || lower.EndsWith(" readme")
            || lower.Contains("documentation")
            || lower == "desktop" || lower == "documents" || lower == "downloads"
            || lower == "music" || lower == "pictures" || lower == "videos";
    }

    /*
    ===============
    SkipPath
    ===============
    */
    static bool SkipPath(string path) {
        string lower = path.ToLowerInvariant();
        string file = System.IO.Path.GetFileName(lower);
        if (SkipExe.Contains(file) || SkipExeName(file)) {
            return true;
        }

        return lower.Contains("\\windows\\installer\\")
            || lower.Contains("\\update\\")
            || lower.Contains("\\redistributable")
            || lower.Contains("\\redist\\");
    }

    /*
    ===============
    SkipExeName
    ===============
    */
    static bool SkipExeName(string file) {
        string lower = file.ToLowerInvariant();
        if (SkipExe.Contains(lower)) {
            return true;
        }

        return lower.StartsWith("unitycrash")
            || lower.StartsWith("crashpad")
            || lower.Contains("unins")
            || lower.Contains("redist")
            || lower.Contains("vcredist")
            || lower is "awesomium_process.exe" or "cefsharp.browsersubprocess.exe";
    }

    /*
    ===============
    VdfValue
    First matching key in a flat AppState-style blob.
    ===============
    */
    static string VdfValue(string text, string key) {
        foreach (Match m in VdfKey.Matches(text)) {
            if (string.Equals(m.Groups["key"].Value, key, StringComparison.OrdinalIgnoreCase)) {
                return UnescapeVdf(m.Groups["val"].Value);
            }
        }

        return "";
    }

    static string UnescapeVdf(string value) => value.Replace("\\\\", "\\");

    static string JsonString(JsonElement root, string name) =>
        root.TryGetProperty(name, out var el) && el.ValueKind == JsonValueKind.String
            ? (el.GetString() ?? "").Trim()
            : "";

    static string Normalize(string value) {
        var chars = value.ToLowerInvariant().Where(char.IsLetterOrDigit).ToArray();
        return new string(chars);
    }

    /*
    ===============
    ResolveLink
    Follow one .lnk through WScript.Shell.
    ===============
    */
    static string? ResolveLink(string link) {
        Type? type = Type.GetTypeFromProgID("WScript.Shell");
        if (type == null) {
            return null;
        }

        object? shell = null;
        object? shortcut = null;
        try {
            shell = Activator.CreateInstance(type);
            if (shell == null) {
                return null;
            }

            shortcut = type.InvokeMember(
                "CreateShortcut",
                System.Reflection.BindingFlags.InvokeMethod,
                null,
                shell,
                new object[] { link });
            if (shortcut == null) {
                return null;
            }

            object? target = shortcut.GetType().InvokeMember(
                "TargetPath",
                System.Reflection.BindingFlags.GetProperty,
                null,
                shortcut,
                null);
            string path = (target as string ?? "").Trim();
            return path.Length == 0 ? null : path;
        } finally {
            if (shortcut != null && Marshal.IsComObject(shortcut)) {
                Marshal.FinalReleaseComObject(shortcut);
            }

            if (shell != null && Marshal.IsComObject(shell)) {
                Marshal.FinalReleaseComObject(shell);
            }
        }
    }
}
