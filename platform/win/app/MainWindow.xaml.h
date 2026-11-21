#pragma once

#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"
#include "session.h"
#include "apps.h"

#include <shellapi.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

struct SecretField {
	winrt::Microsoft::UI::Xaml::Controls::TextBox box{ nullptr };
	std::string real;
	bool hidden = true;
};

namespace winrt::Fairlead::implementation {

struct MainWindow : MainWindowT<MainWindow> {
	MainWindow();
	void ShowMain();
	void ApplyStartHidden();
	void Start();

	void OnDragOver(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const& args);
	void OnDrop(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const& args);
	void OnEscape(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
	void SearchChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
	void TunnelChosen(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
	void TunnelDragStarting(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const& args);
	void TunnelReorder(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const& args);
	void NewTunnel(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void ImportTunnel(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void TogglePower(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void PowerPressed(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
	void PowerReleased(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
	void SetDirect(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void SetThrough(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void PolicySegmentLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void LanToggled(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void QuicToggled(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void OpenEditor(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void ExportSelected(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void AskDelete(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void OpenRoutes(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void AddApp(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void OpenSettings(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void LoginToggled(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void HiddenToggled(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void DefaultChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
	void LanguageChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
	void OpenFolder(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void StartHelper(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void CloseSheet(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void EditorSegmentLoaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void ShowFields(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void ShowRaw(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void SaveEditor(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void RouteChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
	void SaveRoutes(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void ListChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
	void SaveList(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void ConfirmDelete(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void AppSearchChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
	void BrowseApp(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
	void OnTray(LPARAM lp);

private:
	Session session;
	HWND hwnd = nullptr;
	bool quiet = false;
	bool creating = false;
	bool raw = false;
	bool quitting = false;
	bool sized = false;
	bool ready = false;
	bool ticking = false;
	bool rpc_busy = false;	/* up/down owns the helper pipe — status ticks must wait. */
	bool painting = false;
	bool direct_drawn = true;
	int sheet_epoch = 0;
	int app_epoch = 0;
	int app_icon_epoch = 0;
	bool apps_ready = false;
	bool apps_warming = false;
	std::string header_key;
	std::string chrome_language;
	std::string route_auto_name;
	std::string list_sig;
	bool tunnel_dragging = false;
	std::unique_ptr<Tunnel> draft;
	Tunnel* edit = nullptr;
	RouteList* editing_list = nullptr;
	std::shared_ptr<std::atomic<bool>> route_cancel;
	std::shared_ptr<std::atomic<bool>> list_cancel;
	std::vector<SecretField> secrets;
	std::vector<InstalledApp> installed;
	std::vector<std::pair<winrt::Microsoft::UI::Xaml::Controls::Image, std::wstring>> app_icon_jobs;
	std::vector<std::pair<std::wstring, Microsoft::UI::Xaml::Media::ImageSource>> icons;
	std::vector<Microsoft::UI::Xaml::Media::Animation::Storyboard> boards;
	Microsoft::UI::Xaml::Media::Animation::Storyboard sheet_board{ nullptr };
	bool spinning = false;
	/* XAML timer, not DispatcherQueueTimer — that one runs below idle and stalls while a dependent animation is on the UI thread. */
	Microsoft::UI::Xaml::DispatcherTimer timer{ nullptr };
	NOTIFYICONDATAW tray{};
	bool tray_added = false;
	int normal_w = 1479;
	int normal_h = 1013;
	int normal_x = 0;
	int normal_y = 0;
	bool have_pos = false;
	bool helper_starting = false;
	bool notice_shown = false;
	bool banner_shown = false;
	Tunnel* pending_delete = nullptr;
	Microsoft::UI::Xaml::Controls::MenuFlyout row_menu{ nullptr };

	winrt::fire_and_forget BootAsync();
	winrt::fire_and_forget TickAsync();
	winrt::fire_and_forget ToggleAsync(Tunnel* tunnel);
	winrt::Windows::Foundation::IAsyncAction ConnectAsync(Tunnel* tunnel);
	winrt::Windows::Foundation::IAsyncAction DisconnectAsync(Tunnel* tunnel);
	winrt::Windows::Foundation::IAsyncAction PushBypassAsync(Tunnel* tunnel);
	winrt::Windows::Foundation::IAsyncAction EnsureHelperAsync(bool interactive);
	winrt::fire_and_forget SaveRoutesAsync();
	winrt::fire_and_forget SaveListAsync();
	winrt::fire_and_forget ImportAsync();
	winrt::fire_and_forget ExportAsync(Tunnel* tunnel);
	winrt::fire_and_forget DropAsync(Microsoft::UI::Xaml::DragEventArgs args);
	winrt::fire_and_forget BrowseAsync();
	winrt::fire_and_forget WarmAppsAsync();
	winrt::fire_and_forget LoadAppsAsync(int epoch);
	winrt::fire_and_forget PaintAppIconsAsync(int epoch);
	winrt::fire_and_forget HelperAsync(bool interactive);
	winrt::fire_and_forget QuitAsync();
	winrt::fire_and_forget ConfirmDeleteAsync();

	void Paint();
	void PaintChrome();
	void PaintRules();
	void PaintPolicy();
	void PaintPower();
	void PaintHelper();
	void PaintAwg(bool animate);
	void PaintNotice(winrt::Microsoft::UI::Xaml::Controls::Border const& card, winrt::Microsoft::UI::Xaml::Controls::TextBlock const& text, std::string const& content, bool& shown);
	void PaintEditorSegment();
	void NoteSelection();
	void RebuildList();
	void FillDefaults();
	void FillLanguages();
	void ShowSettings();
	void ShowEditor();
	void ShowSheet(Microsoft::UI::Xaml::UIElement const& pane);
	void FinishClose(int epoch);
	void Hold(Microsoft::UI::Xaml::Media::Animation::Storyboard const& board);
	void HookSwitch(Microsoft::UI::Xaml::Controls::Primitives::ToggleButton const& button);
	void ShowSwitch(Microsoft::UI::Xaml::Controls::Primitives::ToggleButton const& button, bool animate);
	void BuildFields();
	void AddField(std::wstring const& key, std::wstring const& label, std::string const& value, bool secret);
	void SuggestRouteName();
	void ApplyRouteAdd();
	void ApplyListSave();
	void FillAppList();
	void OpenList(RouteList* list);
	void OpenApp(AppRule* app);
	void AskDeleteTunnel(Tunnel* tunnel);
	void OnTunnelMenu(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& args);
	void SyncTunnelReorder(bool allow);
	void FinishTunnelDrag();
	void HookHwnd();
	void RestoreBounds();
	void SaveBounds();
	void AddTray();
	void RemoveTray();
	void ShowTrayMenu();
	Microsoft::UI::Xaml::Media::ImageSource IconFor(std::wstring const& path);
	winrt::hstring H(std::string const& text) const;
	std::string S(winrt::hstring const& text) const;
};

}

namespace winrt::Fairlead::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}
