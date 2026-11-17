#include "pch.h"
#include "MainWindow.xaml.h"

#include "l10n.h"
#include "text.h"

#include <cctype>
#include <filesystem>
#include <shobjidl.h>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
namespace pickers = winrt::Windows::Storage::Pickers;

namespace winrt::Fairlead::implementation {

namespace {

struct flag_off {
	bool& bit;
	~flag_off() { bit = false; }
};

std::string problem_lines(std::vector<RouteProblem> const& problems) {
	std::string out;
	for (auto const& problem : problems) {
		if (!out.empty()) {
			out += "\n";
		}

		out += problem.line;
	}

	return out;
}

std::string problem_text(std::vector<RouteProblem> const& problems) {
	std::string out;
	for (auto const& problem : problems) {
		if (problem.message.empty()) {
			continue;
		}

		if (!out.empty()) {
			out += "\n";
		}

		out += problem.line.empty() ? problem.message : problem.line + ": " + problem.message;
	}

	return out;
}

bool problem_kept(std::vector<RouteProblem> const& problems) {
	for (auto const& problem : problems) {
		if (!problem.message.empty()) {
			return true;
		}
	}

	return false;
}

}

winrt::fire_and_forget MainWindow::BootAsync() {
	auto life = get_strong();
	auto queue = DispatcherQueue();
	co_await EnsureHelperAsync(false);
	auto* tunnel = session.ResolveDefault();
	if (!tunnel) {
		/* None: tear down any leftover helper session so reopen does not look like auto-connect. */
		for (auto const& one : session.tunnels) {
			if (one->connected || one->phase == "Connecting") {
				co_await DisconnectAsync(one.get());
			}
		}
		if (session.helper_online) {
			auto body = session.DownBody();
			rpc_busy = true;
			flag_off clear_rpc{ rpc_busy };
			co_await winrt::resume_background();
			session.Call(body, 8000);
			co_await resume_ui{ queue };
		}
		Paint();
		co_return;
	}

	session.selected = tunnel;
	Paint();
	if (tunnel->connected || tunnel->phase == "Connecting") {
		co_return;
	}

	co_await ConnectAsync(tunnel);
}

winrt::fire_and_forget MainWindow::TickAsync() {
	if (ticking || rpc_busy) {
		co_return;
	}

	auto life = get_strong();
	ticking = true;
	flag_off clear_tick{ ticking };
	auto queue = DispatcherQueue();
	session.RefreshTools();
	bool busy = false;
	for (auto const& tunnel : session.tunnels) {
		if (tunnel->phase == "Connecting" || tunnel->phase == "Disconnecting") {
			busy = true;
		}
	}

	/*
	 * Keep status short when Connected so the 1s metrics ticker is not wedged behind a
	 * 10–20s pipe read. Connect/disconnect still use their own long Call waits.
	 */
	int wait = session.helper_online ? (busy ? 8000 : 3500) : 200;
	auto body = session.StatusBody();
	co_await winrt::resume_background();
	auto reply = session.Call(body, wait);

	/* Do not co_await the UI queue — if enqueue drops, clear_tick still releases ticking. */
	queue.TryEnqueue([life, this, reply = std::move(reply), busy]() mutable {
		if (session.ReplyOk(reply)) {
			session.MarkHelper(true);
		} else if (!busy && !reply.empty()) {
			session.MarkHelper(false);
		}

		if (!reply.empty()) {
			session.ApplyStatus(reply);
		}

		Paint();
	});
}

winrt::fire_and_forget MainWindow::ToggleAsync(Tunnel* tunnel) {
	auto life = get_strong();
	if (!tunnel) {
		co_return;
	}

	if (tunnel->connected || tunnel->phase == "Connecting") {
		co_await DisconnectAsync(tunnel);
	} else {
		co_await ConnectAsync(tunnel);
	}
}

winrt::Windows::Foundation::IAsyncAction MainWindow::ConnectAsync(Tunnel* tunnel) {
	auto life = get_strong();
	auto queue = DispatcherQueue();
	if (!tunnel) {
		co_return;
	}

	std::vector<Tunnel*> others;
	for (auto const& one : session.tunnels) {
		if (one.get() != tunnel && (one->connected || one->phase == "Connecting")) {
			others.push_back(one.get());
		}
	}

	for (auto* other : others) {
		co_await DisconnectAsync(other);
	}

	session.BeginConnect(tunnel);
	Paint();
	if (!session.helper_online) {
		co_await EnsureHelperAsync(true);
	}

	/* mihomo resolves domains itself. No pre-resolve here. */
	auto body = session.Rpc("up", tunnel, true);
	rpc_busy = true;
	flag_off clear_rpc{ rpc_busy };
	co_await winrt::resume_background();
	/* up() waits on TUN; don't lose the reply to a short pipe wait. */
	auto reply = session.Call(body, 60000);
	co_await resume_ui{ queue };
	/* Cancelled while up() ran — DisconnectAsync already moved the phase. */
	if (tunnel->phase != "Connecting") {
		if (session.ReplyOk(reply)) {
			auto down = session.Rpc("down", tunnel, false);
			co_await winrt::resume_background();
			session.Call(down, 30000);
			co_await resume_ui{ queue };
		}
		Paint();
		co_return;
	}

	session.FinishConnect(tunnel, reply);
	Paint();
}

winrt::Windows::Foundation::IAsyncAction MainWindow::DisconnectAsync(Tunnel* tunnel) {
	auto life = get_strong();
	auto queue = DispatcherQueue();
	if (!tunnel) {
		co_return;
	}

	session.BeginDisconnect(tunnel);
	Paint();
	auto body = session.Rpc("down", tunnel, false);
	rpc_busy = true;
	flag_off clear_rpc{ rpc_busy };
	co_await winrt::resume_background();
	session.Call(body, 30000);
	co_await resume_ui{ queue };
	session.FinishDisconnect(tunnel);
	Paint();
}

winrt::Windows::Foundation::IAsyncAction MainWindow::PushBypassAsync(Tunnel* tunnel) {
	auto life = get_strong();
	auto queue = DispatcherQueue();
	/* Same as Mac isUp — allow while Connecting so rules apply mid-reconnect. */
	if (!tunnel || !(tunnel->connected || tunnel->phase == "Connecting")) {
		co_return;
	}

	bool was_connected = tunnel->connected;
	tunnel->phase = "Connecting";
	tunnel->connected = false;
	tunnel->handshake = 0;
	tunnel->detail = l10n::text("Updating routes…");
	Paint();

	auto body = session.Rpc("bypass", tunnel, true);
	co_await winrt::resume_background();
	auto reply = session.Call(body);
	co_await resume_ui{ queue };
	if (session.ReplyOk(reply)) {
		session.ApplyStatus(reply);
		if (session.banner == l10n::text("Could not update routes on the live tunnel.") || session.banner.rfind("Could not update routes", 0) == 0) {
			session.ClearBanner();
		}

		Paint();
		co_return;
	}

	tunnel->connected = false;
	tunnel->handshake = 0;
	if (was_connected) {
		tunnel->phase = "Off";
		tunnel->detail = l10n::text("Tunnel is off");
	} else {
		tunnel->phase = "Connecting";
		tunnel->detail = l10n::text("Checking the tunnel…");
	}
	session.banner = l10n::display(session.ErrorOf(reply));
	if (session.banner.empty()) {
		session.banner = l10n::text("Could not update routes on the live tunnel.");
	}

	Paint();
}

winrt::Windows::Foundation::IAsyncAction MainWindow::EnsureHelperAsync(bool interactive) {
	auto life = get_strong();
	auto queue = DispatcherQueue();
	while (helper_starting) {
		co_await winrt::resume_background();
		Sleep(100);
		co_await resume_ui{ queue };
	}

	helper_starting = true;
	flag_off clear{ helper_starting };
	std::wstring exe = widen(session.exe_dir) + L"\\fairlead-helper.exe";
	if (!std::filesystem::exists(std::filesystem::path(exe))) {
		session.helper_notice_ready = true;
		session.banner = l10n::format("fairlead-helper.exe is missing from %@", session.exe_dir);
		Paint();
		co_return;
	}

	co_await winrt::resume_background();
	bool up = session.Ping();
	co_await resume_ui{ queue };
	if (up) {
		session.MarkHelper(true);
		PaintHelper();
		co_return;
	}

	co_await winrt::resume_background();
	bool scheduled = session.RunScheduled();
	co_await resume_ui{ queue };
	if (scheduled) {
		for (int i = 0; i < 16; i++) {
			co_await winrt::resume_background();
			Sleep(150);
			up = session.Ping();
			co_await resume_ui{ queue };
			if (up) {
				session.MarkHelper(true);
				PaintHelper();
				co_return;
			}
		}
	}

	if (!interactive) {
		co_return;
	}

	std::string error;
	if (!session.LaunchHelper(hwnd, error)) {
		session.helper_notice_ready = true;
		session.banner = error;
		Paint();
		co_return;
	}

	for (int i = 0; i < 20; i++) {
		co_await winrt::resume_background();
		Sleep(150);
		up = session.Ping();
		co_await resume_ui{ queue };
		if (up) {
			session.MarkHelper(true);
			PaintHelper();
			co_return;
		}
	}

	co_await winrt::resume_background();
	session.StopHelpers();
	co_await resume_ui{ queue };
	if (!session.LaunchHelper(hwnd, error)) {
		session.banner = error;
		Paint();
		co_return;
	}

	for (int i = 0; i < 20; i++) {
		co_await winrt::resume_background();
		Sleep(150);
		up = session.Ping();
		co_await resume_ui{ queue };
		if (up) {
			session.MarkHelper(true);
			PaintHelper();
			co_return;
		}
	}

	session.helper_notice_ready = true;
	session.banner = l10n::text("The helper did not start.");
	Paint();
}

winrt::fire_and_forget MainWindow::HelperAsync(bool interactive) {
	auto life = get_strong();
	co_await EnsureHelperAsync(interactive);
	Paint();
}

winrt::fire_and_forget MainWindow::SaveRoutesAsync() {
	auto life = get_strong();
	if (route_cancel) {
		co_return;
	}

	auto cancel = std::make_shared<std::atomic<bool>>(false);
	route_cancel = cancel;
	struct restore {
		MainWindow* self;
		~restore() {
			if (!self) {
				return;
			}

			self->route_cancel.reset();
			self->RouteProgress().Visibility(mux::Visibility::Collapsed);
			self->RouteName().IsEnabled(true);
			self->RouteBox().IsEnabled(true);
			self->ApplyRouteAdd();
		}
	} cleanup{ this };
	RouteAdd().IsEnabled(false);
	RouteName().IsEnabled(false);
	RouteBox().IsEnabled(false);
	RouteError().Visibility(mux::Visibility::Collapsed);
	RouteProgress().Text(L"");
	RouteProgress().Visibility(mux::Visibility::Visible);
	auto built = session.BuildList(S(RouteName().Text()), S(RouteBox().Text()), nullptr);
	if (built.list) {
		RouteProgress().Text(hstring(l10n::wtext("Adding the list")));
		session.CommitNew(std::move(built.list));
		PaintRules();
		co_await PushBypassAsync(session.selected);
	}

	if (cancel->load()) {
		co_return;
	}

	if (!problem_kept(built.problems)) {
		CloseSheet(nullptr, nullptr);
		co_return;
	}

	RouteBox().Text(H(problem_lines(built.problems)));
	RouteError().Text(H(problem_text(built.problems)));
	RouteError().Visibility(mux::Visibility::Visible);
}

winrt::fire_and_forget MainWindow::SaveListAsync() {
	auto life = get_strong();
	if (!editing_list || list_cancel) {
		co_return;
	}

	auto* list = editing_list;
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	list_cancel = cancel;
	struct restore {
		MainWindow* self;
		~restore() {
			if (!self) {
				return;
			}

			self->list_cancel.reset();
			self->ListProgress().Visibility(mux::Visibility::Collapsed);
			self->ListName().IsEnabled(true);
			self->ListEntries().IsEnabled(true);
			self->ApplyListSave();
		}
	} cleanup{ this };
	ListSave().IsEnabled(false);
	ListName().IsEnabled(false);
	ListEntries().IsEnabled(false);
	ListError().Visibility(mux::Visibility::Collapsed);
	ListProgress().Text(L"");
	ListProgress().Visibility(mux::Visibility::Visible);
	auto built = session.BuildList(S(ListName().Text()), S(ListEntries().Text()), list);
	if (built.list) {
		ListProgress().Text(hstring(l10n::wtext("Saving the list")));
		auto name = built.list->name;
		auto entries = std::move(built.list->entries);
		session.CommitUpdate(list, name, std::move(entries));
		PaintRules();
		co_await PushBypassAsync(session.selected);
	}

	if (cancel->load()) {
		co_return;
	}

	if (!problem_kept(built.problems)) {
		CloseSheet(nullptr, nullptr);
		co_return;
	}

	ListEntries().Text(H(problem_lines(built.problems)));
	ListError().Text(H(problem_text(built.problems)));
	ListError().Visibility(mux::Visibility::Visible);
}

winrt::fire_and_forget MainWindow::ImportAsync() {
	auto life = get_strong();
	if (!hwnd) {
		co_return;
	}

	pickers::FileOpenPicker picker;
	picker.as<::IInitializeWithWindow>()->Initialize(hwnd);
	picker.FileTypeFilter().Append(L".conf");
	picker.FileTypeFilter().Append(L".txt");
	picker.FileTypeFilter().Append(L"*");
	auto file = co_await picker.PickSingleFileAsync();
	if (!file) {
		co_return;
	}

	session.ImportFile(narrow(file.Path()));
	Paint();
}

winrt::fire_and_forget MainWindow::ExportAsync(Tunnel* tunnel) {
	auto life = get_strong();
	if (!tunnel || !hwnd) {
		co_return;
	}

	pickers::FileSavePicker picker;
	picker.as<::IInitializeWithWindow>()->Initialize(hwnd);
	picker.SuggestedStartLocation(pickers::PickerLocationId::DocumentsLibrary);
	auto types = winrt::single_threaded_vector<hstring>({ L".conf" });
	picker.FileTypeChoices().Insert(L"WireGuard config", types);
	std::string stem = trim_copy(tunnel->name);
	if (stem.size() >= 5) {
		std::string tail = stem.substr(stem.size() - 5);
		for (char& c : tail) {
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		}

		if (tail == ".conf") {
			stem.resize(stem.size() - 5);
		}
	}

	std::wstring file_name = widen(stem);
	std::wstring invalid = L"\\/:*?\"<>|";
	for (auto& c : file_name) {
		if (invalid.find(c) != std::wstring::npos) {
			c = L' ';
		}
	}

	while (!file_name.empty() && file_name.back() == L' ') {
		file_name.pop_back();
	}

	picker.SuggestedFileName(file_name.empty() ? L"tunnel" : file_name);
	auto file = co_await picker.PickSaveFileAsync();
	if (!file) {
		co_return;
	}

	co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, H(tunnel->config));
}

winrt::fire_and_forget MainWindow::DropAsync(mux::DragEventArgs args) {
	auto life = get_strong();
	auto view = args.DataView();
	if (!view.Contains(winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems())) {
		co_return;
	}

	auto items = co_await view.GetStorageItemsAsync();
	for (auto const& item : items) {
		session.ImportFile(narrow(item.Path()));
	}

	Paint();
}

winrt::fire_and_forget MainWindow::BrowseAsync() {
	auto life = get_strong();
	if (!hwnd) {
		co_return;
	}

	pickers::FileOpenPicker picker;
	picker.as<::IInitializeWithWindow>()->Initialize(hwnd);
	picker.FileTypeFilter().Append(L".exe");
	auto file = co_await picker.PickSingleFileAsync();
	if (!file) {
		co_return;
	}

	std::string path = narrow(file.Path());
	std::string name = narrow(file.Name());
	if (!session.AddProgram(path, name)) {
		co_return;
	}

	PaintRules();
	if (session.selected && session.selected->apps.size() >= 12) {
		CloseSheet(nullptr, nullptr);
		co_return;
	}

	FillAppList();
}

winrt::fire_and_forget MainWindow::LoadAppsAsync(int epoch) {
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
	if (epoch != app_epoch) {
		apps_warming = false;
		co_return;
	}

	installed = std::move(found);
	apps_ready = true;
	apps_warming = false;
	FillAppList();
}

winrt::fire_and_forget MainWindow::QuitAsync() {
	auto life = get_strong();
	auto queue = DispatcherQueue();
	if (quitting) {
		co_return;
	}

	quitting = true;
	if (timer) {
		timer.Stop();
	}

	std::vector<Tunnel*> live;
	for (auto const& tunnel : session.tunnels) {
		if (tunnel->connected || tunnel->phase == "Connecting") {
			live.push_back(tunnel.get());
		}
	}

	for (auto* tunnel : live) {
		co_await DisconnectAsync(tunnel);
	}

	if (session.helper_online) {
		auto body = session.QuitBody();
		co_await winrt::resume_background();
		session.Call(body, 800);
		co_await resume_ui{ queue };
	}

	session.MarkHelper(false);
	RemoveTray();
	Close();
}

winrt::fire_and_forget MainWindow::ConfirmDeleteAsync() {
	auto life = get_strong();
	auto* tunnel = pending_delete ? pending_delete : session.selected;
	pending_delete = nullptr;
	if (!tunnel) {
		CloseSheet(nullptr, nullptr);
		co_return;
	}

	bool live = tunnel->connected || tunnel->phase == "Connecting";
	if (live) {
		co_await DisconnectAsync(tunnel);
	}

	session.Delete(tunnel);
	CloseSheet(nullptr, nullptr);
	Paint();
}

}
