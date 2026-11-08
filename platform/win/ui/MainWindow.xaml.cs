//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// MainWindow.xaml.cs -- The WinUI window. It draws the session and sends the clicks back to it.
//
//===============================================================

using System.Windows.Input;
using System.Runtime.InteropServices;
using H.NotifyIcon;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text.Json;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Animation;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;
using Windows.Storage;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace Fairlead;

public sealed partial class MainWindow : Window {
    public Session Session { get; } = new();
    bool _quiet;
    bool _creating;
    Tunnel? _editing;
    bool _raw;
    bool _quitting;
    readonly Dictionary<TextBox, string> _secrets = new();
    readonly HashSet<Tunnel> _watched = new();
    readonly TaskbarIcon _tray = new();
    readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromSeconds(1) };
    readonly WndProc _sizeProc;
    IntPtr _previousProc;
    bool _sized;
    int _sheetEpoch;
    Storyboard? _sheetBoard;

    /*
    ===============
    MainWindow
    Build the window, the tray, and the first paint.
    ===============
    */
    public MainWindow() {
        _sizeProc = ClampSize;
        InitializeComponent();
        TunnelList.ItemsSource = Session.Filtered;
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(DragRegion);
        AppWindow.TitleBar.BackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.InactiveBackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.ButtonBackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.ButtonInactiveBackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.ButtonForegroundColor = ColorHelper.FromArgb(255, 147, 160, 150);
        AppWindow.TitleBar.ButtonHoverBackgroundColor = ColorHelper.FromArgb(24, 255, 255, 255);
        var icon = Path.Combine(AppContext.BaseDirectory, "Assets", "Fairlead.ico");
        if (File.Exists(icon)) {
            AppWindow.SetIcon(icon);
        }

        AppWindow.Resize(new SizeInt32(1479, 1013));
        if (AppWindow.Presenter is OverlappedPresenter presenter) {
            presenter.PreferredMinimumWidth = 980;
            presenter.PreferredMinimumHeight = 660;
        }

        Activated += (_, _) => HookMinimumSize();
        HookSwitch(LanSwitch);
        HookSwitch(LoginSwitch);
        Closed += (_, _) => {
            if (_quitting) {
                _tray.Dispose();
            }
        };
        AppWindow.Closing += OnClosing;
        Session.PropertyChanged += (_, e) => {
            if (e.PropertyName == nameof(Session.Banner)) {
                Banner.Visibility = Session.Banner.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
                BannerText.Text = Session.Banner;
            }

            if (e.PropertyName is nameof(Session.Selected) or nameof(Session.DirectOn)) {
                PaintPolicy();
            }

            if (e.PropertyName is nameof(Session.Selected) or nameof(Session.Connected)) {
                PaintPower();
            }

            if (e.PropertyName == nameof(Session.Selected)) {
                PaintRules();
            }

            if (e.PropertyName == nameof(Session.HelperText)) {
                PaintHelper();
            }

            if (e.PropertyName == nameof(Session.Selected) && !Equals(TunnelList.SelectedItem, Session.Selected))
                TunnelList.SelectedItem = Session.Selected;
        };
        _quiet = true;
        LanSwitch.IsChecked = Session.ExcludeLan;
        LoginSwitch.IsChecked = Session.LaunchAtLogin;
        _quiet = false;
        PaintRules();
        PaintPolicy();
        PaintPower();
        PaintHelper();
        FillDefaults();
        TunnelList.SelectedItem = Session.Selected;
        TunnelList.AddHandler(UIElement.RightTappedEvent, new RightTappedEventHandler(TunnelMenu), true);
        Session.RulesChanged += () => DispatcherQueue.TryEnqueue(PaintRules);
        _timer.Tick += async (_, _) => await Session.Tick();
        _timer.Start();
        _ = Session.Tick();
        _tray.ToolTipText = "Fairlead";
        _tray.NoLeftClickDelay = true;
        _tray.ContextMenuMode = ContextMenuMode.PopupMenu;
        _tray.Id = new Guid("c3a0e7b1-5d24-4f6a-9b18-7e2d4a91c6f0");
        if (File.Exists(icon)) {
            _tray.Icon = new System.Drawing.Icon(icon);
        }

        _tray.LeftClickCommand = new TrayAction(ShowMain);
        Session.Tunnels.CollectionChanged += (_, _) => RefreshTray();
        RefreshTray();
        _tray.ForceCreate(false);
    }

    /*
    ===============
    RefreshTray
    The tray menu matches the tunnel list.
    ===============
    */
    void RefreshTray() {
        foreach (var tunnel in Session.Tunnels) {
            if (!_watched.Add(tunnel)) {
                continue;
            }

            tunnel.PropertyChanged += (_, e) => {
                if (e.PropertyName is nameof(Tunnel.Connected) or nameof(Tunnel.Name) or nameof(Tunnel.Phase))
                    RebuildTray();
            };
        }

        RebuildTray();
    }

    /*
    ===============
    RebuildTray
    Replace the tray menu.
    ===============
    */
    void RebuildTray() {
        var menu = new MenuFlyout();
        if (Session.Tunnels.Count == 0) {
            menu.Items.Add(new MenuFlyoutItem { Text = "Open Fairlead", Command = new TrayAction(ShowMain) });
        } else {
            foreach (var tunnel in Session.Tunnels) {
                var item = tunnel;
                var mark = item.Connected ? "● " : "";
                menu.Items.Add(new MenuFlyoutItem {
                    Text = mark + item.Name,
                    Command = new TrayAction(() => {
                        _ = Session.Toggle(item);
                    })
                });
            }

            menu.Items.Add(new MenuFlyoutSeparator());
            menu.Items.Add(new MenuFlyoutItem { Text = "Open Fairlead", Command = new TrayAction(ShowMain) });
        }

        menu.Items.Add(new MenuFlyoutSeparator());
        menu.Items.Add(new MenuFlyoutItem { Text = "Settings…", Command = new TrayAction(ShowSettings) });
        menu.Items.Add(new MenuFlyoutItem { Text = "Quit Fairlead", Command = new TrayAction(QuitApp) });
        _tray.ContextFlyout = menu;
    }

    /*
    ===============
    ShowMain
    The window, in front.
    ===============
    */
    internal void ShowMain() {
        DispatcherQueue.TryEnqueue(() => {
            this.Show();
            Activate();
        });
    }

    /*
    ===============
    ShowSettings
    The settings sheet.
    ===============
    */
    void ShowSettings() {
        DispatcherQueue.TryEnqueue(() => {
            this.Show();
            Activate();
            OpenSettings(this, new RoutedEventArgs());
        });
    }

    /*
    ===============
    QuitApp
    Disconnect and exit.
    ===============
    */
    void QuitApp() {
        DispatcherQueue.TryEnqueue(async () => {
            if (_quitting) {
                return;
            }

            _quitting = true;
            try {
                await Session.Shutdown();
            } finally {
                _tray.Dispose();
                Close();
            }
        });
    }

    /*
    ===============
    HookMinimumSize
    Keep the window from shrinking through the layout.
    ===============
    */
    void HookMinimumSize() {
        if (_sized) {
            return;
        }

        _sized = true;
        var hwnd = WindowNative.GetWindowHandle(this);
        _previousProc = SetWindowLongPtr(hwnd, -4, Marshal.GetFunctionPointerForDelegate(_sizeProc));
    }

    /*
    ===============
    ClampSize
    The minimum tracking size.
    ===============
    */
    IntPtr ClampSize(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam) {
        if (msg == 0x0046) {
            var pos = Marshal.PtrToStructure<WindowPos>(lParam);
            if ((pos.flags & 0x0001) == 0) {
                if (pos.cx > 0 && pos.cx < 980) {
                    pos.cx = 980;
                }

                if (pos.cy > 0 && pos.cy < 660) {
                    pos.cy = 660;
                }

                Marshal.StructureToPtr(pos, lParam, false);
            }
        }

        var result = CallWindowProc(_previousProc, hwnd, msg, wParam, lParam);
        if (msg == 0x0024) {
            var info = Marshal.PtrToStructure<MinMax>(lParam);
            if (info.MinTrack.X < 980) {
                info.MinTrack.X = 980;
            }

            if (info.MinTrack.Y < 660) {
                info.MinTrack.Y = 660;
            }

            Marshal.StructureToPtr(info, lParam, false);
        }

        return result;
    }

    delegate IntPtr WndProc(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    struct WindowPos {
        public IntPtr hwnd;
        public IntPtr insertAfter;
        public int x, y, cx, cy;
        public uint flags;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct MinMax {
        public NativePoint Reserved, MaxSize, MaxPosition, MinTrack, MaxTrack;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct NativePoint {
        public int X, Y;
    }

/*
===============
SetWindowLongPtr
===============
*/
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
    static extern IntPtr SetWindowLongPtr(IntPtr hwnd, int index, IntPtr value);

/*
===============
CallWindowProc
===============
*/
    [DllImport("user32.dll", EntryPoint = "CallWindowProcW")]
    static extern IntPtr CallWindowProc(IntPtr previous, IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    /*
    ===============
    OnClosing
    Hide the window. Quit is the tray menu.
    ===============
    */
    void OnClosing(AppWindow sender, AppWindowClosingEventArgs args) {
        if (_quitting) {
            return;
        }

        args.Cancel = true;
        DispatcherQueue.TryEnqueue(() => this.Hide());
    }

    /*
    ===============
    SearchChanged
    The filter box.
    ===============
    */
    void SearchChanged(object sender, TextChangedEventArgs e) {
        Session.Query = Search.Text;
    }

    /*
    ===============
    TunnelChosen
    The list selection.
    ===============
    */
    void TunnelChosen(object sender, SelectionChangedEventArgs e) {
        if (TunnelList.SelectedItem is Tunnel t) {
            Session.Selected = t;
        }

        _quiet = true;
        LanSwitch.IsChecked = Session.ExcludeLan;
        _quiet = false;
        PaintRules();
        PaintPolicy();
        PaintPower();
    }

    /*
    ===============
    NewTunnel
    A blank tunnel in the editor.
    ===============
    */
    void NewTunnel(object sender, RoutedEventArgs e) {
        _creating = true;
        _editing = Session.MakeDraft();
        ShowEditor();
    }

    /*
    ===============
    ImportTunnel
    A file picker, then import.
    ===============
    */
    async void ImportTunnel(object sender, RoutedEventArgs e) {
        var picker = new FileOpenPicker();
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        picker.FileTypeFilter.Add(".conf");
        picker.FileTypeFilter.Add(".txt");
        picker.FileTypeFilter.Add("*");
        var file = await picker.PickSingleFileAsync();
        if (file != null) {
            Session.ImportFile(file.Path);
        }
    }

    /*
    ===============
    TogglePower
    The connect switch.
    ===============
    */
    async void TogglePower(object sender, RoutedEventArgs e) {
        if (Session.Selected != null) {
            await Session.Toggle(Session.Selected);
        }

        PaintPower();
    }

    async void SetDirect(object sender, RoutedEventArgs e) {
        await Session.SetPolicy(false);
        PaintPolicy();
    }

    async void SetThrough(object sender, RoutedEventArgs e) {
        await Session.SetPolicy(true);
        PaintPolicy();
    }

    /*
    ===============
    LanToggled
    The local-network switch.
    ===============
    */
    async void LanToggled(object sender, RoutedEventArgs e) {
        if (_quiet) {
            return;
        }

        await Session.SetLan(LanSwitch.IsChecked == true);
    }

    /*
    ===============
    HookSwitch
    Draw the switch ourselves. WinUI's default fights the theme.
    ===============
    */
    static void HookSwitch(ToggleButton button) {
        button.Loaded += (_, _) => ShowSwitch(button);
        button.Checked += (_, _) => ShowSwitch(button);
        button.Unchecked += (_, _) => ShowSwitch(button);
    }

    /*
    ===============
    ShowSwitch
    Paint one switch on or off.
    ===============
    */
    static void ShowSwitch(ToggleButton button) {
        if (VisualTreeHelper.GetChildrenCount(button) == 0) {
            return;
        }

        if (VisualTreeHelper.GetChild(button, 0) is not Grid root || VisualTreeHelper.GetChildrenCount(root) < 2) {
            return;
        }

        bool on = button.IsChecked == true;
        if (VisualTreeHelper.GetChild(root, 0) is Border track) {
            track.Background = (Brush)Application.Current.Resources[on ? "ToggleSwitchFillOn" : "ToggleSwitchFillOff"];
            track.BorderBrush = (Brush)Application.Current.Resources[on ? "ToggleSwitchStrokeOn" : "ToggleSwitchStrokeOff"];
        }

        if (VisualTreeHelper.GetChild(root, 1) is Microsoft.UI.Xaml.Shapes.Ellipse knob) {
            knob.Fill = (Brush)Application.Current.Resources[on ? "ToggleSwitchKnobFillOn" : "ToggleSwitchKnobFillOff"];
            if (knob.RenderTransform is TranslateTransform shift) {
                shift.X = on ? 20 : 0;
            }
        }
    }

    /*
    ===============
    OpenSettings
    Settings.
    ===============
    */
    void OpenSettings(object sender, RoutedEventArgs e) {
        AwgLine.Text = Session.AwgText;
        PaintHelper();
        FillDefaults();
        ShowSheet(SettingsPane);
    }

    /*
    ===============
    OpenEditor
    The config editor.
    ===============
    */
    void OpenEditor(object sender, RoutedEventArgs e) {
        if (Session.Selected == null) {
            return;
        }

        _creating = false;
        _editing = Session.Selected;
        ShowEditor();
    }

    /*
    ===============
    ShowEditor
    Fill the editor from the selected tunnel.
    ===============
    */
    void ShowEditor() {
        if (_editing == null) {
            return;
        }

        _raw = false;
        RawConfig.Text = _editing.ConfigText;
        BuildFields();
        ShowFields(this, new RoutedEventArgs());
        ShowSheet(EditorPane);
    }

    /*
    ===============
    OpenRoutes
    The route sheet.
    ===============
    */
    void OpenRoutes(object sender, RoutedEventArgs e) {
        RouteBox.Text = "";
        RouteHint.Visibility = Visibility.Visible;
        ShowSheet(RoutePane);
    }

    /*
    ===============
    AddApp
    Pick an application.
    ===============
    */
    async void AddApp(object sender, RoutedEventArgs e) {
        var picker = new FileOpenPicker();
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        picker.FileTypeFilter.Add(".exe");
        var file = await picker.PickSingleFileAsync();
        if (file != null) {
            Session.AddProgram(file.Path);
            PaintRules();
        }
    }

    /*
    ===============
    OpenFolder
    Open the config folder.
    ===============
    */
    void OpenFolder(object sender, RoutedEventArgs e) {
        Session.OpenFolder();
    }

    /*
    ===============
    StartHelper
    The elevated helper. The first run records the login task.
    ===============
    */
    async void StartHelper(object sender, RoutedEventArgs e) {
        await Session.StartHelper();
    }

    void LoginToggled(object sender, RoutedEventArgs e) {
        if (!_quiet) {
            Session.SetLaunch(LoginSwitch.IsChecked == true);
        }
    }

    /*
    ===============
    DefaultChanged
    The default-tunnel combo.
    ===============
    */
    void DefaultChanged(object sender, SelectionChangedEventArgs e) {
        if (_quiet || DefaultCombo.SelectedItem is not ComboBoxItem item) {
            return;
        }

        Session.SetDefault(item.Tag as string ?? "");
    }

    /*
    ===============
    ShowFields
    The form.
    ===============
    */
    void ShowFields(object sender, RoutedEventArgs e) {
        _raw = false;
        EditorFields.Visibility = Visibility.Visible;
        RawConfig.Visibility = Visibility.Collapsed;
        FieldsButton.Style = (Style)Application.Current.Resources["SegmentOn"];
        TextButton.Style = (Style)Application.Current.Resources["SegmentOff"];
    }

    /*
    ===============
    ShowRaw
    The text.
    ===============
    */
    void ShowRaw(object sender, RoutedEventArgs e) {
        _raw = true;
        EditorFields.Visibility = Visibility.Collapsed;
        RawConfig.Visibility = Visibility.Visible;
        FieldsButton.Style = (Style)Application.Current.Resources["SegmentOff"];
        TextButton.Style = (Style)Application.Current.Resources["SegmentOn"];
    }

    /*
    ===============
    SaveEditor
    Write the form or the text back to the tunnel.
    ===============
    */
    void SaveEditor(object sender, RoutedEventArgs e) {
        var tunnel = _editing;
        if (tunnel == null) {
            return;
        }

        string config;
        if (_raw) {
            config = RawConfig.Text;
        } else {
            var iface = new Dictionary<string, string>();
            var peer = new Dictionary<string, string>();
            foreach (var child in EditorFields.Children) {
                if (child is not StackPanel row || row.Tag is not string key) {
                    continue;
                }

                var box = FindBox(row);
                if (box == null) {
                    continue;
                }

                string value = _secrets.TryGetValue(box, out var secret) ? secret : box.Text;
                if (key.StartsWith("peer:")) {
                    peer[key[5..]] = value;
                } else {
                    iface[key] = value;
                }
            }

            tunnel.Name = iface.TryGetValue("Name", out var n) ? n : tunnel.Name;
            iface.Remove("Name");
            string json = JsonSerializer.Serialize(new { @interface = iface, peer });
            config = Core.Apply(tunnel.ConfigText, json);
        }

        bool creating = _creating;
        _creating = false;
        if (creating) {
            Session.Adopt(tunnel);
        }

        Session.SaveConfig(tunnel, config);
        CloseSheet(this, e);
    }

    /*
    ===============
    SaveRoutes
    Commit the route box.
    ===============
    */
    void SaveRoutes(object sender, RoutedEventArgs e) {
        string error = Session.AddRoutes(RouteBox.Text);
        if (error.Length > 0) {
            Banner.Visibility = Visibility.Visible;
            BannerText.Text = error;
        }
        PaintRules();
        CloseSheet(this, e);
    }

    /*
    ===============
    AskDelete
    Confirm deleting the selected tunnel.
    ===============
    */
    void AskDelete(object sender, RoutedEventArgs e) => AskDeleteTunnel(Session.Selected);

    /*
    ===============
    AskDeleteTunnel
    Confirm deleting this tunnel.
    ===============
    */
    void AskDeleteTunnel(Tunnel? tunnel) {
        if (tunnel == null) {
            return;
        }

        Session.Selected = tunnel;
        TunnelList.SelectedItem = tunnel;
        ConfirmText.Text = "Delete the tunnel “" + tunnel.Name + "”?";
        ShowSheet(ConfirmPane);
    }

    /*
    ===============
    ExportSelected
    Export the selected tunnel.
    ===============
    */
    async void ExportSelected(object sender, RoutedEventArgs e) => await ExportTunnel(Session.Selected);

    /*
    ===============
    ExportTunnel
    Write the config to a file the user picks.
    ===============
    */
    async Task ExportTunnel(Tunnel? tunnel) {
        if (tunnel == null) {
            return;
        }

        var picker = new FileSavePicker();
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        picker.SuggestedStartLocation = PickerLocationId.DocumentsLibrary;
        picker.FileTypeChoices.Add("WireGuard config", new List<string> { ".conf" });
        string stem = tunnel.Name.Trim();
        if (stem.EndsWith(".conf", StringComparison.OrdinalIgnoreCase)) {
            stem = stem[..^5];
        }

        foreach (char c in Path.GetInvalidFileNameChars()) stem = stem.Replace(c, ' ');
        picker.SuggestedFileName = stem.Length == 0 ? "tunnel" : stem.Trim();
        var file = await picker.PickSaveFileAsync();
        if (file != null) {
            await FileIO.WriteTextAsync(file, tunnel.ConfigText);
        }
    }

    /*
    ===============
    TunnelMenu
    The right-click menu on a row.
    ===============
    */
    void TunnelMenu(object sender, RightTappedRoutedEventArgs e) {
        var tunnel = TunnelFrom(e.OriginalSource);
        if (tunnel == null) {
            return;
        }

        Session.Selected = tunnel;
        TunnelList.SelectedItem = tunnel;
        var flyout = new MenuFlyout();
        var connect = new MenuFlyoutItem { Text = tunnel.Connected ? "Disconnect" : "Connect" };
        connect.Click += async (_, _) => await Session.Toggle(tunnel);
        var edit = new MenuFlyoutItem { Text = "Edit" };
        edit.Click += (_, _) => OpenEditor(this, new RoutedEventArgs());
        var export = new MenuFlyoutItem { Text = "Export…" };
        export.Click += async (_, _) => await ExportTunnel(tunnel);
        var delete = new MenuFlyoutItem { Text = "Delete" };
        delete.Click += (_, _) => AskDeleteTunnel(tunnel);
        flyout.Items.Add(connect);
        flyout.Items.Add(edit);
        flyout.Items.Add(export);
        flyout.Items.Add(new MenuFlyoutSeparator());
        flyout.Items.Add(delete);
        flyout.ShowAt(TunnelList, e.GetPosition(TunnelList));
        e.Handled = true;
    }

    /*
    ===============
    TunnelFrom
    The tunnel behind a list row.
    ===============
    */
    static Tunnel? TunnelFrom(object source) {
        DependencyObject? node = source as DependencyObject;
        while (node != null) {
            if (node is FrameworkElement element && element.DataContext is Tunnel tunnel) {
                return tunnel;
            }

            node = VisualTreeHelper.GetParent(node);
        }

        return null;
    }

    /*
    ===============
    ConfirmDelete
    Delete after the confirm sheet.
    ===============
    */
    void ConfirmDelete(object sender, RoutedEventArgs e) {
        if (Session.Selected != null) {
            Session.Delete(Session.Selected);
        }

        CloseSheet(this, e);
    }

    /*
    ===============
    CloseSheet
    Start the sheet closing.
    ===============
    */
    void CloseSheet(object sender, RoutedEventArgs e) {
        _creating = false;
        _editing = null;
        if (Overlay.Visibility != Visibility.Visible) {
            return;
        }

        double opacity = Overlay.Opacity;
        double scale = SheetScale.ScaleX;
        if (opacity < 0.2) {
            opacity = 1;
        }

        if (scale < 0.9) {
            scale = 1;
        }

        Overlay.Opacity = opacity;
        SheetScale.ScaleX = scale;
        SheetScale.ScaleY = scale;
        int epoch = ++_sheetEpoch;
        _sheetBoard?.Stop();
        Overlay.Opacity = opacity;
        SheetScale.ScaleX = scale;
        SheetScale.ScaleY = scale;
        _sheetBoard = SheetMotion(opacity, scale, 0, 0.94, 280);
        _sheetBoard.Completed += (_, _) => FinishClose(epoch);
        _sheetBoard.Begin();
    }

    /*
    ===============
    FinishClose
    Drop the sheet after the animation.
    ===============
    */
    void FinishClose(int epoch) {
        if (epoch != _sheetEpoch || Overlay.Visibility != Visibility.Visible) {
            return;
        }

        _sheetEpoch++;
        Overlay.Visibility = Visibility.Collapsed;
        _sheetBoard?.Stop();
        Overlay.Opacity = 1;
        SheetScale.ScaleX = 1;
        SheetScale.ScaleY = 1;
    }

    /*
    ===============
    RouteChanged
    Enable the add button when the box has text.
    ===============
    */
    void RouteChanged(object sender, TextChangedEventArgs e) {
        RouteHint.Visibility = RouteBox.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    /*
    ===============
    ShowSheet
    A sheet over the window.
    ===============
    */
    void ShowSheet(UIElement pane) {
        _sheetEpoch++;
        Overlay.Opacity = 0;
        SheetScale.ScaleX = 0.96;
        SheetScale.ScaleY = 0.96;
        _sheetBoard?.Stop();
        Overlay.Opacity = 0;
        SheetScale.ScaleX = 0.96;
        SheetScale.ScaleY = 0.96;
        SettingsPane.Visibility = pane == SettingsPane ? Visibility.Visible : Visibility.Collapsed;
        EditorPane.Visibility = pane == EditorPane ? Visibility.Visible : Visibility.Collapsed;
        RoutePane.Visibility = pane == RoutePane ? Visibility.Visible : Visibility.Collapsed;
        ConfirmPane.Visibility = pane == ConfirmPane ? Visibility.Visible : Visibility.Collapsed;
        Overlay.Visibility = Visibility.Visible;
        _sheetBoard = SheetMotion(0, 0.96, 1, 1, 200);
        _sheetBoard.Begin();
    }

    /*
    ===============
    SheetMotion
    Slide the sheet.
    ===============
    */
    Storyboard SheetMotion(double fromOpacity, double fromScale, double opacity, double scale, int ms) {
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        var fade = new DoubleAnimation { From = fromOpacity, To = opacity, Duration = TimeSpan.FromMilliseconds(ms), EasingFunction = ease, EnableDependentAnimation = true };
        var grow = new DoubleAnimation { From = fromScale, To = scale, Duration = TimeSpan.FromMilliseconds(ms), EasingFunction = ease, EnableDependentAnimation = true };
        var growY = new DoubleAnimation { From = fromScale, To = scale, Duration = TimeSpan.FromMilliseconds(ms), EasingFunction = ease, EnableDependentAnimation = true };
        Storyboard.SetTarget(fade, Overlay);
        Storyboard.SetTargetProperty(fade, "Opacity");
        Storyboard.SetTarget(grow, SheetScale);
        Storyboard.SetTargetProperty(grow, "ScaleX");
        Storyboard.SetTarget(growY, SheetScale);
        Storyboard.SetTargetProperty(growY, "ScaleY");
        var board = new Storyboard { Duration = TimeSpan.FromMilliseconds(ms), FillBehavior = FillBehavior.HoldEnd };
        board.Children.Add(fade);
        board.Children.Add(grow);
        board.Children.Add(growY);
        return board;
    }

    /*
    ===============
    BuildFields
    The editor rows from the core.
    ===============
    */
    void BuildFields() {
        EditorFields.Children.Clear();
        _secrets.Clear();
        var tunnel = _editing;
        if (tunnel == null) {
            return;
        }

        using var doc = JsonDocument.Parse(Core.Inspect(tunnel.ConfigText));
        AddField("Name", "Name", tunnel.Name, false);
        if (doc.RootElement.TryGetProperty("interface", out var iface))
            foreach (var prop in iface.EnumerateObject())
                AddField(prop.Name, prop.Name, prop.Value.GetString() ?? "", prop.Name == "PrivateKey");
        if (doc.RootElement.TryGetProperty("peer", out var peer))
            foreach (var prop in peer.EnumerateObject())
                AddField("peer:" + prop.Name, prop.Name, prop.Value.GetString() ?? "", false);
    }

    /*
    ===============
    AddField
    One labeled box. Secret fields stay masked.
    ===============
    */
    void AddField(string key, string label, string value, bool secret) {
        var row = new StackPanel { Tag = key, Spacing = 4 };
        row.Children.Add(new TextBlock { Text = label, FontSize = 12, Foreground = (Brush)Application.Current.Resources["MutedBrush"] });
        var box = new TextBox { Text = value, Style = (Style)Application.Current.Resources["QuietBox"], VerticalAlignment = VerticalAlignment.Center };
        EditorFields.Children.Add(row);
        if (!secret) {
            row.Children.Add(box);
            return;
        }

        var line = new Grid { ColumnSpacing = 8 };
        line.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        line.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        line.Children.Add(box);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, VerticalAlignment = VerticalAlignment.Center };
        var show = new Button { Content = "Show", Style = (Style)Application.Current.Resources["PillButton"] };
        var neu = new Button { Content = "New key", Style = (Style)Application.Current.Resources["PillButton"] };
        bool hidden = true;
        string real = value;
        _secrets[box] = real;
        box.Text = new string('•', Math.Min(real.Length, 36));
        show.Click += (_, _) => {
            hidden = !hidden;
            box.Text = hidden ? new string('•', Math.Min(real.Length, 36)) : real;
            box.IsReadOnly = hidden;
        };
        box.TextChanged += (_, _) => {
            if (!hidden) {
                real = box.Text;
                _secrets[box] = real;
            }
        };
        neu.Click += (_, _) => {
            var (priv, _) = Core.Keypair();
            real = priv;
            hidden = false;
            box.IsReadOnly = false;
            box.Text = priv;
        };
        actions.Children.Add(show);
        actions.Children.Add(neu);
        Grid.SetColumn(actions, 1);
        line.Children.Add(actions);
        row.Children.Add(line);
        box.Tag = "secret";
    }

    /*
    ===============
    FindBox
    The text box under a labeled row.
    ===============
    */
    static TextBox? FindBox(DependencyObject node) {
        int count = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChildrenCount(node);
        for (int i = 0; i < count; i++) {
            var child = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChild(node, i);
            if (child is TextBox box) {
                return box;
            }

            var nested = FindBox(child);
            if (nested != null) {
                return nested;
            }
        }

        return null;
    }

    readonly Dictionary<string, ImageSource?> _icons = new(StringComparer.OrdinalIgnoreCase);
    bool _painting;

    /*
    ===============
    AppIcon
    The window icon.
    ===============
    */
    ImageSource? AppIcon(string path) {
        if (_icons.TryGetValue(path, out var cached)) {
            return cached;
        }

        ImageSource? source = null;
        try {
            if (File.Exists(path)) {
                using var icon = System.Drawing.Icon.ExtractAssociatedIcon(path);
                if (icon != null) {
                    using var bmp = icon.ToBitmap();
                    using var ms = new MemoryStream();
                    bmp.Save(ms, System.Drawing.Imaging.ImageFormat.Png);
                    ms.Position = 0;
                    var image = new BitmapImage();
                    image.SetSource(ms.AsRandomAccessStream());
                    source = image;
                }
            }
        } catch {
            source = null;
        }
        _icons[path] = source;
        return source;
    }

    /*
    ===============
    PaintRules
    The route list.
    ===============
    */
    void PaintRules() {
        _painting = true;
        try {
            Rules.Children.Clear();
            var t = Session.Selected;
            if (t == null) {
                return;
            }

            foreach (var ip in t.Ips) Rules.Children.Add(Rule("IP", ip.Cidr, ip.Note, ip.Enabled, null, v => ip.Enabled = v, () => t.Ips.Remove(ip)));
            foreach (var d in t.Domains) Rules.Children.Add(Rule("Domain", d.Name, d.Addresses.Count + " addresses", d.Enabled, null, v => d.Enabled = v, () => t.Domains.Remove(d)));
            foreach (var a in t.Apps) {
                bool missing = !File.Exists(a.Path);
                string sub = missing ? "App not found" : a.Learned.Count + " addresses";
                Rules.Children.Add(Rule("App", a.Name, sub, a.Enabled, a.Path, v => a.Enabled = v, () => t.Apps.Remove(a), missing));
            }
        } finally {
            _painting = false;
        }
    }

/*
===============
Rule
===============
*/
    UIElement Rule(string kind, string title, string sub, bool on, string? iconPath, Action<bool> set, Action remove, bool missing = false) {
        var grid = new Grid { Padding = new Thickness(12, 8, 12, 8), Margin = new Thickness(0, 0, 0, 8), ColumnSpacing = 12 };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(64) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var k = new TextBlock { Text = kind, Foreground = (Brush)Application.Current.Resources["FaintBrush"], VerticalAlignment = VerticalAlignment.Center };
        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        text.Children.Add(new TextBlock { Text = title, Foreground = (Brush)Application.Current.Resources["TextBrush"], TextWrapping = TextWrapping.Wrap });
        if (sub.Length > 0) {
            text.Children.Add(new TextBlock { Text = sub, FontSize = 12, Foreground = (Brush)Application.Current.Resources[missing ? "DangerBrush" : "MutedBrush"], TextWrapping = TextWrapping.Wrap });
        }

        var body = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, VerticalAlignment = VerticalAlignment.Center };
        if (iconPath != null) {
            var icon = new Image { Width = 28, Height = 28, Source = AppIcon(iconPath), Opacity = missing ? 0.4 : 1 };
            body.Children.Add(icon);
        }

        body.Children.Add(text);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, VerticalAlignment = VerticalAlignment.Center };
        var toggle = new ToggleButton { IsChecked = on, Style = (Style)Application.Current.Resources["CompactSwitch"] };
        HookSwitch(toggle);
        bool value = on;
        toggle.Click += async (_, _) => {
            bool next = toggle.IsChecked == true;
            if (_painting || next == value) {
                return;
            }

            value = next;
            set(value);
            await Session.ChangedRules();
        };
        var x = new Button { Style = (Style)Application.Current.Resources["MarkButton"] };
        x.Content = new TextBlock {
            Text = "\uE711",
            FontFamily = new FontFamily("Segoe Fluent Icons, Segoe MDL2 Assets"),
            FontSize = 10,
            Foreground = (Brush)Application.Current.Resources["FaintBrush"],
            VerticalAlignment = VerticalAlignment.Center,
            HorizontalAlignment = HorizontalAlignment.Center,
            TextLineBounds = TextLineBounds.Tight
        };
        x.Click += async (_, _) => {
            remove();
            await Session.ChangedRules();
            PaintRules();
        };
        actions.Children.Add(toggle);
        actions.Children.Add(x);
        Grid.SetColumn(body, 1);
        Grid.SetColumn(actions, 2);
        grid.Children.Add(k);
        grid.Children.Add(body);
        grid.Children.Add(actions);
        grid.Background = new SolidColorBrush(Windows.UI.Color.FromArgb(16, 255, 255, 255));
        grid.CornerRadius = new CornerRadius(10);
        return grid;
    }

    /*
    ===============
    PaintPolicy
    Direct and through, to match the session.
    ===============
    */
    void PaintPolicy() {
        bool direct = Session.DirectOn;
        DirectButton.Style = (Style)Application.Current.Resources[direct ? "SegmentOn" : "SegmentOff"];
        ThroughButton.Style = (Style)Application.Current.Resources[direct ? "SegmentOff" : "SegmentOn"];
    }

    /*
    ===============
    PaintHelper
    The helper line in settings.
    ===============
    */
    void PaintHelper() {
        HelperDot.Fill = (Brush)Application.Current.Resources[Session.HelperOnline ? "AccentBrush" : "FaintBrush"];
        HelperRowDot.Fill = HelperDot.Fill;
        AwgDot.Fill = (Brush)Application.Current.Resources[Session.AwgOk ? "AccentBrush" : "DangerBrush"];
    }

    /*
    ===============
    PaintPower
    The connect switch, without firing its click.
    ===============
    */
    void PaintPower() {
        bool on = Session.Connected;
        Power.BorderBrush = (Brush)Application.Current.Resources[on ? "AccentBrush" : "LineBrush"];
        Power.Background = new SolidColorBrush(on ? Windows.UI.Color.FromArgb(32, 124, 219, 182) : Windows.UI.Color.FromArgb(12, 255, 255, 255));
        if (Power.Content is FontIcon icon)
            icon.Foreground = (Brush)Application.Current.Resources[on ? "AccentBrush" : "MutedBrush"];
    }

    /*
    ===============
    FillDefaults
    The default-tunnel combo.
    ===============
    */
    void FillDefaults() {
        _quiet = true;
        DefaultCombo.Items.Clear();
        DefaultCombo.Items.Add(new ComboBoxItem { Content = "None", Tag = "" });
        foreach (var t in Session.Tunnels)
            DefaultCombo.Items.Add(new ComboBoxItem { Content = t.Name, Tag = t.Id });
        DefaultCombo.SelectedIndex = 0;
        for (int i = 0; i < DefaultCombo.Items.Count; i++)
            if (DefaultCombo.Items[i] is ComboBoxItem item && (item.Tag as string) == Session.DefaultId)
                DefaultCombo.SelectedIndex = i;
        _quiet = false;
    }

    /*
    ===============
    OnDragOver
    Accept a file drop.
    ===============
    */
    void OnDragOver(object sender, DragEventArgs e) {
        e.AcceptedOperation = Windows.ApplicationModel.DataTransfer.DataPackageOperation.Copy;
    }

    /*
    ===============
    OnDrop
    Import the dropped file.
    ===============
    */
    async void OnDrop(object sender, DragEventArgs e) {
        if (!e.DataView.Contains(Windows.ApplicationModel.DataTransfer.StandardDataFormats.StorageItems)) {
            return;
        }

        var items = await e.DataView.GetStorageItemsAsync();
        foreach (var item in items) Session.ImportFile(item.Path);
    }
}

sealed class TrayAction : ICommand {
    readonly Action _run;

    /*
    ===============
    TrayAction
    A tray command.
    ===============
    */
    public TrayAction(Action run) => _run = run;

    /*
    ===============
    CanExecute
    Tray commands are always enabled.
    ===============
    */
    public bool CanExecute(object? parameter) => true;

    /*
    ===============
    Execute
    Run the tray command.
    ===============
    */
    public void Execute(object? parameter) => _run();
    public event EventHandler? CanExecuteChanged { add { } remove { } }
}
