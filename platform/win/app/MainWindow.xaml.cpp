#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.xaml.g.hpp"

#include "coreabi.h"
#include "motion.h"
#include "text.h"
#include "base.h"

#include <algorithm>
#include <filesystem>
#include <functional>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
namespace media = winrt::Microsoft::UI::Xaml::Media;
namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
namespace shapes = winrt::Microsoft::UI::Xaml::Shapes;
namespace imaging = winrt::Microsoft::UI::Xaml::Media::Imaging;
namespace windowing = winrt::Microsoft::UI::Windowing;
namespace input = winrt::Microsoft::UI::Xaml::Input;

struct __declspec(uuid("905a0fef-bc53-11df-8c49-001e4fc686da")) IBufferByteAccess : ::IUnknown {
	virtual HRESULT __stdcall Buffer(uint8_t** value) = 0;
};

namespace {

constexpr UINT tray_message = WM_APP + 7;

std::wstring module_dir_w() {
	wchar_t self[MAX_PATH];
	if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) {
		return L".";
	}

	std::wstring path = self;
	auto slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos) {
		return L".";
	}

	path.resize(slash);
	return path;
}

std::wstring icon_file_path(Session const& session) {
	std::wstring local = module_dir_w() + L"\\Assets\\Fairlead.ico";
	if (std::filesystem::exists(std::filesystem::path(local))) {
		return local;
	}

	std::wstring tools = widen(session.exe_dir) + L"\\Assets\\Fairlead.ico";
	if (std::filesystem::exists(std::filesystem::path(tools))) {
		return tools;
	}

	tools = widen(session.exe_dir) + L"\\bin\\Assets\\Fairlead.ico";
	if (std::filesystem::exists(std::filesystem::path(tools))) {
		return tools;
	}

	return {};
}

HICON load_app_icon() {
	int cx = GetSystemMetrics(SM_CXSMICON);
	int cy = GetSystemMetrics(SM_CYSMICON);
	if (HICON icon = reinterpret_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR))) {
		return icon;
	}

	if (HICON icon = reinterpret_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_DEFAULTCOLOR))) {
		return icon;
	}

	return LoadIconW(nullptr, IDI_APPLICATION);
}

winrt::Windows::Graphics::Imaging::SoftwareBitmap icon_bitmap(std::wstring const& path) {
	if (path.empty() || !std::filesystem::exists(std::filesystem::path(path))) {
		return nullptr;
	}

	SHFILEINFOW info{};
	if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) || !info.hIcon) {
		return nullptr;
	}

	HICON icon = info.hIcon;
	ICONINFO drawn{};
	BITMAP bits{};
	winrt::Windows::Graphics::Imaging::SoftwareBitmap bitmap{ nullptr };
	HBITMAP dib = nullptr;
	HDC screen = nullptr;
	HDC mem = nullptr;
	HGDIOBJ old = nullptr;
	if (GetIconInfo(icon, &drawn)) {
		GetObject(drawn.hbmColor ? drawn.hbmColor : drawn.hbmMask, sizeof(bits), &bits);
		int w = bits.bmWidth > 0 ? bits.bmWidth : 32;
		int h = drawn.hbmColor && bits.bmHeight > 0 ? bits.bmHeight : 32;
		BITMAPINFO header{};
		header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		header.bmiHeader.biWidth = w;
		header.bmiHeader.biHeight = -h;
		header.bmiHeader.biPlanes = 1;
		header.bmiHeader.biBitCount = 32;
		header.bmiHeader.biCompression = BI_RGB;
		void* pixels = nullptr;
		screen = GetDC(nullptr);
		dib = CreateDIBSection(screen, &header, DIB_RGB_COLORS, &pixels, nullptr, 0);
		mem = CreateCompatibleDC(screen);
		if (dib && mem && pixels) {
			old = SelectObject(mem, dib);
			DrawIconEx(mem, 0, 0, icon, w, h, 0, nullptr, DI_NORMAL);
			auto* bgra = static_cast<uint8_t*>(pixels);
			for (int i = 0; i < w * h; i++) {
				uint8_t a = bgra[i * 4 + 3];
				bgra[i * 4] = static_cast<uint8_t>(bgra[i * 4] * a / 255);
				bgra[i * 4 + 1] = static_cast<uint8_t>(bgra[i * 4 + 1] * a / 255);
				bgra[i * 4 + 2] = static_cast<uint8_t>(bgra[i * 4 + 2] * a / 255);
			}

			try {
				winrt::Windows::Storage::Streams::Buffer buffer(w * h * 4);
				buffer.Length(w * h * 4);
				winrt::com_ptr<::IBufferByteAccess> access;
				winrt::check_hresult(winrt::get_unknown(buffer)->QueryInterface(access.put()));
				uint8_t* dest = nullptr;
				winrt::check_hresult(access->Buffer(&dest));
				memcpy(dest, pixels, w * h * 4);
				bitmap = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
					buffer,
					winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
					w,
					h,
					winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);
			} catch (...) {
				bitmap = nullptr;
			}
		}

		if (drawn.hbmColor) {
			DeleteObject(drawn.hbmColor);
		}

		if (drawn.hbmMask) {
			DeleteObject(drawn.hbmMask);
		}
	}

	if (old && mem) {
		SelectObject(mem, old);
	}

	if (mem) {
		DeleteDC(mem);
	}

	if (dib) {
		DeleteObject(dib);
	}

	if (screen) {
		ReleaseDC(nullptr, screen);
	}

	DestroyIcon(icon);
	return bitmap;
}

winrt::Windows::UI::Color rgba(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
	return { a, r, g, b };
}

mux::Media::Brush brush(wchar_t const* key) {
	return mux::Application::Current().Resources().Lookup(winrt::box_value(winrt::hstring(key))).as<mux::Media::Brush>();
}

mux::Style style_of(wchar_t const* key) {
	return mux::Application::Current().Resources().Lookup(winrt::box_value(winrt::hstring(key))).as<mux::Style>();
}

mux::Visibility shown(bool on) {
	return on ? mux::Visibility::Visible : mux::Visibility::Collapsed;
}

bool is_on(controls::Primitives::ToggleButton const& button) {
	auto checked = button.IsChecked();
	return checked && checked.Value();
}

controls::TextBox find_box(mux::UIElement const& node) {
	if (auto box = node.try_as<controls::TextBox>()) {
		return box;
	}

	if (auto panel = node.try_as<controls::Panel>()) {
		for (auto const& child : panel.Children()) {
			if (auto found = find_box(child)) {
				return found;
			}
		}
	}

	return nullptr;
}

std::wstring bullets(size_t count) {
	if (count > 36) {
		count = 36;
	}

	return std::wstring(count, L'\u2022');
}

LRESULT CALLBACK subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
	auto* self = reinterpret_cast<winrt::Fairlead::implementation::MainWindow*>(data);
	if (msg == tray_message && self) {
		self->OnTray(lp);
		return 0;
	}

	if (msg == WM_WINDOWPOSCHANGING) {
		auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
		if ((pos->flags & SWP_NOSIZE) == 0) {
			if (pos->cx > 0 && pos->cx < 980) {
				pos->cx = 980;
			}

			if (pos->cy > 0 && pos->cy < 660) {
				pos->cy = 660;
			}
		}
	}

	LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
	if (msg == WM_GETMINMAXINFO) {
		auto* info = reinterpret_cast<MINMAXINFO*>(lp);
		if (info->ptMinTrackSize.x < 980) {
			info->ptMinTrackSize.x = 980;
		}

		if (info->ptMinTrackSize.y < 660) {
			info->ptMinTrackSize.y = 660;
		}
	}

	return result;
}

}

namespace winrt::Fairlead::implementation {

hstring MainWindow::H(std::string const& text) const {
	return hstring(widen(text));
}

std::string MainWindow::S(hstring const& text) const {
	return narrow(text);
}

MainWindow::MainWindow() {
	InitializeComponent();
	Title(L"Fairlead");
	ExtendsContentIntoTitleBar(true);
	SetTitleBar(DragRegion());
	auto title = AppWindow().TitleBar();
	auto clear = rgba(0, 0, 0, 0);
	title.BackgroundColor(clear);
	title.InactiveBackgroundColor(clear);
	title.ButtonBackgroundColor(clear);
	title.ButtonInactiveBackgroundColor(clear);
	title.ButtonForegroundColor(rgba(255, 147, 160, 150));
	title.ButtonHoverBackgroundColor(rgba(24, 255, 255, 255));
	if (std::wstring icon = icon_file_path(session); !icon.empty()) {
		AppWindow().SetIcon(icon);
	}

	if (auto presenter = AppWindow().Presenter().try_as<windowing::OverlappedPresenter>()) {
		presenter.PreferredMinimumWidth(980);
		presenter.PreferredMinimumHeight(660);
	}

	RestoreBounds();
	AppWindow().Changed([this](auto&&, windowing::AppWindowChangedEventArgs const& args) {
		if (!ready) {
			return;
		}

		if (auto presenter = AppWindow().Presenter().try_as<windowing::OverlappedPresenter>()) {
			if (presenter.State() == windowing::OverlappedPresenterState::Maximized) {
				return;
			}
		}

		if (args.DidSizeChange()) {
			auto size = AppWindow().Size();
			normal_w = size.Width;
			normal_h = size.Height;
		}

		if (args.DidPositionChange()) {
			auto pos = AppWindow().Position();
			normal_x = pos.X;
			normal_y = pos.Y;
			have_pos = true;
		}
	});
	Activated([this](auto&&, auto&&) { HookHwnd(); });
	AppWindow().Closing([this](auto&&, windowing::AppWindowClosingEventArgs const& args) {
		SaveBounds();
		if (quitting) {
			return;
		}

		args.Cancel(true);
		DispatcherQueue().TryEnqueue([this] { AppWindow().Hide(); });
	});
	HookSwitch(LanSwitch());
	HookSwitch(LoginSwitch());
	HookSwitch(HiddenSwitch());
	quiet = true;
	LanSwitch().IsChecked(session.selected ? session.selected->exclude_lan : true);
	LoginSwitch().IsChecked(session.launch_at_login);
	HiddenSwitch().IsChecked(session.start_hidden);
	quiet = false;
	Paint();
	FillDefaults();
	TunnelList().RightTapped([this](auto&& sender, auto&& args) {
		OnTunnelMenu(sender, args);
	});
	timer = DispatcherQueue().CreateTimer();
	timer.Interval(std::chrono::seconds(1));
	timer.Tick([this](auto&&, auto&&) {
		if (!ticking) {
			TickAsync();
		}
	});
	timer.Start();
	TickAsync();
}

void MainWindow::ShowMain() {
	DispatcherQueue().TryEnqueue([this] {
		AppWindow().Show();
		Activate();
	});
}

void MainWindow::ApplyStartHidden() {
	if (!session.start_hidden) {
		return;
	}

	DispatcherQueue().TryEnqueue([this] { AppWindow().Hide(); });
}

void MainWindow::Start() {
	BootAsync();
	WarmAppsAsync();
}

void MainWindow::HookHwnd() {
	if (sized) {
		return;
	}

	sized = true;
	hwnd = winrt::Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());
	SetWindowSubclass(hwnd, subclass_proc, 1, reinterpret_cast<DWORD_PTR>(this));
	AddTray();
}

void MainWindow::RestoreBounds() {
	int width = normal_w;
	int height = normal_h;
	bool maximized = false;
	try {
		std::string text = read_file(widen(session.dir) + L"\\window.json");
		if (!text.empty()) {
			arena_t arena;
			arena_init(&arena, 1 << 16);
			if (auto* root = json_parse(&arena, text.c_str())) {
				if (char const* value = json_string(json_get(root, "width"))) {
					width = atoi(value);
				}

				if (char const* value = json_string(json_get(root, "height"))) {
					height = atoi(value);
				}

				if (char const* value = json_string(json_get(root, "x"))) {
					normal_x = atoi(value);
					have_pos = true;
				}

				if (char const* value = json_string(json_get(root, "y"))) {
					normal_y = atoi(value);
					have_pos = true;
				}

				if (auto* flag = json_get(root, "maximized")) {
					maximized = flag->type == JSON_BOOL ? flag->boolean != 0 : json_string(flag) && strcmp(json_string(flag), "true") == 0;
				}
			}

			arena_free(&arena);
		}
	} catch (...) {
	}

	width = std::max(980, width);
	height = std::max(660, height);
	normal_w = width;
	normal_h = height;
	AppWindow().Resize({ width, height });
	if (have_pos) {
		AppWindow().Move({ normal_x, normal_y });
	} else {
		auto pos = AppWindow().Position();
		normal_x = pos.X;
		normal_y = pos.Y;
		have_pos = true;
	}

	if (maximized) {
		if (auto presenter = AppWindow().Presenter().try_as<windowing::OverlappedPresenter>()) {
			presenter.Maximize();
		}
	}

	ready = true;
}

void MainWindow::SaveBounds() {
	try {
		bool maximized = false;
		if (auto presenter = AppWindow().Presenter().try_as<windowing::OverlappedPresenter>()) {
			maximized = presenter.State() == windowing::OverlappedPresenterState::Maximized;
		}

		if (!maximized) {
			auto size = AppWindow().Size();
			auto pos = AppWindow().Position();
			normal_w = size.Width;
			normal_h = size.Height;
			normal_x = pos.X;
			normal_y = pos.Y;
		}

		int width = std::max(980, normal_w);
		int height = std::max(660, normal_h);
		std::string json =
			"{\n  \"width\": " + std::to_string(width) +
			",\n  \"height\": " + std::to_string(height) +
			",\n  \"x\": " + std::to_string(normal_x) +
			",\n  \"y\": " + std::to_string(normal_y) +
			",\n  \"maximized\": " + (maximized ? "true" : "false") +
			"\n}\n";
		std::filesystem::create_directories(std::filesystem::path(widen(session.dir)));
		write_file(widen(session.dir) + L"\\window.json", json);
	} catch (...) {
	}
}

void MainWindow::Hold(anim::Storyboard const& board) {
	if (!board) {
		return;
	}

	boards.push_back(board);
	auto life = get_strong();
	board.Completed([this, life](IInspectable const& sender, IInspectable const&) {
		auto done = sender.as<anim::Storyboard>();
		boards.erase(std::remove(boards.begin(), boards.end(), done), boards.end());
	});
	board.Begin();
}

void MainWindow::HookSwitch(controls::Primitives::ToggleButton const& button) {
	button.Loaded([this, button](auto&&, auto&&) { ShowSwitch(button, false); });
	button.Checked([this, button](auto&&, auto&&) { ShowSwitch(button, !quiet && button.IsLoaded()); });
	button.Unchecked([this, button](auto&&, auto&&) { ShowSwitch(button, !quiet && button.IsLoaded()); });
}

void MainWindow::ShowSwitch(controls::Primitives::ToggleButton const& button, bool animate) {
	if (mux::Media::VisualTreeHelper::GetChildrenCount(button) == 0) {
		return;
	}

	auto root = mux::Media::VisualTreeHelper::GetChild(button, 0).try_as<controls::Grid>();
	if (!root || mux::Media::VisualTreeHelper::GetChildrenCount(root) < 2) {
		return;
	}

	bool on = is_on(button);
	if (auto track = mux::Media::VisualTreeHelper::GetChild(root, 0).try_as<controls::Border>()) {
		track.Background(brush(on ? L"ToggleSwitchFillOn" : L"ToggleSwitchFillOff"));
		track.BorderBrush(brush(on ? L"ToggleSwitchStrokeOn" : L"ToggleSwitchStrokeOff"));
	}

	if (auto knob = mux::Media::VisualTreeHelper::GetChild(root, 1).try_as<shapes::Ellipse>()) {
		knob.Fill(brush(on ? L"ToggleSwitchKnobFillOn" : L"ToggleSwitchKnobFillOff"));
		Hold(motion::knob(knob, on ? 15 : 0, animate));
	}
}

void MainWindow::Paint() {
	auto* tunnel = session.selected;
	bool has = tunnel != nullptr;
	Empty().Visibility(shown(!has));
	Detail().Visibility(shown(has));
	NoticeCard().Visibility(shown(!session.notice.empty()));
	NoticeText().Text(H(session.notice));
	Banner().Visibility(shown(!session.banner.empty()));
	BannerText().Text(H(session.banner));
	TitleText().Text(H(tunnel ? tunnel->name : ""));
	EndpointText().Text(H(tunnel ? tunnel->endpoint : ""));
	AddressText().Text(H(tunnel ? tunnel->address : ""));
	PhaseText().Text(H(tunnel ? tunnel->phase : ""));
	DetailText().Text(H(tunnel ? tunnel->detail : ""));
	IfaceText().Text(H(tunnel ? tunnel->iface : ""));
	DownText().Text(H(tunnel ? tunnel->down : "0 B/s"));
	UpText().Text(H(tunnel ? tunnel->up : "0 B/s"));
	RxText().Text(H(tunnel ? tunnel->rx : "0 B"));
	TxText().Text(H(tunnel ? tunnel->tx : "0 B"));
	PolicyTitleText().Text(H(session.PolicyTitle()));
	PolicyBodyText().Text(H(session.PolicyBody()));
	LanBody().Text(H(tunnel ? tunnel->lan : "The current Wi-Fi or Ethernet network. It appears after you connect."));
	RulesEmpty().Visibility(shown(session.RulesEmpty()));
	NoteSelection();
	RebuildList();
	PaintPolicy();
	PaintPower();
	PaintHelper();
}

void MainWindow::NoteSelection() {
	std::string key = session.selected ? session.selected->id : "";
	if (key == header_key) {
		PaintAwg(false);
		return;
	}

	header_key = key;
	DetailScroll().ChangeView(nullptr, winrt::box_value(0.0).as<winrt::Windows::Foundation::IReference<double>>(), nullptr, true);
	quiet = true;
	LanSwitch().IsChecked(session.selected ? session.selected->exclude_lan : true);
	quiet = false;
	if (!session.selected) {
		PaintAwg(false);
		PaintTitlebar();
		PaintRules();
		return;
	}

	motion::snap(HeaderMeta());
	Hold(motion::swap_in(HeaderMeta()));
	PaintAwg(true);
	PaintTitlebar();
	PaintRules();
}

void MainWindow::RebuildList() {
	std::string content;
	for (auto* tunnel : session.Filtered()) {
		content += tunnel->id + "\t" + tunnel->name + "\t" + tunnel->endpoint + "\t" + (tunnel->connected ? "1" : "0") + "\n";
	}

	std::string select = session.selected ? session.selected->id : "";
	if (content == list_sig) {
		bool was = quiet;
		quiet = true;
		controls::ListViewItem chosen{ nullptr };
		for (auto const& item : TunnelList().Items()) {
			auto row = item.try_as<controls::ListViewItem>();
			if (!row) {
				continue;
			}

			auto tag = row.Tag();
			std::string id = tag ? narrow(winrt::unbox_value_or<hstring>(tag, L"")) : "";
			if (id == select) {
				chosen = row;
				break;
			}
		}

		TunnelList().SelectedItem(chosen);
		quiet = was;
		return;
	}

	list_sig = content;
	bool was = quiet;
	quiet = true;
	TunnelList().Items().Clear();
	controls::ListViewItem chosen{ nullptr };
	for (auto* tunnel : session.Filtered()) {
		controls::Grid grid;
		grid.Padding({ 10, 9, 10, 9 });
		grid.ColumnSpacing(10);
		controls::ColumnDefinition dot;
		dot.Width({ 0, mux::GridUnitType::Auto });
		controls::ColumnDefinition text;
		text.Width({ 1, mux::GridUnitType::Star });
		grid.ColumnDefinitions().Append(dot);
		grid.ColumnDefinitions().Append(text);
		shapes::Ellipse mark;
		mark.Width(7);
		mark.Height(7);
		mark.HorizontalAlignment(mux::HorizontalAlignment::Left);
		mark.VerticalAlignment(mux::VerticalAlignment::Center);
		mark.Fill(media::SolidColorBrush(tunnel->connected ? rgba(255, 124, 219, 182) : rgba(255, 94, 107, 98)));
		controls::StackPanel lines;
		lines.Spacing(2);
		controls::TextBlock name;
		name.Text(H(tunnel->name));
		name.FontSize(14);
		name.Foreground(brush(L"TextBrush"));
		name.TextTrimming(mux::TextTrimming::CharacterEllipsis);
		controls::TextBlock endpoint;
		endpoint.Text(H(tunnel->endpoint));
		endpoint.FontSize(12);
		endpoint.Foreground(brush(L"MutedBrush"));
		endpoint.TextTrimming(mux::TextTrimming::CharacterEllipsis);
		lines.Children().Append(name);
		lines.Children().Append(endpoint);
		grid.Children().Append(mark);
		grid.Children().Append(lines);
		controls::Grid::SetColumn(lines, 1);
		controls::ListViewItem item;
		item.Content(grid);
		item.Tag(box_value(H(tunnel->id)));
		TunnelList().Items().Append(item);
		if (tunnel == session.selected) {
			chosen = item;
		}
	}

	if (chosen) {
		TunnelList().SelectedItem(chosen);
	}

	quiet = was;
}

void MainWindow::PaintPolicy() {
	bool direct = session.DirectOn();
	auto ink = brush(L"InkBrush");
	auto muted = brush(L"MutedBrush");
	DirectButton().Foreground(direct ? ink : muted);
	ThroughButton().Foreground(direct ? muted : ink);
	double cell = PolicySegment().ActualWidth() > 0 ? (PolicySegment().ActualWidth() - 8) / 2.0 : 85;
	if (std::abs(PolicyThumb().Width() - cell) > 0.5) {
		PolicyThumb().Width(cell);
	}

	Hold(motion::slide_x(PolicyThumbX(), direct ? 0 : cell, 280, !quiet));
	direct_drawn = direct;
}

void MainWindow::PaintEditorSegment() {
	auto ink = brush(L"InkBrush");
	auto muted = brush(L"MutedBrush");
	FieldsButton().Foreground(raw ? muted : ink);
	TextButton().Foreground(raw ? ink : muted);
	double cell = EditorSegment().ActualWidth() > 0 ? (EditorSegment().ActualWidth() - 8) / 2.0 : 70;
	if (std::abs(EditorThumb().Width() - cell) > 0.5) {
		EditorThumb().Width(cell);
	}

	Hold(motion::slide_x(EditorThumbX(), raw ? cell : 0, 280, true));
}

void MainWindow::PaintPower() {
	auto* tunnel = session.selected;
	std::string phase = tunnel ? tunnel->phase : "";
	bool on = tunnel && (tunnel->connected || phase == "Connected");
	bool busy = phase == "Connecting" || phase == "Disconnecting";
	Power().IsEnabled(!busy);
	Power().Background(media::SolidColorBrush(on ? rgba(32, 124, 219, 182) : rgba(12, 255, 255, 255)));
	PowerRing().Stroke(brush(on ? L"AccentBrush" : L"LineBrush"));
	PowerIcon().Foreground(brush(on ? L"AccentBrush" : L"MutedBrush"));
	if (busy) {
		PowerSpin().Visibility(mux::Visibility::Visible);
		if (!spin_board) {
			spin_board = motion::spin(PowerSpinRotate());
			spin_board.Begin();
		}
	} else {
		PowerSpin().Visibility(mux::Visibility::Collapsed);
		if (spin_board) {
			spin_board.Stop();
			spin_board = nullptr;
			PowerSpinRotate().Angle(0);
		}
	}

	PaintTitlebar();
}

void MainWindow::PaintTitlebar() {
	auto* tunnel = session.selected;
	std::string phase = tunnel ? tunnel->phase : "";
	bool live = tunnel && !phase.empty() && phase != "Off";
	TitlePhase().Visibility(shown(live));
	TitlePhaseDot().Fill(brush(L"AccentBrush"));
	bool busy = phase == "Connecting" || phase == "Disconnecting";
	TitlePhaseDot().Opacity(live ? (busy ? 0.7 : 1) : 0.55);
	if (!live) {
		return;
	}

	TitlePhaseText().Text(H(busy ? phase + "…" : phase));
	TitlePhaseText().Foreground(brush(L"AccentBrush"));
}

void MainWindow::PaintHelper() {
	auto fill = brush(session.helper_online ? L"AccentBrush" : L"FaintBrush");
	HelperDot().Fill(fill);
	HelperDot().Opacity(session.helper_online ? 1 : 0.45);
	mux::Controls::ToolTipService::SetToolTip(HelperDot(), box_value(H(session.HelperText())));
	HelperRowDot().Fill(fill);
	AwgDot().Fill(brush(session.awg_ok ? L"AccentBrush" : L"DangerBrush"));
	AwgLine().Text(H(session.awg_text));
}

void MainWindow::PaintAwg(bool animate) {
	bool show = session.selected && session.selected->amnezia;
	if (!show) {
		AwgBadge().Visibility(mux::Visibility::Collapsed);
		motion::snap(AwgBadge());
		return;
	}

	AwgBadge().Visibility(mux::Visibility::Visible);
	if (animate) {
		Hold(motion::slide_in_x(AwgBadge(), 18, 260));
	} else {
		motion::snap(AwgBadge());
	}
}

void MainWindow::PaintRules() {
	painting = true;
	bool was = quiet;
	quiet = true;
	Rules().Children().Clear();
	auto* tunnel = session.selected;
	if (!tunnel) {
		quiet = was;
		painting = false;
		RulesEmpty().Visibility(mux::Visibility::Collapsed);
		return;
	}

	auto summary = [](RouteList const* list) {
		int domains = 0;
		int addresses = 0;
		for (auto const& entry : list->entries) {
			if (entry.kind == "domain") {
				domains++;
			} else {
				addresses++;
			}
		}

		if (domains > 0 && addresses > 0) {
			return std::to_string(domains) + (domains == 1 ? " domain, " : " domains, ") + std::to_string(addresses) + (addresses == 1 ? " address" : " addresses");
		}

		if (domains > 0) {
			return std::to_string(domains) + (domains == 1 ? " domain" : " domains");
		}

		return std::to_string(addresses) + (addresses == 1 ? " address" : " addresses");
	};
	auto add = [&](std::string const& kind, std::string const& title, std::string const& sub, bool on, std::wstring const& icon_path, bool missing, std::function<void(bool)> set, std::function<void()> remove, std::function<void()> look) {
		controls::Grid grid;
		grid.Padding({ 11, 11, 11, 11 });
		grid.Margin({ 0, 0, 0, 8 });
		grid.ColumnSpacing(8);
		grid.CornerRadius({ 10, 10, 10, 10 });
		grid.Background(media::SolidColorBrush(rgba(8, 255, 255, 255)));
		controls::ColumnDefinition c0;
		c0.Width({ 0, mux::GridUnitType::Auto });
		controls::ColumnDefinition c1;
		c1.Width({ 1, mux::GridUnitType::Star });
		controls::ColumnDefinition c2;
		c2.Width({ 0, mux::GridUnitType::Auto });
		grid.ColumnDefinitions().Append(c0);
		grid.ColumnDefinitions().Append(c1);
		grid.ColumnDefinitions().Append(c2);
		controls::TextBlock k;
		k.Text(H(kind));
		k.FontSize(10);
		k.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
		k.Foreground(brush(L"MutedBrush"));
		k.VerticalAlignment(mux::VerticalAlignment::Center);
		k.MinWidth(28);
		k.Margin({ 0, 0, 2, 0 });
		controls::StackPanel text;
		text.Spacing(2);
		text.VerticalAlignment(mux::VerticalAlignment::Center);
		controls::TextBlock title_block;
		title_block.Text(H(title));
		title_block.FontSize(14);
		title_block.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Medium());
		title_block.Foreground(brush(on ? L"TextBrush" : L"FaintBrush"));
		title_block.TextWrapping(mux::TextWrapping::Wrap);
		text.Children().Append(title_block);
		if (!sub.empty()) {
			controls::TextBlock line;
			line.Text(H(sub));
			line.FontSize(12);
			line.Foreground(brush(missing ? L"DangerBrush" : L"MutedBrush"));
			line.TextWrapping(mux::TextWrapping::Wrap);
			text.Children().Append(line);
		}

		controls::StackPanel body;
		body.Orientation(controls::Orientation::Horizontal);
		body.Spacing(10);
		body.VerticalAlignment(mux::VerticalAlignment::Center);
		if (!icon_path.empty()) {
			controls::Image icon;
			icon.Width(28);
			icon.Height(28);
			icon.Opacity(missing ? 0.4 : 1);
			if (auto source = IconFor(icon_path)) {
				icon.Source(source);
			}

			body.Children().Append(icon);
		}

		body.Children().Append(text);
		controls::StackPanel actions;
		actions.Orientation(controls::Orientation::Horizontal);
		actions.Spacing(8);
		actions.VerticalAlignment(mux::VerticalAlignment::Center);
		if (look) {
			controls::Button view;
			view.Content(box_value(hstring(L"View")));
			view.Style(style_of(L"LinkButton"));
			view.Click([look](auto&&, auto&&) { look(); });
			actions.Children().Append(view);
		}

		controls::Primitives::ToggleButton toggle;
		toggle.IsChecked(on);
		toggle.Style(style_of(L"CompactSwitch"));
		HookSwitch(toggle);
		toggle.Click([this, toggle, title_block, set, on](auto&&, auto&&) mutable {
			bool next = is_on(toggle);
			if (painting || next == on) {
				return;
			}

			on = next;
			set(on);
			title_block.Foreground(brush(on ? L"TextBrush" : L"FaintBrush"));
			session.SaveRules();
			PushBypassAsync(session.selected);
		});
		controls::Button remove_button;
		remove_button.Style(style_of(L"MarkButton"));
		controls::TextBlock glyph;
		glyph.Text(L"\uE711");
		glyph.FontFamily(media::FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
		glyph.FontSize(10);
		glyph.Foreground(brush(L"FaintBrush"));
		glyph.VerticalAlignment(mux::VerticalAlignment::Center);
		glyph.HorizontalAlignment(mux::HorizontalAlignment::Center);
		remove_button.Content(glyph);
		remove_button.Click([this, remove](auto&&, auto&&) {
			remove();
			session.SaveRules();
			PaintRules();
			PushBypassAsync(session.selected);
		});
		actions.Children().Append(toggle);
		actions.Children().Append(remove_button);
		grid.Children().Append(k);
		grid.Children().Append(body);
		grid.Children().Append(actions);
		controls::Grid::SetColumn(body, 1);
		controls::Grid::SetColumn(actions, 2);
		Rules().Children().Append(grid);
	};
	for (auto const& list : tunnel->lists) {
		RouteList* item = list.get();
		add("List", item->name, summary(item), item->enabled, L"", false, [item](bool value) { item->enabled = value; }, [this, tunnel, item] {
			std::erase_if(tunnel->lists, [&](std::unique_ptr<RouteList> const& one) { return one.get() == item; });
		}, [this, item] { OpenList(item); });
	}

	bool pending = false;
	for (auto const& list : tunnel->lists) {
		if (!list->enabled) {
			continue;
		}

		for (auto const& entry : list->entries) {
			if (entry.kind == "domain" && entry.addresses.empty() && entry.status.empty()) {
				pending = true;
			}
		}
	}

	if (pending && !resolve_posted) {
		resolve_posted = true;
		ResolveAsync(tunnel);
	}

	for (auto const& app : tunnel->apps) {
		AppRule* item = app.get();
		bool missing = !std::filesystem::exists(std::filesystem::path(widen(item->path)));
		std::string sub = missing ? "App not found" : std::to_string(item->learned.size()) + " addresses";
		add("App", item->name, sub, item->enabled, widen(item->path), missing, [item](bool value) { item->enabled = value; }, [tunnel, item] {
			std::erase_if(tunnel->apps, [&](std::unique_ptr<AppRule> const& one) { return one.get() == item; });
		}, [this, item] { OpenApp(item); });
	}

	RulesEmpty().Visibility(shown(session.RulesEmpty()));
	quiet = was;
	painting = false;
}

mux::Media::ImageSource MainWindow::IconFor(std::wstring const& path) {
	for (auto const& one : icons) {
		if (iequals(one.first, path)) {
			return one.second;
		}
	}

	imaging::SoftwareBitmapSource source{ nullptr };
	try {
		if (auto bitmap = icon_bitmap(path)) {
			source = imaging::SoftwareBitmapSource();
			source.SetBitmapAsync(bitmap);
		}
	} catch (...) {
		source = nullptr;
	}

	icons.push_back({ path, source });
	return source;
}

void MainWindow::FillDefaults() {
	bool was = quiet;
	quiet = true;
	DefaultCombo().Items().Clear();
	auto add = [&](wchar_t const* label, std::string const& tag) {
		controls::ComboBoxItem item;
		item.Content(box_value(hstring(label)));
		item.Tag(box_value(H(tag)));
		DefaultCombo().Items().Append(item);
	};
	add(L"None", "");
	add(L"Last active", Session::last_key);
	for (auto const& tunnel : session.tunnels) {
		controls::ComboBoxItem item;
		item.Content(box_value(H(tunnel->name)));
		item.Tag(box_value(H(tunnel->id)));
		DefaultCombo().Items().Append(item);
	}

	DefaultCombo().SelectedIndex(0);
	for (uint32_t i = 0; i < DefaultCombo().Items().Size(); i++) {
		auto item = DefaultCombo().Items().GetAt(i).as<controls::ComboBoxItem>();
		auto tag = item.Tag().try_as<hstring>();
		if (tag && S(*tag) == session.default_id) {
			DefaultCombo().SelectedIndex(static_cast<int32_t>(i));
		}
	}

	quiet = was;
}

void MainWindow::ShowSettings() {
	AwgLine().Text(H(session.awg_text));
	PaintHelper();
	FillDefaults();
	ShowSheet(SettingsPane());
}

void MainWindow::ShowSheet(mux::UIElement const& pane) {
	sheet_epoch++;
	if (sheet_board) {
		sheet_board.Stop();
	}

	Overlay().Opacity(0);
	SheetScale().ScaleX(0.96);
	SheetScale().ScaleY(0.96);
	SettingsPane().Visibility(shown(pane == SettingsPane()));
	EditorPane().Visibility(shown(pane == EditorPane()));
	RoutePane().Visibility(shown(pane == RoutePane()));
	ListPane().Visibility(shown(pane == ListPane()));
	ConfirmPane().Visibility(shown(pane == ConfirmPane()));
	AppPane().Visibility(shown(pane == AppPane()));
	Overlay().Visibility(mux::Visibility::Visible);
	sheet_board = motion::sheet(Overlay(), SheetScale(), 0, 0.96, 1, 1, 200);
	sheet_board.Begin();
}

void MainWindow::CloseSheet(IInspectable const&, mux::RoutedEventArgs const&) {
	creating = false;
	edit = nullptr;
	editing_list = nullptr;
	draft.reset();
	if (route_cancel) {
		route_cancel->store(true);
	}

	if (list_cancel) {
		list_cancel->store(true);
	}

	app_epoch++;
	if (Overlay().Visibility() != mux::Visibility::Visible) {
		return;
	}

	double opacity = Overlay().Opacity();
	double scale = SheetScale().ScaleX();
	if (opacity < 0.2) {
		opacity = 1;
	}

	if (scale < 0.9) {
		scale = 1;
	}

	int epoch = ++sheet_epoch;
	if (sheet_board) {
		sheet_board.Stop();
	}

	Overlay().Opacity(opacity);
	SheetScale().ScaleX(scale);
	SheetScale().ScaleY(scale);
	sheet_board = motion::sheet(Overlay(), SheetScale(), opacity, scale, 0, 0.94, 280);
	auto life = get_strong();
	sheet_board.Completed([this, life, epoch](auto&&, auto&&) { FinishClose(epoch); });
	sheet_board.Begin();
}

void MainWindow::FinishClose(int epoch) {
	if (epoch != sheet_epoch || Overlay().Visibility() != mux::Visibility::Visible) {
		return;
	}

	sheet_epoch++;
	Overlay().Visibility(mux::Visibility::Collapsed);
	if (sheet_board) {
		sheet_board.Stop();
		sheet_board = nullptr;
	}

	Overlay().Opacity(1);
	SheetScale().ScaleX(1);
	SheetScale().ScaleY(1);
}

void MainWindow::OnEscape(mux::Input::KeyboardAccelerator const&, mux::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
	if (Overlay().Visibility() != mux::Visibility::Visible) {
		return;
	}

	args.Handled(true);
	CloseSheet(nullptr, nullptr);
}

void MainWindow::SearchChanged(IInspectable const&, controls::TextChangedEventArgs const&) {
	session.query = S(Search().Text());
	RebuildList();
}

void MainWindow::TunnelChosen(IInspectable const&, controls::SelectionChangedEventArgs const&) {
	if (quiet) {
		return;
	}

	auto item = TunnelList().SelectedItem().try_as<controls::ListViewItem>();
	if (!item) {
		return;
	}

	auto tag = item.Tag().try_as<hstring>();
	if (!tag) {
		return;
	}

	auto* tunnel = session.Find(S(*tag));
	if (!tunnel || tunnel == session.selected) {
		return;
	}

	session.selected = tunnel;
	Paint();
}

void MainWindow::OnTunnelMenu(IInspectable const&, input::RightTappedRoutedEventArgs const& args) {
	auto node = args.OriginalSource().try_as<mux::DependencyObject>();
	controls::ListViewItem row{ nullptr };
	while (node) {
		row = node.try_as<controls::ListViewItem>();
		if (row) {
			break;
		}

		node = mux::Media::VisualTreeHelper::GetParent(node);
	}

	if (!row) {
		return;
	}

	auto tag = row.Tag().try_as<hstring>();
	if (!tag) {
		return;
	}

	auto* tunnel = session.Find(S(*tag));
	if (!tunnel) {
		return;
	}

	session.selected = tunnel;
	TunnelList().SelectedItem(row);
	Paint();
	controls::MenuFlyout flyout;
	controls::MenuFlyoutItem connect;
	connect.Text(tunnel->connected ? L"Disconnect" : L"Connect");
	connect.Click([this, tunnel](auto&&, auto&&) { ToggleAsync(tunnel); });
	controls::MenuFlyoutItem edit_item;
	edit_item.Text(L"Edit");
	edit_item.Click([this](auto&&, auto&&) { OpenEditor(nullptr, nullptr); });
	controls::MenuFlyoutItem exp;
	exp.Text(L"Export…");
	exp.Click([this, tunnel](auto&&, auto&&) { ExportAsync(tunnel); });
	controls::MenuFlyoutItem del;
	del.Text(L"Delete");
	del.Click([this, tunnel](auto&&, auto&&) { AskDeleteTunnel(tunnel); });
	flyout.Items().Append(connect);
	flyout.Items().Append(edit_item);
	flyout.Items().Append(exp);
	flyout.Items().Append(controls::MenuFlyoutSeparator());
	flyout.Items().Append(del);
	row_menu = flyout;
	flyout.ShowAt(TunnelList(), args.GetPosition(TunnelList()));
	args.Handled(true);
}

void MainWindow::NewTunnel(IInspectable const&, mux::RoutedEventArgs const&) {
	creating = true;
	draft = session.MakeDraft();
	edit = draft.get();
	ShowEditor();
}

void MainWindow::OpenEditor(IInspectable const&, mux::RoutedEventArgs const&) {
	if (!session.selected) {
		return;
	}

	creating = false;
	draft.reset();
	edit = session.selected;
	ShowEditor();
}

void MainWindow::ShowEditor() {
	if (!edit) {
		return;
	}

	raw = false;
	RawConfig().Text(H(edit->config));
	BuildFields();
	EditorScroll().Visibility(mux::Visibility::Visible);
	RawConfig().Visibility(mux::Visibility::Collapsed);
	PaintEditorSegment();
	ShowSheet(EditorPane());
}

void MainWindow::BuildFields() {
	EditorFields().Children().Clear();
	secrets.clear();
	if (!edit) {
		return;
	}

	AddField(L"Name", L"Name", edit->name, false);
	std::string text = coreabi::inspect(edit->config);
	arena_t arena;
	arena_init(&arena, static_cast<int>(std::max<size_t>(text.size() * 4 + 65536, 65536)));
	json_t* root = json_parse(&arena, text.c_str());
	auto walk = [&](json_t* obj, bool peer) {
		if (!obj) {
			return;
		}

		for (auto* prop = obj->child; prop; prop = prop->next) {
			if (!prop->key) {
				continue;
			}

			std::string value = json_string(prop) ? json_string(prop) : "";
			std::string key = peer ? std::string("peer:") + prop->key : prop->key;
			AddField(widen(key), widen(prop->key), value, !peer && strcmp(prop->key, "PrivateKey") == 0);
		}
	};
	if (root) {
		walk(json_get(root, "interface"), false);
		walk(json_get(root, "peer"), true);
	}

	arena_free(&arena);
}

void MainWindow::AddField(std::wstring const& key, std::wstring const& label, std::string const& value, bool secret) {
	controls::StackPanel row;
	row.Tag(box_value(hstring(key)));
	row.Spacing(6);
	controls::TextBlock caption;
	caption.Text(label);
	caption.FontSize(11);
	caption.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Medium());
	caption.Foreground(brush(L"MutedBrush"));
	row.Children().Append(caption);
	controls::TextBox box;
	box.Text(H(value));
	box.Style(style_of(L"QuietBox"));
	box.VerticalAlignment(mux::VerticalAlignment::Center);
	if (!secret) {
		row.Children().Append(box);
		EditorFields().Children().Append(row);
		return;
	}

	secrets.push_back({ box, value, true });
	box.Text(hstring(bullets(value.size())));
	box.IsReadOnly(true);
	box.TextChanged([this, box](auto&&, auto&&) {
		for (auto& one : secrets) {
			if (one.box == box && !one.hidden) {
				one.real = S(box.Text());
			}
		}
	});
	controls::Grid line;
	line.ColumnSpacing(8);
	controls::ColumnDefinition grow;
	grow.Width({ 1, mux::GridUnitType::Star });
	controls::ColumnDefinition rest;
	rest.Width({ 0, mux::GridUnitType::Auto });
	line.ColumnDefinitions().Append(grow);
	line.ColumnDefinitions().Append(rest);
	line.Children().Append(box);
	controls::StackPanel actions;
	actions.Orientation(controls::Orientation::Horizontal);
	actions.Spacing(8);
	actions.VerticalAlignment(mux::VerticalAlignment::Center);
	controls::Button show;
	show.Content(box_value(hstring(L"Show")));
	show.Style(style_of(L"LinkButton"));
	show.Click([this, box](auto&&, auto&&) {
		for (auto& one : secrets) {
			if (one.box != box) {
				continue;
			}

			one.hidden = !one.hidden;
			box.IsReadOnly(one.hidden);
			box.Text(one.hidden ? hstring(bullets(one.real.size())) : H(one.real));
		}
	});
	controls::Button neu;
	neu.Content(box_value(hstring(L"New key")));
	neu.Style(style_of(L"LinkButton"));
	neu.Click([this, box](auto&&, auto&&) {
		std::string priv, pub;
		coreabi::keypair(priv, pub);
		for (auto& one : secrets) {
			if (one.box != box) {
				continue;
			}

			one.real = priv;
			one.hidden = false;
			box.IsReadOnly(false);
			box.Text(H(priv));
		}
	});
	actions.Children().Append(show);
	actions.Children().Append(neu);
	line.Children().Append(actions);
	controls::Grid::SetColumn(actions, 1);
	row.Children().Append(line);
	EditorFields().Children().Append(row);
}

void MainWindow::ShowFields(IInspectable const&, mux::RoutedEventArgs const&) {
	raw = false;
	EditorScroll().Visibility(mux::Visibility::Visible);
	RawConfig().Visibility(mux::Visibility::Collapsed);
	PaintEditorSegment();
}

void MainWindow::ShowRaw(IInspectable const&, mux::RoutedEventArgs const&) {
	raw = true;
	EditorScroll().Visibility(mux::Visibility::Collapsed);
	RawConfig().Visibility(mux::Visibility::Visible);
	PaintEditorSegment();
}

void MainWindow::EditorSegmentLoaded(IInspectable const&, mux::RoutedEventArgs const&) {
	PaintEditorSegment();
}

void MainWindow::PolicySegmentLoaded(IInspectable const&, mux::RoutedEventArgs const&) {
	PaintPolicy();
}

void MainWindow::PowerPressed(IInspectable const&, input::PointerRoutedEventArgs const&) {
	motion::press(Power(), true);
}

void MainWindow::PowerReleased(IInspectable const&, input::PointerRoutedEventArgs const&) {
	motion::press(Power(), false);
}

void MainWindow::SaveEditor(IInspectable const&, mux::RoutedEventArgs const&) {
	auto* tunnel = edit;
	if (!tunnel) {
		return;
	}

	std::string config;
	if (raw) {
		config = S(RawConfig().Text());
	} else {
		std::string iface = "{";
		std::string peer = "{";
		bool iface_first = true;
		bool peer_first = true;
		for (auto const& child : EditorFields().Children()) {
			auto row = child.try_as<controls::StackPanel>();
			if (!row) {
				continue;
			}

			auto tag = row.Tag().try_as<hstring>();
			auto box = find_box(row);
			if (!tag || !box) {
				continue;
			}

			std::string key = S(*tag);
			std::string value = S(box.Text());
			for (auto const& secret : secrets) {
				if (secret.box == box) {
					value = secret.real;
				}
			}

			if (key == "Name") {
				tunnel->name = value;
				continue;
			}

			bool is_peer = key.rfind("peer:", 0) == 0;
			std::string& dest = is_peer ? peer : iface;
			bool& first = is_peer ? peer_first : iface_first;
			if (is_peer) {
				key = key.substr(5);
			}

			if (!first) {
				dest += ",";
			}

			first = false;
			dest += json_quote(key) + ":" + json_quote(value);
		}

		iface += "}";
		peer += "}";
		config = coreabi::apply(tunnel->config, "{\"interface\":" + iface + ",\"peer\":" + peer + "}");
	}

	bool was_creating = creating;
	creating = false;
	if (was_creating) {
		session.Adopt(std::move(draft));
	}

	bool live = tunnel->connected;
	session.SaveConfig(tunnel, config);
	CloseSheet(nullptr, nullptr);
	Paint();
	if (live) {
		DisconnectAsync(tunnel);
	}
}

void MainWindow::OpenRoutes(IInspectable const&, mux::RoutedEventArgs const&) {
	route_auto_name.clear();
	RouteName().Text(L"");
	RouteBox().Text(L"");
	RouteHint().Visibility(mux::Visibility::Visible);
	RouteError().Visibility(mux::Visibility::Collapsed);
	RouteError().Text(L"");
	RouteProgress().Visibility(mux::Visibility::Collapsed);
	RouteProgress().Text(L"");
	RouteName().IsEnabled(true);
	RouteBox().IsEnabled(true);
	RouteAdd().IsEnabled(false);
	ShowSheet(RoutePane());
}

void MainWindow::RouteChanged(IInspectable const& sender, controls::TextChangedEventArgs const&) {
	RouteHint().Visibility(shown(RouteBox().Text().empty()));
	if (sender == RouteBox()) {
		SuggestRouteName();
	}

	ApplyRouteAdd();
}

void MainWindow::SuggestRouteName() {
	std::string current = trim_copy(S(RouteName().Text()));
	if (!current.empty() && current != route_auto_name) {
		return;
	}

	int count = 0;
	std::string alone;
	std::string text = S(RouteBox().Text());
	for (char& c : text) {
		if (c == '\r') {
			c = '\n';
		}
	}

	size_t at = 0;
	while (at <= text.size()) {
		size_t next = text.find('\n', at);
		std::string line = trim_copy(text.substr(at, next == std::string::npos ? std::string::npos : next - at));
		at = next == std::string::npos ? text.size() + 1 : next + 1;
		if (line.empty()) {
			continue;
		}

		std::string kind, value, note, error;
		coreabi::classify(line, kind, value, note, error);
		if (!note.empty() || kind == "error" || kind.empty()) {
			continue;
		}

		count++;
		if (kind == "domain" && count == 1) {
			alone = value;
		} else {
			alone.clear();
		}
	}

	std::string suggested = count == 0 ? "" : (!alone.empty() ? alone : (count == 1 ? "1 element" : std::to_string(count) + " elements"));
	route_auto_name = suggested;
	if (S(RouteName().Text()) != suggested) {
		RouteName().Text(H(suggested));
	}
}

void MainWindow::ApplyRouteAdd() {
	if (route_cancel) {
		RouteAdd().IsEnabled(false);
		return;
	}

	RouteAdd().IsEnabled(!trim_copy(S(RouteName().Text())).empty() && !trim_copy(S(RouteBox().Text())).empty());
}

void MainWindow::OpenList(RouteList* list) {
	editing_list = list;
	ListTitle().Text(L"Edit list");
	ListName().Text(H(list->name));
	ListName().IsReadOnly(false);
	ListName().Visibility(mux::Visibility::Visible);
	ListHint().Visibility(mux::Visibility::Visible);
	std::string body;
	for (auto const& entry : list->entries) {
		if (!body.empty()) {
			body += "\n";
		}

		body += entry.value;
	}

	ListEntries().Text(H(body));
	ListEntries().IsReadOnly(false);
	ListEntries().Height(180);
	std::string resolved;
	for (auto const& entry : list->entries) {
		if (entry.kind != "domain") {
			continue;
		}

		if (!resolved.empty()) {
			resolved += "\n";
		}

		resolved += entry.value;
		if (entry.addresses.empty()) {
			resolved += std::string("\n  ") + (entry.status.empty() ? "Looking up the address" : entry.status);
		} else {
			for (auto const& ip : entry.addresses) {
				resolved += "\n  " + ip;
			}
		}
	}

	ListResolved().Text(H(resolved));
	ListResolvedPane().Visibility(shown(!resolved.empty()));
	ListError().Visibility(mux::Visibility::Collapsed);
	ListError().Text(L"");
	ListProgress().Visibility(mux::Visibility::Collapsed);
	ListClose().Visibility(mux::Visibility::Collapsed);
	ListCancel().Visibility(mux::Visibility::Visible);
	ListSave().Visibility(mux::Visibility::Visible);
	ApplyListSave();
	ShowSheet(ListPane());
}

void MainWindow::OpenApp(AppRule* app) {
	editing_list = nullptr;
	ListTitle().Text(H(app->name));
	ListName().Text(L"");
	ListName().Visibility(mux::Visibility::Collapsed);
	ListHint().Visibility(mux::Visibility::Collapsed);
	std::string body = app->learned.empty() ? "Nothing learned yet." : "";
	for (auto const& ip : app->learned) {
		if (!body.empty()) {
			body += "\n";
		}

		body += ip;
	}

	ListEntries().Text(H(body));
	ListEntries().IsReadOnly(true);
	ListEntries().Height(280);
	ListResolved().Text(L"");
	ListResolvedPane().Visibility(mux::Visibility::Collapsed);
	ListError().Visibility(mux::Visibility::Collapsed);
	ListProgress().Visibility(mux::Visibility::Collapsed);
	ListClose().Visibility(mux::Visibility::Visible);
	ListCancel().Visibility(mux::Visibility::Collapsed);
	ListSave().Visibility(mux::Visibility::Collapsed);
	ShowSheet(ListPane());
}

void MainWindow::ListChanged(IInspectable const&, controls::TextChangedEventArgs const&) {
	if (!editing_list) {
		return;
	}

	ApplyListSave();
}

void MainWindow::ApplyListSave() {
	if (list_cancel || !editing_list) {
		ListSave().IsEnabled(false);
		return;
	}

	ListSave().IsEnabled(!trim_copy(S(ListName().Text())).empty() && !trim_copy(S(ListEntries().Text())).empty());
}

void MainWindow::AskDelete(IInspectable const&, mux::RoutedEventArgs const&) {
	AskDeleteTunnel(session.selected);
}

void MainWindow::AskDeleteTunnel(Tunnel* tunnel) {
	if (!tunnel) {
		return;
	}

	pending_delete = tunnel;
	session.selected = tunnel;
	Paint();
	ConfirmText().Text(H("Delete the tunnel \"" + tunnel->name + "\"?"));
	ShowSheet(ConfirmPane());
}

void MainWindow::OpenSettings(IInspectable const&, mux::RoutedEventArgs const&) {
	ShowSettings();
}

void MainWindow::LoginToggled(IInspectable const&, mux::RoutedEventArgs const&) {
	if (!quiet) {
		session.SetLaunch(is_on(LoginSwitch()));
	}
}

void MainWindow::HiddenToggled(IInspectable const&, mux::RoutedEventArgs const&) {
	if (!quiet) {
		session.SetStartHidden(is_on(HiddenSwitch()));
	}
}

void MainWindow::DefaultChanged(IInspectable const&, controls::SelectionChangedEventArgs const&) {
	if (quiet) {
		return;
	}

	auto item = DefaultCombo().SelectedItem().try_as<controls::ComboBoxItem>();
	if (!item) {
		return;
	}

	auto tag = item.Tag().try_as<hstring>();
	session.SetDefault(tag ? S(*tag) : "");
}

void MainWindow::OpenFolder(IInspectable const&, mux::RoutedEventArgs const&) {
	session.OpenFolder();
}

void MainWindow::LanToggled(IInspectable const&, mux::RoutedEventArgs const&) {
	if (quiet || !session.selected) {
		return;
	}

	session.SetLan(is_on(LanSwitch()));
	Paint();
	PushBypassAsync(session.selected);
}

void MainWindow::SetDirect(IInspectable const&, mux::RoutedEventArgs const&) {
	session.SetPolicy(false);
	Paint();
	PushBypassAsync(session.selected);
}

void MainWindow::SetThrough(IInspectable const&, mux::RoutedEventArgs const&) {
	session.SetPolicy(true);
	Paint();
	PushBypassAsync(session.selected);
}

void MainWindow::TogglePower(IInspectable const&, mux::RoutedEventArgs const&) {
	ToggleAsync(session.selected);
}

void MainWindow::ImportTunnel(IInspectable const&, mux::RoutedEventArgs const&) {
	ImportAsync();
}

void MainWindow::ExportSelected(IInspectable const&, mux::RoutedEventArgs const&) {
	ExportAsync(session.selected);
}

void MainWindow::SaveRoutes(IInspectable const&, mux::RoutedEventArgs const&) {
	SaveRoutesAsync();
}

void MainWindow::SaveList(IInspectable const&, mux::RoutedEventArgs const&) {
	SaveListAsync();
}

void MainWindow::ConfirmDelete(IInspectable const&, mux::RoutedEventArgs const&) {
	ConfirmDeleteAsync();
}

void MainWindow::AddApp(IInspectable const&, mux::RoutedEventArgs const&) {
	AppSearch().Text(L"");
	AppList().Items().Clear();
	ShowSheet(AppPane());
	if (apps_ready) {
		AppStatus().Visibility(mux::Visibility::Collapsed);
		FillAppList();
		return;
	}

	AppList().Visibility(mux::Visibility::Collapsed);
	AppStatus().Text(L"Looking for apps…");
	AppStatus().Visibility(mux::Visibility::Visible);
	if (apps_warming) {
		return;
	}

	LoadAppsAsync(++app_epoch);
}

void MainWindow::AppSearchChanged(IInspectable const&, controls::TextChangedEventArgs const&) {
	if (AppPane().Visibility() != mux::Visibility::Visible) {
		return;
	}

	FillAppList();
}

void MainWindow::FillAppList() {
	std::vector<std::wstring> taken;
	if (session.selected) {
		for (auto const& app : session.selected->apps) {
			taken.push_back(widen(app->path));
		}
	}

	std::wstring query = trim_w(AppSearch().Text().c_str());
	AppList().Items().Clear();
	app_icon_jobs.clear();
	int shown_rows = 0;
	for (auto const& app : installed) {
		std::wstring path = widen(app.path);
		bool used = false;
		for (auto const& one : taken) {
			if (iequals(one, path)) {
				used = true;
			}
		}

		if (used) {
			continue;
		}

		if (!query.empty() && !icontains(widen(app.name), query) && !icontains(path, query)) {
			continue;
		}

		controls::Grid grid;
		grid.Padding({ 4, 8, 4, 8 });
		grid.ColumnSpacing(12);
		controls::ColumnDefinition c0;
		c0.Width({ 0, mux::GridUnitType::Auto });
		controls::ColumnDefinition c1;
		c1.Width({ 1, mux::GridUnitType::Star });
		grid.ColumnDefinitions().Append(c0);
		grid.ColumnDefinitions().Append(c1);
		controls::Border plate;
		plate.Width(28);
		plate.Height(28);
		plate.CornerRadius({ 6, 6, 6, 6 });
		plate.Background(media::SolidColorBrush(rgba(20, 255, 255, 255)));
		controls::Image icon;
		icon.Width(28);
		icon.Height(28);
		icon.Stretch(media::Stretch::UniformToFill);
		bool cached = false;
		for (auto const& one : icons) {
			if (iequals(one.first, path)) {
				icon.Source(one.second);
				cached = true;
				break;
			}
		}

		if (!cached) {
			app_icon_jobs.push_back({ icon, path });
		}

		plate.Child(icon);
		controls::StackPanel lines;
		lines.Spacing(2);
		lines.VerticalAlignment(mux::VerticalAlignment::Center);
		controls::TextBlock name;
		name.Text(H(app.name));
		name.FontSize(14);
		name.Foreground(brush(L"TextBrush"));
		name.TextTrimming(mux::TextTrimming::CharacterEllipsis);
		controls::TextBlock file;
		file.Text(hstring(path));
		file.FontSize(11);
		file.Foreground(brush(L"MutedBrush"));
		file.TextTrimming(mux::TextTrimming::CharacterEllipsis);
		lines.Children().Append(name);
		lines.Children().Append(file);
		grid.Children().Append(plate);
		grid.Children().Append(lines);
		controls::Grid::SetColumn(lines, 1);
		std::string path_utf = app.path;
		std::string name_utf = app.name;
		grid.Tapped([this, path_utf, name_utf](auto&&, mux::Input::TappedRoutedEventArgs const& args) {
			args.Handled(true);
			if (!session.AddProgram(path_utf, name_utf)) {
				return;
			}

			PaintRules();
			if (session.selected && session.selected->apps.size() >= 12) {
				CloseSheet(nullptr, nullptr);
				return;
			}

			FillAppList();
		});
		AppList().Items().Append(grid);
		shown_rows++;
	}

	if (shown_rows == 0) {
		AppList().Visibility(mux::Visibility::Collapsed);
		AppStatus().Text(installed.empty() ? L"No apps found. Use Browse… to pick an .exe." : L"No matches.");
		AppStatus().Visibility(mux::Visibility::Visible);
	} else {
		AppStatus().Visibility(mux::Visibility::Collapsed);
		AppList().Visibility(mux::Visibility::Visible);
	}

	if (!app_icon_jobs.empty()) {
		PaintAppIconsAsync(++app_icon_epoch);
	}
}

winrt::fire_and_forget MainWindow::WarmAppsAsync() {
	if (apps_ready || apps_warming) {
		co_return;
	}

	auto life = get_strong();
	auto queue = DispatcherQueue();
	apps_warming = true;
	co_await winrt::resume_background();
	std::vector<InstalledApp> found;
	try {
		found = scan_installed();
	} catch (...) {
		found.clear();
	}

	co_await resume_ui{ queue };
	installed = std::move(found);
	apps_ready = true;
	apps_warming = false;
	if (AppPane().Visibility() == mux::Visibility::Visible) {
		FillAppList();
	}
}

winrt::fire_and_forget MainWindow::PaintAppIconsAsync(int epoch) {
	auto life = get_strong();
	auto queue = DispatcherQueue();
	auto jobs = app_icon_jobs;
	for (size_t i = 0; i < jobs.size(); i++) {
		if (epoch != app_icon_epoch) {
			co_return;
		}

		std::wstring path = jobs[i].second;
		mux::Media::ImageSource cached{ nullptr };
		for (auto const& one : icons) {
			if (iequals(one.first, path)) {
				cached = one.second;
				break;
			}
		}

		if (cached) {
			jobs[i].first.Source(cached);
			continue;
		}

		co_await winrt::resume_background();
		winrt::Windows::Graphics::Imaging::SoftwareBitmap bitmap{ nullptr };
		try {
			bitmap = icon_bitmap(path);
		} catch (...) {
			bitmap = nullptr;
		}

		co_await resume_ui{ queue };
		if (epoch != app_icon_epoch) {
			co_return;
		}

		imaging::SoftwareBitmapSource source{ nullptr };
		if (bitmap) {
			try {
				source = imaging::SoftwareBitmapSource();
				co_await source.SetBitmapAsync(bitmap);
			} catch (...) {
				source = nullptr;
			}
		}

		icons.push_back({ path, source });
		if (source) {
			jobs[i].first.Source(source);
		}
	}
}

void MainWindow::BrowseApp(IInspectable const&, mux::RoutedEventArgs const&) {
	BrowseAsync();
}

void MainWindow::StartHelper(IInspectable const&, mux::RoutedEventArgs const&) {
	HelperAsync(true);
}

void MainWindow::OnDragOver(IInspectable const&, mux::DragEventArgs const& args) {
	args.AcceptedOperation(winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
}

void MainWindow::OnDrop(IInspectable const&, mux::DragEventArgs const& args) {
	DropAsync(args);
}

void MainWindow::AddTray() {
	if (tray_added || !hwnd) {
		return;
	}

	tray = {};
	tray.cbSize = sizeof(tray);
	tray.hWnd = hwnd;
	tray.uID = 1;
	tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
	tray.uCallbackMessage = tray_message;
	wcscpy_s(tray.szTip, L"Fairlead");
	tray.hIcon = load_app_icon();
	if (!tray.hIcon) {
		if (std::wstring path = icon_file_path(session); !path.empty()) {
			tray.hIcon = reinterpret_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE));
		}
	}

	if (!tray.hIcon) {
		tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	}

	Shell_NotifyIconW(NIM_ADD, &tray);
	tray.uVersion = NOTIFYICON_VERSION_4;
	Shell_NotifyIconW(NIM_SETVERSION, &tray);
	tray_added = true;
}

void MainWindow::RemoveTray() {
	if (!tray_added) {
		return;
	}

	Shell_NotifyIconW(NIM_DELETE, &tray);
	if (tray.hIcon) {
		DestroyIcon(tray.hIcon);
		tray.hIcon = nullptr;
	}

	tray_added = false;
}

void MainWindow::OnTray(LPARAM lp) {
	switch (LOWORD(lp)) {
	case NIN_SELECT:
	case WM_LBUTTONUP:
		ShowMain();
		break;
	case WM_CONTEXTMENU:
	case WM_RBUTTONUP:
		ShowTrayMenu();
		break;
	default:
		break;
	}
}

void MainWindow::ShowTrayMenu() {
	HMENU menu = CreatePopupMenu();
	std::vector<Tunnel*> order;
	UINT id = 100;
	if (session.tunnels.empty()) {
		AppendMenuW(menu, MF_STRING, 1, L"Open Fairlead");
	} else {
		for (auto const& tunnel : session.tunnels) {
			std::wstring label = (tunnel->connected ? L"\u25CF " : L"") + widen(tunnel->name);
			AppendMenuW(menu, MF_STRING, id++, label.c_str());
			order.push_back(tunnel.get());
		}

		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING, 1, L"Open Fairlead");
	}

	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, 2, L"Settings…");
	AppendMenuW(menu, MF_STRING, 3, L"Quit Fairlead");
	POINT pt{};
	GetCursorPos(&pt);
	SetForegroundWindow(hwnd);
	int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
	PostMessageW(hwnd, WM_NULL, 0, 0);
	DestroyMenu(menu);
	if (cmd == 1) {
		ShowMain();
	} else if (cmd == 2) {
		ShowMain();
		ShowSettings();
	} else if (cmd == 3) {
		QuitAsync();
	} else if (cmd >= 100) {
		int index = cmd - 100;
		if (index >= 0 && index < static_cast<int>(order.size())) {
			ToggleAsync(order[index]);
		}
	}
}

}
