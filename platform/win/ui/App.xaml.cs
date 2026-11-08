//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// App.xaml.cs -- One instance. A second launch wakes the window that is already open.
//
//===============================================================

using System.Threading;
using Microsoft.UI.Xaml;

namespace Fairlead;

public partial class App : Application {
    private readonly object _gate = new();
    private MainWindow? _window;
    private Mutex? _instance;
    private EventWaitHandle? _show;
    private bool _showPending;

    /*
    ===============
    App
    The WinUI application object.
    ===============
    */
    public App() {
        InitializeComponent();
    }

    /*
    ===============
    OnLaunched
    The first window, or a wake of the one already open.
    ===============
    */
    protected override void OnLaunched(LaunchActivatedEventArgs args) {
        _show = new EventWaitHandle(false, EventResetMode.AutoReset, @"Local\Fairlead.Show");
        _instance = new Mutex(false, @"Local\Fairlead");
        bool owned;
        try {
            owned = _instance.WaitOne(0);
        } catch (AbandonedMutexException) {
            owned = true;
        }

        if (!owned) {
            _show.Set();
            Environment.Exit(0);
        }

        ThreadPool.RegisterWaitForSingleObject(_show, OnShow, null, Timeout.Infinite, false);
        var window = new MainWindow();
        lock (_gate) {
            _window = window;
        }

        window.Activate();
        bool pending;
        lock (_gate) {
            pending = _showPending;
        }

        if (pending) {
            window.ShowMain();
        }

        _ = window.Session.EnsureHelper();
    }

    /*
    ===============
    OnShow
    A second launch asked for the window.
    ===============
    */
    void OnShow(object? state, bool timedOut) {
        MainWindow? window;
        lock (_gate) {
            window = _window;
        }

        if (window == null) {
            lock (_gate) {
                _showPending = true;
            }

            return;
        }

        window.ShowMain();
    }
}
