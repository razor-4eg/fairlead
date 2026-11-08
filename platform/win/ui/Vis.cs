//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// Vis.cs -- Bind a bool to Visible or Collapsed.
//
//===============================================================

using Microsoft.UI.Xaml;

namespace Fairlead;

public static class Vis {
    /*
    ===============
    Show
    Visible when the flag is set.
    ===============
    */
    public static Visibility Show(bool on) => on ? Visibility.Visible : Visibility.Collapsed;

    /*
    ===============
    Hide
    Collapsed when the flag is set.
    ===============
    */
    public static Visibility Hide(bool on) => on ? Visibility.Collapsed : Visibility.Visible;
}
