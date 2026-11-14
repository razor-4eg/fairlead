#include "pch.h"
#include "App.xaml.h"

#include "MainWindow.xaml.h"

#include <mutex>
#include <thread>

namespace {

HANDLE instance_mutex = nullptr;
HANDLE show_event = nullptr;
winrt::Fairlead::MainWindow live_window{ nullptr };
bool show_pending = false;
std::mutex show_gate;

}

namespace winrt::Fairlead::implementation {

App::App() {
	InitializeComponent();
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
	std::thread([] {
		for (;;) {
			if (WaitForSingleObject(show_event, INFINITE) != WAIT_OBJECT_0) {
				return;
			}

			winrt::Fairlead::MainWindow window{ nullptr };
			{
				std::lock_guard<std::mutex> lock(show_gate);
				window = live_window;
				if (!window) {
					show_pending = true;
				}
			}

			if (window) {
				window.ShowMain();
			}
		}
	}).detach();

	Microsoft::UI::Xaml::ResourceDictionary root;
	root.MergedDictionaries().Append(Microsoft::UI::Xaml::Controls::XamlControlsResources());
	Microsoft::UI::Xaml::ResourceDictionary theme;
	Microsoft::UI::Xaml::Application::LoadComponent(
		theme,
		Windows::Foundation::Uri{ L"ms-appx:///Theme.xaml" },
		Microsoft::UI::Xaml::Controls::Primitives::ComponentResourceLocation::Application);
	root.MergedDictionaries().Append(theme);
	Resources(root);

	window = winrt::make<MainWindow>();
	bool pending = false;
	{
		std::lock_guard<std::mutex> lock(show_gate);
		live_window = window;
		pending = show_pending;
		show_pending = false;
	}

	window.Activate();
	if (pending) {
		window.ShowMain();
	} else {
		window.ApplyStartHidden();
	}

	window.Start();
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	show_event = CreateEventW(nullptr, FALSE, FALSE, L"Local\\Fairlead.Show");
	instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\Fairlead");
	DWORD waited = WaitForSingleObject(instance_mutex, 0);
	if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
		if (show_event) {
			SetEvent(show_event);
			CloseHandle(show_event);
		}

		if (instance_mutex) {
			CloseHandle(instance_mutex);
		}

		return 0;
	}

	winrt::init_apartment(winrt::apartment_type::single_threaded);
	winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
		winrt::make<winrt::Fairlead::implementation::App>();
	});
	return 0;
}
